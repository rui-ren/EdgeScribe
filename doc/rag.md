# EDGESCRIBE RAG System — Design & Overhead Analysis

## Overview

This document covers adding Retrieval-Augmented Generation (RAG) to EDGESCRIBE — letting users ingest their own research reports, clinical guidelines, or documentation, and have the LLM answer questions grounded in that content.

**Design goal**: Zero new dependencies, minimal memory overhead, no impact on inference speed.

---

## How RAG Works in EDGESCRIBE

```
User ingests a document
    ↓
Read as plain text → split into ~500-word chunks
    ↓
INSERT INTO chunks (text) → SQLite FTS5 auto-indexes
    ↓
User asks a question
    ↓
FTS5 MATCH query → top-K relevant chunks (< 1ms)
    ↓
Chunks prepended to LLM prompt as context
    ↓
LLM generates answer grounded in user's documents
```

No embedding model needed. No vector database. Just SQLite FTS5 (built into SQLite, zero overhead).

---

## Supported Input Formats

| Format | Handling | Overhead |
|---|---|---|
| `.txt` | `fread()` directly | Zero |
| `.md` | `fread()` directly (markdown is text) | Zero |
| `.csv` | `fread()`, split rows | Zero |
| `.pdf` | Shell out to `pdftotext` (Poppler) | Zero in-binary (external tool) |
| `.docx` | Not supported — user converts to `.txt` | Zero |

**Decision**: Support text-based formats natively. For PDF, optionally call `pdftotext` as an external process — no library linked into the binary.

```cpp
// PDF handling — zero library overhead
std::string ExtractPdfText(const std::string& pdf_path) {
    std::string txt_path = pdf_path + ".txt";
    std::string cmd = "pdftotext \"" + pdf_path + "\" \"" + txt_path + "\"";
    if (std::system(cmd.c_str()) != 0) {
        throw std::runtime_error("pdftotext not found. Install poppler-utils or convert to .txt manually.");
    }
    // Read and return txt_path contents
}
```

---

## Total Overhead Analysis

### Memory Overhead

| Component | RAM Cost | Notes |
|---|---|---|
| **SQLite library** | ~600 KB code | Compiled into binary (amalgamation) |
| **SQLite database (open)** | ~2-4 MB | Page cache, WAL journal |
| **FTS5 index** | ~0 extra at runtime | Disk-based, paged on demand |
| **MemoryStore object** | < 1 KB | Just a `sqlite3*` pointer + mutex |
| **Chunked text in DB** | 0 (on disk) | Only loaded on query, not resident |
| **Search results** | ~10-50 KB per query | Top-K chunk strings, temporary |
| **Total idle overhead** | **~3-5 MB** | Negligible vs model memory (~2 GB) |

For comparison:
- Qwen3-VL Q4 model: **~1.5 GB** in RAM
- Nemotron ASR model: **~670 MB** in RAM
- SQLite + memory system: **~3-5 MB** in RAM → **0.2% of model memory**

### Binary Size Overhead

| Component | Size | Notes |
|---|---|---|
| `sqlite3.c` compiled | ~250 KB in binary | One `.c` file, public domain |
| `memory_store.cpp` | ~20-30 KB in binary | Our wrapper code |
| **Total binary increase** | **~280 KB** | Current binary: ~2 MB → 2.3 MB |

### Disk Overhead

| Component | Disk Usage | Notes |
|---|---|---|
| SQLite database file | 1-50 MB typical | Depends on transcript history |
| FTS5 index | ~30% of indexed text | Built into the `.db` file |
| 1 hour of transcripts | ~50 KB | ~150 words/min × 60 min × ~5 bytes/word |
| 100 KB document chunked | ~130 KB in DB | Text + FTS5 index overhead |
| **Typical after 1 month** | **~5-20 MB** | Very conservative |

### CPU Overhead

| Operation | Time | When |
|---|---|---|
| FTS5 search (10K chunks) | < 1 ms | On every RAG-augmented query |
| SQLite INSERT (transcript segment) | < 0.1 ms | Every few seconds during ASR |
| Text chunking (100 KB doc) | < 10 ms | One-time on ingestion |
| **Impact on inference** | **Zero** | Search completes before generation starts |

---

## The Context Window Problem

### Current State

Your LLM and Vision engines use **llama.cpp C API** with a fixed context window:

```cpp
// llm_engine.h / vision_engine.h
int n_ctx_ = 4096;  // Fixed at engine initialization

// llm_engine.cpp
llama_context_params cparams = llama_context_default_params();
cparams.n_ctx = n_ctx_;     // 4096 tokens — KV cache pre-allocated
cparams.n_batch = 512;
ctx_ = llama_init_from_model(model_, cparams);
```

**KV cache is pre-allocated for the full 4096-token window** on engine creation. This memory is consumed whether you use 100 tokens or 4000. Each call clears and reuses the same cache.

### KV Cache Memory Cost

For Qwen3-VL-2B Q4_K_M with 4096 context:

```
KV cache size ≈ 2 × n_layers × n_ctx × d_head × n_heads × sizeof(float16)
             ≈ 2 × 24 × 4096 × 128 × 16 × 2 bytes
             ≈ ~800 MB (already allocated today)
```

This is **already paid** — RAG doesn't increase it. RAG just fills more of the pre-allocated window with useful content.

### How RAG Affects the Context Budget

With 4096 tokens total, here's how the budget gets split:

```
┌────────────────────────────────────────────────────────────┐
│                    4096 token context window                │
├──────────┬──────────────┬───────────┬──────────────────────┤
│ System   │ RAG Context  │ User      │ Generation           │
│ prompt   │ (retrieved   │ query     │ (LLM output)         │
│          │  chunks)     │           │                      │
│ ~100     │ ~1000-1500   │ ~50-200   │ ~2000-2500           │
│ tokens   │ tokens       │ tokens    │ tokens               │
├──────────┴──────────────┴───────────┴──────────────────────┤
│ ◄──────── prompt (input) ──────────►◄── generation ──────► │
└────────────────────────────────────────────────────────────┘
```

**The tradeoff**: More RAG context = less room for generation output. You must budget carefully.

### Budget Strategy

```cpp
static constexpr int kContextWindow = 4096;
static constexpr int kSystemPromptBudget = 150;   // system instructions
static constexpr int kUserQueryBudget = 200;       // user's question
static constexpr int kGenerationBudget = 2048;     // LLM output
static constexpr int kRagBudget =                  // what's left for RAG
    kContextWindow - kSystemPromptBudget - kUserQueryBudget - kGenerationBudget;
// kRagBudget = 4096 - 150 - 200 - 2048 = 1698 tokens (~1200 words)

// In BuildContext():
// 1. Retrieve top-K chunks from FTS5
// 2. Concatenate chunks until total tokens ≈ kRagBudget
// 3. Truncate the last chunk if over budget
// 4. Return as context string to inject into prompt
```

### What If 4096 Isn't Enough?

Three options, ordered by complexity:

#### Option 1: Increase n_ctx (simple, costs RAM)

```cpp
// Just change the constant:
int n_ctx_ = 8192;  // Double the window

// KV cache cost doubles: ~800 MB → ~1.6 GB
// Total model RAM: ~1.5 GB model + ~1.6 GB KV = ~3.1 GB
```

Feasible on laptops with 16+ GB RAM. llama.cpp handles this fine.

#### Option 2: Make n_ctx configurable (recommended)

```cpp
// llm_engine.h
explicit LlmEngine(const std::string& model_path,
                   const std::string& device = "cpu",
                   int context_size = 4096);  // user-configurable

// CLI:
// edgescribe chat --context 8192 "..."
// edgescribe serve --context 8192
```

Let the user choose based on their available RAM.

#### Option 3: Chunked summarization (no context increase)

For documents that don't fit in the context window:

```
Large document → split into N chunks that each fit
    ↓
Summarize chunk 1 → summary 1
Summarize chunk 2 → summary 2
...
Summarize chunk N → summary N
    ↓
Concatenate all summaries → final summary prompt → LLM → answer
```

This is a **map-reduce** pattern. Works with any context size but requires multiple LLM calls.

---

## How llama.cpp vs onnxruntime-genai Handle Context

### llama.cpp (what you use for LLM/Vision)

| Feature | llama.cpp | Notes |
|---|---|---|
| **Context allocation** | Set at init via `n_ctx` | Pre-allocates KV cache |
| **Dynamic resize** | ❌ Not at runtime | Must recreate context |
| **Context shifting** | ✅ `llama_kv_cache_seq_rm` | Shift old tokens out, slide window |
| **Flash Attention** | ✅ Optional (`-fa`) | Reduces KV cache memory ~50% |
| **Quantized KV** | ✅ Q8_0 / Q4_0 cache | Reduces KV memory 2-4× |
| **Max context** | Model-dependent | Qwen3-VL: up to 32K in theory |
| **Grouped-Query Attention** | ✅ Supported | Fewer KV heads = less memory |

**llama.cpp advantages over onnxruntime-genai**:
- Can quantize the KV cache (Q8_0 saves ~50% KV memory)
- Flash attention support reduces peak memory
- Context shifting allows "infinite" conversations by dropping old tokens
- Much more control over memory allocation

### onnxruntime-genai (what you use for ASR only)

| Feature | onnxruntime-genai | Notes |
|---|---|---|
| **Context allocation** | Fixed at model export time | Baked into ONNX graph |
| **Dynamic resize** | ❌ No | Must re-export model |
| **Context shifting** | ❌ No | No KV cache manipulation API |
| **Flash Attention** | ❌ Limited | Depends on EP (execution provider) |
| **Quantized KV** | ❌ No | Full precision KV only |
| **Max context** | Fixed by model config | Cannot exceed exported value |

**onnxruntime-genai pain points**:
- Context length baked into the ONNX model at export — cannot change at runtime
- KV cache always full precision (FP16/FP32) — wastes memory
- No sliding window or cache eviction — prompt must fit or it fails
- No way to trade memory for context length dynamically

### Why your architecture is actually fine

You use **llama.cpp for LLM/Vision** (the context-sensitive workloads) and **onnxruntime-genai only for ASR** (which processes fixed-size audio chunks, no long context needed). This is the right split:

```
ASR (onnxruntime-genai): Fixed context is OK — audio chunks are small
LLM (llama.cpp):         Flexible context — can increase n_ctx as needed
Vision (llama.cpp):       Flexible context — can increase n_ctx as needed
TTS (onnxruntime):        No context — just runs inference on phonemes
```

---

## RAG Impact on Inference Performance

### Does RAG slow down token generation?

**No.** RAG only adds tokens to the **prompt** (input). The generation speed (tokens/sec) is determined by model size and hardware, not prompt length.

However, longer prompts affect:

| Metric | Impact | Why |
|---|---|---|
| **Time to first token** | ⚠️ Slightly longer | More prompt tokens to process in prefill phase |
| **Tokens per second** | ✅ No change | Decoding speed is independent of prompt length |
| **Total generation time** | ⚠️ Slightly longer | Due to prefill, not decoding |
| **Memory** | ✅ No change | KV cache already pre-allocated for full n_ctx |

### Embedding overhead (all-MiniLM-L6-v2)

| Operation | Time | When |
|---|---|---|
| Embed a query (1 sentence) | ~2-5 ms | Each RAG-augmented query |
| Embed a chunk on ingest | ~2-5 ms | One-time per chunk |
| Embed 100 chunks (document ingest) | ~200-500 ms | One-time per document |
| Model RAM | ~100 MB | While loaded |

### Prefill overhead estimate

```
Without RAG:  ~200 prompt tokens  → prefill: ~50ms
With RAG:     ~1500 prompt tokens → prefill: ~300ms
Difference:   +250ms (one-time, before first token)
```

This is **imperceptible** to the user — they see the first token 250ms later, then streaming is identical.

---

## Recommended RAG Configuration for EDGESCRIBE

```
Context window:     16,384 tokens (default — see doc/context_window.md)
RAG chunk size:     ~400 tokens per chunk (~300 words)
Top-K retrieval:    3-5 chunks
RAG context budget: ~2000-4000 tokens
Memory context:     ~500-1000 tokens (past turns / session history)
Generation budget:  ~10,000-14,000 tokens
Search method:      FTS5 keyword (Phase 1), + cosine embeddings (Phase 3)
```

### Context Budget at 16,384 Tokens

With the recommended 16,384-token context window, there's ample room for RAG + memory + long output:

```
┌──────────────────────────────────────────────────────────────────────┐
│                    16,384 token context window                       │
├──────────┬───────────────┬───────────┬──────────┬────────────────────┤
│ System   │ Memory        │ RAG       │ User     │ Generation         │
│ prompt   │ context       │ chunks    │ query    │ (LLM output)       │
│          │ (past turns)  │ (KB docs) │          │                    │
│ ~150     │ ~500-1000     │ ~2000-4000│ ~200     │ ~10,000-14,000     │
│ tokens   │ tokens        │ tokens    │ tokens   │ tokens             │
└──────────┴───────────────┴───────────┴──────────┴────────────────────┘
```

### Example Budget Scenarios

| Use Case | System | Memory | RAG | Query | Output | Total |
|---|---|---|---|---|---|---|
| **Simple chat** | 150 | 0 | 0 | 100 | 2,000 | 2,250 |
| **RAG query** | 150 | 0 | 2,000 | 100 | 4,000 | 6,250 |
| **Full RAG + history** | 150 | 800 | 3,000 | 100 | 6,000 | 10,050 |
| **SOAP + RAG + history** | 150 | 800 | 2,000 | 4,000 | 8,000 | 14,950 |

All fit within 16,384. Even the heaviest use case has room.

### Example Prompt Assembly

```
[System: 150 tokens]
You are a medical assistant. Answer based on the provided context.

[Memory: ~800 tokens]
Previous session: Patient discussed diabetes management, A1C was 7.2...

[RAG Context: ~2000 tokens]
--- Retrieved from: clinical_guidelines.txt ---
Chunk 1: "For type 2 diabetes, first-line treatment is metformin..."
Chunk 2: "If A1C remains above 7% after 3 months of metformin..."
Chunk 3: "Contraindications for metformin include eGFR < 30..."

[User Query: ~100 tokens]
What should we adjust in this patient's treatment plan?

[Generation budget: ~13,334 tokens]
→ LLM generates a detailed, grounded answer
```

### Memory Cost of 16,384 Context

The KV cache is pre-allocated at engine startup for the full context window:

```
Model weights (Qwen3-VL-2B Q4):  ~1,500 MB
KV cache (16384, FP16):          ~  460 MB
SQLite + memory system:          ~    5 MB
Total:                           ~2,000 MB (2 GB)
```

This is comfortable on any 8+ GB laptop. See `doc/context_window.md` for detailed analysis, including KV quantization options that can halve KV memory.

---

## Future Enhancements

### Near-term (no code changes)
- **Smarter chunking**: Use semantic boundaries (paragraphs, sections) instead of fixed-size
- **Better retrieval**: Return fewer, more relevant chunks instead of more chunks
- **Pre-summarize on ingest**: Summarize each chunk to ~100 tokens, store summary alongside full text

### Medium-term (code changes)
- **Configurable n_ctx**: Let user set `--context 8192` (low RAM) or `--context 32768` (workstation)
- **Quantized KV cache**: Use llama.cpp's Q8_0 KV quantization for half the KV memory
- **Flash attention**: Enable if hardware supports it

### Long-term
- **Context shifting / sliding window**: Drop old KV entries to make room for new tokens
- **Hierarchical RAG**: First retrieve documents, then retrieve chunks from best document
- **Map-reduce summarization**: Chunked summarization for documents exceeding context window

---

## Summary

| Question | Answer |
|---|---|
| **Total RAM overhead of RAG + memory?** | ~3-5 MB (0.2% of model memory) |
| **Binary size increase?** | ~280 KB (SQLite amalgamation) |
| **Does RAG slow down inference?** | No — adds ~250ms prefill, generation speed unchanged |
| **Does it increase KV cache memory?** | No — KV cache already pre-allocated for full n_ctx |
| **New dependencies?** | Zero — SQLite is one `.c` file, FTS5 is built-in |
| **Context window concern?** | Budget ~1500 tokens for RAG context within 4096 window |
| **Can we increase context?** | Yes — llama.cpp supports it, just costs more RAM |
| **Why llama.cpp is better than ORT-GenAI here?** | Dynamic n_ctx, KV quantization, flash attention, sliding window |

# EDGESCRIBE Context Window — Design & Analysis

## Current Configuration

EDGESCRIBE uses **llama.cpp C API** for LLM and Vision engines (not onnxruntime-genai). Context window is set at engine initialization:

```cpp
// src/llm/llm_engine.h — current default
int n_ctx_ = 4096;

// src/llm/llm_engine.cpp — applied at context creation
llama_context_params cparams = llama_context_default_params();
cparams.n_ctx = n_ctx_;     // Pre-allocates KV cache for full window
cparams.n_batch = 512;
ctx_ = llama_init_from_model(model_, cparams);
```

The same pattern is used in `vision_engine.h/cpp`.

**Key behavior**: llama.cpp pre-allocates the KV cache for the **full n_ctx window** at context creation time. This memory is consumed whether the prompt uses 100 tokens or 16,000.

---

## Recommended Default: 16384 Tokens

### Why 16384?

| Reason | Detail |
|---|---|
| **Long transcripts** | A 20-min appointment ≈ 3000 words ≈ 4000 tokens. 4096 can't fit both input + output. |
| **RAG context** | With RAG, ~1500-4000 tokens go to retrieved chunks. Needs room for output too. |
| **Long-form output** | Detailed SOAP notes, multi-page summaries, full reports need 4000-10000 output tokens. |
| **Modern laptops** | 8-16 GB RAM is standard. 2 GB total (model + KV) is fine. |

### Memory Budget at 16384

```
Qwen3-VL-2B Q4_K_M model weights:   ~1,500 MB
KV cache (16384, FP16):              ~  460 MB
SQLite + memory system:              ~    5 MB
Application + misc:                  ~   35 MB
───────────────────────────────────────────────
Total:                               ~2,000 MB (2 GB)

Remaining on 8 GB laptop:            ~6 GB for OS + apps  ✅
Remaining on 16 GB laptop:           ~14 GB for OS + apps ✅✅
```

---

## KV Cache Memory by Context Size

### Qwen3-VL-2B Architecture

```
Layers:        28
KV heads:      2  (Grouped-Query Attention)
Head dim:      128
KV cache = 2 × n_layers × n_kv_heads × head_dim × n_ctx × sizeof(dtype)
```

### Memory Table

| n_ctx | FP16 (default) | Q8_0 (quantized) | Q4_0 (aggressive) |
|---|---|---|---|
| 2,048 | ~58 MB | ~29 MB | ~15 MB |
| 4,096 | ~115 MB | ~58 MB | ~29 MB |
| 8,192 | ~230 MB | ~115 MB | ~58 MB |
| **16,384** | **~460 MB** | **~230 MB** | **~115 MB** |
| 32,768 | ~920 MB | ~460 MB | ~230 MB |

### KV Quantization (Optional Optimization)

llama.cpp supports quantizing the KV cache to reduce memory with minimal quality loss:

```cpp
llama_context_params cparams = llama_context_default_params();
cparams.n_ctx = 16384;
cparams.type_k = GGML_TYPE_Q8_0;  // Quantize Key cache — half memory
cparams.type_v = GGML_TYPE_Q8_0;  // Quantize Value cache — half memory
```

Q8_0 KV quantization at 16384 context: **~230 MB** (same as FP16 at 8192). Negligible quality impact. Recommended for memory-constrained devices.

---

## How Context Window Affects Performance

### Prefill Time (Processing the Prompt)

Prefill processes all input tokens before generating the first output token. Time scales linearly with prompt length:

| Prompt Length | Prefill Time (CPU) | Notes |
|---|---|---|
| 200 tokens | ~50 ms | Short chat query |
| 1,000 tokens | ~200 ms | Chat with short RAG context |
| 4,000 tokens | ~800 ms | Full RAG + transcript |
| 12,000 tokens | ~2-3 s | Long document input |
| 16,000 tokens | ~3-4 s | Near-max context |

**User experience**: A few seconds delay before the first token appears, then streaming proceeds at normal speed.

### Generation Speed (Decoding Tokens)

Generation speed is **independent of prompt length** — determined by model size and hardware:

| Hardware | Qwen3-VL-2B Q4 | Notes |
|---|---|---|
| Modern laptop CPU (i7/Ryzen 7) | ~15-25 tokens/sec | Comfortable streaming |
| Older laptop CPU | ~8-15 tokens/sec | Usable |
| Apple M1/M2/M3 | ~25-40 tokens/sec | Metal acceleration |

**Increasing n_ctx does NOT slow down generation.** Only prefill takes slightly longer with larger prompts.

### Memory Allocation Timing

| Event | What happens |
|---|---|
| Engine created (`LlmEngine()`) | Model weights loaded (~1.5 GB) |
| Context created (`llama_init_from_model`) | KV cache allocated (~460 MB at 16384) |
| Each generation call | KV cache **reused** (cleared with `llama_kv_cache_clear`) |
| Engine destroyed | All memory freed |

The KV cache is allocated **once** at engine startup and reused across all calls. No per-request allocation.

---

## When Is Context Allocated? (llama.cpp vs onnxruntime-genai)

A common source of confusion: KV cache allocation is **not per-prompt**. It happens at engine initialization and stays resident until the engine is destroyed.

### llama.cpp (used for LLM and Vision in EDGESCRIBE)

Context size is chosen **at startup** when creating the context object. You pick `n_ctx` once, KV cache is `malloc`'d for that full size, and it stays allocated:

```cpp
// Startup — happens ONCE
llama_context_params cparams = llama_context_default_params();
cparams.n_ctx = 16384;                              // ← choose size here
ctx_ = llama_init_from_model(model_, cparams);       // ← 460 MB allocated HERE

// Every request — reuse same memory, no reallocation
void Generate(...) {
    llama_kv_cache_clear(ctx_);  // marks all slots empty (no free/malloc)
    // ... fill with new prompt ...
    // ... generate tokens ...
}
// ↑ This can run 1000 times — KV memory never changes

// Shutdown — freed here
llama_free(ctx_);  // ← 460 MB freed
```

**Key point**: `llama_kv_cache_clear()` does NOT free memory. It just resets the "used" counter to zero so slots can be reused. The 460 MB stays allocated the entire time the engine is alive.

### onnxruntime-genai (used for ASR in EDGESCRIBE)

Even more restrictive — context length is baked into the **ONNX model file at export time**. You cannot change it at startup or at runtime:

```
Model exported with max_seq_len = 1500
    ↓
You load it → context = 1500, always, no option to change
    ↓
Want 4096? → Re-export the entire model with new max_seq_len
```

### Comparison

| | llama.cpp | onnxruntime-genai |
|---|---|---|
| **When is context set?** | At engine startup (`llama_init_from_model`) | At model export (baked into ONNX file) |
| **Can you change it?** | Yes — restart engine with different `n_ctx` | No — must re-export model |
| **Memory lifetime** | Engine startup → engine shutdown | Model load → model unload |
| **Per-request realloc?** | No — same memory, cleared and reused | No — same memory |

---

## Why Context Pools Don't Work

A tempting idea: pre-allocate multiple contexts of different sizes and pick the right one per request.

```cpp
// DON'T DO THIS
llama_context* ctx_small = create_context(model, 4096);   // 115 MB — always allocated
llama_context* ctx_large = create_context(model, 16384);  // 460 MB — always allocated
// Total KV: 575 MB — WORSE than just one large context!
```

**The problem**: Both contexts are allocated simultaneously. You're paying for 575 MB to avoid paying for 460 MB. It only saves memory if you **destroy** one context and create another per request — but `llama_init_from_model` takes ~50-100ms, adding noticeable latency to every call.

### What actually works

| Approach | Memory | Latency | Complexity | Verdict |
|---|---|---|---|---|
| **Fixed 16384** | 460 MB | None | Trivial | ✅ **Recommended** |
| **Context pools (both alive)** | 575 MB | None | Medium | ❌ Wastes more memory |
| **Lazy create/destroy** | Varies | +50-100ms per call | Medium | ⚠️ Only if RAM-starved |
| **KV quantization (Q8_0)** | 230 MB | None | Trivial | ✅ **Best if RAM matters** |

**The correct strategy**: Pick one fixed `n_ctx` (16384), optionally enable Q8_0 KV quantization if memory is tight. Don't over-engineer this.

```cpp
// The right approach — simple and efficient
llama_context_params cparams = llama_context_default_params();
cparams.n_ctx = 16384;

// Optional: halve KV memory with negligible quality impact
cparams.type_k = GGML_TYPE_Q8_0;  // 460 MB → 230 MB
cparams.type_v = GGML_TYPE_Q8_0;

ctx_ = llama_init_from_model(model_, cparams);
// Done. This context handles everything — chats, RAG, SOAP, summarization.
```

---

## Context Budget Planning

### With RAG + Memory System

```
┌──────────────────────────────────────────────────────────────────────┐
│                    16,384 token context window                       │
├──────────┬───────────────┬───────────┬──────────┬────────────────────┤
│ System   │ Memory        │ RAG       │ User     │ Generation         │
│ prompt   │ context       │ chunks    │ query    │ (LLM output)       │
│          │ (past turns)  │ (KB docs) │          │                    │
│ ~150     │ ~500-1000     │ ~1500-4000│ ~200     │ ~10,000-14,000     │
│ tokens   │ tokens        │ tokens    │ tokens   │ tokens             │
└──────────┴───────────────┴───────────┴──────────┴────────────────────┘
```

### Budget Examples

| Use Case | System | Memory | RAG | Query | Output | Total |
|---|---|---|---|---|---|---|
| **Simple chat** | 150 | 0 | 0 | 100 | 2,000 | 2,250 |
| **Chat with history** | 150 | 800 | 0 | 100 | 2,000 | 3,050 |
| **RAG query** | 150 | 0 | 2,000 | 100 | 2,000 | 4,250 |
| **Full RAG + history** | 150 | 800 | 2,000 | 100 | 4,000 | 7,050 |
| **Long SOAP note** | 150 | 0 | 0 | 4,000 | 8,000 | 12,150 |
| **SOAP + RAG + history** | 150 | 800 | 2,000 | 4,000 | 8,000 | 14,950 |

All cases fit comfortably within 16,384. Even the heaviest use case (SOAP from long transcript + RAG guidelines + memory context) leaves room.

### Overflow Handling

Current code clips `max_length` if prompt + generation would exceed `n_ctx`:

```cpp
// src/llm/llm_engine.cpp
if (n_prompt + max_length > n_ctx_) {
    max_length = n_ctx_ - n_prompt;  // Reduce generation length
    if (max_length <= 0) {
        throw std::runtime_error("Prompt too long for context window");
    }
}
```

This is correct — graceful degradation rather than crashing.

---

## Configurable Context (Future Enhancement)

Allow users to set context size based on their hardware:

```cpp
// llm_engine.h — make n_ctx configurable
explicit LlmEngine(const std::string& model_path,
                   const std::string& device = "cpu",
                   int context_size = 16384);  // Default 16384, user-configurable
```

```bash
# CLI
edgescribe chat --context 8192 "..."     # Low-memory device
edgescribe serve --context 32768         # 32 GB RAM workstation

# Or via environment variable
set EDGESCRIBE_CONTEXT_SIZE=8192
```

Suggested defaults by RAM:

| System RAM | Recommended n_ctx | KV Cache |
|---|---|---|
| 4 GB | 4,096 | ~115 MB |
| 8 GB | 16,384 | ~460 MB |
| 16 GB | 16,384 | ~460 MB |
| 32 GB+ | 32,768 | ~920 MB |

---

## llama.cpp vs onnxruntime-genai: Context Handling

### The Same Fundamental Limit

Both llama.cpp and onnxruntime-genai share the same hard constraint: **if your prompt exceeds the context window, it fails.** Neither can magically grow the context at runtime. The KV cache is a fixed-size block of memory.

```cpp
// llama.cpp — EDGESCRIBE's current overflow handling
if (n_prompt + max_length > n_ctx_) {
    max_length = n_ctx_ - n_prompt;  // tries to shrink output budget
    if (max_length <= 0) {
        throw std::runtime_error("Prompt too long for context window");
        // ↑ Same failure as onnxruntime-genai — prompt doesn't fit
    }
}
```

### Where llama.cpp Pulls Ahead

The difference isn't that llama.cpp avoids the problem — it gives you **far more tools to manage it**:

| Capability | llama.cpp | onnxruntime-genai |
|---|---|---|
| **Prompt > context** | ❌ Fails | ❌ Also fails |
| **Choose context size at startup** | ✅ `cparams.n_ctx = any_value` | ❌ Baked into ONNX at export |
| **Want bigger context later?** | ✅ Change `n_ctx`, restart engine | ❌ Re-export entire model |
| **Trim old tokens from KV** | ✅ `llama_kv_cache_seq_rm()` | ❌ No API |
| **Sliding window (infinite chat)** | ✅ Drop oldest tokens, keep generating | ❌ Not possible |
| **Quantize KV to fit more** | ✅ Q8_0 halves memory, Q4_0 quarters it | ❌ FP16/FP32 only |
| **Flash Attention** | ✅ Reduces peak memory during prefill | ❌ Limited/no support |
| **Context shifting** | ✅ Shift token positions in KV cache | ❌ No API |
| **Grouped-Query Attention** | ✅ Fewer KV heads = less memory per token | ✅ If model uses it |

### What This Means in Practice

**onnxruntime-genai**:
```
Model exported with context = 4096
→ You're stuck with 4096 forever
→ Need 8192? Re-export the model (requires Python, config changes, hours of work)
→ KV cache is always FP16/FP32 — no way to reduce memory
→ Prompt hits 4097 tokens? Hard failure, no workaround
```

**llama.cpp**:
```
Start engine with n_ctx = 16384
→ Want 32768 next time? Just change one number and restart
→ KV too big? Add Q8_0 quantization — half the memory, same context
→ Prompt hits 16385 tokens? You can:
   1. Truncate input (current approach)
   2. Use context shifting to drop old tokens and keep going
   3. Restart with larger n_ctx
```

### Why EDGESCRIBE's Architecture Split is Correct

```
ASR (onnxruntime-genai):  ✅ Fine — audio chunks are small fixed-size windows
                             Context limit doesn't matter for speech recognition

LLM (llama.cpp):          ✅ Correct choice — needs flexible context for:
                             - Long transcripts (SOAP notes)
                             - RAG context injection
                             - Multi-turn chat history
                             - Long-form output generation

Vision (llama.cpp):       ✅ Correct choice — image + text prompts can be large
                             Need room for detailed analysis output

TTS (onnxruntime):        ✅ Fine — no context concept, just runs phoneme→audio
```

The key insight: **onnxruntime-genai is acceptable for ASR** (fixed audio chunk sizes, no need for variable context), but **llama.cpp is essential for LLM/Vision** where prompt sizes vary wildly and context flexibility is critical.

---

## Summary

| Decision | Value | Rationale |
|---|---|---|
| **Default n_ctx** | 16,384 | Fits 2 GB total with model. Room for RAG + long output. |
| **KV cache type** | FP16 (default) | Simple. Q8_0 available if memory-constrained. |
| **Impact on generation speed** | None | Only prefill is slower with longer prompts. |
| **Impact on RAM** | +345 MB vs 4096 | Acceptable on 8+ GB laptops. |
| **Configurable?** | Future enhancement | Let users set via CLI flag or env var. |

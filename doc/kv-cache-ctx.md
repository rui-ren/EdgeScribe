# KV Cache & Context Length — How It Works

## What is KV Cache?

When an LLM reads your prompt, each transformer layer computes two vectors for every token: a **Key** (K) and a **Value** (V). These are stored in the KV cache so the model doesn't recompute them when generating the next token.

```
Prompt: "The patient has diabetes"

Token 1: "The"      → K₁, V₁  (computed, stored in cache)
Token 2: "patient"  → K₂, V₂  (computed, stored in cache)
Token 3: "has"      → K₃, V₃  (computed, stored in cache)
Token 4: "diabetes" → K₄, V₄  (computed, stored in cache)

Now generate token 5:
  → Model looks at ALL stored K₁-K₄, V₁-V₄ (attention mechanism)
  → Produces token 5: "and"
  → Store K₅, V₅ in cache

Generate token 6:
  → Model looks at K₁-K₅, V₁-V₅
  → Produces token 6: "is"
  → Store K₆, V₆ in cache

...and so on
```

**Without KV cache**: the model would recompute K and V for ALL previous tokens on every single new token. That's O(n²) work — impossibly slow for long sequences.

**With KV cache**: just look up stored vectors. Generating one token = one forward pass through the model. Fast.

---

## What is n_ctx (Context Length)?

`n_ctx` defines the **maximum number of tokens** (prompt + generated output combined) the model can handle in one call. It directly controls how much KV cache memory is pre-allocated.

```
n_ctx = 4096  → pre-allocate RAM for 4096 K,V pairs per layer
n_ctx = 16384 → pre-allocate RAM for 16384 K,V pairs per layer
```

### The Theater Analogy

Think of it like reserving seats in a theater:

```
n_ctx = 4096:   [□□□□□□□□......4096 seats......□□□□□□□□]
                 ↑ can seat up to 4096 tokens total (prompt + output)

n_ctx = 16384:  [□□□□□□□□..........16384 seats..........□□□□□□□□]
                 ↑ more seats = more memory allocated upfront
```

**Even if only 200 tokens sit down, all seats are built (memory allocated).**

---

## How KV Cache Affects Memory

### Memory Formula

```
KV cache size = 2 × n_layers × n_kv_heads × head_dim × n_ctx × sizeof(dtype)
                ↑               ↑            ↑          ↑        ↑
              K and V      GQA heads    per head    context   FP16 = 2 bytes
```

### For Qwen3-VL-2B (28 layers, 2 KV heads, 128 head dim)

```
FP16: 2 × 28 × 2 × 128 × n_ctx × 2 bytes = 28,672 × n_ctx bytes
```

| n_ctx | KV Cache Size | Total with Model (~1.5 GB) |
|---|---|---|
| 2,048 | ~58 MB | ~1.56 GB |
| 4,096 | ~115 MB | ~1.62 GB |
| 8,192 | ~230 MB | ~1.73 GB |
| **16,384** | **~460 MB** | **~1.96 GB** |
| 32,768 | ~920 MB | ~2.42 GB |

---

## How It Works in EDGESCRIBE

### Current Implementation (llama.cpp C API)

```cpp
// At engine startup — KV cache allocated here
llama_context_params cparams = llama_context_default_params();
cparams.n_ctx = 16384;     // ← All 16384 slots allocated NOW
cparams.n_batch = 512;
ctx_ = llama_init_from_model(model_, cparams);
// ↑ ~460 MB KV cache allocated at this line

// At each generation call — KV cache reused, not reallocated
void Generate(...) {
    llama_kv_cache_clear(ctx_);  // Clear old K,V entries (doesn't free memory)
    // ... fill with new prompt tokens ...
    // ... generate output tokens ...
}
// KV cache stays allocated until engine is destroyed
```

### Visual: What Happens During a Request

```
Engine starts:
KV Cache: [□□□□□□□□□□□□□□□□...16384 empty slots...□□□□□□□□□□□□]
          ↑ 460 MB allocated, all empty

User sends: "What is diabetes?" (5 tokens)

Prefill phase (process prompt):
KV Cache: [■■■■■□□□□□□□□□□□...16379 empty slots...□□□□□□□□□□□□]
           ↑↑↑↑↑
           prompt tokens — K,V computed and stored

Generation phase (produce output, one token at a time):
KV Cache: [■■■■■■□□□□□□□□□□...16378 empty...□□□□]  → "Diabetes"
KV Cache: [■■■■■■■□□□□□□□□□...16377 empty...□□□□]  → "is"
KV Cache: [■■■■■■■■□□□□□□□□...16376 empty...□□□□]  → "a"
KV Cache: [■■■■■■■■■□□□□□□□...16375 empty...□□□□]  → "chronic"
...continues until EOS token or n_ctx reached...

Next request: llama_kv_cache_clear(ctx_)
KV Cache: [□□□□□□□□□□□□□□□□...16384 empty slots...□□□□□□□□□□□□]
          ↑ memory NOT freed — just marked empty for reuse
```

---

## Why Can't KV Cache Be Dynamic?

### The core problem: contiguous memory

```
Static allocation (what llama.cpp does):
┌──────────────────────────────────────────────────────────┐
│    KV cache: one big contiguous block of memory          │
│    [slot 0][slot 1][slot 2][slot 3]...[slot 16383]       │
└──────────────────────────────────────────────────────────┘
→ Fast: pointer arithmetic to any slot
→ Cache-friendly: sequential memory access
→ GPU-compatible: tensor operations need contiguous arrays

Dynamic allocation (grow as needed):
┌────────┐     ┌────────┐     ┌────────┐
│ slot 0 │ ... │ slot 1 │ ... │ slot 2 │ ...  ← scattered in RAM
└────────┘     └────────┘     └────────┘
→ Slow: memory fragmentation, pointer chasing
→ Cache-hostile: random memory access patterns
→ GPU-incompatible: can't realloc GPU tensors mid-inference
```

GPU and CPU tensor operations require **contiguous memory blocks**. You can't `realloc` a GPU buffer while the model is running. Even on CPU, fragmented memory causes cache misses that destroy performance.

### Why not realloc?

```
Step 1: malloc 100 slots     → [■■■...100 slots...]
Step 2: need 101 slots       → realloc?

realloc might need to:
  1. Find a bigger contiguous block elsewhere in RAM
  2. Copy ALL existing data to the new location
  3. Free the old block

For 460 MB of KV data, that copy takes ~50-100ms.
If this happens every few tokens, generation becomes unusably slow.
```

---

## Practical Approaches for EDGESCRIBE

### Approach 1: Fixed 16384 (Recommended — current)

```cpp
cparams.n_ctx = 16384;  // Just do it
```

- **460 MB** on an 8 GB laptop = 5.7% of RAM
- Simple, fast, no edge cases
- This is what Ollama, llama.cpp server, and LM Studio all do
- ✅ **Recommended for EDGESCRIBE**

### Approach 2: Context Pools (Future optimization)

Pre-allocate two contexts, pick the right one per request:

```cpp
// At startup — allocate both
llama_context* ctx_small = create_context(model, 4096);   // 115 MB — quick chats
llama_context* ctx_large = create_context(model, 16384);  // 460 MB — RAG/SOAP

// Per request — pick based on need
llama_context* PickContext(int prompt_tokens, int max_output) {
    int needed = prompt_tokens + max_output;
    if (needed <= 4096) return ctx_small;   // Light, fast
    return ctx_large;                        // Full power
}
```

**Total KV memory: 575 MB** (both contexts), but simple queries use the smaller, faster context.

### Approach 3: Lazy Context Creation (Dynamic-ish)

Create and destroy context per request, sized exactly to need:

```cpp
std::string Generate(const std::string& prompt, int max_length) {
    int needed = CountTokens(prompt) + max_length + 256;  // padding

    // Create context sized for this specific request
    auto cparams = llama_context_default_params();
    cparams.n_ctx = needed;
    auto ctx = llama_init_from_model(model_, cparams);

    // ... run inference ...

    llama_free(ctx);  // Free KV cache immediately after
}
```

**Tradeoff**: ~50-100ms overhead per call to allocate/free context. Memory is always minimal, but adds latency. Not recommended for streaming use cases.

### Approach 4: KV Cache Quantization (Memory reduction)

Reduce KV cache memory without changing n_ctx:

```cpp
llama_context_params cparams = llama_context_default_params();
cparams.n_ctx = 16384;
cparams.type_k = GGML_TYPE_Q8_0;  // Quantize Key cache
cparams.type_v = GGML_TYPE_Q8_0;  // Quantize Value cache
// ↑ KV cache: ~230 MB instead of ~460 MB (half!)
// Quality impact: negligible
```

| KV Type | Memory at 16384 | Quality Impact |
|---|---|---|
| FP16 (default) | ~460 MB | Baseline |
| Q8_0 | ~230 MB | Negligible |
| Q4_0 | ~115 MB | Slight degradation on long contexts |

---

## Summary

```
KV Cache = stored Key/Value vectors so the model doesn't recompute them
n_ctx    = how many slots are pre-allocated (prompt + output combined)
Memory   = fixed at engine startup, reused across requests

Can it be dynamic? Not efficiently — contiguous memory required.
Best approach for EDGESCRIBE: fixed 16384 (460 MB, trivial on modern laptops).
Future option: KV quantization (Q8_0) to halve KV memory if needed.
```

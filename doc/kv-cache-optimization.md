# KV Cache Optimization — Prompt Caching for Multi-Turn Chat

## The Problem

In the current implementation, `ResetContext()` clears the entire KV cache before **every** generation call:

```cpp
// Current: llm_engine.cpp
std::string LlmEngine::RunGeneration(...) {
    ResetContext();  // ← Wipes all K,V pairs
    // ... tokenize entire history from scratch
    // ... process ALL tokens through the model again
}
```

For multi-turn chat, this means re-processing the full conversation history every turn:

```
Turn 1:  [system + user1]                          →  200 tokens processed
Turn 2:  [system + user1 + asst1 + user2]          →  800 tokens processed (600 wasted)
Turn 3:  [system + user1 + asst1 + user2 + asst2 + user3]  →  2000 tokens processed (1800 wasted)
Turn 10: [full history]                              → 8000 tokens processed (7800 wasted)
```

The entire prefix (system prompt + all previous turns) is re-tokenized and re-computed through all 28 transformer layers, even though the K,V pairs for those tokens were already computed in the previous turn.

---

## The Solution: KV Cache Reuse (Prompt Caching)

Instead of clearing the KV cache between turns, **keep it** and only process the new tokens:

```
Turn 1:  [system + user1]                    → compute 200 tokens → KEEP KV ✅
Turn 2:  [asst1 + user2]                     → compute 200 tokens (ONLY new ones) ✅
Turn 3:  [asst2 + user3]                     → compute 200 tokens (ONLY new ones) ✅
Turn 10: [asst9 + user10]                    → compute 200 tokens (ONLY new ones) ✅
```

### Performance Impact

| Turn | Current (clear KV) | Optimized (reuse KV) | Savings |
|---|---|---|---|
| Turn 1 | 200 tokens, ~50ms | 200 tokens, ~50ms | None (first turn) |
| Turn 5 | 2000 tokens, ~400ms | 200 tokens, ~50ms | **87% faster** |
| Turn 10 | 5000 tokens, ~1s | 200 tokens, ~50ms | **95% faster** |
| Turn 20 | 12000 tokens, ~2.5s | 200 tokens, ~50ms | **98% faster** |

The user feels this as **time to first token** — the delay before the LLM starts streaming a response. With KV cache reuse, multi-turn chat feels instant regardless of conversation length.

---

## How It Works in llama.cpp

### Current Flow (Stateless)

```cpp
// Each call:
ResetContext();                           // 1. Clear all KV cache
tokens = tokenize(full_history);          // 2. Tokenize ENTIRE conversation
for (batch : tokens)                      // 3. Process ALL tokens through model
    llama_decode(ctx_, batch);            //    (recomputes K,V for everything)
token = llama_sampler_sample(ctx_, -1);   // 4. Generate from last position
```

### Optimized Flow (Stateful)

```cpp
// First call:
tokens = tokenize(full_history);          // 1. Tokenize full prompt
for (batch : tokens)                      // 2. Process all tokens
    llama_decode(ctx_, batch);
cached_token_count = tokens.size();       // 3. Remember how many tokens are cached
// ... generate response ...
// 4. DON'T clear KV cache

// Subsequent calls:
new_tokens = tokenize(full_history);      // 1. Tokenize full history
prefix_match = findCommonPrefix(          // 2. Find how many tokens match the cache
    cached_tokens, new_tokens);
// 3. Only decode tokens AFTER the common prefix
for (i = prefix_match; i < new_tokens.size(); i += n_batch)
    llama_decode(ctx_, batch_from(new_tokens, i));
cached_token_count = new_tokens.size();   // 4. Update cache position
// ... generate response ...
```

### Key llama.cpp APIs Used

```cpp
// Get current number of tokens in KV cache
int n_cached = llama_memory_n_tokens(llama_get_memory(ctx_));

// Remove specific token range from KV cache (for sliding window)
llama_memory_seq_rm(llama_get_memory(ctx_), 0, start_pos, end_pos);

// Clear entire KV cache (current approach — what we want to STOP doing)
llama_memory_clear(llama_get_memory(ctx_), true);

// Decode only new tokens (starting at position n_cached)
llama_batch batch = llama_batch_get_one(new_tokens.data() + n_cached, n_new);
llama_decode(ctx_, batch);
```

---

## Implementation Design

### New State in LlmEngine

```cpp
// llm_engine.h — add to private section
class LlmEngine {
 private:
    // ... existing members ...

    // KV cache tracking for prompt caching
    std::vector<llama_token> cached_tokens_;  // Tokens currently in KV cache
    bool cache_valid_ = false;                // Whether cache can be reused
};
```

### Modified RunGeneration

```cpp
std::string LlmEngine::RunGeneration(const std::string& formatted_prompt,
                                     int max_length,
                                     TokenCallback on_token) {
    const auto* vocab = llama_model_get_vocab(model_);

    // Tokenize the full prompt
    std::vector<llama_token> tokens = Tokenize(formatted_prompt);

    // Find common prefix with cached tokens
    int prefix_len = 0;
    if (cache_valid_) {
        prefix_len = FindCommonPrefix(cached_tokens_, tokens);

        if (prefix_len < static_cast<int>(cached_tokens_.size())) {
            // Cache has diverged — remove tokens after the common prefix
            llama_memory_seq_rm(llama_get_memory(ctx_), 0, prefix_len, -1);
        }
        // If prefix_len == cached_tokens_.size(), cache is perfectly reusable
    } else {
        // No valid cache — clear everything
        llama_memory_clear(llama_get_memory(ctx_), true);
        prefix_len = 0;
    }

    llama_sampler_reset(sampler_);

    int n_prompt = static_cast<int>(tokens.size());
    int n_new = n_prompt - prefix_len;  // Only process NEW tokens

    // Check context size
    if (n_prompt + max_length > n_ctx_) {
        max_length = n_ctx_ - n_prompt;
        if (max_length <= 0) {
            throw std::runtime_error("Prompt too long for context window");
        }
    }

    // Process ONLY new tokens (skip cached prefix)
    auto t_prompt_start = std::chrono::steady_clock::now();
    const int n_batch = llama_n_batch(ctx_);
    for (int i = prefix_len; i < n_prompt; i += n_batch) {
        int n_eval = std::min(n_batch, n_prompt - i);
        llama_batch batch = llama_batch_get_one(tokens.data() + i, n_eval);
        if (llama_decode(ctx_, batch) != 0) {
            throw std::runtime_error("Failed to decode prompt batch");
        }
    }

    // ... generate tokens (same as current) ...

    // After generation: update cache state
    // Include generated tokens in the cache for next turn
    cached_tokens_ = tokens;
    for (llama_token t : generated_tokens) {
        cached_tokens_.push_back(t);
    }
    cache_valid_ = true;

    return result;
}
```

### FindCommonPrefix Helper

```cpp
int LlmEngine::FindCommonPrefix(const std::vector<llama_token>& cached,
                                const std::vector<llama_token>& current) {
    int max_len = std::min(static_cast<int>(cached.size()),
                           static_cast<int>(current.size()));
    for (int i = 0; i < max_len; i++) {
        if (cached[i] != current[i]) return i;
    }
    return max_len;
}
```

### When to Invalidate Cache

```cpp
void LlmEngine::InvalidateCache() {
    cache_valid_ = false;
    cached_tokens_.clear();
    llama_memory_clear(llama_get_memory(ctx_), true);
}
```

Cache should be invalidated when:
- User starts a new conversation (`/clear` in CLI, clear button in UI)
- System prompt changes
- A different model is loaded
- Single-turn mode (no benefit from caching)

Cache should be KEPT when:
- Multi-turn chat continues (the common case)
- Same conversation, new user message appended

---

## Edge Cases

### 1. User edits/deletes a message mid-conversation

```
Cached:  [system, user1, asst1, user2, asst2]
New:     [system, user1, asst1, user2_EDITED, ...]
                                 ↑ diverges at position N

Solution: FindCommonPrefix returns N
         → llama_memory_seq_rm(ctx_, 0, N, -1)  // Remove everything after N
         → Decode from position N onwards
```

### 2. Context window overflow

```
Cached tokens: 14,000
New user turn: +2,000
Total: 16,000 — FULL (n_ctx = 16,384)

Solution: Sliding window
  → Remove oldest turns from the beginning
  → Keep system prompt + last N turns that fit
  → Invalidate cache (prefix no longer matches)
  → Fall back to full recompute (happens rarely)
```

```cpp
// Sliding window: drop oldest turns to fit
while (CountTokens(messages) + max_generation > n_ctx_) {
    // Remove the oldest user+assistant pair (keep system prompt at [0])
    if (messages.size() > 3) {  // system + at least one exchange
        messages.erase(messages.begin() + 1, messages.begin() + 3);
        InvalidateCache();  // Prefix changed, must recompute
    } else {
        break;  // Can't drop more
    }
}
```

### 3. Parallel API requests

```
Request A: [system, user1]  → uses cache
Request B: [system, user1, asst1, user2]  → same prefix, extends cache ✅
Request C: [system, DIFFERENT_user1]  → prefix diverges at position 1 → recompute

Solution: The existing llm_mutex serializes requests.
         Cache is per-engine, so concurrent access is already prevented.
```

---

## What Changes Per File

### `src/llm/llm_engine.h`
- Add `cached_tokens_` vector and `cache_valid_` flag to private members
- Add `FindCommonPrefix()` and `InvalidateCache()` method declarations

### `src/llm/llm_engine.cpp`
- Modify `RunGeneration()` — prefix matching instead of `ResetContext()`
- Add `FindCommonPrefix()` implementation
- Add `InvalidateCache()` implementation
- Track generated tokens to append to `cached_tokens_`
- Update performance stats to show cache hit info

### `src/cli/main.cpp`
- Call `InvalidateCache()` on `/clear` command in interactive chat

### `src/server/api_server.cpp`
- No changes needed (cache reuse is automatic within the engine)

### `src/vision/vision_engine.cpp`
- Same optimization can be applied (but lower priority — vision prompts are usually single-turn)

---

## Measuring the Improvement

Add cache stats to the performance output:

```
prompt: 2000 tokens | cached: 1800 | new: 200 | 4000 tok/s | TTFT: 50 ms
output: 150 tokens | 22.4 tok/s | total: 6.7s
```

vs current:

```
prompt: 2000 tokens | 500 tok/s | TTFT: 400 ms
output: 150 tokens | 22.4 tok/s | total: 7.05s
```

---

## Summary

```
What:     Keep KV cache between multi-turn calls, only process new tokens
Why:      Reduces TTFT from ~400ms-2.5s to ~50ms on turn 5-20
How:      Token-level prefix matching + llama_memory_seq_rm for divergence
Risk:     Low — fallback to full recompute if prefix doesn't match
Effort:   ~50 lines of code change in llm_engine.cpp/h
Priority: v1.1 — after shipping v1.0
```

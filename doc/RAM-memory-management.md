# EDGESCRIBE Memory Management & Model Loading Strategy

## Overview

EDGESCRIBE runs four independent AI engines in a single process. Naively loading
all models at startup would consume **~5.5 GB of RAM** even when idle. This
document describes the hybrid loading strategy that keeps idle RAM under 200 MB
while avoiding cold-start latency for the most-used engines.

---

## The Two Loading Strategies

EDGESCRIBE uses different loading strategies based on how each inference runtime
manages memory:

| Engine | Runtime | Loading | Why |
|:---:|:---:|:---:|-----|
| LLM | llama.cpp | **Eager (mmap)** | Near-zero idle RAM, no cold start |
| Vision | llama.cpp | **Eager (mmap)** | Same model as LLM, shared mapping |
| ASR | onnxruntime | **Lazy + idle unload** | Full RAM load; defer until needed |
| TTS | onnxruntime | **Lazy + idle unload** | Full RAM load; defer until needed |

---

## How mmap Works (LLM/Vision)

llama.cpp loads GGUF models via `mmap()` — the operating system's memory-mapped
file mechanism. This is fundamentally different from reading a file into RAM:

```
Traditional file loading (what ONNX Runtime does):
─────────────────────────────────────────────────
  1. open("model.onnx")
  2. malloc(670 MB)          ← physical RAM allocated immediately
  3. read(fd, buffer, 670MB) ← entire file copied into RAM
  4. RAM stays occupied until explicitly freed

  Result: 670 MB physical RAM consumed, whether or not the model is being used.


Memory-mapped loading (what llama.cpp does):
─────────────────────────────────────────────
  1. open("model.gguf")
  2. mmap(990 MB)            ← virtual address space reserved
                               NO physical RAM allocated yet
  3. First access to page    ← OS page fault → loads 4 KB from disk
  4. Subsequent accesses     ← already in RAM, instant
  5. Under memory pressure   ← OS evicts least-recently-used pages
  6. Re-access evicted page  ← transparent page-in, no crash, no reload

  Result: Physical RAM usage adapts to actual access patterns automatically.
```

### What This Means in Practice

| State | Virtual Memory | Physical RAM | Explanation |
|-------|:-:|:-:|-------------|
| Model just loaded (mmap) | 990 MB | **~0 MB** | Address space mapped, nothing paged in yet |
| First chat request | 990 MB | **~1.5 GB** | Active weight pages faulted in + KV cache |
| Chat complete, 30s idle | 990 MB | **~200 MB** | OS reclaims unused pages for other processes |
| Chat complete, 5 min idle | 990 MB | **~50 MB** | Most pages evicted, only hot metadata remains |
| New chat request | 990 MB | **~1.5 GB** | Pages fault back in transparently (no "reload") |

**Key insight**: The mmap'd model handle stays open. There is no "unload" and "reload"
cycle — the OS manages physical RAM as a cache. From the application's perspective, the
model is always loaded. From the OS's perspective, it only uses RAM for pages that are
actually being accessed.

### Why Not mmap Everything?

ONNX Runtime does **not** use mmap by default. It reads the entire model file into
a malloc'd buffer and keeps it resident. This means:

- ASR model (670 MB ONNX) → 670 MB physical RAM the moment it loads
- TTS model (300 MB ONNX) → 300-700 MB physical RAM the moment it loads
- These stay resident until the `Ort::Session` object is destroyed

This is why ASR and TTS use lazy loading with idle unloading — the only way to
reclaim their RAM is to destroy the engine object.

---

## Server Memory Lifecycle

### Startup

```
edgescribe serve --port 8080
  │
  ├─ LLM/Vision (llama.cpp)
  │    mmap("qwen3-vl.gguf")     → instant, ~0 MB physical RAM
  │    Model handle ready         → no cold start on first request
  │
  ├─ ASR (onnxruntime)
  │    Not loaded yet             → 0 MB
  │    Will load on first /v1/transcribe/* request
  │
  └─ TTS (onnxruntime)
       Not loaded yet             → 0 MB
       Will load on first /v1/tts/* request

Server RAM at startup: ~50 MB (process + httplib + mmap metadata)
```

### Under Load

```
Time     Event                         Physical RAM    Notes
──────   ──────                        ────────────    ─────
0s       Server starts                 ~50 MB          mmap'd, not resident
5s       GET /v1/health                ~50 MB          No model access
10s      POST /v1/chat                 ~1.5 GB         LLM pages fault in
11s      Chat streaming...             ~1.5 GB         KV cache active
15s      Chat complete                 ~1.5 GB         Pages still hot
60s      No requests (1 min idle)      ~500 MB         OS reclaims LLM pages
90s      POST /v1/transcribe/file      ~1.7 GB         ASR lazy-loads (1.2 GB)
95s      Transcription complete        ~1.7 GB         ASR stays resident
120s     POST /v1/tts/synthesize       ~2.4 GB         TTS lazy-loads (700 MB)
125s     TTS complete                  ~2.4 GB         TTS stays resident
420s     5 min idle (no requests)      ~200 MB         ASR unloaded, TTS unloaded
                                                        LLM pages evicted by OS
600s     POST /v1/chat                 ~1.5 GB         LLM pages fault back in
                                                        (transparent, no "reload")
601s     POST /v1/transcribe/file      ~2.7 GB         ASR re-loads (~2s delay)
```

### Idle Unloading (ASR/TTS Only)

A background monitor thread checks every 30 seconds for idle ONNX engines:

```cpp
// Pseudocode — see api_server.cpp for actual implementation
while (server_running) {
    sleep(30 seconds);

    if (asr_loaded && asr_idle > 5 minutes) {
        unload ASR;  // frees ~1.2 GB
    }
    if (tts_loaded && tts_idle > 5 minutes) {
        unload TTS;  // frees ~700 MB
    }
    // LLM/Vision: never explicitly unloaded — OS manages via mmap
}
```

**Default idle timeout: 300 seconds (5 minutes)**. Defined as `kIdleTimeoutSeconds`
in `api_server.cpp`.

---

## Memory Budget by Scenario

### Scenario 1: Chat Only (Most Common)

```
LLM loaded (mmap)     → ~1.5 GB active, ~50 MB idle
ASR not loaded         → 0 MB
TTS not loaded         → 0 MB
────────────────────────────────
Peak:                    ~1.5 GB
Idle:                    ~50 MB
```

### Scenario 2: Transcription + SOAP Notes (Medical Workflow)

```
LLM loaded (mmap)     → ~1.5 GB (for SOAP note generation)
ASR loaded (lazy)     → ~1.2 GB (for transcription)
TTS not loaded         → 0 MB
────────────────────────────────
Peak:                    ~2.7 GB
After ASR idle unload:   ~1.5 GB (then ~50 MB when LLM pages evicted)
```

### Scenario 3: Full Stack (Server Mode, All Features Used)

```
LLM loaded (mmap)     → ~1.5 GB active
Vision (shared model) → ~3.5 GB active (with image embeddings)
ASR loaded (lazy)     → ~1.2 GB
TTS loaded (lazy)     → ~0.7 GB
────────────────────────────────
Peak:                    ~5.4 GB (all engines active simultaneously)
After 5 min idle:        ~50 MB  (ASR+TTS unloaded, LLM pages evicted)
```

### Scenario 4: Jetson Orin Nano (8 GB Shared Memory)

```
Available after OS:      ~6.5 GB
LLM loaded (mmap)     → ~1.5 GB active
ASR loaded (lazy)     → ~1.2 GB
TTS not loaded         → 0 MB (skip TTS to save memory)
────────────────────────────────
Peak:                    ~2.7 GB
Headroom:                ~3.8 GB (enough for CUDA kernels + OS)
```

---

## Comparison with Naive Loading

| Approach | RAM at Startup | RAM When Idle | Cold Start | Implementation |
|----------|:-:|:-:|:-:|:---:|
| **Load everything at startup** | 5.5 GB | 5.5 GB | None | Simple |
| **Lazy load everything** | 50 MB | 50 MB | 2-5s per engine | Complex |
| **Hybrid (current)** | **50 MB** | **50 MB** | **None for LLM** | Moderate |

The hybrid approach gives you the best of both worlds:
- **No cold start** for LLM/Vision (the most-used engines) — mmap is instant
- **Near-zero idle RAM** — OS manages mmap pages, ONNX engines unloaded
- **Minimal peak RAM** — only load what's actually being used

---

## Configuration

### Idle Timeout

The default idle timeout for ONNX engines (ASR/TTS) is **300 seconds (5 minutes)**.
To change it, modify `kIdleTimeoutSeconds` in `src/server/api_server.cpp`:

```cpp
static constexpr int kIdleTimeoutSeconds = 300;  // Change this value
```

### Disabling Lazy Loading

To preload all engines at startup (original behavior), modify `InitEngines()` in
`api_server.cpp` to eagerly create all engine objects. This uses more RAM but
eliminates any cold-start delay for ASR/TTS.

### Disabling Idle Unloading

To keep ONNX engines loaded permanently once they're first used, set the timeout
to a very large value or remove the idle monitor logic.

---

## Implementation Details

### Source Files

| File | What It Does |
|------|-------------|
| `src/server/api_server.cpp` | `EnsureASR()`, `EnsureTTS()` — lazy loaders |
| | `EnsureLLM()`, `EnsureVision()` — eager (mmap) availability check |
| | `StartIdleMonitor()` — background thread for ONNX unloading |
| | `InitEngines()` — startup: mmap LLM/Vision, defer ASR/TTS |

### Thread Safety

- Each engine has its own `std::mutex` (asr_mutex, llm_mutex, etc.)
- `EnsureASR()` / `EnsureTTS()` acquire the lock before checking/loading
- The idle monitor acquires locks before unloading
- Endpoint handlers acquire locks before using engines
- No deadlock risk — each handler touches only one engine's mutex

### Ensure* Pattern

Every endpoint uses this pattern:

```cpp
svr.Post("/v1/chat", [this](const Request& req, Response& res) {
    if (!EnsureLLM(res)) return;  // loads if needed, or returns 503

    std::lock_guard<std::mutex> lock(llm_mutex);
    std::string result = llm->Chat(system, prompt, max_length);
    res.set_content(JsonObj({{"text", JsonStr(result)}}), "application/json");
});
```

For LLM/Vision (mmap'd), `Ensure*` just checks the pointer is non-null.
For ASR/TTS (lazy), `Ensure*` loads the engine on first call and updates
the last-used timestamp.

---

## FAQ

**Q: Does mmap cause latency on first request?**

The first request will experience page faults as the OS loads model weights
from disk. On an SSD, this adds ~100-500ms total (spread across the generation).
On HDD, it can be 1-2s. Subsequent requests are instant because pages are cached.
This is much faster than a full model load (which would be 3-5s for a 990 MB model).

**Q: What if the system runs low on RAM?**

The OS evicts mmap'd LLM pages first (they can be re-read from disk). If RAM
is still insufficient, the ONNX engines' idle unloading kicks in after 5 min.
On extremely constrained systems (4 GB), you may want to only configure the
engines you actually need via `edgescribe serve --asr-model ... --vlm-model ...`
and omit the others.

**Q: Can I run on a 4 GB Raspberry Pi?**

Yes, but only one engine at a time. Configure the server with only the models
you need. The mmap'd LLM alone uses ~1.5 GB active, which fits in 4 GB with
OS overhead. Don't load ASR and LLM simultaneously on 4 GB.

**Q: What about GPU VRAM management?**

When using `--device cuda`, llama.cpp offloads model layers to GPU VRAM.
The mmap behavior applies to the CPU portion. VRAM-resident layers stay
loaded and are not subject to OS paging. For GPU deployments, memory
management is simpler — VRAM is dedicated and not shared with the OS.

# EDGESCRIBE GPU & Backend Strategy

## Current State

EDGESCRIBE v1.0 targets **CPU-only** inference on Windows x64 and macOS ARM64. The inference backends are:

| Engine | Backend | Format | Hardware |
|---|---|---|---|
| LLM | llama.cpp | GGUF | CPU (x86 AVX2 / ARM NEON+AMX) |
| Vision | llama.cpp | GGUF | CPU |
| ASR | onnxruntime-genai | ONNX | CPU |
| TTS | onnxruntime | ONNX | CPU |

---

## GPU Backend Options

### llama.cpp GPU backends (for LLM / Vision)

llama.cpp supports multiple GPU backends at compile time. You choose which to build with:

| Backend | GPU Vendor | Compile Flag | Performance | Maturity |
|---|---|---|---|---|
| **Vulkan** | Any (NVIDIA, AMD, Intel) | `-DGGML_VULKAN=ON` | ✅ Good | ✅ Stable |
| **CUDA** | NVIDIA only | `-DGGML_CUDA=ON` | ✅✅ Best on NVIDIA | ✅ Mature |
| **Metal** | Apple only | `-DGGML_METAL=ON` | ✅✅ Best on Mac | ✅ Mature |
| **SYCL** | Intel only | `-DGGML_SYCL=ON` | ✅ Good | ⚠️ Newer |
| **HIP/ROCm** | AMD only | `-DGGML_HIP=ON` | ✅ Good | ⚠️ Linux mainly |

### ONNX Runtime GPU backends (for ASR / TTS)

ONNX Runtime uses "Execution Providers":

| Execution Provider | GPU Vendor | Performance | Package Size |
|---|---|---|---|
| **CPU** (default) | None | Baseline | ~15 MB |
| **CUDA** | NVIDIA | ✅✅ Best | +~300 MB (CUDA libs) |
| **DirectML** | Any (Windows) | ✅ Good | +~30 MB |
| **CoreML** | Apple | ✅ Good | Built into macOS |
| **TensorRT** | NVIDIA | ✅✅ Best throughput | +~500 MB |
| **Vulkan** (via NNAPI) | Any | ⚠️ Limited | +~20 MB |

---

## TensorRT-LLM: Overhead Analysis

### What TensorRT-LLM is

TensorRT-LLM is NVIDIA's C++/CUDA inference runtime for LLMs. It's **not PyTorch** at runtime — models are compiled to GPU-specific engine binaries:

```
Build time (Python):   model.pt → trtllm-build → engine.trt   (needs Python, one-time)
Runtime (C++):         engine.trt → TensorRT-LLM C++ API       (pure C++, no Python)
```

### Overhead if added to EDGESCRIBE

| Metric | llama.cpp (current) | + TensorRT-LLM |
|---|---|---|
| **Binary size** | ~2 MB | +~50 MB (TRT-LLM libs) |
| **Runtime deps** | None | CUDA Toolkit (~500 MB), cuDNN (~300 MB) |
| **Total package** | ~2 MB | ~850 MB (with CUDA/cuDNN) |
| **Model format** | GGUF (~1.5 GB) | TRT engine (~2-3 GB, GPU-specific) |
| **GPU VRAM needed** | N/A (CPU) | ~2-4 GB |
| **Build complexity** | Simple | Very complex (CUDA toolkit, GPU arch targeting) |

### Performance gain

| Metric | llama.cpp CPU | llama.cpp CUDA | TensorRT-LLM |
|---|---|---|---|
| **Tokens/sec (Qwen 2B)** | ~15-25 | ~60-100 | ~100-150 |
| **First token latency** | ~500ms | ~100ms | ~50ms |
| **Best for** | Any hardware | NVIDIA GPU users | Max throughput, NVIDIA |

### Verdict on TensorRT-LLM

**Don't add it.** The overhead is enormous (~850 MB deps) and the benefit over llama.cpp CUDA is marginal for single-user edge use (~100 vs ~150 tok/s). TensorRT-LLM shines at high-concurrency serving (100+ users), not single-user edge.

---

## Recommended Strategy: Ship Model + Best Backend

Since EDGESCRIBE ships dedicated models (not a general model browser), you can **pick the optimal backend per model and per platform** at build time:

### Per-platform builds

```
EDGESCRIBE-win-x64-cpu.zip          ← llama.cpp CPU + ONNX CPU
EDGESCRIBE-win-x64-cuda.zip         ← llama.cpp CUDA + ONNX CUDA
EDGESCRIBE-win-x64-vulkan.zip       ← llama.cpp Vulkan + ONNX DirectML
EDGESCRIBE-osx-arm64.tar.gz         ← llama.cpp Metal + ONNX CoreML
```

### Why this works

You ship **3-4 fixed models** (nemotron, qwen3-vl, kokoro, embeddings). You know exactly:
- What model architectures you need
- What quantization formats work best
- What hardware the user has (they pick the right download)

Unlike Ollama/LM Studio (which must support thousands of models on unknown hardware), you can **optimize the binary for your exact models**.

### Recommended backend per platform

| Platform | LLM/Vision Backend | ASR/TTS Backend | Why |
|---|---|---|---|
| **Windows CPU** | llama.cpp (AVX2) | ONNX Runtime CPU | Universal, no deps |
| **Windows NVIDIA GPU** | llama.cpp CUDA | ONNX Runtime CUDA | Best NVIDIA perf |
| **Windows any GPU** | llama.cpp Vulkan | ONNX Runtime DirectML | Works on AMD, Intel, NVIDIA |
| **macOS ARM64** | llama.cpp Metal | ONNX Runtime CoreML | Best Apple Silicon perf |

---

## Vulkan vs CUDA: Which to Add First?

### Vulkan (Recommended first)

```
Pros:
✅ Works on ANY GPU (NVIDIA, AMD, Intel)
✅ No vendor lock-in
✅ Small dependency (~2 MB Vulkan loader, usually pre-installed)
✅ Supported on Windows, Linux, macOS (MoltenVK)
✅ llama.cpp Vulkan backend is stable and well-tested
✅ One build covers all GPU vendors

Cons:
⚠️ ~10-20% slower than CUDA on NVIDIA GPUs
⚠️ Slightly less mature than CUDA backend
```

### CUDA (Add second, NVIDIA-only)

```
Pros:
✅ Best performance on NVIDIA GPUs
✅ Most mature GPU backend in llama.cpp
✅ cuBLAS highly optimized

Cons:
❌ NVIDIA only — excludes AMD and Intel GPU users
❌ Requires CUDA Toolkit (~500 MB dependency)
❌ GPU-architecture-specific builds (sm_75, sm_86, sm_89, etc.)
❌ Licensing complexity (CUDA redistributable terms)
```

### Recommendation

```
v1.0:  CPU only (ship now)
v1.1:  + Vulkan (covers ALL GPUs with one build)
v1.2:  + Metal (macOS, auto-detected, near-zero effort)
v2.0:  + CUDA (NVIDIA-specific build for maximum perf, optional)
```

**Vulkan first** because:
1. One build covers NVIDIA + AMD + Intel GPUs
2. Tiny dependency (Vulkan loader is usually already installed)
3. Performance is 80-90% of CUDA — good enough for single-user edge
4. Users with AMD/Intel GPUs get GPU acceleration too (CUDA would exclude them)

---

## ONNX Runtime GPU Strategy (ASR / TTS)

### For Windows: DirectML

```cpp
// Just add one line — DirectML works on any Windows GPU
Ort::SessionOptions opts;
opts.AppendExecutionProvider("DML");  // NVIDIA, AMD, Intel — all work
```

- Built into Windows 10/11 — **zero additional dependencies**
- Works on any DirectX 12 GPU
- ~30% faster than CPU for ASR, significant for TTS
- No CUDA toolkit needed

### For macOS: CoreML

```cpp
// Apple's native ML framework — optimal on M-series chips
Ort::SessionOptions opts;
opts.AppendExecutionProvider("CoreML");
```

- Built into macOS — **zero additional dependencies**
- Leverages Apple Neural Engine (ANE) for massive speedup
- Automatic on ARM64 macOS builds

### Recommendation for ONNX Runtime

| Platform | Execution Provider | Dependency | Effort |
|---|---|---|---|
| **Windows** | DirectML | None (built into Windows) | Trivial — one line |
| **macOS** | CoreML | None (built into macOS) | Trivial — one line |
| **Linux** | CUDA (if NVIDIA) or CPU | CUDA Toolkit | Medium |

**DirectML and CoreML are free performance** — zero dependencies, one line of code each. Add them in v1.1.

---

## Package Size Impact

| Build Variant | Binary | Dependencies | Total Package |
|---|---|---|---|
| **CPU only** (current) | ~2 MB | ORT libs ~15 MB | ~17 MB |
| **+ Vulkan** | ~3 MB | + Vulkan loader ~2 MB | ~20 MB |
| **+ DirectML** (Windows) | ~3 MB | + DirectML.dll ~30 MB | ~48 MB |
| **+ CUDA** | ~5 MB | + CUDA/cuBLAS ~500 MB | ~520 MB |
| **+ TensorRT-LLM** | ~50 MB | + CUDA + cuDNN ~800 MB | ~850 MB |

**Vulkan and DirectML are nearly free in package size.** CUDA adds significant bloat. TensorRT-LLM is massive.

---

## Model Shipping Strategy

Since EDGESCRIBE ships dedicated models, you can pre-optimize per backend:

```
Models downloaded by user:
├── nemotron/           (ASR, ONNX — works on CPU, DirectML, CoreML)
├── qwen3-vl/           (LLM+Vision, GGUF — works on CPU, Vulkan, CUDA, Metal)
├── kokoro/             (TTS, ONNX — works on CPU, DirectML, CoreML)
└── embeddings/         (Embedding, ONNX — works on CPU, DirectML, CoreML)
```

**GGUF and ONNX formats are backend-agnostic** — the same model file works on CPU, Vulkan, CUDA, Metal, and DirectML. The user downloads models once and the binary auto-selects the best backend at runtime:

```cpp
// Auto-detect best backend at startup
std::string DetectBestBackend() {
    if (HasNvidiaGpu())    return "cuda";     // or "vulkan"
    if (HasAnyGpu())       return "vulkan";   // AMD, Intel
    if (IsAppleSilicon())  return "metal";
    return "cpu";
}
```

**No need for separate model downloads per backend.** One model, auto-detected backend. This is a major advantage of GGUF and ONNX over TensorRT (which requires GPU-specific engine compilation).

---

## Summary

| Decision | Value | Rationale |
|---|---|---|
| **v1.0** | CPU only | Ship it, get users |
| **v1.1 GPU** | Vulkan (LLM) + DirectML/CoreML (ASR/TTS) | Covers all GPUs, near-zero deps |
| **v1.2 GPU** | + Metal (macOS auto-detect) | Free perf on Apple Silicon |
| **v2.0 GPU** | + CUDA (optional NVIDIA build) | Maximum perf for NVIDIA users |
| **Skip** | TensorRT-LLM | Massive overhead, marginal single-user benefit |
| **Model format** | GGUF + ONNX (backend-agnostic) | Same model works on all backends |
| **Detection** | Auto-detect GPU at startup | User doesn't need to choose |

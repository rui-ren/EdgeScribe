# EDGESCRIBE Fine-Tuning — Strategy & Technical Plan

## Why Fine-Tuning Matters More on Edge

On-device models cannot compensate for weak output with cloud techniques:

```
Cloud (GPT-4, Claude):                    Edge (Qwen3-VL-2B Q4):
  175B+ params                              2B params
  + unlimited context                       + 16K context
  + agent loops (call tools 10x)            + one-shot (too slow for loops)
  + RAG with thousands of chunks            + RAG with 3-5 chunks
  = Can brute-force quality                 = Model must be precise by itself
```

A cloud LLM can be mediocre at SOAP notes and still produce good output via prompting, RAG, and agent retries. A 2B edge model **must nail it in one pass** — and fine-tuning is the only way to bake that precision into the weights.

---

## Revenue Model: Specialty LoRA Adapters

### Product Structure

```
Base EDGESCRIBE:  Free (open-source, general Qwen3-VL-2B)
                  Good enough for basic transcription + chat

Specialty Adapters: $99-299 per model (one-time download)
  ├── edgescribe-cardiology      — cardiology SOAP notes, echo reports
  ├── edgescribe-dermatology     — derm encounter notes, lesion descriptions
  ├── edgescribe-psychiatry      — therapy session notes, mental status exams
  ├── edgescribe-radiology       — radiology report generation, findings
  ├── edgescribe-orthopedics     — MSK exams, surgical notes
  ├── edgescribe-pediatrics      — pediatric encounters, growth assessments
  ├── edgescribe-legal           — legal dictation, deposition summaries
  └── edgescribe-veterinary      — veterinary encounter notes
```

### CLI Integration

```bash
# Download base model (free)
edgescribe pull qwen3-vl

# Download specialty adapter (paid — requires license key)
edgescribe pull cardiology --key XXXX-XXXX-XXXX

# Use with adapter
edgescribe chat --adapter cardiology "Generate SOAP notes for..."
edgescribe process --soap --adapter cardiology transcript.txt

# List installed adapters
edgescribe list --adapters
```

---

## QLoRA Adapters — Why Not Full Models

Instead of distributing fully fine-tuned models (1.5 GB each), distribute **LoRA adapters** — small weight patches that modify specific layers of the base model.

### How LoRA Works

```
Base model weights (frozen):     W₀  (1.5 GB, everyone has this)
LoRA adapter (trained):          ΔW = A × B  (50-100 MB, the paid product)
Final weights at runtime:        W = W₀ + ΔW

Where:
  W₀ = original Qwen3-VL-2B weights (unchanged)
  A  = low-rank matrix (e.g., rank 16-64)
  B  = low-rank matrix
  ΔW = the learned specialty knowledge, compressed into ~1-5% of total params
```

### Comparison

| Approach | File Size | Training Cost | Distribution | Speed Impact |
|---|---|---|---|---|
| **Full fine-tuned model** | ~1.5 GB | High (full model) | Heavy download | None |
| **QLoRA adapter** | ~50-100 MB | Low (only LoRA weights) | Quick download | ~5% slower (adapter merge) |
| **Merged + quantized** | ~1.5 GB | Medium | Heavy download | None (pre-merged) |

**Recommended**: Distribute QLoRA adapters for download speed. Optionally offer pre-merged GGUF for users who want zero runtime overhead.

### llama.cpp LoRA Support

llama.cpp natively supports loading LoRA adapters at runtime:

```cpp
// Load base model
llama_model_params mparams = llama_model_default_params();
llama_model* model = llama_model_load_from_file("qwen3-vl-2b-q4.gguf", mparams);

// Apply specialty adapter
llama_lora_adapter* adapter = llama_lora_adapter_init(model, "cardiology-lora.gguf");
// → Adapter weights merged into model at specified layers
// → ~50-100 MB additional memory
// → ~5% inference overhead (negligible)

// Use normally — model now specialized for cardiology
llama_context* ctx = llama_init_from_model(model, cparams);
// ... generate SOAP notes with improved cardiology knowledge ...

// Remove adapter if switching specialty
llama_lora_adapter_free(adapter);
```

**Key benefit**: Users can hot-swap adapters without reloading the 1.5 GB base model.

---

## Training Pipeline

### What You Need

| Requirement | How to Get It | Cost |
|---|---|---|
| **Training data** | Partner with clinics — offer free EDGESCRIBE in exchange for anonymized transcripts + SOAP notes | Free (partnership) |
| **GPU for training** | Cloud GPU: Lambda Labs, RunPod, or Vast.ai (A100 80GB) | ~$1-2/hr |
| **Fine-tuning framework** | Unsloth, Axolotl, or HuggingFace TRL (all open-source) | Free |
| **Domain expert** | Doctor or specialist to validate output quality | $50-200/hr consulting |
| **Quantization** | Convert trained adapter to GGUF format for llama.cpp | Free (llama.cpp tools) |

### Training Data Format

```jsonl
{"instruction": "Generate SOAP notes from this cardiology encounter transcript.", "input": "Doctor: Tell me about your chest pain... Patient: It started two days ago, sharp pain on the left side, worse when I breathe deeply...", "output": "SUBJECTIVE:\nPatient presents with a 2-day history of sharp left-sided chest pain, pleuritic in nature...\n\nOBJECTIVE:\n...\n\nASSESSMENT:\n...\n\nPLAN:\n..."}
```

### Training Process

```
Step 1: Collect Data
  ├── Partner with 2-3 clinics per specialty
  ├── Collect 500-2000 transcript → SOAP note pairs
  ├── De-identify all PHI (names, dates, MRNs)
  └── Expert review for quality

Step 2: Fine-Tune (QLoRA)
  ├── Base model: Qwen3-VL-2B (full precision for training)
  ├── Method: QLoRA (4-bit quantized base + FP16 LoRA weights)
  ├── Rank: 32-64 (higher = more capacity, larger adapter)
  ├── Target modules: q_proj, v_proj, k_proj, o_proj, gate_proj, up_proj, down_proj
  ├── Learning rate: 2e-4
  ├── Epochs: 3-5
  ├── Training time: ~2-4 hours on A100
  └── Training cost: ~$5-10 per adapter

Step 3: Evaluate
  ├── Hold-out test set (10-20% of data)
  ├── ROUGE / BERTScore against reference SOAP notes
  ├── Domain expert blind review (fine-tuned vs base model)
  └── Must show clear improvement over base model

Step 4: Package & Distribute
  ├── Merge LoRA weights → export to GGUF format
  ├── Quantize adapter (Q4_K_M or Q8_0)
  ├── Host on private HuggingFace repo or own server
  ├── Gate downloads with license key
  └── Add to EDGESCRIBE model manifest
```

### Training Cost Per Adapter

```
Data collection:    $0 (clinic partnership, in exchange for free software)
GPU training:       ~$5-10 (2-4 hrs on A100 at $1.50/hr)
Expert validation:  ~$200-400 (doctor reviews 50-100 outputs)
Total per adapter:  ~$200-400

Revenue per sale:   $149-299
Break even:         1-2 sales per adapter
```

**Zero marginal cost after creation.** Train once, sell unlimited downloads.

---

## Data Acquisition Strategy

### The Flywheel

```
1. Give EDGESCRIBE free to clinics
   ↓
2. Clinics generate transcripts + SOAP notes daily
   ↓
3. With consent, clinics share anonymized training data
   ↓
4. You fine-tune specialty adapters
   ↓
5. Sell adapters back to clinics (and others in the same specialty)
   ↓
6. Better models → more clinics adopt → more data → better models
```

### Data Privacy Requirements

| Requirement | Implementation |
|---|---|
| **De-identification** | Remove all PHI: names, dates, MRNs, addresses, phone numbers |
| **Consent** | Written data sharing agreement with each clinic |
| **HIPAA Safe Harbor** | Follow 18 Safe Harbor identifiers for de-identification |
| **Data stays local during collection** | EDGESCRIBE exports anonymized data only (never raw PHI) |
| **Audit trail** | Log what data was shared, when, by whom |

### Built-in Data Export (Future Feature)

```bash
# Clinic opts in to data sharing
edgescribe config set data-sharing enabled

# Export anonymized training pairs
edgescribe export --anonymize --format jsonl --output training-data.jsonl
# → Strips all PHI using NER (Named Entity Recognition)
# → Outputs instruction/input/output pairs ready for training
```

---

## Adapter Distribution

### Model Manifest Extension

```cpp
// model_manager.cpp — add adapter entries
{"cardiology", "EDGESCRIBE Cardiology Adapter", "edgescribe/cardiology-lora",
 "Specialized for cardiology SOAP notes and echo reports",
 "adapter", "adapter_config.json", 80, false, ""},
```

### License Key Validation

```bash
# User purchases adapter on website → receives license key
edgescribe pull cardiology --key EDGS-CARD-XXXX-XXXX

# Key validated against simple API (or offline hash check)
# Adapter downloaded from private HuggingFace repo or own CDN
```

For v1, a simple offline approach works:
- Generate license keys as signed hashes (no server needed)
- Validate locally against a public key embedded in the binary
- No internet required after initial download

---

## Competitive Moat

| Advantage | Detail |
|---|---|
| **Data moat** | Once you have clinic partnerships + anonymized training data, competitors can't replicate it |
| **Small file = easy delivery** | 50-100 MB adapter vs 1.5 GB full model — users download in seconds |
| **Recurring revenue** | New specialties, updated adapters, version upgrades |
| **Hard to pirate** | Adapters are specific to your exact base model quantization. Different quant = incompatible adapter. |
| **Scales infinitely** | Train once on cloud GPU ($5-10), sell unlimited downloads, zero marginal cost |
| **Network effect** | More users → more data partnerships → better adapters → more users |

---

## Roadmap

### Phase 1: Foundation (v1.0-v1.1)
- Ship base EDGESCRIBE with general Qwen3-VL-2B
- Build user base, get feedback on output quality
- Identify which specialties need the most improvement

### Phase 2: First Adapters (v1.3+)
- Partner with 2-3 clinics (ideally different specialties)
- Collect 500+ transcript-SOAP pairs per specialty
- Train and validate first 2-3 adapters (e.g., cardiology, psychiatry, dermatology)
- Add LoRA loading support to LlmEngine
- Add `--adapter` flag to CLI
- Launch adapter store on website

### Phase 3: Scale (v2.0+)
- Expand to 10+ specialties
- Add built-in anonymized data export tool
- Automated training pipeline (new data → retrain → push update)
- Adapter version management and updates
- Community adapters (users fine-tune and share their own)

---

## Summary

```
Fine-tuning is NOT optional for edge AI — it's the primary quality lever.
Cloud can brute-force quality with scale. Edge must bake quality into weights.

Revenue model:
  Base model:        Free (open-source, builds trust + adoption)
  Specialty adapters: $149-299 each (QLoRA, 50-100 MB)
  Training cost:     ~$200-400 per adapter (one-time)
  Margin:            ~99% after first 2 sales
  Moat:              Clinic data partnerships (can't be replicated)
```

---

## Training Runtime: PyTorch → Convert → Deploy

### Why You Cannot Train on GGUF or ONNX

Training requires **backpropagation** — computing gradients and updating weights. Inference runtimes strip this out entirely to be fast and small:

```
Training (cloud GPU):                    Deployment (user's device):
┌─────────────────────┐                 ┌──────────────────────┐
│  PyTorch (FP16/BF16) │                │  llama.cpp (GGUF Q4)  │
│  Full precision       │  ──convert──► │  or ONNX Runtime      │
│  Backpropagation ✅   │                │  Forward-only ✅       │
│  Gradient updates ✅  │                │  Backpropagation ❌    │
│  GPU required         │                │  CPU optimized         │
└─────────────────────┘                 └──────────────────────┘
```

| Runtime | Forward pass (inference) | Backward pass (training) | On-device training? |
|---|---|---|---|
| **PyTorch** | ✅ | ✅ | ✅ But needs GPU + lots of RAM |
| **llama.cpp (GGUF)** | ✅ | ❌ No autograd engine | ❌ Not possible |
| **ONNX Runtime** | ✅ | ⚠️ ORT Training exists but very limited | ❌ Not practical on CPU |
| **GGML** | ✅ | ❌ No autograd engine | ❌ Not possible |

**llama.cpp and ONNX Runtime are inference-only.** They strip out gradient computation, optimizer states, and backward graphs. You physically cannot fine-tune a GGUF or ONNX model on the user's device.

### Per-Engine Fine-Tuning Strategy

| Engine | Model | Train With | Convert To | Deploy With |
|---|---|---|---|---|
| **LLM** | Qwen3-VL-2B | PyTorch + QLoRA (Unsloth/Axolotl) | GGUF | llama.cpp |
| **Vision** | Qwen3-VL-2B | PyTorch + QLoRA | GGUF | llama.cpp |
| **ASR** | Nemotron 0.6B | PyTorch (NVIDIA NeMo) | ONNX | onnxruntime-genai |
| **TTS** | Kokoro | PyTorch | ONNX | onnxruntime |

### Conversion Pipeline

```
PyTorch model (FP16, ~4 GB)
    │
    ├──► For LLM/Vision (llama.cpp):
    │    1. Export to HuggingFace safetensors
    │    2. Convert: python convert_hf_to_gguf.py --outtype f16
    │    3. Quantize: llama-quantize model-f16.gguf model-q4_k_m.gguf Q4_K_M
    │    4. Result: ~1.5 GB GGUF file
    │
    └──► For ASR/TTS (ONNX Runtime):
         1. Export: torch.onnx.export(model, ...)
         2. Optimize: python -m onnxruntime.transformers.optimizer
         3. Quantize: onnxruntime quantize_dynamic (INT8)
         4. Result: ONNX model file
```

For LoRA adapters specifically:
```
QLoRA adapter (PyTorch, ~200 MB FP16)
    │
    1. Export adapter weights to safetensors
    2. Convert: python convert_lora_to_gguf.py
    3. Result: ~50-100 MB GGUF LoRA file
    4. User loads at runtime: llama_lora_adapter_init(model, "adapter.gguf")
```

---

## Why On-Device Training Is Not Practical (2026)

The dream: user clicks "learn from my style" → model improves locally on their laptop.

**Reality:**

| Requirement | Training Needs | User's Laptop Has |
|---|---|---|
| **RAM** | ~8-16 GB for QLoRA on 2B model | ✅ 16 GB (barely enough) |
| **Compute** | GPU with FP16 (A100, RTX 4090) | ❌ CPU only (100x slower) |
| **Time** | ~2-4 hrs on A100 | ❌ ~200+ hrs on CPU |
| **Framework** | PyTorch + CUDA installed | ❌ Not on doctor's laptop |
| **Complexity** | Learning rate, epochs, validation | ❌ User is a doctor |
| **Storage** | FP16 model + optimizer states (~12 GB) | ⚠️ Tight |

**A fine-tune that takes 3 hours on an A100 GPU would take 200+ hours (8+ days) on a laptop CPU.** This is not viable.

### What IS Practical On-Device (Without Training)

Instead of actual fine-tuning, **personalize** the model on-device using techniques that require zero training:

```
1. System prompt presets   — Store user's preferred instructions
                             "Always use bullet points for Assessment"

2. RAG with user data      — Retrieve from user's past transcripts/notes
                             "How did I document this last time?"

3. Few-shot examples       — Store 3-5 of user's best SOAP notes
                             Inject into prompt as examples

4. Preference memory       — SQLite key-value store
                             "Dr. Smith prefers ICD-10 codes in Plan"

5. Template library        — User-defined SOAP templates per specialty
                             Applied as structured output format
```

These all work within the existing memory system (v1.2) and RAG (v1.3) architecture. They give the **experience of a personalized model** without any weight updates.

### The Correct Workflow

```
You (the developer):
  1. Collect anonymized training data from clinic partners
  2. Fine-tune on cloud GPU with QLoRA ($5-10 per adapter)
  3. Convert: PyTorch → GGUF adapter (50-100 MB)
  4. Distribute via edgescribe pull <specialty> --key XXXX

User (the doctor):
  1. edgescribe pull cardiology --key XXXX
  2. Adapter downloaded (~50-100 MB, takes seconds)
  3. Model now specialized — no training, no GPU, no complexity
  4. Additionally: system prompts + RAG + memory personalize further
```

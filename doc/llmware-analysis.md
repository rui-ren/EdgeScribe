# llmware vs EDGESCRIBE — Competitive Analysis & Feature Ideas

## What is llmware?

[llmware](https://github.com/llmware-ai/llmware) is a **Python framework** for building knowledge-based LLM applications on local devices. It's a toolkit/library (pip install), not a standalone app.

### Key Stats
- Language: Python
- License: Apache 2.0
- Stars: ~14K
- Target: Enterprise developers building RAG pipelines
- Models: 300+ in catalog, including 50+ custom fine-tuned models

---

## Architecture Comparison

| | llmware | EDGESCRIBE |
|---|---|---|
| **Language** | Python | C++ |
| **Type** | Developer library (pip install) | Standalone app (single binary) |
| **Target user** | Developers building AI apps | End users (doctors, clinicians) |
| **Inference** | GGUF, ONNX, OpenVINO, PyTorch, Cloud APIs | llama.cpp + ONNX Runtime |
| **Models** | 300+ in catalog (generic + custom) | 3 curated (ASR + LLM/Vision + TTS) |
| **RAG** | Full pipeline (parse → chunk → embed → retrieve → generate) | Planned (v1.2) |
| **ASR** | ❌ None | ✅ Full (Nemotron, real-time streaming) |
| **TTS** | ❌ None | ✅ Full (Kokoro, multi-voice) |
| **Vision** | ❌ None | ✅ Full (Qwen3-VL, OCR) |
| **Document parsing** | ✅ Excellent (PDF, DOCX, XLSX, PPTX, CSV, HTML, JSON) | ❌ Text only |
| **Database** | MongoDB, SQLite, Postgres, vector DBs | SQLite |
| **UI** | ❌ None (code-only) | ✅ Web UI + native desktop app |
| **Memory** | ❌ No conversation memory | ✅ SQLite auto-save |
| **Deployment** | Python environment required | Zero-dependency binary |

---

## llmware's Unique Strengths (What EDGESCRIBE Can Learn)

### 1. SLIM Models — Structured Output from Small Models

This is llmware's **best idea**. SLIM (Structured Language Instruction Models) are tiny (1-3B) models fine-tuned for ONE specific task that output structured data:

```python
# SLIM-NER: extract entities
result = slim_ner.inference("Dr. Smith prescribed metformin 500mg for diabetes")
# → {"entities": [{"name": "Dr. Smith", "type": "person"},
#                  {"name": "metformin 500mg", "type": "medication"},
#                  {"name": "diabetes", "type": "condition"}]}

# SLIM-Sentiment: classify sentiment
result = slim_sentiment.inference("The patient is feeling much better")
# → {"sentiment": "positive"}

# SLIM-SQL: generate SQL from natural language
result = slim_sql.inference("Find all patients with A1C above 7")
# → {"sql": "SELECT * FROM patients WHERE a1c > 7.0"}
```

**Why this matters for EDGESCRIBE:**
- Your Qwen3-VL-2B does everything (chat, SOAP, summarize) — but a dedicated 500M SLIM model for entity extraction would be faster AND more accurate
- Structured output (JSON) is more reliable from a purpose-trained model than from prompt engineering
- Multiple tiny models (500M each) can run sequentially faster than one 2B model doing everything

**Feature idea: EDGESCRIBE SLIM models**
```
slim-ner         (500M) — Extract medications, conditions, procedures from transcript
slim-icd10       (500M) — Map diagnoses to ICD-10 codes
slim-soap-router (500M) — Classify transcript sections into S/O/A/P
slim-drug-check  (500M) — Flag potential drug interactions
```

### 2. Document Parsing (PDF, DOCX, XLSX)

llmware has excellent document ingestion:

```python
lib = Library().create_new_library("patient_records")
lib.add_files("/path/to/clinical/documents/")
# Automatically handles: PDF, DOCX, XLSX, PPTX, CSV, HTML, JSON
# → parsed, text chunked, indexed, ready for RAG
```

**Why this matters for EDGESCRIBE:**
- Your RAG plan (v1.2) only supports TXT/CSV/MD
- Clinics have PDFs everywhere (lab reports, guidelines, insurance forms)
- Adding PDF support would make the knowledge base much more useful

**Feature idea:** Add PDF parsing without a heavy library:
```bash
# Option A: Shell out to pdftotext (zero binary overhead)
edgescribe kb add report.pdf
# → internally: pdftotext report.pdf temp.txt → ingest temp.txt

# Option B: Lightweight C library (poppler or mupdf)
# Adds ~2 MB but handles complex PDFs better
```

### 3. Library / Knowledge Base Abstraction

llmware's "Library" concept is clean — a knowledge base is a first-class object:

```python
# Create isolated knowledge bases for different purposes
guidelines = Library().create_new_library("clinical_guidelines")
patient_records = Library().create_new_library("patient_records")

# Each library has its own embeddings, indexes, and queries
guidelines.add_files("/guidelines/")
guidelines.install_new_embedding("mini-lm-sbert", "sqlite")
```

**Feature idea for EDGESCRIBE:** Named knowledge bases
```bash
# Instead of one flat KB, let users organize by topic
edgescribe kb create "cardiology-guidelines"
edgescribe kb add --collection "cardiology-guidelines" guidelines.pdf
edgescribe kb create "drug-reference"
edgescribe kb add --collection "drug-reference" drug_interactions.csv

# Query searches specific or all collections
edgescribe chat --kb "cardiology-guidelines" "What's first-line for AFib?"
```

### 4. Multiple Embedding Models

llmware supports swapping embedding models per library:

```python
lib.install_new_embedding("mini-lm-sbert", "milvus")
lib.install_new_embedding("industry-bert-sec", "chromadb")
```

**For EDGESCRIBE:** Not needed now (one embedding model is fine), but good to design the schema to support this later.

### 5. Model Catalog / Easy Model Switching

llmware's 300+ model catalog with unified API is powerful:

```python
model = ModelCatalog().load_model("llmware/bling-phi-3-gguf")
output = model.inference("question", add_context="context")
```

**Feature idea:** Model profiles in EDGESCRIBE
```bash
# User can switch between quality levels
edgescribe config set llm-quality high    # Uses Qwen3-VL-7B (slower, better)
edgescribe config set llm-quality fast    # Uses Qwen3-VL-2B (faster, good enough)
edgescribe config set llm-quality minimal # Uses TinyLlama 1B (fastest, basic)
```

---

## What EDGESCRIBE Does That llmware Doesn't

| Feature | EDGESCRIBE | llmware |
|---|---|---|
| **Speech-to-text** | ✅ Real-time ASR | ❌ No audio at all |
| **Text-to-speech** | ✅ Multi-voice TTS | ❌ None |
| **Vision / OCR** | ✅ Image analysis | ❌ None |
| **End-user app** | ✅ Desktop app with UI | ❌ Code-only library |
| **Single binary** | ✅ 2 MB, no dependencies | ❌ Python + pip + dependencies |
| **Medical workflows** | ✅ SOAP notes, terminology | ❌ Generic enterprise |
| **Native performance** | ✅ C++, zero-boundary | ❌ Python overhead |

**Your multi-modal pipeline (ASR → LLM → TTS) is your biggest differentiator.** llmware doesn't even try to do audio.

---

## Features to Consider Building

### High Priority (v1.2-v1.3)

| Feature | Inspired by | Effort | Value |
|---|---|---|---|
| **PDF parsing** | llmware's parser | Low (shell to pdftotext) | High — clinics have PDFs |
| **Named knowledge bases** | llmware's Library | Low (add collection column to chunks table) | Medium — organization |
| **Structured output (JSON mode)** | llmware's SLIM | Medium (constrained decoding in llama.cpp) | High — reliable automation |
| **Entity extraction** | llmware's SLIM-NER | Medium (fine-tune small model) | High — drug/condition detection |

### Medium Priority (v1.4+)

| Feature | Inspired by | Effort | Value |
|---|---|---|---|
| **ICD-10 code mapping** | llmware's industry models | Medium (fine-tune classifier) | High for billing |
| **Multi-embedding support** | llmware's Library | Low (schema supports it) | Low — one model is fine |
| **Model quality profiles** | llmware's catalog | Low (config switch) | Medium — power users |
| **Batch document processing** | llmware's add_files | Low (loop in code) | Medium — bulk ingestion |

### Skip (not relevant)

| Feature | Why skip |
|---|---|
| **Multiple vector DB backends** | SQLite is enough for edge — don't add Milvus/Chroma complexity |
| **Cloud API fallback** | Contradicts privacy story (see `cloud-api-enterprise.md`) |
| **Python bindings** | Your users aren't developers — they use the CLI/UI |
| **300+ model catalog** | You curate 3-5 models. Quality over quantity. |

---

## The Key Insight from llmware

### "Use the smallest model that can do the job"

llmware's philosophy is: **don't use a 70B model for classification when a 500M model does it better.** They stack multiple tiny specialized models instead of one big general-purpose model.

Applied to EDGESCRIBE:

```
Current approach (one model does everything):
  Qwen3-VL-2B → SOAP notes, summarize, chat, fix terms, entity extraction
  → OK quality on everything, great on nothing

llmware-inspired approach (specialized models):
  Qwen3-VL-2B → SOAP notes, chat, summarize (general tasks)
  slim-ner     → entity extraction (medications, conditions) — FAST, accurate
  slim-icd10   → ICD-10 coding — deterministic, structured output
  slim-router  → classify transcript sections — instant routing
  → Each model excellent at its specific task
```

This maps perfectly to your **workflow pipeline** architecture (`doc/workflow-pipelines.md`) — each pipeline step could use a different specialized model.

---

## Summary

| From llmware | For EDGESCRIBE | When |
|---|---|---|
| **SLIM concept** | Tiny specialized models for extraction/classification | v1.3+ (fine-tune on clinical data) |
| **Document parsing** | Add PDF support to knowledge base | v1.2 (shell to pdftotext) |
| **Named libraries** | Organize KB by collection/topic | v1.2 (add column to schema) |
| **Structured output** | JSON mode for reliable automation | v1.3 (llama.cpp grammar-based sampling) |
| **Model catalog** | Quality profiles (fast/balanced/best) | v1.4 (config switch) |
| **"Smallest model" philosophy** | Stack small models in pipelines | v2.0 (SLIM-style fine-tunes) |

**Don't try to become llmware.** It's a Python developer toolkit. You're a desktop app for non-technical users. Take the best ideas (SLIM models, document parsing, structured output) and integrate them into your clinical workflow product.

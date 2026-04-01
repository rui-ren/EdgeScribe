# EDGESCRIBE Memory System — Design Document

## The Problem

Today, every EDGESCRIBE session is stateless:

```
Session 1: "Patient has diabetes, A1C is 7.2"  → transcript → lost
Session 2: "Summarize today's patients"          → "I have no context"
```

A clinical assistant MUST remember:
- What was said in past sessions
- Patient context across visits
- Doctor preferences and templates
- Medical knowledge for accurate responses

---

## Design Decisions

### Why SQLite, not a vector database?

| Factor | EDGESCRIBE (edge/laptop) | When you'd need FAISS/Chroma |
|---|---|---|
| **Entries** | Hundreds to low thousands | Millions+ |
| **Search time** | <1ms brute-force on 10K vectors | 100ms+ brute-force on 1M+ |
| **Dependencies** | Zero — SQLite is a single `.c` file | FAISS, Annoy, sqlite-vss |
| **Accuracy** | ✅ Exact (brute-force cosine) | ⚠️ Approximate (ANN) |
| **Complexity** | Trivial | Index tuning, rebuilds |
| **Deployment** | ✅ Compiles into the binary | ❌ Extra shared libs |

**Decision**: Plain SQLite. Store embeddings as raw `float[]` BLOBs. Compute cosine similarity in C++ at query time. No vector index, no extensions.

### Why not use Qwen3-VL for embeddings?

The current `LlmEngine` and `VisionEngine` wrap `onnxruntime-genai` for **text generation only**. The GenAI API does not expose hidden-state vectors — it only returns decoded tokens. Extracting embeddings would require:

1. Bypassing the GenAI high-level API
2. Running the ONNX model directly via `onnxruntime` C++ API
3. Extracting the last hidden state from the transformer

This is complex and fragile. Instead:

**Decision**: Use a dedicated lightweight embedding model (`all-MiniLM-L6-v2`, ~80 MB ONNX) loaded via raw `onnxruntime` C++ API (already linked for TTS). Start with FTS5 keyword search (zero models needed), add semantic embeddings as an upgrade path.

---

## Architecture: Two-Tier Memory

```
┌─────────────────────────────────────────────────────────────────┐
│                     EDGESCRIBE Memory System                    │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  SHORT-TERM MEMORY (in-process, per session)              │  │
│  │                                                           │  │
│  │  • Rolling window of recent conversation turns            │  │
│  │  • Current transcript buffer                              │  │
│  │  • Auto-expires on exit                                   │  │
│  │  • Storage: std::vector<ChatMessage> in memory            │  │
│  └───────────────────────────────────────────────────────────┘  │
│                         │ auto-persist on session end           │
│                         ▼                                       │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  LONG-TERM MEMORY (SQLite, persistent across sessions)    │  │
│  │                                                           │  │
│  │  • All past transcripts with timestamps                   │  │
│  │  • Chat conversations                                     │  │
│  │  • Generated notes (SOAP, summaries)                      │  │
│  │  • User preferences                                       │  │
│  │  • Knowledge base documents (chunked)                     │  │
│  │  • Embedding vectors (BLOB, brute-force cosine search)    │  │
│  │  • FTS5 keyword index                                     │  │
│  │  • Storage: ~/.EDGESCRIBE/edgescribe.db                   │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Short-Term Memory (Layer 1)

**Purpose**: Keep context within a single running session.

**Lifetime**: Created on launch, lost on exit. Persisted to long-term on session close.

```cpp
namespace edgescribe::memory {

struct SessionContext {
    int64_t session_id;                     // DB row id (set after persist)
    std::string source;                     // "live", "file:meeting.wav"
    std::vector<ChatMessage> chat_history;  // rolling window, max N turns
    std::string transcript_buffer;          // accumulated transcript text
    std::chrono::steady_clock::time_point started_at;

    // Sliding window: keep last N messages to fit in context window
    void AddMessage(const ChatMessage& msg);
    std::string GetContextForPrompt(int max_tokens = 2048) const;
};

}  // namespace edgescribe::memory
```

**How it integrates with the LLM**:

```
System: You are a medical assistant.
[Memory context injected here — recent transcript + relevant past memories]
Recent transcript: "Patient has diabetes, A1C is 7.2, on metformin 500mg BID..."

User: Generate SOAP notes
```

---

## Long-Term Memory (Layer 2)

**Purpose**: Persistent store for all sessions, searchable via FTS5 and optional semantic embeddings.

**Storage**: `~/.EDGESCRIBE/edgescribe.db` (SQLite, same dir as model cache)

### Database Schema

```sql
-- Transcription/chat sessions
CREATE TABLE sessions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    started_at  TEXT NOT NULL DEFAULT (datetime('now')),
    ended_at    TEXT,
    duration_s  REAL,
    source      TEXT NOT NULL,      -- 'live', 'file:meeting.wav'
    model       TEXT                -- 'nemotron', 'qwen3-vl'
);

-- Transcript segments (from ASR)
CREATE TABLE transcripts (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
    timestamp_s REAL,               -- seconds from session start
    speaker     TEXT,               -- 'Speaker 1', NULL if no diarization
    text        TEXT NOT NULL,
    embedding   BLOB                -- float32[] vector, NULL until embedded
);

-- Chat messages
CREATE TABLE messages (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
    role        TEXT NOT NULL,       -- 'system', 'user', 'assistant'
    content     TEXT NOT NULL,
    created_at  TEXT DEFAULT (datetime('now'))
);

-- Generated outputs (SOAP, summaries, etc.)
CREATE TABLE notes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER REFERENCES sessions(id) ON DELETE SET NULL,
    type        TEXT NOT NULL,       -- 'soap', 'summary', 'fix-terms'
    input_text  TEXT,
    output_text TEXT NOT NULL,
    created_at  TEXT DEFAULT (datetime('now'))
);

-- Knowledge base documents (user-uploaded)
CREATE TABLE documents (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    filename    TEXT NOT NULL,
    title       TEXT,
    content     TEXT NOT NULL,
    added_at    TEXT DEFAULT (datetime('now'))
);

-- Document chunks for retrieval
CREATE TABLE chunks (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
    chunk_index INTEGER NOT NULL,
    text        TEXT NOT NULL,
    embedding   BLOB,               -- float32[] vector, NULL until embedded
    token_count INTEGER
);

-- User preferences (key-value)
CREATE TABLE preferences (
    key         TEXT PRIMARY KEY,
    value       TEXT NOT NULL,
    updated_at  TEXT DEFAULT (datetime('now'))
);

-- Full-text search indexes
CREATE VIRTUAL TABLE transcripts_fts USING fts5(
    text, speaker,
    content='transcripts', content_rowid='id'
);

CREATE VIRTUAL TABLE notes_fts USING fts5(
    output_text,
    content='notes', content_rowid='id'
);

CREATE VIRTUAL TABLE chunks_fts USING fts5(
    text,
    content='chunks', content_rowid='id'
);
```

### Auto-Save Behavior

Transcription sessions auto-persist — no manual save:

1. **Session start** → `INSERT INTO sessions` → get `session_id`
2. **Each decoded segment** → `INSERT INTO transcripts` (with timestamp)
3. **Session end** → `UPDATE sessions SET ended_at, duration_s`
4. **SOAP/summary generated** → `INSERT INTO notes`
5. **Chat messages** → `INSERT INTO messages` per turn

---

## Embedding & Retrieval Strategy

### Embedding Model: all-MiniLM-L6-v2

**Decision**: Use `all-MiniLM-L6-v2` ONNX (~80 MB) as a dedicated embedding model. It is trained with contrastive learning specifically for semantic similarity search — far better retrieval quality than repurposing Qwen3-VL's hidden states.

| Property | Value |
|---|---|
| Model | `all-MiniLM-L6-v2` |
| Format | ONNX (runs via onnxruntime C++ API — already linked for TTS) |
| Size on disk | ~80 MB |
| RAM at runtime | ~100 MB |
| Output dimension | 384 (float32) |
| Inference time | ~2-5ms per sentence (CPU) |
| Training objective | Contrastive learning (sentence similarity) |
| Storage per embedding | 1,536 bytes (384 × 4 bytes) |

**Why not Qwen3-VL for embeddings?** Qwen3-VL internally computes embeddings (every transformer does), and llama.cpp can expose them via `llama_get_embeddings()`. However, LLM embeddings are trained for next-token prediction, not similarity search. They cluster by topic/style rather than semantic meaning, producing mediocre retrieval results. A purpose-built embedding model gives significantly better recall.

**Integration**: Loaded via raw `onnxruntime` C++ API (same runtime as TTS engine — zero new dependencies). Added to model manifest as `edgescribe pull embeddings`.

### Cosine Similarity (C++)

No vector index needed at edge scale. Brute-force in C++:

```cpp
float CosineSimilarity(const float* a, const float* b, int dim) {
    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (int i = 0; i < dim; i++) {
        dot    += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
}
```

For 10K entries × 384-dim vectors: **<1ms** on any modern CPU.

### Retrieval Flow

```
Query comes in
    ↓
1. Embed query → float[384] vector  (via embedding model)
2. SELECT id, embedding, text FROM chunks WHERE embedding IS NOT NULL
3. Compute cosine similarity in C++ loop
4. Sort by similarity, take top-K
5. Inject top-K text chunks into LLM prompt as context
```

### Hybrid Search (FTS5 + Embeddings)

```
Query → FTS5 keyword search  → candidate set A
Query → embedding similarity  → candidate set B
Merge A ∪ B, re-rank, take top-K
```

FTS5 catches exact keyword matches that embeddings might miss. Embeddings catch semantic matches that keywords miss. Combined = best recall.

### Embedding Model

### Embedding Model

| Phase | Search Method | Model | Quality |
|---|---|---|---|
| **Phase 1** | FTS5 keyword only | None | Good for exact matches |
| **Phase 2** | FTS5 + cosine similarity | `all-MiniLM-L6-v2` (~80 MB ONNX) | Best — trained for semantic similarity |

`all-MiniLM-L6-v2` runs via raw `onnxruntime` C++ API (already linked for TTS — zero new dependencies). Outputs 384-dim vectors, ~2-5ms per sentence on CPU.

```bash
edgescribe pull embeddings   # downloads all-MiniLM-L6-v2 ONNX (~80 MB)
```

### When to re-evaluate this design

If the database exceeds **50K+ embedded entries** and search feels slow, add `sqlite-vss` (a single `.c` file SQLite extension for approximate nearest-neighbor). For a personal edge tool, this threshold is unlikely to be reached.

---

## Module Design

### New module: `src/memory/`

```
src/memory/
├── memory_store.h          -- MemoryStore class (public API)
├── memory_store.cpp         -- SQLite operations, schema migration
├── session_context.h       -- SessionContext (short-term, in-memory)
└── embedding_engine.h/cpp  -- (Phase 2) ONNX embedding model wrapper
```

### MemoryStore API

```cpp
namespace edgescribe::memory {

class MemoryStore {
 public:
    explicit MemoryStore(const std::string& db_path);
    ~MemoryStore();

    // Session lifecycle
    int64_t StartSession(const std::string& source, const std::string& model);
    void EndSession(int64_t session_id);

    // Transcript persistence
    void SaveTranscript(int64_t session_id, double timestamp_s,
                        const std::string& text, const std::string& speaker = "");

    // Chat persistence
    void SaveMessage(int64_t session_id, const std::string& role,
                     const std::string& content);

    // Notes persistence
    void SaveNote(int64_t session_id, const std::string& type,
                  const std::string& input, const std::string& output);

    // Knowledge base
    int64_t AddDocument(const std::string& filename, const std::string& content);
    void RemoveDocument(int64_t document_id);
    std::vector<DocumentInfo> ListDocuments();

    // Search (FTS5)
    std::vector<SearchResult> SearchTranscripts(const std::string& query, int limit = 10);
    std::vector<SearchResult> SearchNotes(const std::string& query, int limit = 10);
    std::vector<SearchResult> SearchKnowledge(const std::string& query, int limit = 10);

    // Semantic search (Phase 2 — requires embedding model)
    std::vector<SearchResult> SemanticSearch(const float* query_vec, int dim,
                                             int limit = 5);

    // History queries
    std::vector<SessionInfo> GetTodaySessions();
    std::vector<SessionInfo> GetRecentSessions(int days = 7);
    std::string GetSessionTranscript(int64_t session_id);

    // Context building (for LLM prompt injection)
    std::string BuildContext(const std::string& query, int max_tokens = 2048);

    // Preferences
    void SetPreference(const std::string& key, const std::string& value);
    std::string GetPreference(const std::string& key,
                              const std::string& default_val = "");

    // Maintenance
    void DeleteSessionsBefore(const std::string& iso_date);
    void Vacuum();

 private:
    struct Impl;
    std::unique_ptr<Impl> impl_;  // Pimpl — hides sqlite3*
};

}  // namespace edgescribe::memory
```

Follows existing patterns: Pimpl, `std::unique_ptr<Impl>`, no copy, exceptions on error.

---

## CLI Commands

```bash
# History
edgescribe history                     # List recent sessions
edgescribe history today               # Today's sessions
edgescribe history search "metformin"  # FTS5 search across transcripts
edgescribe history show <id>           # Show full transcript for session
edgescribe history delete --before 2026-01-01

# Knowledge base
edgescribe kb add guidelines.txt       # Ingest document
edgescribe kb list                     # List documents
edgescribe kb search "amoxicillin"     # Search chunks
edgescribe kb remove <id>             # Remove document

# Memory-augmented chat (automatic)
edgescribe chat "Summarize today's patients"
# → MemoryStore::BuildContext() retrieves today's transcripts
# → Injected into LLM prompt automatically
```

## REST API Endpoints

```
GET    /v1/memory/sessions              — List recent sessions
GET    /v1/memory/sessions/today        — Today's sessions
GET    /v1/memory/sessions/:id          — Get session transcript
POST   /v1/memory/search               — Search transcripts/notes { "query": "..." }
DELETE /v1/memory/sessions/:id          — Delete a session

POST   /v1/kb/add                       — Upload document (multipart)
GET    /v1/kb/list                      — List documents
POST   /v1/kb/search                   — Search knowledge base { "query": "..." }
DELETE /v1/kb/:id                       — Remove document
```

---

## Data Flow with Memory

### Before (stateless)

```
User speaks → ASR → transcript (lost on exit)
User asks "summarize" → LLM has no context → generic response
```

### After (with memory)

```
User speaks → ASR → transcript → auto-saved to SQLite with timestamps
                               → session context updated in memory

User asks "summarize today's patients"
    → BuildContext() queries: SELECT text FROM transcripts WHERE date = today
    → Today's transcripts injected into LLM prompt
    → LLM generates accurate summary grounded in actual data

User uploads clinical_guidelines.txt → chunked → stored in KB
User asks "what's first-line for hypertension?"
    → SearchKnowledge() retrieves relevant chunks (FTS5 + optional cosine)
    → Chunks injected into LLM prompt as context
    → LLM answers grounded in uploaded document
```

---

## Security & Privacy

| Concern | How we handle it |
|---|---|
| **PHI in database** | SQLite file is local only, never uploaded |
| **Database encryption** | Future: SQLite SEE or sqlcipher for encryption at rest |
| **Data retention** | `edgescribe history delete --before DATE` for purging |
| **Access control** | Single-user edge tool — no multi-user needed |
| **Backup** | Single file (`edgescribe.db`) — backup by copying `~/.EDGESCRIBE/` |

---

## Implementation Phases

### Phase 1: Core Memory (SQLite + FTS5)
- Add `sqlite3.c` amalgamation to `include/`
- Implement `MemoryStore` class in `src/memory/`
- Schema creation and auto-migration
- Auto-save transcripts from ASR pipeline
- Auto-save chat messages
- FTS5 keyword search
- `edgescribe history` CLI commands
- Memory context injection into LLM prompts
- REST API endpoints for memory

### Phase 2: Knowledge Base (RAG with FTS5)
- Document ingestion (TXT/CSV → chunks)
- FTS5 search on chunks
- `edgescribe kb` CLI commands
- RAG-augmented LLM prompts (auto-retrieve relevant chunks)
- REST API endpoints for KB

### Phase 3: Semantic Embeddings
- Add `all-MiniLM-L6-v2` ONNX model to model manifest
- `EmbeddingEngine` class (raw onnxruntime C++ API)
- Embed chunks on ingestion, store as BLOBs
- Brute-force cosine similarity search in C++
- Hybrid search (FTS5 + cosine)
- Background re-embedding of existing data

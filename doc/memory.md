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

## Architecture: Three Layers of Memory

```
┌─────────────────────────────────────────────────────────────────┐
│                     EDGESCRIBE Memory System                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Layer 1: SESSION MEMORY (short-term)                            │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ Current conversation context                               │  │
│  │ • Live transcript buffer                                   │  │
│  │ • Chat history (this session)                              │  │
│  │ • Temporary processing state                               │  │
│  │ Storage: In-memory (lost on exit)                          │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  Layer 2: PERSISTENT MEMORY (long-term)                          │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ All past sessions, searchable                              │  │
│  │ • Transcripts with timestamps                              │  │
│  │ • SOAP notes generated                                     │  │
│  │ • Chat conversations                                       │  │
│  │ • User preferences / templates                             │  │
│  │ Storage: SQLite (~/.EDGESCRIBE/EDGESCRIBE.db)              │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  Layer 3: KNOWLEDGE BASE (RAG)                                   │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ Reference documents, embedded + searchable                 │  │
│  │ • Medical guidelines (uploaded by user)                    │  │
│  │ • Drug interaction databases                               │  │
│  │ • Practice protocols                                       │  │
│  │ • Custom knowledge documents                               │  │
│  │ Storage: SQLite FTS5 + embedding vectors                   │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Layer 1: Session Memory

**Purpose**: Keep context within a single session.

**Lifetime**: Starts when EDGESCRIBE launches, lost on exit.

**Implementation**: Already partially exists (Transcriber holds transcript in memory).

### What changes

```cpp
struct SessionContext {
  std::string transcript;              // Current live transcript
  std::vector<ChatMessage> chat_history; // Chat messages this session
  std::string active_patient;          // Currently discussed patient (if any)
  std::chrono::time_point session_start;
};
```

The LLM engine should include recent session context in every prompt:

```
System: You are a medical assistant.
Context: The current transcription session started 5 minutes ago.
Recent transcript: "Patient has diabetes, A1C is 7.2, on metformin 500mg BID..."

User: Generate SOAP notes
```

This gives the LLM awareness of what's happening NOW.

---

## Layer 2: Persistent Memory (SQLite)

**Purpose**: Remember everything across sessions. The doctor's complete history.

**Storage**: `~/.EDGESCRIBE/EDGESCRIBE.db` (SQLite)

### Database Schema

```sql
-- Every transcription session
CREATE TABLE sessions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    started_at  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    ended_at    DATETIME,
    duration_s  REAL,
    source      TEXT NOT NULL,  -- 'live', 'file:meeting.wav'
    device      TEXT,           -- 'cpu', 'cuda'
    model       TEXT            -- 'nemotron'
);

-- Transcript segments with timestamps
CREATE TABLE transcripts (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER NOT NULL REFERENCES sessions(id),
    timestamp_s REAL,           -- seconds from session start
    speaker_id  INTEGER,        -- from diarizer (NULL if no diarization)
    speaker     TEXT,           -- 'Speaker 1', 'Doctor', 'Patient'
    text        TEXT NOT NULL,
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Generated SOAP notes, summaries, etc.
CREATE TABLE notes (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER REFERENCES sessions(id),
    type        TEXT NOT NULL,  -- 'soap', 'summary', 'fix-terms', 'custom'
    input_text  TEXT,           -- source transcript
    output_text TEXT NOT NULL,  -- generated content
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Chat conversations
CREATE TABLE conversations (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER REFERENCES sessions(id),
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
    title       TEXT             -- auto-generated from first message
);

CREATE TABLE messages (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id INTEGER NOT NULL REFERENCES conversations(id),
    role            TEXT NOT NULL,  -- 'system', 'user', 'assistant'
    content         TEXT NOT NULL,
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- User preferences and templates
CREATE TABLE preferences (
    key         TEXT PRIMARY KEY,
    value       TEXT NOT NULL,
    updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Full-text search index on transcripts
CREATE VIRTUAL TABLE transcripts_fts USING fts5(
    text, speaker,
    content='transcripts',
    content_rowid='id'
);

-- Full-text search on notes
CREATE VIRTUAL TABLE notes_fts USING fts5(
    output_text,
    content='notes',
    content_rowid='id'
);
```

### Use Cases

```bash
# Search past transcripts
EDGESCRIBE history search "metformin"
# → 3 results:
#   [2026-03-12 10:30] Session #42: "...patient on metformin 500mg BID..."
#   [2026-03-11 14:15] Session #38: "...switch from metformin to insulin..."

# List today's sessions
EDGESCRIBE history today
# → Session #42: 10:30 AM (5.2 min, live)
#   Session #43: 11:15 AM (3.1 min, live)

# Get a past transcript
EDGESCRIBE history show 42

# Summarize today's sessions
EDGESCRIBE chat "Summarize all of today's patient encounters"
# → LLM gets today's transcripts as context automatically

# Delete old sessions
EDGESCRIBE history delete --before 2026-01-01
```

### Auto-Save Behavior

Every transcription session automatically:
1. Creates a `sessions` row on start
2. Inserts `transcripts` rows as text is decoded (with timestamps)
3. Updates `sessions.ended_at` and `duration_s` on stop/flush

No manual save needed. Everything is captured.

---

## Layer 3: Knowledge Base (Local RAG)

**Purpose**: Give the LLM access to reference documents the user uploads.

**Why not skills?** Skills are pre-programmed behaviors (like "generate SOAP notes"). We already have those hardcoded. RAG is more flexible — the user drops in ANY document and the LLM can reference it.

### How it works

```
User uploads: clinical_guidelines.pdf, drug_interactions.csv, practice_protocols.docx
    ↓
EDGESCRIBE chunks the text (512 tokens per chunk)
    ↓
Each chunk → embedding vector (via Qwen3-VL or a small embedding model)
    ↓
Stored in SQLite (text + vector)
    ↓
When user asks a question:
    ↓
Query → embedding → cosine similarity search → top-K relevant chunks
    ↓
Chunks injected into LLM prompt as context
    ↓
LLM generates answer grounded in the user's documents
```

### Database Schema (extension)

```sql
-- Knowledge base documents
CREATE TABLE documents (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    filename    TEXT NOT NULL,
    title       TEXT,
    content     TEXT NOT NULL,     -- full text
    added_at    DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Document chunks for RAG retrieval
CREATE TABLE chunks (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    document_id INTEGER NOT NULL REFERENCES documents(id),
    chunk_index INTEGER NOT NULL,
    text        TEXT NOT NULL,
    embedding   BLOB,             -- float32 vector, stored as binary blob
    token_count INTEGER
);

-- FTS on chunks for keyword search fallback
CREATE VIRTUAL TABLE chunks_fts USING fts5(
    text,
    content='chunks',
    content_rowid='id'
);
```

### RAG vs Skills — Why RAG is better here

| | Skills (hardcoded) | RAG (user documents) |
|---|---|---|
| SOAP notes | ✅ Already built | — |
| Summarize | ✅ Already built | — |
| "What's the dosage for amoxicillin?" | ❌ LLM guesses from training data | ✅ Looks up user's drug reference |
| "What's our clinic's protocol for diabetes?" | ❌ LLM doesn't know | ✅ Retrieves from uploaded protocol doc |
| Custom templates | ❌ Need to code each one | ✅ Upload a template doc, LLM follows it |
| Up-to-date guidelines | ❌ LLM training data is old | ✅ User uploads latest guidelines |

**Skills are already implemented** (SOAP, summarize, fix-terms). RAG adds the ability to **ground the LLM in the user's own documents** — much more powerful than hardcoding more skills.

### CLI for Knowledge Base

```bash
# Add a document to the knowledge base
EDGESCRIBE kb add clinical_guidelines.pdf
EDGESCRIBE kb add drug_reference.csv

# List documents
EDGESCRIBE kb list

# Search the knowledge base
EDGESCRIBE kb search "amoxicillin dosage"

# Remove a document
EDGESCRIBE kb remove clinical_guidelines.pdf

# Chat with RAG context
EDGESCRIBE chat "What's the recommended first-line treatment for type 2 diabetes?"
# → LLM retrieves relevant chunks from uploaded guidelines before answering
```

### API Endpoints

```
POST /v1/kb/add          — Upload document (multipart)
GET  /v1/kb/list         — List documents
POST /v1/kb/search       — Search chunks { "query": "..." }
DELETE /v1/kb/{id}       — Remove document
```

---

## Embedding Strategy

For RAG to work, we need text embeddings (convert text → vector for similarity search).

### Options

| Approach | Model | Size | Quality |
|---|---|---|---|
| **Use Qwen3-VL as embedder** | Already loaded | 0 extra | OK — not designed for embeddings |
| **Small embedding ONNX model** | `all-MiniLM-L6-v2` | ~80 MB | Good — designed for this |
| **BM25 keyword search (no embeddings)** | None | 0 | Basic — FTS5 only, no semantic search |

### Recommendation

**Start with FTS5 keyword search** (zero extra models). Upgrade to embedding model later if semantic search quality isn't good enough.

FTS5 is built into SQLite — no extra dependencies:

```sql
-- Keyword search (works today)
SELECT text FROM chunks_fts WHERE chunks_fts MATCH 'amoxicillin dosage';
```

This covers 80% of use cases. Semantic embeddings can be added later as a new model:

```bash
EDGESCRIBE pull embeddings   # ~80 MB all-MiniLM-L6-v2 ONNX
```

---

## Implementation Plan

### Phase 1: Persistent Memory (SQLite)

| Task | Effort |
|------|--------|
| Add SQLite (`sqlite3.c`, single file, public domain) | 1h |
| Create database schema + migration | 2h |
| Auto-save transcripts with timestamps | 2h |
| `EDGESCRIBE history` commands (today, search, show) | 2h |
| Inject recent history into LLM context | 2h |
| API endpoints for history | 2h |
| **Total** | **~11h** |

### Phase 2: Knowledge Base (RAG)

| Task | Effort |
|------|--------|
| Document ingestion (PDF/TXT/CSV → chunks) | 3h |
| FTS5 search on chunks | 2h |
| RAG-augmented LLM prompts | 2h |
| `EDGESCRIBE kb` commands | 2h |
| API endpoints for KB | 2h |
| **Total** | **~11h** |

### Phase 3: Smart Memory

| Task | Effort |
|------|--------|
| Embedding model integration | 3h |
| Semantic search (cosine similarity on vectors) | 2h |
| Auto-summarize sessions on close | 1h |
| Patient entity extraction (auto-detect patient names) | 3h |
| **Total** | **~9h** |

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
    → Memory Layer queries: SELECT * FROM transcripts WHERE date = today
    → Last 3 sessions' transcripts injected into LLM prompt
    → LLM generates accurate summary grounded in actual data

User asks "what medications was patient 2 on?"
    → Memory Layer searches: FTS5 MATCH 'medication'
    → Relevant transcript segments retrieved
    → LLM answers from actual recorded data

User uploads clinical_guidelines.pdf → Knowledge Base
User asks "what's first-line for hypertension?"
    → RAG retrieves relevant guideline chunks
    → LLM answers grounded in uploaded document
```

---

## Security & Privacy

| Concern | How we handle it |
|---|---|
| **PHI in database** | SQLite file is local only, never uploaded |
| **Database encryption** | Optional: SQLite SEE or sqlcipher for encryption at rest |
| **Data retention** | `EDGESCRIBE history delete --before DATE` for purging |
| **Access control** | Single-user tool — no multi-user access control needed |
| **Backup** | Single file (`EDGESCRIBE.db`) — easy to backup/move |

---

## Summary

```
Layer 1: Session Memory    — in-memory, current conversation context
Layer 2: Persistent Memory — SQLite, all past sessions + search
Layer 3: Knowledge Base    — SQLite FTS5 + optional embeddings, user documents

Start with: Layer 2 (persistent memory auto-save + search)
Then add:   Layer 3 (knowledge base with FTS5)
Later:      Semantic embeddings for better search
```

The key insight: **SQLite does everything**. No vector database, no Redis, no extra infrastructure. One `.db` file that's automatically backed up when the user copies their `.EDGESCRIBE/` folder.

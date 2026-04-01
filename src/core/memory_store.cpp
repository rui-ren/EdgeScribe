// EDGESCRIBE — Memory Store Implementation
// SQLite-backed persistent storage for chat history, transcripts, and notes.

#include "core/memory_store.h"
#include "core/sqlite3.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <shlobj.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace EDGESCRIBE {

// Generate a short random session ID (e.g., "s_a3f8b2")
static std::string GenerateSessionId() {
  static const char kChars[] = "0123456789abcdef";
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);

  std::string id = "s_";
  for (int i = 0; i < 8; i++) {
    id += kChars[dis(gen)];
  }
  return id;
}

// Execute SQL with no result expected
static void ExecSQL(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : "Unknown SQLite error";
    sqlite3_free(err);
    throw std::runtime_error("SQLite error: " + msg);
  }
}

struct MemoryStore::Impl {
  sqlite3* db = nullptr;
  std::mutex mutex;

  void CreateSchema() {
    const char* schema = R"(
      CREATE TABLE IF NOT EXISTS sessions (
          id         TEXT PRIMARY KEY,
          type       TEXT NOT NULL,
          model      TEXT,
          started_at TEXT DEFAULT (datetime('now')),
          ended_at   TEXT
      );

      CREATE TABLE IF NOT EXISTS messages (
          id         INTEGER PRIMARY KEY AUTOINCREMENT,
          session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
          role       TEXT NOT NULL,
          content    TEXT NOT NULL,
          created_at TEXT DEFAULT (datetime('now'))
      );

      CREATE TABLE IF NOT EXISTS notes (
          id          INTEGER PRIMARY KEY AUTOINCREMENT,
          session_id  TEXT REFERENCES sessions(id) ON DELETE CASCADE,
          type        TEXT NOT NULL,
          input_text  TEXT,
          output_text TEXT NOT NULL,
          created_at  TEXT DEFAULT (datetime('now'))
      );

      CREATE INDEX IF NOT EXISTS idx_messages_session ON messages(session_id);
      CREATE INDEX IF NOT EXISTS idx_notes_session ON notes(session_id);
    )";

    ExecSQL(db, schema);
  }
};

MemoryStore::MemoryStore(const std::string& db_path) : impl_(std::make_unique<Impl>()) {
  // Ensure parent directory exists
  fs::path p(db_path);
  if (p.has_parent_path()) {
    fs::create_directories(p.parent_path());
  }

  int rc = sqlite3_open(db_path.c_str(), &impl_->db);
  if (rc != SQLITE_OK) {
    throw std::runtime_error("Failed to open database: " + db_path);
  }

  // Enable WAL mode for better concurrent performance
  ExecSQL(impl_->db, "PRAGMA journal_mode=WAL;");
  ExecSQL(impl_->db, "PRAGMA foreign_keys=ON;");

  impl_->CreateSchema();
}

MemoryStore::~MemoryStore() {
  if (impl_->db) {
    sqlite3_close(impl_->db);
  }
}

std::string MemoryStore::StartSession(const std::string& type,
                                      const std::string& model) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::string id = GenerateSessionId();

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(impl_->db,
      "INSERT INTO sessions (id, type, model) VALUES (?, ?, ?)",
      -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, model.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return id;
}

void MemoryStore::EndSession(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(impl_->mutex);

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(impl_->db,
      "UPDATE sessions SET ended_at = datetime('now') WHERE id = ?",
      -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void MemoryStore::SaveMessage(const std::string& session_id,
                              const std::string& role,
                              const std::string& content) {
  std::lock_guard<std::mutex> lock(impl_->mutex);

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(impl_->db,
      "INSERT INTO messages (session_id, role, content) VALUES (?, ?, ?)",
      -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, role.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void MemoryStore::SaveNote(const std::string& session_id,
                           const std::string& type,
                           const std::string& input_text,
                           const std::string& output_text) {
  std::lock_guard<std::mutex> lock(impl_->mutex);

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(impl_->db,
      "INSERT INTO notes (session_id, type, input_text, output_text) VALUES (?, ?, ?, ?)",
      -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, input_text.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, output_text.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

std::vector<SessionInfo> MemoryStore::GetRecentSessions(int limit) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<SessionInfo> sessions;

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(impl_->db,
      "SELECT s.id, s.type, s.model, s.started_at, s.ended_at, "
      "  (SELECT COUNT(*) FROM messages m WHERE m.session_id = s.id) as msg_count "
      "FROM sessions s ORDER BY s.started_at DESC LIMIT ?",
      -1, &stmt, nullptr);
  sqlite3_bind_int(stmt, 1, limit);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    SessionInfo info;
    info.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    info.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    if (sqlite3_column_text(stmt, 2))
      info.model = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    info.started_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    if (sqlite3_column_text(stmt, 4))
      info.ended_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    info.message_count = sqlite3_column_int(stmt, 5);
    sessions.push_back(info);
  }
  sqlite3_finalize(stmt);

  return sessions;
}

std::vector<MessageInfo> MemoryStore::GetMessages(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<MessageInfo> messages;

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(impl_->db,
      "SELECT id, session_id, role, content, created_at "
      "FROM messages WHERE session_id = ? ORDER BY id",
      -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    MessageInfo msg;
    msg.id = sqlite3_column_int(stmt, 0);
    msg.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    msg.role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    msg.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    msg.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    messages.push_back(msg);
  }
  sqlite3_finalize(stmt);

  return messages;
}

std::vector<NoteInfo> MemoryStore::GetNotes(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<NoteInfo> notes;

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(impl_->db,
      "SELECT id, session_id, type, input_text, output_text, created_at "
      "FROM notes WHERE session_id = ? ORDER BY id",
      -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    NoteInfo note;
    note.id = sqlite3_column_int(stmt, 0);
    note.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    note.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    if (sqlite3_column_text(stmt, 3))
      note.input_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    note.output_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    note.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    notes.push_back(note);
  }
  sqlite3_finalize(stmt);

  return notes;
}

std::vector<MessageInfo> MemoryStore::SearchMessages(const std::string& query,
                                                     int limit) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<MessageInfo> messages;

  std::string like_query = "%" + query + "%";

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(impl_->db,
      "SELECT id, session_id, role, content, created_at "
      "FROM messages WHERE content LIKE ? ORDER BY created_at DESC LIMIT ?",
      -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, like_query.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, limit);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    MessageInfo msg;
    msg.id = sqlite3_column_int(stmt, 0);
    msg.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    msg.role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    msg.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    msg.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    messages.push_back(msg);
  }
  sqlite3_finalize(stmt);

  return messages;
}

void MemoryStore::DeleteSession(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  ExecSQL(impl_->db, "PRAGMA foreign_keys=ON;");

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(impl_->db,
      "DELETE FROM sessions WHERE id = ?", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void MemoryStore::DeleteBefore(const std::string& iso_date) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  ExecSQL(impl_->db, "PRAGMA foreign_keys=ON;");

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(impl_->db,
      "DELETE FROM sessions WHERE started_at < ?", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, iso_date.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

std::string GetDefaultDbPath() {
  std::string base_dir;

#ifdef _WIN32
  const char* local_app_data = std::getenv("LOCALAPPDATA");
  if (local_app_data) {
    base_dir = std::string(local_app_data) + "\\EDGESCRIBE";
  } else {
    const char* home = std::getenv("USERPROFILE");
    base_dir = home ? std::string(home) + "\\.EDGESCRIBE"
                    : ".EDGESCRIBE";
  }
#else
  const char* home = std::getenv("HOME");
  if (!home) {
    struct passwd* pw = getpwuid(getuid());
    home = pw ? pw->pw_dir : ".";
  }
  base_dir = std::string(home) + "/.EDGESCRIBE";
#endif

  const char* override_dir = std::getenv("EDGESCRIBE_DATA_DIR");
  if (override_dir && override_dir[0] != '\0') {
    base_dir = override_dir;
  }

  return base_dir + "/edgescribe.db";
}

}  // namespace EDGESCRIBE

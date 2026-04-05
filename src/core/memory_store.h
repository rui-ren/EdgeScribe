// EDGESCRIBE — Memory Store
// Persistent storage for chat history, transcripts, and notes using SQLite.

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace EDGESCRIBE {

struct SessionInfo {
  std::string id;
  std::string type;        // "chat", "transcribe", "process"
  std::string model;
  std::string title;
  bool pinned = false;
  std::string started_at;
  std::string ended_at;
  int message_count = 0;
};

struct MessageInfo {
  int id = 0;
  std::string session_id;
  std::string role;        // "system", "user", "assistant"
  std::string content;
  std::string created_at;
};

struct NoteInfo {
  int id = 0;
  std::string session_id;
  std::string type;        // "soap", "summary", "fix-terms", "transcript"
  std::string input_text;
  std::string output_text;
  std::string created_at;
};

class MemoryStore {
 public:
  explicit MemoryStore(const std::string& db_path);
  ~MemoryStore();

  MemoryStore(const MemoryStore&) = delete;
  MemoryStore& operator=(const MemoryStore&) = delete;

  // Session lifecycle
  std::string StartSession(const std::string& type,
                           const std::string& model = "");
  void EndSession(const std::string& session_id);

  // Chat message persistence
  void SaveMessage(const std::string& session_id, const std::string& role,
                   const std::string& content);

  // Note persistence (SOAP, summary, etc.)
  void SaveNote(const std::string& session_id, const std::string& type,
                const std::string& input_text, const std::string& output_text);

  // Query sessions
  std::vector<SessionInfo> GetRecentSessions(int limit = 20);
  std::vector<MessageInfo> GetMessages(const std::string& session_id);
  std::vector<NoteInfo> GetNotes(const std::string& session_id);

  // Search (basic LIKE query)
  std::vector<MessageInfo> SearchMessages(const std::string& query,
                                          int limit = 20);

  // Session management
  void RenameSession(const std::string& session_id, const std::string& title);
  void PinSession(const std::string& session_id, bool pinned);

  // Maintenance
  void DeleteSession(const std::string& session_id);
  void DeleteBefore(const std::string& iso_date);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Helper: get default database path (~/.EDGESCRIBE/edgescribe.db)
std::string GetDefaultDbPath();

}  // namespace EDGESCRIBE

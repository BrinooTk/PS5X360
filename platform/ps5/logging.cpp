// SPDX-License-Identifier: MIT
// Synchronous, bounded logging for the native title. Uses real Xenia's
// logging interface and preserves errors instead of dropping unresolved calls.
#include "xenia/base/logging.h"
#include "xenia/base/cvar.h"
#include "xenia/base/threading.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <mutex>
DEFINE_path(log_file, "", "Native title log file", "Logging");
DEFINE_int32(log_level, 2, "Maximum log severity level", "Logging");
DEFINE_bool(log_to_stdout, true, "Log to standard output", "Logging");
DEFINE_bool(flush_log, true, "Flush after every log line", "Logging");
namespace xe {
namespace {
std::mutex log_mutex;
FILE* log_output = nullptr;
thread_local std::array<char, 65536> thread_buffer;
}
// Optional second destination set by the native title (the console's kernel log).
void (*log_mirror)(char prefix, const char* line, size_t size) = nullptr;
FileLogSink::~FileLogSink() { if (owns_file_ && file_) std::fclose(file_); }
void FileLogSink::Write(const char* buffer, size_t size) { if (file_) std::fwrite(buffer, 1, size, file_); }
void FileLogSink::Flush() { if (file_) std::fflush(file_); }
void DebugPrintLogSink::Write(const char* buffer, size_t size) { std::fwrite(buffer, 1, size, stderr); }
void InitializeLogging(std::string_view app_name) {
  { std::lock_guard lock(log_mutex);
    if (log_output) std::fclose(log_output);
    log_output = nullptr;
    if (!cvars::log_file.empty()) log_output = std::fopen(cvars::log_file.c_str(), "a");
  }
  logging::AppendLogLine(LogLevel::Info, 'i', app_name);
}
void ShutdownLogging() {
  std::lock_guard lock(log_mutex);
  if (log_output) { std::fflush(log_output); std::fclose(log_output); log_output = nullptr; }
}
namespace logging {
bool ShouldLog(LogLevel level) { return int(level) <= cvars::log_level; }
void AppendLogLine(LogLevel level, char prefix, std::string_view line) {
  if (!ShouldLog(level)) return;
  std::lock_guard lock(log_mutex);
  auto write = [&](FILE* out) {
#if XE_PLATFORM_PS5
    std::fprintf(out, "%c> ", prefix);
#else
    // Host traces say which thread made each call.
    std::fprintf(out, "%c> %08X ", prefix, xe::threading::current_thread_system_id());
#endif
    std::fwrite(line.data(), 1, line.size(), out);
    std::fputc('\n', out);
    if (cvars::flush_log || level == LogLevel::Error) std::fflush(out);
  };
  // Debug lines (the kernel-call trace) are too many for a file flushed per line.
  if (log_output && (level <= LogLevel::Warning || cvars::log_level >= 3)) write(log_output);
  if (log_mirror) log_mirror(prefix, line.data(), line.size());
  if (cvars::log_to_stdout) write(stdout);
  if (!log_output && !cvars::log_to_stdout && level == LogLevel::Error) write(stderr);
}
namespace internal {
std::pair<char*, size_t> GetThreadBuffer() { return {thread_buffer.data(), thread_buffer.size()}; }
void AppendLogLine(LogLevel level, char prefix, size_t written) {
  logging::AppendLogLine(level, prefix, std::string_view(thread_buffer.data(), std::min(written, thread_buffer.size())));
}
}
}
void FatalError(std::string_view message) {
  std::fwrite(message.data(), 1, message.size(), stderr); std::fputc('\n', stderr); std::fflush(stderr);
  logging::AppendLogLine(LogLevel::Error, '!', message); ShutdownLogging(); std::abort();
}
}

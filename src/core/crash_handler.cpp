#include "crash_handler.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef _WIN32
// clang-format off
#  include <windows.h>
#  include <dbghelp.h>
// clang-format on
#  pragma comment(lib, "dbghelp.lib")
#else
#  include <fcntl.h>
#  include <signal.h>
#  include <string.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>

// Use execinfo.h on glibc; fall back to libunwind on musl/others.
#  if defined(__GLIBC__)
#    include <execinfo.h>
#    define GEOVIEWER_HAS_EXECINFO 1
#  elif __has_include(<libunwind.h>)
#    define UNW_LOCAL_ONLY
#    include <libunwind.h>
#    define GEOVIEWER_HAS_LIBUNWIND 1
#  endif
#endif

namespace geoviewer::core {

namespace {

std::string g_dump_dir;
std::string g_exception_log_path;

#ifdef _WIN32
std::string g_minidump_path;
#else
char g_crash_log_path_buf[1024] = {0};

void SafeAppendChar(char* buf, size_t& pos, size_t max_len, char c) {
  if (pos < max_len - 1) {
    buf[pos++] = c;
    buf[pos] = '\0';
  }
}

void SafeAppendString(char* buf, size_t& pos, size_t max_len, const char* str) {
  while (*str && pos < max_len - 1) {
    buf[pos++] = *str++;
  }
  buf[pos] = '\0';
}

void SafeAppendInt(char* buf, size_t& pos, size_t max_len, long long val) {
  if (val == 0) {
    SafeAppendChar(buf, pos, max_len, '0');
    return;
  }
  if (val < 0) {
    SafeAppendChar(buf, pos, max_len, '-');
    val = -val;
  }
  char temp[32];
  int temp_pos = 0;
  while (val > 0 && temp_pos < 31) {
    temp[temp_pos++] = '0' + (val % 10);
    val /= 10;
  }
  while (temp_pos > 0) {
    SafeAppendChar(buf, pos, max_len, temp[--temp_pos]);
  }
}

#if defined(GEOVIEWER_HAS_LIBUNWIND)
// Async-signal-safe hex conversion for pointer values
void SafeAppendHex(char* buf, size_t& pos, size_t max_len, uintptr_t val) {
  SafeAppendString(buf, pos, max_len, "0x");
  if (val == 0) {
    SafeAppendChar(buf, pos, max_len, '0');
    return;
  }
  char temp[32];
  int temp_pos = 0;
  while (val > 0 && temp_pos < 31) {
    int digit = val & 0xf;
    temp[temp_pos++] = digit < 10 ? ('0' + digit) : ('a' + digit - 10);
    val >>= 4;
  }
  while (temp_pos > 0) {
    SafeAppendChar(buf, pos, max_len, temp[--temp_pos]);
  }
}


// Async-signal-safe backtrace writing via libunwind (for musl/non-glibc).
void WriteBacktraceWithLibunwind(int fd) {
  unw_cursor_t cursor;
  unw_context_t context;
  unw_getcontext(&context);
  unw_init_local(&cursor, &context);

  int frame_idx = 0;
  char line_buf[256];
  char sym_buf[128];

  while (unw_step(&cursor) > 0) {
    unw_word_t ip = 0;
    unw_get_reg(&cursor, UNW_REG_IP, &ip);

    size_t pos = 0;
    line_buf[0] = '\0';
    SafeAppendInt(line_buf, pos, sizeof(line_buf), frame_idx);
    SafeAppendString(line_buf, pos, sizeof(line_buf), ": ");

    unw_word_t offset = 0;
    if (unw_get_proc_name(&cursor, sym_buf, sizeof(sym_buf), &offset) == 0) {
      SafeAppendString(line_buf, pos, sizeof(line_buf), sym_buf);
      SafeAppendString(line_buf, pos, sizeof(line_buf), "+");
      SafeAppendHex(line_buf, pos, sizeof(line_buf),
                    static_cast<uintptr_t>(offset));
    } else {
      SafeAppendString(line_buf, pos, sizeof(line_buf), "[");
      SafeAppendHex(line_buf, pos, sizeof(line_buf),
                    static_cast<uintptr_t>(ip));
      SafeAppendString(line_buf, pos, sizeof(line_buf), "]");
    }
    SafeAppendChar(line_buf, pos, sizeof(line_buf), '\n');
    [[maybe_unused]] auto res = write(fd, line_buf, pos);
    ++frame_idx;
  }
}
#endif

// Write stack trace to file descriptor (async-signal-safe).
void WriteBacktraceToFd(int fd) {
#if defined(GEOVIEWER_HAS_EXECINFO)
  void* array[100];
  int size = backtrace(array, 100);
  backtrace_symbols_fd(array, size, fd);
#elif defined(GEOVIEWER_HAS_LIBUNWIND)
  WriteBacktraceWithLibunwind(fd);
#else
  const char* msg = "(no backtrace support on this platform)\n";
  [[maybe_unused]] auto res = write(fd, msg, strlen(msg));
#endif
}

// Collect stack trace as string (for C++ exception handler, not signal-safe).
void WriteBacktraceToStream(std::ostream& out) {
#if defined(GEOVIEWER_HAS_EXECINFO)
  void* array[100];
  int size = backtrace(array, 100);
  char** messages = backtrace_symbols(array, size);
  if (messages != nullptr) {
    out << "Stack trace:\n";
    for (int i = 0; i < size; ++i) {
      out << i << ": " << messages[i] << "\n";
    }
    free(messages);
  }
#elif defined(GEOVIEWER_HAS_LIBUNWIND)
  unw_cursor_t cursor;
  unw_context_t context;
  unw_getcontext(&context);
  unw_init_local(&cursor, &context);

  out << "Stack trace:\n";
  int frame_idx = 0;
  char sym_buf[256];
  while (unw_step(&cursor) > 0) {
    unw_word_t ip = 0;
    unw_get_reg(&cursor, UNW_REG_IP, &ip);

    unw_word_t offset = 0;
    if (unw_get_proc_name(&cursor, sym_buf, sizeof(sym_buf), &offset) == 0) {
      out << frame_idx << ": " << sym_buf << "+0x" << std::hex << offset
          << std::dec << "\n";
    } else {
      out << frame_idx << ": [0x" << std::hex << ip << std::dec << "]\n";
    }
    ++frame_idx;
  }
#else
  out << "(no backtrace support on this platform)\n";
#endif
}

#endif  // !_WIN32

std::string GetTimestampString() {
  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);
  struct tm buf;
#ifdef _WIN32
  localtime_s(&buf, &in_time_t);
#else
  localtime_r(&in_time_t, &buf);
#endif
  std::stringstream ss;
  ss << std::put_time(&buf, "%Y%m%d_%H%M%S");
  return ss.str();
}

#ifdef _WIN32

LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* ep) {
  if (g_minidump_path.empty()) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  HANDLE hFile =
      CreateFileA(g_minidump_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                  nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (hFile != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION mdei;
    mdei.ThreadId = GetCurrentThreadId();
    mdei.ExceptionPointers = ep;
    mdei.ClientPointers = FALSE;

    MINIDUMP_TYPE mdt = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo |
        MiniDumpWithHandleData | MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules);

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, mdt,
                      &mdei, nullptr, nullptr);
    CloseHandle(hFile);
  }

  return EXCEPTION_EXECUTE_HANDLER;
}

#else

void PosixSignalHandler(int sig, siginfo_t* /*info*/, void* /*secret*/) {
  if (g_crash_log_path_buf[0] == '\0') {
    signal(sig, SIG_DFL);
    raise(sig);
    return;
  }

  int fd = open(g_crash_log_path_buf, O_CREAT | O_WRONLY | O_TRUNC,
                S_IRUSR | S_IWUSR);
  if (fd < 0) {
    signal(sig, SIG_DFL);
    raise(sig);
    return;
  }

  const char* sig_msg = "Caught signal: ";
  [[maybe_unused]] auto res1 = write(fd, sig_msg, strlen(sig_msg));

  char sig_buf[32];
  size_t sig_pos = 0;
  sig_buf[0] = '\0';
  SafeAppendInt(sig_buf, sig_pos, sizeof(sig_buf), sig);
  SafeAppendChar(sig_buf, sig_pos, sizeof(sig_buf), '\n');
  [[maybe_unused]] auto res2 = write(fd, sig_buf, sig_pos);

  const char* trace_msg = "Stack trace:\n";
  [[maybe_unused]] auto res3 = write(fd, trace_msg, strlen(trace_msg));

  WriteBacktraceToFd(fd);

  close(fd);

  signal(sig, SIG_DFL);
  raise(sig);
}

void SetupPosixSignalHandlers() {
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_RESETHAND;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = PosixSignalHandler;

  sigaction(SIGSEGV, &sa, nullptr);
  sigaction(SIGABRT, &sa, nullptr);
  sigaction(SIGILL, &sa, nullptr);
  sigaction(SIGFPE, &sa, nullptr);
  sigaction(SIGBUS, &sa, nullptr);
}

#endif

// Unhandled C++ exception handler
void TerminateHandler() {
  if (!g_exception_log_path.empty()) {
    std::ofstream out(g_exception_log_path);
    if (out) {
      out << "Terminated due to unhandled C++ exception.\n";
      try {
        auto ext = std::current_exception();
        if (ext) {
          std::rethrow_exception(ext);
        }
      } catch (const std::exception& e) {
        out << "Exception what(): " << e.what() << "\n";
      } catch (...) {
        out << "Unknown exception.\n";
      }

#ifndef _WIN32
      WriteBacktraceToStream(out);
#endif
      out.close();
    }
  }

  abort();
}

}  // namespace

void CrashHandler::Initialize(const std::string& dump_dir) {
  if (!dump_dir.empty()) {
    std::error_code ec;
    if (!std::filesystem::exists(dump_dir, ec)) {
      std::filesystem::create_directories(dump_dir, ec);
    }
  }

  g_dump_dir = dump_dir;

  std::string base_name = "/crash_" + GetTimestampString();
  g_exception_log_path = dump_dir + base_name + "_exception.log";

#ifdef _WIN32
  g_minidump_path = dump_dir + base_name + ".dmp";
  SetUnhandledExceptionFilter(UnhandledExceptionHandler);
#else
  size_t pos = 0;
  g_crash_log_path_buf[0] = '\0';
  SafeAppendString(g_crash_log_path_buf, pos, sizeof(g_crash_log_path_buf),
                   dump_dir.c_str());
  SafeAppendString(g_crash_log_path_buf, pos, sizeof(g_crash_log_path_buf),
                   base_name.c_str());
  SafeAppendString(g_crash_log_path_buf, pos, sizeof(g_crash_log_path_buf),
                   "_");
  SafeAppendInt(g_crash_log_path_buf, pos, sizeof(g_crash_log_path_buf),
                (long long)getpid());
  SafeAppendString(g_crash_log_path_buf, pos, sizeof(g_crash_log_path_buf),
                   ".log");

  SetupPosixSignalHandlers();
#endif

  std::set_terminate(TerminateHandler);
}

}  // namespace geoviewer::core

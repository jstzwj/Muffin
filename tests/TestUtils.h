#pragma once

#include <QDebug>
#include <QSettings>
#include <QString>
#include <QVariant>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

#if defined(_WIN32)
// WIN32_LEAN_AND_MEAN keeps windows.h from dragging in msxml.h, whose
// CMARK_NODE_* #defines collide with cmark-gfm's enums in TUs that include
// both this header and the parser.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

inline void require(bool condition, const char* message) {
  if (!condition) {
    qCritical().noquote() << message;
    fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}

inline void require(bool condition, const QString& message) {
  if (!condition) {
    qCritical().noquote() << message;
    fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
    std::exit(1);
  }
}

// The RUN line goes to stderr via plain fprintf and is flushed before the
// test body runs: Qt's default message handler drops qInfo output entirely
// in the ctest pipe environment on Windows, and buffered output is lost when
// a test crashes the process — a segfault would otherwise report with an
// empty log (two blind CI rounds on the ARM64 runner).
inline void runTest(const char* name, void (*test)()) {
  std::fprintf(stderr, "RUN %s\n", name);
  std::fflush(stderr);
  test();
}

inline void runTest(const char* name, const std::function<void()>& test) {
  std::fprintf(stderr, "RUN %s\n", name);
  std::fflush(stderr);
  test();
}

#if defined(_WIN32)
// Crash localizer for CI-only failures (the ARM64 SHARED-library teardown
// segfault): prints the faulting module + offset and a small frame backtrace
// straight to unbuffered stderr, where ctest reliably captures it. No symbol
// resolution — module+offset is enough to say exe vs MuffinUi.dll vs a Qt DLL.
inline void installMuffinTestCrashHandler() {
  static auto handler = +[](EXCEPTION_POINTERS* info) -> LONG {
    std::fprintf(stderr, "CRASH code=0x%08lX addr=%p\n",
                 info->ExceptionRecord->ExceptionCode,
                 info->ExceptionRecord->ExceptionAddress);
    void* frames[12] = {};
    const USHORT captured = CaptureStackBackTrace(0, 12, frames, nullptr);
    for (USHORT index = 0; index < captured; ++index) {
      HMODULE module = nullptr;
      char moduleName[MAX_PATH] = "?";
      if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             static_cast<LPCWSTR>(frames[index]), &module) &&
          module) {
        GetModuleFileNameA(module, moduleName, MAX_PATH);
        const char* base = std::strrchr(moduleName, '\\');
        base = base ? base + 1 : moduleName;
        std::fprintf(stderr, "  frame %u: %s+0x%tx\n", index, base,
                     reinterpret_cast<char*>(frames[index]) -
                         reinterpret_cast<char*>(module));
      } else {
        std::fprintf(stderr, "  frame %u: %p (no module)\n", index, frames[index]);
      }
    }
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
  };
  SetUnhandledExceptionFilter(handler);
}
#else
inline void installMuffinTestCrashHandler() {}
#endif

// RAII guard that sets a QSettings key for the duration of a scope and restores (or removes) the
// prior value on destruction. QSettings is process-global, so tests that exercise a preference
// (editor/*, markdown/*, ...) must restore the default afterwards or they poison every later test
// run in the same process.
class SettingsOverride {
public:
  SettingsOverride(const char* key, const QVariant& value) : key_(QString::fromLatin1(key)) {
    QSettings settings;
    hadOld_ = settings.contains(key_);
    oldValue_ = settings.value(key_);
    settings.setValue(key_, value);
  }
  ~SettingsOverride() {
    QSettings settings;
    if (hadOld_) {
      settings.setValue(key_, oldValue_);
    } else {
      settings.remove(key_);
    }
  }
  SettingsOverride(const SettingsOverride&) = delete;
  SettingsOverride& operator=(const SettingsOverride&) = delete;

private:
  QString key_;
  QVariant oldValue_;
  bool hadOld_ = false;
};

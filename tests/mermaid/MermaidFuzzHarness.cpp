// libFuzzer entry point for the mermaid flowchart parser (milestone H4).
//
// This is NOT built by the normal CMake/MSVC toolchain (which has no
// -fsanitize=fuzzer). It exists so the parser can be fuzzed externally with
// coverage-guided mutation + sanitizers, e.g.:
//
//   clang++ -std=c++20 -fsanitize=fuzzer,address -I src \
//     tests/mermaid/MermaidFuzzHarness.cpp src/mermaid/flowchart/Flowchart.cpp \
//     <MuffinCore object files> $(qt-libs) -o mermaid_fuzz
//   ./mermaid_fuzz -max_total_time=120
//
// The invariant is the same as MermaidFuzzTest: LLVMFuzzerTestOneInput must
// return (the parser either yields FlowchartData or throws FlowchartParseError)
// — a crash/hang/ASan violation here is a real bug. No upstream oracle needed.

#include "mermaid/flowchart/Flowchart.h"

#include <cstdint>
#include <cstring>
#include <QtGlobal>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Default Muffin limits apply (safety boundary); LIMIT-exceeded throws are
  // expected and not crashes.
  const QString source = QString::fromUtf8(reinterpret_cast<const char*>(data), static_cast<int>(size));
  try {
    muffin::mermaid::flowchart::Flowchart::parse(source);
  } catch (const muffin::mermaid::flowchart::FlowchartParseError&) {
    // expected for malformed/over-limit input
  }
  return 0;
}

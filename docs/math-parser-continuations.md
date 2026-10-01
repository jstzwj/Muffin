# Math parser continuations

`MathParser::parse()` and `MathMacroExpander::expand()` are synchronous APIs.
Their internal grammar and macro-expansion calls use `MathParseTask<T>` so
nested input does not consume a native call-stack frame per grammar level.
This avoids compiler- and architecture-dependent stack exhaustion, especially
on Windows ARM64, without changing the accepted nesting budget.

Each task initially suspends. `run()` drives a heap-backed stack of coroutine
handles, resuming one task at a time. `co_await` schedules a child on that stack;
it does not resume the child itself. Completed children are popped before
their parent resumes. Results and exceptions flow through `await_resume()`.
Exception propagation and frame destruction therefore happen one logical
frame at a time as well.

When adding a grammar handler that calls other handlers, return a
`MathParseTask<T>` and use `co_await` for those calls. Only the public synchronous
entry points call `run()`. Calling `run()` from another continuation would
reintroduce native recursion. Ordinary, non-recursive token readers and policy
helpers remain ordinary functions. Reference arguments must remain alive until
the child finishes; the awaiting parent's locals and full-expression
temporaries provide that lifetime in the current grammar.

The existing limits remain resource policies: 512 active guarded parser
levels, 200 nested `\expandafter` operations, and the configured `maxExpand`
budget. These limits also bound downstream parse-tree work. Expanded
definitions (`\edef`/`\xdef`) share the caller's expansion counter and reserve
their expansion before scheduling a child. A rejected depth-guard constructor
does not increment its counter.

`MuffinMathParserStackTest` runs on all platforms in a thread with a 256 KB
stack. It verifies deep continuation return/error/destructor propagation,
the exact brace-nesting boundary, deeply nested grammar and raw-text paths,
macro ordering and token flags, and the shared expanded-definition budget.
The existing math rendering tests continue to validate syntax and output.

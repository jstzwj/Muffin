#pragma once

#include <QElapsedTimer>
#include <coroutine>
#include <exception>
#include <utility>

namespace muffin {
// A GUI-thread-only, cooperatively scheduled build. Destroying it cancels the
// unpublished layout; no nested event loop and no cross-thread Qt resources.
class CooperativeTask {
public:
  struct promise_type {
    std::exception_ptr error;
    CooperativeTask get_return_object() {
      return CooperativeTask(std::coroutine_handle<promise_type>::from_promise(*this));
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(std::nullptr_t) noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() { error = std::current_exception(); }
  };
  CooperativeTask() = default;
  CooperativeTask(CooperativeTask&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
  CooperativeTask& operator=(CooperativeTask&& other) noexcept {
    if (this == &other)
      return *this;
    if (handle_)
      handle_.destroy();
    handle_ = std::exchange(other.handle_, {});
    return *this;
  }
  ~CooperativeTask() {
    if (handle_)
      handle_.destroy();
  }
  bool resumeFor(int budgetMs = 4) {
    QElapsedTimer timer;
    timer.start();
    while (handle_ && !handle_.done()) {
      handle_.resume();
      if (handle_.promise().error)
        std::rethrow_exception(handle_.promise().error);
      if (budgetMs >= 0 && timer.elapsed() >= budgetMs)
        break;
    }
    return handle_ && !handle_.done();
  }

private:
  explicit CooperativeTask(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
  std::coroutine_handle<promise_type> handle_;
};
} // namespace muffin

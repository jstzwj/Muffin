#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>
#include <vector>

namespace muffin::math {

// Internal, synchronous parser continuation. Nested grammar calls suspend onto
// a heap-backed work stack; only run() resumes them. In particular, neither
// await_suspend nor final_suspend resumes another coroutine (even symmetric
// transfer can accumulate native stack frames on some compiler configurations).
template <typename T>
class MathParseTask {
public:
  struct promise_type;
  using Handle = std::coroutine_handle<promise_type>;
  using WorkStack = std::vector<std::coroutine_handle<>>;

  struct promise_type {
    WorkStack* work = nullptr;
    std::optional<T> value;
    std::exception_ptr error;

    MathParseTask get_return_object() { return MathParseTask(Handle::from_promise(*this)); }
    std::suspend_always initial_suspend() const noexcept { return {}; }
    std::suspend_always final_suspend() const noexcept { return {}; }
    void return_value(T result) { value.emplace(std::move(result)); }
    void unhandled_exception() noexcept { error = std::current_exception(); }
  };

  MathParseTask(MathParseTask&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
  MathParseTask(const MathParseTask&) = delete;
  MathParseTask& operator=(const MathParseTask&) = delete;
  ~MathParseTask() { if (handle_) handle_.destroy(); }

  bool await_ready() const noexcept { return false; }
  template <typename ParentPromise>
  void await_suspend(std::coroutine_handle<ParentPromise> parent) {
    handle_.promise().work = parent.promise().work;
    handle_.promise().work->push_back(handle_);
  }
  T await_resume() {
    if (handle_.promise().error) std::rethrow_exception(handle_.promise().error);
    return std::move(*handle_.promise().value);
  }

  T run() && {
    WorkStack work;
    handle_.promise().work = &work;
    work.push_back(handle_);
    while (!work.empty()) {
      const auto next = work.back();
      next.resume();
      // A child is removed before its parent resumes and destroys its frame.
      // Errors also propagate one continuation at a time, without recursive
      // resumption or recursive destruction of suspended coroutine chains.
      if (next.done()) work.pop_back();
    }
    return await_resume();
  }

private:
  explicit MathParseTask(Handle handle) noexcept : handle_(handle) {}
  Handle handle_;
};

}  // namespace muffin::math

#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace areca {

// Một thread chạy lần lượt từng việc theo thứ tự đưa vào. Dùng cho các thao tác
// phải chờ (roundtrip Wayland, nghỉ giữa các phím X11) để không chặn event loop
// của fcitx5. Thread chỉ được tạo ở việc đầu tiên.
class SerialWorker final {
public:
  SerialWorker() = default;
  ~SerialWorker() { stop(); }

  SerialWorker(const SerialWorker &) = delete;
  SerialWorker &operator=(const SerialWorker &) = delete;

  void post(std::function<void()> task) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return;
      }
      tasks_.push_back(std::move(task));
      if (!thread_.joinable()) {
        thread_ = std::thread(&SerialWorker::run, this);
      }
    }
    wake_.notify_one();
  }

  // Bỏ các việc chưa chạy. Việc đang chạy vẫn chạy hết.
  void dropPending() {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.clear();
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      tasks_.clear();
    }
    wake_.notify_one();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

private:
  void run() {
    while (true) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
        if (stopping_) {
          return;
        }
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }
      task();
    }
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> tasks_;
  std::thread thread_;
  bool stopping_ = false;
};

} // namespace areca

#pragma once

#include <cstdint>
#include <deque>
#include <string>

#include <fcitx-utils/trackableobject.h>
#include <fcitx/inputcontext.h>

namespace areca {

struct QueuedKey {
  uint64_t sequence = 0;
  uint32_t codepoint = 0;
  std::string utf8Text;
  bool isBackspace = false;
  fcitx::TrackableObjectReference<fcitx::InputContext> inputContext;
};

class KeyQueue {
public:
  void push(QueuedKey key) { keys_.push_back(std::move(key)); }

  bool empty() const { return keys_.empty(); }
  size_t size() const { return keys_.size(); }
  const QueuedKey &front() const { return keys_.front(); }

  QueuedKey pop() {
    auto key = std::move(keys_.front());
    keys_.pop_front();
    return key;
  }

  void removeFor(fcitx::InputContext &inputContext) {
    auto it = keys_.begin();
    while (it != keys_.end()) {
      auto *queuedContext = it->inputContext.get();
      if (!queuedContext || queuedContext == &inputContext) {
        it = keys_.erase(it);
      } else {
        ++it;
      }
    }
  }

private:
  std::deque<QueuedKey> keys_;
};

} // namespace areca

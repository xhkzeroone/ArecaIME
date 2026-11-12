#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx-utils/trackableobject.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>

#include "rewrite_backend.h"
#include "virtual_keyboard_device.h"
#include "virtual_keyboard_keymap.h"

namespace areca {

// Xoá và gõ lại chữ hoàn toàn bằng phím ảo, không commitString: Backspace và
// chữ mới đi chung một kênh nên compositor giữ đúng thứ tự. Chỉ dùng khi chọn
// Virtual keyboard cho ứng dụng.
class VirtualKeyboardBackspaceBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;

  VirtualKeyboardBackspaceBackend(fcitx::EventLoop &eventLoop,
                                  VirtualKeyboardDevice &device,
                                  DebugProvider debugProvider);
  ~VirtualKeyboardBackspaceBackend() override;

  VirtualKeyboardBackspaceBackend(const VirtualKeyboardBackspaceBackend &) =
      delete;
  VirtualKeyboardBackspaceBackend &
  operator=(const VirtualKeyboardBackspaceBackend &) = delete;

  const char *name() const override { return "virtual-keyboard-backspace"; }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool isAvailable();
  bool hasPending() const { return transactionId_ != 0; }
  // Phím ảo đi vào ứng dụng rồi quay lại fcitx5; chuyển thẳng cho ứng dụng
  // thay vì để Bamboo xử lý lại.
  bool handleKeyEvent(fcitx::KeyEvent &event);

  // Compositor không trả lời trong khoảng này thì coi như hỏng.
  static constexpr uint32_t SyncTimeoutMs = 3000;
  // Phím dội về muộn vẫn được nhận trong khoảng này sau khi xong.
  static constexpr uint64_t EchoGraceUsec = 1000 * 1000;

private:
  bool sendNextBurst();
  void onBurstSynced(uint64_t transactionId, bool ok);
  void armSyncTimeout();
  void complete(RewriteOutcome outcome);
  void completeWithoutSending();
  void clearPending();
  void clearEcho();

  fcitx::EventLoop &eventLoop_;
  VirtualKeyboardDevice &device_;
  DebugProvider debugProvider_;

  std::unique_ptr<fcitx::EventSourceTime> syncTimeout_;
  fcitx::TrackableObjectReference<fcitx::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t deletesLeft_ = 0;
  std::deque<std::vector<uint32_t>> bursts_;

  fcitx::TrackableObjectReference<fcitx::InputContext> echoContext_;
  // 0 là Backspace, còn lại là mã Unicode, theo đúng thứ tự đã phát.
  std::deque<uint32_t> pendingEcho_;
  std::set<uint32_t> injectedCodepoints_;
  uint64_t echoDeadlineUsec_ = 0;
};

} // namespace areca

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <string>

#include <fcitx-utils/event.h>
#include <fcitx-utils/trackableobject.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>

#include "rewrite_backend.h"
#include "virtual_keyboard_device.h"

namespace areca {

// Xoá và gõ lại chữ hoàn toàn bằng phím ảo Wayland (zwp_virtual_keyboard_v1),
// không dùng commitString. Chỉ dùng khi chọn Virtual keyboard cho ứng dụng và
// compositor có hỗ trợ.
class VirtualKeyboardBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;
  // Chạy hàm trên event loop của fcitx5; phải gọi được từ thread khác.
  using MainThreadPoster = std::function<void(std::function<void()>)>;

  VirtualKeyboardBackend(fcitx::EventLoop &eventLoop,
                         std::unique_ptr<VirtualKeyboardDevice> device,
                         MainThreadPoster postToMain,
                         DebugProvider debugProvider);
  ~VirtualKeyboardBackend() override;

  VirtualKeyboardBackend(const VirtualKeyboardBackend &) = delete;
  VirtualKeyboardBackend &operator=(const VirtualKeyboardBackend &) = delete;

  const char *name() const override { return "virtual-keyboard"; }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool isAvailable();
  bool hasPending() const { return transactionId_ != 0; }
  // Phím ảo đi vào ứng dụng rồi quay lại fcitx5; chuyển thẳng cho ứng dụng thay
  // vì để Bamboo xử lý lại.
  bool handleKeyEvent(fcitx::KeyEvent &event);

  // Thời gian chờ thiết bị báo xong trước khi coi là hỏng.
  static constexpr uint32_t kWatchdogMs = 3000;
  // Phím dội ngược đến muộn vẫn được nhận trong khoảng này sau khi xong.
  static constexpr uint64_t kEchoGraceUsec = 1000 * 1000;

private:
  void finish(uint64_t transactionId, bool ok);
  void clearEcho();

  fcitx::EventLoop &eventLoop_;
  std::unique_ptr<VirtualKeyboardDevice> device_;
  MainThreadPoster postToMain_;
  DebugProvider debugProvider_;
  // Callback từ thread thiết bị chỉ chạy tiếp khi backend còn sống.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  std::unique_ptr<fcitx::EventSourceTime> watchdog_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;

  fcitx::TrackableObjectReference<fcitx::InputContext> echoContext_;
  // 0 là Backspace, còn lại là mã Unicode, theo đúng thứ tự đã phát.
  std::deque<uint32_t> pendingEcho_;
  std::set<uint32_t> injectedCodepoints_;
  uint64_t echoDeadlineUsec_ = 0;
};

} // namespace areca

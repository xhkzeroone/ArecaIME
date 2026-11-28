#pragma once

#include <functional>

#include <fcitx/inputcontext.h>

#include "types.h"

namespace areca {

enum class ApplyStatus {
  Completed,
  Pending,
  Failed,
};

enum class RewriteOutcome {
  Succeeded,
  Failed,
  // Backend chưa xoá hay commit gì: scheduler chạy lại lượt này bằng backend
  // dự phòng trong RewriteBackendSelection.
  NeedsFallback,
};

using RewriteDone =
    std::function<void(uint64_t transactionId, RewriteOutcome outcome)>;

// Đường marker tới ứng dụng trước khi backend xoá.
enum class MarkerDelivery {
  // Không dùng marker: backend xoá đúng phần Bamboo yêu cầu (surrounding).
  None,
  // Xoá đi qua kênh phím khác commit: phải chờ marker tới ứng dụng.
  Keys,
  // Backend tự gõ marker trên kênh của nó.
  Typed,
};

class RewriteBackend {
public:
  virtual ~RewriteBackend() = default;
  virtual const char *name() const = 0;
  virtual MarkerDelivery markerDelivery() const { return MarkerDelivery::Keys; }
  virtual ApplyStatus apply(fcitx::InputContext &inputContext,
                            const RewritePlan &plan, RewriteDone onDone) = 0;
};

} // namespace areca

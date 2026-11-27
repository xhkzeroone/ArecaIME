#pragma once

#include "rewrite_backend.h"

namespace areca {

// Bọc một backend kênh phím để chạy như trước khi có marker: scheduler không
// commit "·", không chờ, backend xoá đúng phần Bamboo yêu cầu. Dùng cho ứng
// dụng mà marker gây hại, đổi lại gợi ý tự hoàn thành không bị cắt.
class UnmarkedRewriteBackend final : public RewriteBackend {
public:
  UnmarkedRewriteBackend(RewriteBackend &inner, const char *name)
      : inner_(inner), name_(name) {}

  UnmarkedRewriteBackend(const UnmarkedRewriteBackend &) = delete;
  UnmarkedRewriteBackend &operator=(const UnmarkedRewriteBackend &) = delete;

  const char *name() const override { return name_; }
  MarkerDelivery markerDelivery() const override {
    return MarkerDelivery::None;
  }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override {
    return inner_.apply(inputContext, plan, std::move(onDone));
  }

private:
  RewriteBackend &inner_;
  const char *name_;
};

} // namespace areca

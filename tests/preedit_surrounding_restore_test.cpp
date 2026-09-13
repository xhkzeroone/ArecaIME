#include <cassert>
#include <string>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "preedit_mode.h"

namespace {

class TestInputContext final : public fcitx::InputContext {
public:
  explicit TestInputContext(fcitx::InputContextManager &manager)
      : fcitx::InputContext(manager, "org.gnome.TextEditor") {}
  ~TestInputContext() override { destroy(); }

  const char *frontend() const override { return "wayland"; }

  void commitStringImpl(const std::string &text) override {
    commits.push_back(text);
  }
  void deleteSurroundingTextImpl(int offset, unsigned int count) override {
    deletes.push_back({offset, count});
  }
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}

  struct DeleteCall {
    int offset;
    unsigned int count;
  };
  std::vector<std::string> commits;
  std::vector<DeleteCall> deletes;
};

} // namespace

int main() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  areca::PreeditModeHandler::StateFactory factory(
      [](fcitx::InputContext &) {
        return new areca::PreeditInputState("Telex 2", true, true, true,
                                            "Unicode", false, false, 1, {});
      });
  manager.registerProperty("preedit-surrounding-restore-test", &factory);

  bool enabled = true;
  areca::PreeditModeHandler handler(
      eventLoop, factory, [] { return false; }, [] { return false; },
      [&] { return enabled; });

  TestInputContext context(manager);
  context.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
  context.surroundingText().setText("tưởng", 5, 5);
  handler.activate(context);

  // Khi bật, từ cũ được xóa đúng một lần khỏi ứng dụng rồi chuyển vào preedit;
  // phím mới tiếp tục sửa trên state Bamboo vừa phục hồi.
  fcitx::KeyEvent toneKey(&context, fcitx::Key(FcitxKey_s));
  handler.handleKeyEvent(toneKey);
  assert(toneKey.accepted());
  assert(context.deletes.size() == 1);
  assert(context.deletes[0].offset == -5);
  assert(context.deletes[0].count == 5);
  assert(context.propertyFor(&factory)->composing == "tướng");

  fcitx::KeyEvent boundary(&context, fcitx::Key(FcitxKey_space));
  handler.handleKeyEvent(boundary);
  assert(boundary.accepted());
  assert(context.commits == std::vector<std::string>{"tướng "});
  assert(context.propertyFor(&factory)->composing.empty());

  // Khi tắt, cùng snapshot chỉ tạo preedit từ phím mới và tuyệt đối không xóa
  // phần text đã commit trước con trỏ.
  enabled = false;
  TestInputContext disabledContext(manager);
  disabledContext.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
  disabledContext.surroundingText().setText("tưởng", 5, 5);
  handler.activate(disabledContext);
  fcitx::KeyEvent plainKey(&disabledContext, fcitx::Key(FcitxKey_a));
  handler.handleKeyEvent(plainKey);
  assert(plainKey.accepted());
  assert(disabledContext.deletes.empty());
  assert(disabledContext.propertyFor(&factory)->composing == "a");
}

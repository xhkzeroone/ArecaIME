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
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &event) override {
    if (event.rawKey().check(FcitxKey_BackSpace)) {
      forwardedBackspaces.push_back(event.isRelease());
    }
  }
  void updatePreeditImpl() override {}

  struct DeleteCall {
    int offset;
    unsigned int count;
  };
  std::vector<std::string> commits;
  std::vector<DeleteCall> deletes;
  std::vector<bool> forwardedBackspaces;
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
  bool backspaceRecovery = true;
  areca::PreeditModeHandler handler(
      eventLoop, factory, [] { return false; }, [] { return false; },
      [&] { return backspaceRecovery; }, [&] { return enabled; });

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

  // Space đã commit ngay. Backspace dùng snapshot chính xác để xóa cả từ và
  // boundary khỏi ứng dụng, sau đó đưa riêng từ trở lại preedit để sửa tiếp.
  fcitx::KeyEvent undoBoundary(&context, fcitx::Key(FcitxKey_BackSpace));
  handler.handleKeyEvent(undoBoundary);
  assert(undoBoundary.accepted());
  assert(context.deletes.size() == 2);
  assert(context.deletes[1].offset == -6);
  assert(context.deletes[1].count == 6);
  assert(context.propertyFor(&factory)->composing == "tướng");
  fcitx::KeyEvent retone(&context, fcitx::Key(FcitxKey_f));
  handler.handleKeyEvent(retone);
  assert(retone.accepted());
  assert(context.propertyFor(&factory)->composing == "tường");

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

  // App không có SurroundingText không bị xóa text phỏng đoán: kể cả adapter
  // còn snapshot, handler reset nó và forward Backspace gốc cho ứng dụng.
  TestInputContext noSurroundingContext(manager);
  handler.activate(noSurroundingContext);
  for (const char key : std::string("as")) {
    fcitx::KeyEvent keyEvent(
        &noSurroundingContext,
        fcitx::Key(fcitx::Key::keySymFromUnicode(
            static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
    assert(keyEvent.accepted());
  }
  fcitx::KeyEvent firstSpace(&noSurroundingContext,
                             fcitx::Key(FcitxKey_space));
  handler.handleKeyEvent(firstSpace);
  assert(firstSpace.accepted());
  assert(noSurroundingContext.commits == std::vector<std::string>{"á "});
  assert(noSurroundingContext.propertyFor(&factory)->composing.empty());

  for (int index = 0; index < 2; ++index) {
    fcitx::KeyEvent extraSpace(&noSurroundingContext,
                               fcitx::Key(FcitxKey_space));
    handler.handleKeyEvent(extraSpace);
    assert(!extraSpace.accepted());
  }
  for (int index = 0; index < 2; ++index) {
    fcitx::KeyEvent crossSpace(&noSurroundingContext,
                               fcitx::Key(FcitxKey_BackSpace));
    handler.handleKeyEvent(crossSpace);
    assert(!crossSpace.accepted());
    assert(noSurroundingContext.propertyFor(&factory)->composing.empty());
  }
  fcitx::KeyEvent restoreWord(&noSurroundingContext,
                              fcitx::Key(FcitxKey_BackSpace));
  handler.handleKeyEvent(restoreWord);
  assert(!restoreWord.accepted());
  assert(noSurroundingContext.forwardedBackspaces.empty());
  assert(noSurroundingContext.propertyFor(&factory)->composing.empty());

  // Có SurroundingText thì snapshot vẫn sống qua nhiều Space đã commit/forward.
  // Backspace lùi hết boundary mới xóa chính xác `từ + Space` và mở lại preedit.
  TestInputContext multipleSpacesContext(manager);
  multipleSpacesContext.setCapabilityFlags(
      fcitx::CapabilityFlag::SurroundingText);
  multipleSpacesContext.surroundingText().setText("", 0, 0);
  handler.activate(multipleSpacesContext);
  for (const char key : std::string("as")) {
    fcitx::KeyEvent keyEvent(
        &multipleSpacesContext,
        fcitx::Key(fcitx::Key::keySymFromUnicode(
            static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
  }
  fcitx::KeyEvent committedSpace(&multipleSpacesContext,
                                 fcitx::Key(FcitxKey_space));
  handler.handleKeyEvent(committedSpace);
  assert(committedSpace.accepted());
  for (int index = 0; index < 2; ++index) {
    fcitx::KeyEvent extraSpace(&multipleSpacesContext,
                               fcitx::Key(FcitxKey_space));
    handler.handleKeyEvent(extraSpace);
    assert(!extraSpace.accepted());
  }
  for (int index = 0; index < 2; ++index) {
    fcitx::KeyEvent crossSpace(&multipleSpacesContext,
                               fcitx::Key(FcitxKey_BackSpace));
    handler.handleKeyEvent(crossSpace);
    assert(!crossSpace.accepted());
  }
  fcitx::KeyEvent restoreAfterSpaces(&multipleSpacesContext,
                                     fcitx::Key(FcitxKey_BackSpace));
  handler.handleKeyEvent(restoreAfterSpaces);
  assert(restoreAfterSpaces.accepted());
  assert(multipleSpacesContext.deletes.size() == 1);
  assert(multipleSpacesContext.deletes[0].offset == -2);
  assert(multipleSpacesContext.deletes[0].count == 2);
  assert(multipleSpacesContext.propertyFor(&factory)->composing == "á");

  // Phím kế tiếp bắt đầu composition mới; từ trước đó đã commit ngay từ Space,
  // không có trạng thái pending làm chậm trải nghiệm gõ.
  TestInputContext immediateCommitContext(manager);
  handler.activate(immediateCommitContext);
  for (const char key : std::string("as b")) {
    fcitx::KeyEvent keyEvent(
        &immediateCommitContext,
        fcitx::Key(fcitx::Key::keySymFromUnicode(
            static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
    assert(keyEvent.accepted());
  }
  assert(immediateCommitContext.commits ==
         std::vector<std::string>{"á "});
  assert(immediateCommitContext.propertyFor(&factory)->composing == "b");

  // BackspaceRecovery trong Preedit dùng processBackspace thay vì xóa thô, nên
  // chuỗi Latin do spell-check trả về có thể được dựng lại thành âm tiết đúng.
  TestInputContext recoveryContext(manager);
  handler.activate(recoveryContext);
  for (const char key : std::string("nhanhsh")) {
    fcitx::KeyEvent keyEvent(
        &recoveryContext,
        fcitx::Key(fcitx::Key::keySymFromUnicode(
            static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
    assert(keyEvent.accepted());
  }
  assert(recoveryContext.propertyFor(&factory)->composing == "nhanhsh");
  fcitx::KeyEvent recover(&recoveryContext, fcitx::Key(FcitxKey_BackSpace));
  handler.handleKeyEvent(recover);
  assert(recover.accepted());
  assert(recoveryContext.propertyFor(&factory)->composing == "nhánh");

  backspaceRecovery = false;
  TestInputContext plainBackspaceContext(manager);
  handler.activate(plainBackspaceContext);
  for (const char key : std::string("nhanhsh")) {
    fcitx::KeyEvent keyEvent(
        &plainBackspaceContext,
        fcitx::Key(fcitx::Key::keySymFromUnicode(
            static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
  }
  fcitx::KeyEvent plainBackspace(&plainBackspaceContext,
                                 fcitx::Key(FcitxKey_BackSpace));
  handler.handleKeyEvent(plainBackspace);
  assert(plainBackspace.accepted());
  assert(plainBackspaceContext.propertyFor(&factory)->composing == "nhanhs");
}

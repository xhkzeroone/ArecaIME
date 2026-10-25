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
  explicit TestInputContext(fcitx::InputContextManager &manager,
                            const char *frontend = "wayland")
      : fcitx::InputContext(manager, "org.gnome.TextEditor"),
        frontend_(frontend) {}
  ~TestInputContext() override { destroy(); }

  const char *frontend() const override { return frontend_; }

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

private:
  const char *frontend_;
};

} // namespace

int main() {
  fcitx::InputContextManager manager;
  areca::PreeditModeHandler::StateFactory factory(
      [](fcitx::InputContext &) {
        return new areca::PreeditInputState("Telex 2", true, true, true,
                                            "Unicode", false, false, 1, {});
      });
  manager.registerProperty("preedit-surrounding-restore-test", &factory);

  bool enabled = true;
  bool backspaceRecovery = true;
  uint32_t requestedBackspaces = 0;
  areca::PreeditModeHandler::FinalizedWordEraseDone eraseDone;
  areca::PreeditModeHandler handler(
      factory, [] { return false; }, [] { return false; },
      [&] { return backspaceRecovery; }, [&] { return enabled; },
      [&](fcitx::InputContext &, uint32_t count,
          areca::PreeditModeHandler::FinalizedWordEraseDone done) {
        requestedBackspaces = count;
        eraseDone = std::move(done);
        return true;
      });

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

  // Space đã commit ngay. Backspace dùng backend để xóa cả từ và boundary,
  // sau đó đưa riêng từ trở lại preedit để sửa tiếp.
  fcitx::KeyEvent undoBoundary(&context, fcitx::Key(FcitxKey_BackSpace));
  handler.handleKeyEvent(undoBoundary);
  assert(undoBoundary.accepted());
  assert(context.propertyFor(&factory)->finalizedWordRecovery ==
         areca::FinalizedWordRecoveryState::AwaitingBackspaceRelease);
  assert(requestedBackspaces == 0);
  fcitx::KeyEvent undoBoundaryRelease(
      &context, fcitx::Key(FcitxKey_BackSpace), true);
  handler.handleKeyEvent(undoBoundaryRelease);
  assert(undoBoundaryRelease.accepted());
  assert(context.deletes.size() == 1);
  assert(requestedBackspaces == 6);
  assert(eraseDone);
  eraseDone(true);
  assert(context.propertyFor(&factory)->composing == "tướng");
  fcitx::KeyEvent retone(&context, fcitx::Key(FcitxKey_f));
  handler.handleKeyEvent(retone);
  assert(retone.accepted());
  assert(context.propertyFor(&factory)->composing == "tường");

  // Click đặt caret giữa `tiến`, ngay sau `ế`: prefix `tiế` chưa hoàn chỉnh
  // được thu hẹp tới `ế`. `ti` và `n` vẫn nằm trong app nên f tạo `tiền`.
  TestInputContext middleContext(manager);
  middleContext.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
  middleContext.surroundingText().setText("tiến", 3, 3);
  handler.activate(middleContext);
  assert(handler.prepareSurroundingRestoreAfterClick(middleContext));
  fcitx::KeyEvent middleTone(&middleContext, fcitx::Key(FcitxKey_f));
  handler.handleKeyEvent(middleTone);
  assert(middleTone.accepted());
  assert(middleContext.deletes.size() == 1);
  assert(middleContext.deletes[0].offset == -1);
  assert(middleContext.deletes[0].count == 1);
  assert(middleContext.propertyFor(&factory)->composing == "ề");

  // Không được reset ngầm một preedit còn hiển thị chỉ vì tracker thấy click.
  assert(!handler.prepareSurroundingRestoreAfterClick(middleContext));
  assert(middleContext.propertyFor(&factory)->composing == "ề");

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

  // Regression macro `k → không`: snapshot thiếu dấu chấm có thể tới trễ sau
  // khi macro đã commit. Space là boundary nên không được hút "không" vào
  // Bamboo; nếu restore nhầm sẽ xóa từ và tạo kết quả lặp kiểu "không.hông ".
  enabled = true;
  TestInputContext macroBoundaryContext(manager);
  macroBoundaryContext.setCapabilityFlags(
      fcitx::CapabilityFlag::SurroundingText);
  macroBoundaryContext.surroundingText().setText("không", 5, 5);
  handler.activate(macroBoundaryContext);
  fcitx::KeyEvent macroSpace(&macroBoundaryContext,
                             fcitx::Key(FcitxKey_space));
  handler.handleKeyEvent(macroSpace);
  assert(!macroSpace.accepted());
  assert(macroBoundaryContext.deletes.empty());
  assert(macroBoundaryContext.propertyFor(&factory)->composing.empty());
  assert(!macroBoundaryContext.propertyFor(&factory)
              ->surroundingRestoreArmed);

  // App không có SurroundingText dùng backend Backspace để xóa chính xác từ
  // vừa commit cùng boundary rồi đưa từ trở lại preedit.
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
  // Frontend thường gửi reset ngay sau commit. Reset này không được xóa
  // snapshot finalized cần cho Backspace quay lại sửa từ vừa gõ.
  handler.requestProtectedReset(noSurroundingContext);
  assert(noSurroundingContext.propertyFor(&factory)
             ->resetProtectedUntilUsec != 0);

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
  assert(restoreWord.accepted());
  assert(noSurroundingContext.propertyFor(&factory)->finalizedWordRecovery ==
         areca::FinalizedWordRecoveryState::AwaitingBackspaceRelease);
  fcitx::KeyEvent restoreWordRelease(
      &noSurroundingContext, fcitx::Key(FcitxKey_BackSpace), true);
  handler.handleKeyEvent(restoreWordRelease);
  assert(restoreWordRelease.accepted());
  assert(requestedBackspaces == 2);
  assert(eraseDone);
  assert(noSurroundingContext.propertyFor(&factory)->finalizedWordRecovery ==
         areca::FinalizedWordRecoveryState::Erasing);
  fcitx::KeyEvent injectedBackspace(&noSurroundingContext,
                                    fcitx::Key(FcitxKey_BackSpace));
  handler.handleKeyEvent(injectedBackspace);
  assert(!injectedBackspace.accepted());
  assert(noSurroundingContext.propertyFor(&factory)->finalizedWordRecovery ==
         areca::FinalizedWordRecoveryState::Erasing);
  eraseDone(true);
  assert(noSurroundingContext.forwardedBackspaces.empty());
  assert(noSurroundingContext.propertyFor(&factory)->composing == "á");

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
  fcitx::KeyEvent restoreAfterSpacesRelease(
      &multipleSpacesContext, fcitx::Key(FcitxKey_BackSpace), true);
  handler.handleKeyEvent(restoreAfterSpacesRelease);
  assert(restoreAfterSpacesRelease.accepted());
  assert(multipleSpacesContext.deletes.empty());
  assert(requestedBackspaces == 2);
  assert(eraseDone);
  eraseDone(true);
  assert(multipleSpacesContext.propertyFor(&factory)->composing == "á");

  // Nhấn giữ Backspace hủy việc mở lại từ và cho repeat đi thẳng xuống app để
  // người dùng tiếp tục xóa nhanh như trong Rewrite mode.
  TestInputContext heldBackspaceContext(manager);
  handler.activate(heldBackspaceContext);
  for (const char key : std::string("as ")) {
    fcitx::KeyEvent keyEvent(
        &heldBackspaceContext,
        fcitx::Key(fcitx::Key::keySymFromUnicode(
            static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
  }
  fcitx::KeyEvent heldPress(&heldBackspaceContext,
                            fcitx::Key(FcitxKey_BackSpace));
  handler.handleKeyEvent(heldPress);
  assert(heldPress.accepted());
  fcitx::KeyEvent heldRepeat(
      &heldBackspaceContext,
      fcitx::Key(FcitxKey_BackSpace, fcitx::KeyState::Repeat));
  handler.handleKeyEvent(heldRepeat);
  assert(!heldRepeat.accepted());
  assert(heldBackspaceContext.propertyFor(&factory)->finalizedWordRecovery ==
         areca::FinalizedWordRecoveryState::Idle);
  assert(heldBackspaceContext.propertyFor(&factory)
             ->engine->currentText().empty());

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

  // Khi Backspace chuyển thành repeat, commit phần preedit đang thấy rồi xóa
  // state Bamboo; repeat hiện tại và các repeat sau đi thẳng xuống ứng dụng.
  TestInputContext recoveryRepeatContext(manager);
  handler.activate(recoveryRepeatContext);
  for (const char key : std::string("nhanhsh")) {
    fcitx::KeyEvent keyEvent(
        &recoveryRepeatContext,
        fcitx::Key(fcitx::Key::keySymFromUnicode(
            static_cast<unsigned char>(key))));
    handler.handleKeyEvent(keyEvent);
  }
  fcitx::KeyEvent recoveryRepeat(
      &recoveryRepeatContext,
      fcitx::Key(FcitxKey_BackSpace, fcitx::KeyState::Repeat));
  handler.handleKeyEvent(recoveryRepeat);
  assert(!recoveryRepeat.accepted());
  assert(recoveryRepeatContext.commits ==
         std::vector<std::string>{"nhanhsh"});
  assert(recoveryRepeatContext.propertyFor(&factory)->composing.empty());
  assert(recoveryRepeatContext.propertyFor(&factory)
             ->engine->currentText().empty());

  // IBus không gắn Repeat: nhấn giữ hủy khôi phục từ đã commit.
  {
    TestInputContext ibusContext(manager, "ibus");
    handler.activate(ibusContext);
    auto *ibusState = ibusContext.propertyFor(&factory);
    for (const char key : std::string("as ")) {
      fcitx::KeyEvent typed(
          &ibusContext, fcitx::Key(fcitx::Key::keySymFromUnicode(key)));
      handler.handleKeyEvent(typed);
    }
    const auto erasuresBefore = requestedBackspaces;
    fcitx::KeyEvent press(&ibusContext, fcitx::Key(FcitxKey_BackSpace));
    handler.handleKeyEvent(press);
    assert(press.accepted());
    for (int i = 0; i < 3; ++i) {
      fcitx::KeyEvent repeat(&ibusContext, fcitx::Key(FcitxKey_BackSpace));
      handler.handleKeyEvent(repeat);
      assert(!repeat.accepted());
      assert(ibusState->composing.empty());
      assert(ibusState->engine->currentText().empty());
      assert(ibusState->finalizedWordRecovery ==
             areca::FinalizedWordRecoveryState::Idle);
    }
    fcitx::KeyEvent release(&ibusContext, fcitx::Key(FcitxKey_BackSpace), true);
    handler.handleKeyEvent(release);
    assert(!release.accepted());
    assert(requestedBackspaces == erasuresBefore);
    assert(ibusContext.commits ==
           std::vector<std::string>{"á "});

    // Release phải kết thúc lần nhấn giữ để lần gõ sau recovery bình thường.
    for (const char key : std::string("nhanhsh")) {
      fcitx::KeyEvent typed(
          &ibusContext, fcitx::Key(fcitx::Key::keySymFromUnicode(key)));
      handler.handleKeyEvent(typed);
    }
    fcitx::KeyEvent tap(&ibusContext, fcitx::Key(FcitxKey_BackSpace));
    handler.handleKeyEvent(tap);
    assert(tap.accepted());
    assert(ibusState->composing == "nhánh");
    // Press tiếp theo vẫn sửa trong preedit, không ép commit ra ứng dụng.
    fcitx::KeyEvent secondTap(&ibusContext, fcitx::Key(FcitxKey_BackSpace));
    handler.handleKeyEvent(secondTap);
    assert(secondTap.accepted());
    assert(!ibusState->composing.empty());
    assert(ibusContext.commits == std::vector<std::string>{"á "});
  }

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

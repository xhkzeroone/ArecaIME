#pragma once

#include "dbus_raii.h"

#include <string>

namespace areca {

struct AtspiFocusInfo {
  int processId = -1;
  int role = -1;
  bool terminal = false;
  bool documentWeb = false;
  bool ignoredWindowManager = false;
  std::string program;
  std::string className;
};

class AtspiAccessible {
public:
  explicit AtspiAccessible(DBusConnection *bus) : bus_(bus) {}

  // Chỉ hỏi bus daemon nên nhanh và không phụ thuộc ứng dụng có phản hồi hay
  // không. Dùng để công bố tên chương trình trước khi hỏi chi tiết.
  int senderProcessId(const char *sender) const;
  static std::string programForProcess(int processId);
  // Focus vào shell hay compositor không phải ô nhập của ứng dụng nào.
  static bool isWindowManagerProgram(const std::string &program);

  // `processId` lấy từ senderProcessId; <= 0 nghĩa là chưa biết.
  AtspiFocusInfo inspectFocus(const char *sender, const char *path,
                              int processId) const;
  // Đánh thức accessibility của các ứng dụng. Không chờ trả lời.
  void pokeApps() const;

private:
  DBusConnection *bus_;
};

} // namespace areca
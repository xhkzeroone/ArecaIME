/*
 * Compatibility declaration for the public Fcitx5 Notifications addon API.
 * Some distributions ship the runtime addon without its generated CMake
 * package and public header.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <fcitx/addoninstance.h>

namespace fcitx {

using NotificationActionCallback = std::function<void(const std::string &)>;
using NotificationClosedCallback = std::function<void(uint32_t)>;

} // namespace fcitx

FCITX_ADDON_DECLARE_FUNCTION(
    Notifications, sendNotification,
    uint32_t(const std::string &appName, uint32_t replaceId,
             const std::string &appIcon, const std::string &summary,
             const std::string &body, const std::vector<std::string> &actions,
             int32_t timeout, fcitx::NotificationActionCallback actionCallback,
             fcitx::NotificationClosedCallback closedCallback));

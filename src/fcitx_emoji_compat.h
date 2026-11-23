/*
 * Compatibility declaration for the public Fcitx5 Emoji addon API.
 * Some distributions ship the runtime addon without its generated CMake
 * package and public header.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <fcitx/addoninstance.h>

FCITX_ADDON_DECLARE_FUNCTION(Emoji, query,
                             const std::vector<std::string> &(
                                 const std::string &language,
                                 const std::string &key, bool fallbackToEn));

FCITX_ADDON_DECLARE_FUNCTION(
    Emoji, prefix,
    void(const std::string &language, const std::string &key, bool fallbackToEn,
         const std::function<bool(const std::string &,
                                  const std::vector<std::string> &)> &));

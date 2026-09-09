#pragma once

#include <array>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>

namespace areca::settings {

template <typename Option> bool checkbox(const char *label, Option &option) {
    bool value = option.value();
    if (!ImGui::Checkbox(label, &value)) {
        return false;
    }
    option.setValue(value);
    return true;
}

template <typename Option> bool inputInt(const char *label, Option &option) {
    int value = option.value();
    ImGui::SetNextItemWidth(140.0F);
    if (!ImGui::InputInt(label, &value)) {
        return false;
    }
    option.setValue(value);
    return true;
}

template <typename Option> bool inputText(const char *label, Option &option) {
    std::array<char, 4096> buffer{};
    SDL_strlcpy(buffer.data(), option.value().c_str(), buffer.size());
    ImGui::SetNextItemWidth(-1.0F);
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) {
        return false;
    }
    option.setValue(buffer.data());
    return true;
}

template <typename Option>
bool stringCombo(const char *label, Option &option,
                 const std::vector<std::string> &choices, const float width) {
    const auto current = option.value();
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, current.c_str())) {
        for (const auto &choice : choices) {
            const bool selected = choice == current;
            if (ImGui::Selectable(choice.c_str(), selected)) {
                option.setValue(choice);
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace areca::settings

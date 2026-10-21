#include "areca_config.h"

#include <cassert>

int main() {
  using areca::AppBackendMode;
  using areca::PresentationMode;
  using areca::presentationModeForAppSetting;

  assert(presentationModeForAppSetting(AppBackendMode::Auto,
                                       PresentationMode::Preedit) ==
         PresentationMode::Preedit);
  assert(presentationModeForAppSetting(AppBackendMode::Auto,
                                       PresentationMode::Redirect) ==
         PresentationMode::Redirect);

  assert(presentationModeForAppSetting(AppBackendMode::SurroundingText,
                                       PresentationMode::Preedit) ==
         PresentationMode::Rewrite);
  assert(presentationModeForAppSetting(AppBackendMode::ShiftSelect,
                                       PresentationMode::Redirect) ==
         PresentationMode::Rewrite);
  assert(presentationModeForAppSetting(AppBackendMode::Backspace,
                                       PresentationMode::Preedit) ==
         PresentationMode::Rewrite);
  assert(presentationModeForAppSetting(AppBackendMode::Preedit,
                                       PresentationMode::Rewrite) ==
         PresentationMode::Preedit);
  assert(presentationModeForAppSetting(AppBackendMode::Redirect,
                                       PresentationMode::Rewrite) ==
         PresentationMode::Redirect);
}

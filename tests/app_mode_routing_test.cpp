#include "areca_config.h"

#include <cassert>
#include <utility>

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

  areca::AppTimingProfilesConfig timingProfiles;
  assert(timingProfiles.appTimings.value().empty());
  areca::AppTimingOverrideEntry timing;
  assert(timing.backspaceDelayMs.value() == -1);
  assert(timing.afterBackspaceWaitMs.value() == -1);
  assert(timing.postCommitDelayMs.value() == -1);
  timing.appName.setValue("soffice.bin");
  timing.afterBackspaceWaitMs.setValue(0);
  timingProfiles.appTimings.mutableValue()->push_back(std::move(timing));
  assert(timingProfiles.appTimings.value()[0].appName.value() ==
         "soffice.bin");
  // Zero is a real override; only -1 means inherit from the frontend.
  assert(timingProfiles.appTimings.value()[0].afterBackspaceWaitMs.value() ==
         0);
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

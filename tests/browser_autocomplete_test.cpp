#include <cassert>
#include <iostream>

#include "browser_autocomplete.h"

int main() {
  using areca::isBrowserAutocomplete;

  assert(areca::isBrowserLikeProgram("google-chrome"));
  assert(areca::isBrowserLikeProgram("/usr/bin/firefox.desktop"));
  assert(areca::isBrowserLikeProgram("thorium"));
  assert(areca::isBrowserLikeProgram("ladybird"));
  assert(areca::isBrowserLikeProgram("min"));
  assert(areca::isBrowserLikeProgram("luakit"));
  assert(areca::isBrowserLikeProgram(""));
  assert(!areca::isBrowserLikeProgram("org.kde.kate"));
  // User typed "go" and the browser selected "ogle" through line end.
  assert(isBrowserAutocomplete("google", 2, 6, "go"));

  // No selection and no appended suffix is normal surrounding text.
  assert(!isBrowserAutocomplete("go", 2, 2, "go"));

  // Without selection (cursor == anchor), text after cursor is middle-of-text editing, not autocomplete.
  assert(!isBrowserAutocomplete("google", 2, 2, "go"));
  assert(!isBrowserAutocomplete("goo", 2, 2, "go"));
  assert(!isBrowserAutocomplete("go\nogle", 2, 2, "go"));

  // Selection not extending to line end is not browser inline autocomplete.
  assert(!isBrowserAutocomplete("google more", 2, 6, "go"));

  // The shown composition must be the suffix immediately before cursor.
  assert(!isBrowserAutocomplete("xxgoogle", 3, 8, "go"));

  // Reverse cursor/anchor direction is handled like OpenKey.
  assert(isBrowserAutocomplete("google", 6, 2, "go"));

  // Autocomplete never spans a newline.
  assert(!isBrowserAutocomplete("go\nogle", 2, 7, "go"));

  std::cout << "Browser autocomplete tests passed\n";
  return 0;
}

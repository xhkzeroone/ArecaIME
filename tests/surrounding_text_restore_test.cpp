#include <cassert>
#include <string>

#include "surrounding_text_restore.h"

int main() {
  using areca::extractSurroundingRestoreCandidate;

  auto candidate = extractSurroundingRestoreCandidate(
      "Xin chào", 8, 8);
  assert(candidate && candidate->text == "chào" &&
         candidate->characterCount == 4);

  candidate = extractSurroundingRestoreCandidate("anh", 3, 3);
  assert(candidate && candidate->text == "anh");

  assert(!extractSurroundingRestoreCandidate("chào", 4, 3));
  assert(!extractSurroundingRestoreCandidate("chào", 0, 0));
  assert(!extractSurroundingRestoreCandidate("chào", 99, 99));
  assert(!extractSurroundingRestoreCandidate("chào ", 5, 5));

  assert(!extractSurroundingRestoreCandidate(
      "abcdefghijklmnopq", 17, 17, 16));
  assert(!extractSurroundingRestoreCandidate(
      std::string("a\xFF", 2), 2, 2));
  assert(!extractSurroundingRestoreCandidate("cha\u0300o", 5, 5));
}

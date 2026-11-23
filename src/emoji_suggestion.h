#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fcitx-utils/utf8.h>

namespace areca {

// Từ khoá người dùng gõ sau dấu ":" ngay trước con trỏ, kèm đoạn ":từ" sẽ bị
// thay bằng emoji.
struct EmojiQuery {
  std::string keyword;
  std::string replacedText;
  uint32_t replacedLength = 0;
};

// Từ khoá emoji tiếng Việt có tới vài chữ ("mặt cười toét"), nên cho phép
// khoảng trắng giữa các chữ nhưng giới hạn độ dài để câu thường có dấu ":"
// không bị coi là từ khoá.
inline constexpr std::size_t kMaxEmojiKeywordChars = 24;
inline constexpr std::size_t kMaxEmojiKeywordWords = 3;

// Tìm ":từ" ở cuối đoạn text Areca biết chắc nằm trước con trỏ. Dấu ":" phải
// đứng đầu đoạn hoặc sau khoảng trắng để bỏ qua "10:30", "http://" hay "a:b";
// ngay sau ":" phải là chữ để bỏ qua "Ghi chú: ...".
inline std::optional<EmojiQuery>
emojiQueryBeforeCursor(std::string_view known) {
  const auto colon = known.rfind(':');
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }
  if (colon > 0) {
    const char before = known[colon - 1];
    if (before != ' ' && before != '\t' && before != '\n') {
      return std::nullopt;
    }
  }
  const std::string_view keyword = known.substr(colon + 1);
  if (keyword.empty() || keyword.front() == ' ' || keyword.back() == ' ') {
    return std::nullopt;
  }

  const auto length = fcitx::utf8::lengthValidated(keyword);
  if (length == fcitx::utf8::INVALID_LENGTH || length > kMaxEmojiKeywordChars) {
    return std::nullopt;
  }
  std::size_t words = 1;
  std::string lowered;
  lowered.reserve(keyword.size());
  for (std::size_t i = 0; i < keyword.size(); ++i) {
    const char ch = keyword[i];
    if (ch == ' ') {
      if (keyword[i - 1] == ' ' || ++words > kMaxEmojiKeywordWords) {
        return std::nullopt;
      }
      lowered.push_back(ch);
      continue;
    }
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 0x80) {
      // Chữ cái, chữ số và "+-_" của shortcode kiểu chat (+1, thumbs_up); dấu
      // câu khác hay ký tự điều khiển kết thúc từ khoá.
      const bool allowed =
          (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == '_';
      if (!allowed) {
        return std::nullopt;
      }
      lowered.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + 32)
                                               : ch);
      continue;
    }
    lowered.push_back(ch);
  }

  EmojiQuery query;
  query.keyword = std::move(lowered);
  query.replacedText = std::string(known.substr(colon));
  query.replacedLength = static_cast<uint32_t>(length + 1);
  return query;
}

struct EmojiShortcode {
  std::string_view code;
  std::string_view emoji;
};

// Shortcode quen dùng khi chat mà từ điển CLDR không có ("like", "haha",
// "+1"). Có thể trùng emoji, ví dụ "like" và "+1" đều là 👍.
inline constexpr std::array<EmojiShortcode, 60> kEmojiShortcodes{{
    {"+1", "👍"},      {"-1", "👎"},       {"like", "👍"},
    {"dislike", "👎"}, {"thumbsup", "👍"}, {"thumbsdown", "👎"},
    {"ok", "👌"},      {"clap", "👏"},     {"pray", "🙏"},
    {"muscle", "💪"},  {"wave", "👋"},     {"hi", "👋"},
    {"bye", "👋"},     {"haha", "😂"},     {"hihi", "😆"},
    {"hehe", "😁"},    {"lol", "🤣"},      {"rofl", "🤣"},
    {"joy", "😂"},     {"smile", "😊"},    {"grin", "😁"},
    {"wink", "😉"},    {"blush", "😊"},    {"cool", "😎"},
    {"love", "❤️"},     {"heart", "❤️"},     {"hearts", "😍"},
    {"kiss", "😘"},    {"hug", "🤗"},      {"sad", "😢"},
    {"cry", "😭"},     {"sob", "😭"},      {"angry", "😠"},
    {"rage", "😡"},    {"wow", "😮"},      {"omg", "😱"},
    {"shock", "😱"},   {"think", "🤔"},    {"hmm", "🤔"},
    {"shrug", "🤷"},   {"facepalm", "🤦"}, {"sleepy", "😴"},
    {"sweat", "😅"},   {"sick", "🤒"},     {"party", "🥳"},
    {"tada", "🎉"},    {"fire", "🔥"},     {"100", "💯"},
    {"star", "⭐"},    {"sparkles", "✨"}, {"check", "✅"},
    {"x", "❌"},       {"warning", "⚠️"},   {"eyes", "👀"},
    {"skull", "💀"},   {"poop", "💩"},     {"rocket", "🚀"},
    {"gift", "🎁"},    {"coffee", "☕"},   {"beer", "🍺"},
}};

// Một emoji gợi ý kèm từ khoá đã khớp, để hiện dạng "😂 :haha:".
struct EmojiMatch {
  std::string emoji;
  std::string name;
};

// Thêm vào danh sách gợi ý nếu emoji chưa có và danh sách chưa đầy.
inline void appendEmojiMatch(std::vector<EmojiMatch> &out,
                             std::string_view emoji, std::string_view name,
                             std::size_t limit) {
  if (out.size() >= limit ||
      std::any_of(out.begin(), out.end(), [emoji](const EmojiMatch &match) {
        return match.emoji == emoji;
      })) {
    return;
  }
  out.push_back({std::string(emoji), std::string(name)});
}

// Emoji theo shortcode: khớp đúng trước, rồi các shortcode bắt đầu bằng từ
// khoá, theo thứ tự trong bảng và không lặp emoji.
inline std::vector<EmojiMatch> emojiShortcodeMatches(std::string_view keyword,
                                                     std::size_t limit) {
  std::vector<EmojiMatch> out;
  for (const auto &entry : kEmojiShortcodes) {
    if (entry.code == keyword) {
      appendEmojiMatch(out, entry.emoji, entry.code, limit);
    }
  }
  for (const auto &entry : kEmojiShortcodes) {
    if (entry.code != keyword && entry.code.starts_with(keyword)) {
      appendEmojiMatch(out, entry.emoji, entry.code, limit);
    }
  }
  return out;
}

} // namespace areca

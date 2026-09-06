#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

#include "imrtc/Errors.h"
#include "imrtc/Json.h"

namespace imrtc {
namespace {

/** kMaxSafeInteger = 2^53-1：协议 §2.4 规则 7 的整数上界（JS Number 安全区）。 */
constexpr std::int64_t kMaxSafeInteger = 9007199254740991LL;

/** kMaxDepth 防御深层嵌套导致的递归爆栈。协议本身只允许两层（§2.4 规则 5）。 */
constexpr int kMaxDepth = 32;

[[noreturn]] void fail(const std::string& reason) {
  throw RtcError(ErrorCode::BadEnvelope, reason);
}

/**
 * Parser 是一个手写的递归下降 JSON 解析器。
 *
 * **数字的处理是它存在的理由**：协议按**值**判定整数（`1e3` 是整数 1000，
 * `15e-1` 不是），而多数库只给一个 double 或按字面量分类。这里的规则是——
 * 带小数点/指数的先按 double 读，若其值是整数且落在 ±(2^53-1) 内就收成 Int，
 * 否则留作 Double 交给 checkDiscipline() 去拒。
 */
class Parser {
public:
  explicit Parser(const std::string& text) : text_(text) {}

  Json run() {
    skipWhitespace();
    Json value = parseValue(0);
    skipWhitespace();
    if (pos_ != text_.size()) {
      fail("JSON 文本尾部有多余内容，位置 " + std::to_string(pos_));
    }
    return value;
  }

private:
  const std::string& text_;
  std::size_t pos_ = 0;

  bool atEnd() const { return pos_ >= text_.size(); }
  char peek() const { return atEnd() ? '\0' : text_[pos_]; }

  void skipWhitespace() {
    while (!atEnd()) {
      const char ch = text_[pos_];
      if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') break;
      ++pos_;
    }
  }

  void expect(char ch) {
    if (atEnd() || text_[pos_] != ch) {
      fail(std::string("位置 ") + std::to_string(pos_) + " 期望 '" + ch + "'");
    }
    ++pos_;
  }

  bool consumeLiteral(const char* literal) {
    const std::size_t length = std::char_traits<char>::length(literal);
    if (text_.compare(pos_, length, literal) != 0) return false;
    pos_ += length;
    return true;
  }

  Json parseValue(int depth) {
    if (depth > kMaxDepth) fail("JSON 嵌套过深");
    if (atEnd()) fail("JSON 文本意外结束");

    switch (peek()) {
      case '{': return parseObject(depth);
      case '[': return parseArray(depth);
      case '"': return Json::make(parseString());
      case 't':
        if (consumeLiteral("true")) return Json::make(true);
        fail("位置 " + std::to_string(pos_) + " 无法识别的字面量");
      case 'f':
        if (consumeLiteral("false")) return Json::make(false);
        fail("位置 " + std::to_string(pos_) + " 无法识别的字面量");
      case 'n':
        if (consumeLiteral("null")) return Json::makeNull();
        fail("位置 " + std::to_string(pos_) + " 无法识别的字面量");
      default: return parseNumber();
    }
  }

  Json parseObject(int depth) {
    expect('{');
    Json::Object members;
    skipWhitespace();
    if (peek() == '}') {
      ++pos_;
      return Json::makeObject(std::move(members));
    }
    while (true) {
      skipWhitespace();
      std::string key = parseString();
      skipWhitespace();
      expect(':');
      skipWhitespace();
      Json value = parseValue(depth + 1);
      // 重复键取**最后一个**，与 JS/Go/Swift 的解析器一致，避免五端不一致。
      bool replaced = false;
      for (Json::Member& member : members) {
        if (member.first == key) {
          member.second = std::move(value);
          replaced = true;
          break;
        }
      }
      if (!replaced) members.emplace_back(std::move(key), std::move(value));

      skipWhitespace();
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      expect('}');
      break;
    }
    return Json::makeObject(std::move(members));
  }

  Json parseArray(int depth) {
    expect('[');
    Json::Array items;
    skipWhitespace();
    if (peek() == ']') {
      ++pos_;
      return Json::makeArray(std::move(items));
    }
    while (true) {
      skipWhitespace();
      items.push_back(parseValue(depth + 1));
      skipWhitespace();
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      expect(']');
      break;
    }
    return Json::makeArray(std::move(items));
  }

  std::string parseString() {
    expect('"');
    std::string out;
    while (true) {
      if (atEnd()) fail("字符串未闭合");
      const char ch = text_[pos_++];
      if (ch == '"') break;
      if (ch != '\\') {
        // 协议禁止字符串内嵌 NUL（§2.4 补充）。
        if (ch == '\0') fail("字符串里出现 NUL");
        out.push_back(ch);
        continue;
      }
      if (atEnd()) fail("转义序列未完成");
      const char escape = text_[pos_++];
      switch (escape) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': appendUnicodeEscape(out); break;
        default: fail(std::string("未知转义 \\") + escape);
      }
    }
    return out;
  }

  /** appendUnicodeEscape 把 \uXXXX（含代理对）解成 UTF-8。 */
  void appendUnicodeEscape(std::string& out) {
    std::uint32_t code = readHex4();
    if (code >= 0xD800 && code <= 0xDBFF) {
      // 高代理，必须紧跟一个低代理，否则不是合法 UTF-16。
      if (text_.compare(pos_, 2, "\\u") != 0) fail("代理对缺少低位");
      pos_ += 2;
      const std::uint32_t low = readHex4();
      if (low < 0xDC00 || low > 0xDFFF) fail("代理对低位非法");
      code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
    }
    appendUtf8(code, out);
  }

  std::uint32_t readHex4() {
    if (pos_ + 4 > text_.size()) fail("\\u 转义不足四位");
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char ch = text_[pos_++];
      value <<= 4;
      if (ch >= '0' && ch <= '9') {
        value += static_cast<std::uint32_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        value += static_cast<std::uint32_t>(ch - 'a' + 10);
      } else if (ch >= 'A' && ch <= 'F') {
        value += static_cast<std::uint32_t>(ch - 'A' + 10);
      } else {
        fail("\\u 转义含非十六进制字符");
      }
    }
    return value;
  }

  static void appendUtf8(std::uint32_t code, std::string& out) {
    if (code < 0x80) {
      out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (code >> 6)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (code >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (code >> 18)));
      out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
  }

  Json parseNumber() {
    const std::size_t start = pos_;
    if (peek() == '-') ++pos_;
    bool hasFraction = false;
    while (!atEnd()) {
      const char ch = text_[pos_];
      if (ch >= '0' && ch <= '9') {
        ++pos_;
      } else if (ch == '.' || ch == 'e' || ch == 'E' || ch == '+' || ch == '-') {
        hasFraction = true;
        ++pos_;
      } else {
        break;
      }
    }
    const std::string token = text_.substr(start, pos_ - start);
    if (token.empty() || token == "-") fail("位置 " + std::to_string(start) + " 不是合法的值");

    if (!hasFraction) {
      errno = 0;
      char* end = nullptr;
      const long long parsed = std::strtoll(token.c_str(), &end, 10);
      if (end != nullptr && *end == '\0' && errno != ERANGE) {
        return Json::make(static_cast<std::int64_t>(parsed));
      }
      // 溢出 int64：当作浮点带出去，让 checkDiscipline() 报 2^53 越界。
    }

    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end == nullptr || *end != '\0') fail("数字 \"" + token + "\" 格式非法");
    if (!std::isfinite(value)) fail("数字 \"" + token + "\" 不是有限值");

    // **按值判定**：1e3 是整数 1000，收成 Int；15e-1 = 1.5 不是，留作 Double。
    const double integral = std::trunc(value);
    if (integral == value && std::fabs(value) <= static_cast<double>(kMaxSafeInteger)) {
      return Json::make(static_cast<std::int64_t>(integral));
    }
    return Json::makeDouble(value);
  }
};

}  // namespace

Json Json::parse(const std::string& text) { return Parser(text).run(); }

}  // namespace imrtc

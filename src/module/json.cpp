/** @file json.cpp
 * @brief 线性扫描 RFC 8259 文法，保留文本，限制栈深度，不依赖 Qt。
 */
#include "json_p.hpp"
#include <cstdint>

namespace cordis::module::detail {
namespace {
class Scanner {
public:
    explicit Scanner(std::string_view input) : input_(input) {}
    bool scan() noexcept { return value(0) && (space(), at_ == input_.size()); }

private:
    char peek() const noexcept { return at_ < input_.size() ? input_[at_] : '\0'; }
    void space() noexcept
    {
        while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n')
            ++at_;
    }
    bool take(char expected) noexcept
    {
        if (at_ == input_.size() || input_[at_] != expected)
            return false;
        ++at_;
        return true;
    }
    bool literal(std::string_view word) noexcept
    {
        if (input_.substr(at_, word.size()) != word)
            return false;
        at_ += word.size();
        return true;
    }
    bool hex(std::uint32_t& code) noexcept
    {
        code = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const auto c = peek();
            unsigned digit;
            if (c >= '0' && c <= '9')
                digit = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                digit = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                digit = static_cast<unsigned>(c - 'A' + 10);
            else
                return false;
            code = (code << 4) | digit;
            ++at_;
        }
        return true;
    }
    bool unicode() noexcept
    {
        const auto lead = static_cast<unsigned char>(input_[at_++]);
        unsigned extra;
        std::uint32_t code, minimum;
        if (lead >= 0xc2 && lead <= 0xdf) {
            extra = 1;
            code = lead & 0x1f;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            extra = 2;
            code = lead & 0x0f;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            extra = 3;
            code = lead & 0x07;
            minimum = 0x10000;
        } else
            return false;
        for (unsigned i = 0; i < extra; ++i) {
            if (at_ == input_.size())
                return false;
            const auto c = static_cast<unsigned char>(input_[at_++]);
            if ((c & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (c & 0x3f);
        }
        return code >= minimum && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff);
    }
    bool string() noexcept
    {
        if (!take('"'))
            return false;
        while (at_ < input_.size()) {
            const auto c = static_cast<unsigned char>(peek());
            if (c == '"') {
                ++at_;
                return true;
            }
            if (c < 0x20)
                return false;
            if (c >= 0x80) {
                if (!unicode())
                    return false;
                continue;
            }
            ++at_;
            if (c != '\\')
                continue;
            const auto escaped = peek();
            if (escaped == '"' || escaped == '\\' || escaped == '/' || escaped == 'b' || escaped == 'f' ||
                escaped == 'n' || escaped == 'r' || escaped == 't') {
                ++at_;
                continue;
            }
            if (!take('u'))
                return false;
            std::uint32_t high, low;
            if (!hex(high))
                return false;
            if (high >= 0xdc00 && high <= 0xdfff)
                return false;
            if (high >= 0xd800 && high <= 0xdbff &&
                (!take('\\') || !take('u') || !hex(low) || low < 0xdc00 || low > 0xdfff))
                return false;
        }
        return false;
    }
    static bool digit(char c) noexcept { return c >= '0' && c <= '9'; }
    bool number() noexcept
    {
        (void)take('-');
        if (!take('0')) {
            if (peek() < '1' || peek() > '9')
                return false;
            while (digit(peek()))
                ++at_;
        }
        if (take('.')) {
            if (!digit(peek()))
                return false;
            while (digit(peek()))
                ++at_;
        }
        if (take('e') || take('E')) {
            if (!take('+'))
                (void)take('-');
            if (!digit(peek()))
                return false;
            while (digit(peek()))
                ++at_;
        }
        return true;
    }
    bool container(unsigned depth, bool object) noexcept
    {
        if (depth >= 64)
            return false;
        ++at_;
        space();
        const char end = object ? '}' : ']';
        if (take(end))
            return true;
        for (;;) {
            if (object && (!string() || (space(), !take(':'))))
                return false;
            if (!value(depth + 1))
                return false;
            space();
            if (take(end))
                return true;
            if (!take(','))
                return false;
            space();
        }
    }
    bool value(unsigned depth) noexcept
    {
        space();
        switch (peek()) {
        case '{':
            return container(depth, true);
        case '[':
            return container(depth, false);
        case '"':
            return string();
        case 't':
            return literal("true");
        case 'f':
            return literal("false");
        case 'n':
            return literal("null");
        default:
            return number();
        }
    }
    std::string_view input_;
    std::size_t at_ = 0;
};
} // namespace
bool validAbiJson(std::string_view text) noexcept
{
    return Scanner(text).scan();
}
} // namespace cordis::module::detail

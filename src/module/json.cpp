/** @file json.cpp
 * @brief 线性扫描 RFC 8259 文法，保留文本，限制栈深度，不依赖 Qt。
 */
#include "json_p.hpp"
#include <algorithm>
#include <cstdint>

namespace cordis::module::detail {
namespace {
bool readUnicode(std::string_view text, std::size_t& at) noexcept
{
    const auto lead = static_cast<unsigned char>(text[at++]);
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
        if (at == text.size())
            return false;
        const auto byte = static_cast<unsigned char>(text[at++]);
        if ((byte & 0xc0) != 0x80)
            return false;
        code = (code << 6) | (byte & 0x3f);
    }
    return code >= minimum && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff);
}
class Scanner {
public:
    explicit Scanner(std::string_view input) : input_(input) {}
    bool scan() noexcept { return value(0) && (space(), at_ == input_.size()); }
    std::optional<std::vector<std::string>> strings()
    {
        space();
        if (!take('['))
            return {};
        std::vector<std::string> result;
        space();
        if (take(']'))
            return (space(), at_ == input_.size()) ? std::optional(result) : std::nullopt;
        for (;;) {
            space();
            std::string item;
            if (result.size() >= 128 || !string(&item) || item.empty() || item.size() > 128 ||
                std::find(result.begin(), result.end(), item) != result.end())
                return {};
            result.push_back(std::move(item));
            space();
            if (take(']'))
                return (space(), at_ == input_.size()) ? std::optional(result) : std::nullopt;
            if (!take(','))
                return {};
        }
    }

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
    static void appendCode(std::string& output, std::uint32_t code)
    {
        if (code <= 0x7f)
            output.push_back(static_cast<char>(code));
        else if (code <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (code >> 6)));
            output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else if (code <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (code >> 12)));
            output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xf0 | (code >> 18)));
            output.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
    }
    bool string(std::string* output = nullptr)
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
                const auto begin = at_;
                if (!readUnicode(input_, at_))
                    return false;
                if (output)
                    output->append(input_.substr(begin, at_ - begin));
                continue;
            }
            ++at_;
            if (c != '\\') {
                if (output)
                    output->push_back(static_cast<char>(c));
                continue;
            }
            const auto escaped = peek();
            if (escaped == '"' || escaped == '\\' || escaped == '/' || escaped == 'b' || escaped == 'f' ||
                escaped == 'n' || escaped == 'r' || escaped == 't') {
                ++at_;
                if (output) {
                    switch (escaped) {
                    case 'b':
                        output->push_back('\b');
                        break;
                    case 'f':
                        output->push_back('\f');
                        break;
                    case 'n':
                        output->push_back('\n');
                        break;
                    case 'r':
                        output->push_back('\r');
                        break;
                    case 't':
                        output->push_back('\t');
                        break;
                    default:
                        output->push_back(escaped);
                        break;
                    }
                }
                continue;
            }
            if (!take('u'))
                return false;
            std::uint32_t high, low = 0;
            if (!hex(high))
                return false;
            if (high >= 0xdc00 && high <= 0xdfff)
                return false;
            if (high >= 0xd800 && high <= 0xdbff &&
                (!take('\\') || !take('u') || !hex(low) || low < 0xdc00 || low > 0xdfff))
                return false;
            if (output)
                appendCode(*output, high >= 0xd800 && high <= 0xdbff
                                        ? 0x10000 + ((high - 0xd800) << 10) + (low - 0xdc00)
                                        : high);
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
bool validAbiUtf8(std::string_view text) noexcept
{
    std::size_t at = 0;
    while (at < text.size()) {
        if (static_cast<unsigned char>(text[at]) < 0x80)
            ++at;
        else if (!readUnicode(text, at))
            return false;
    }
    return true;
}
std::optional<std::vector<std::string>> abiStringArray(std::string_view text)
{
    if (text.size() > 1024 * 1024)
        return {};
    return Scanner(text).strings();
}
} // namespace cordis::module::detail

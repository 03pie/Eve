#pragma once

#include <cstddef>
#include <string_view>

namespace cordis {
struct BuildInfo {
    std::string_view version;
    std::string_view compiler;
    std::size_t pointerBits;
};

[[nodiscard]] BuildInfo buildInfo() noexcept;
} // namespace cordis

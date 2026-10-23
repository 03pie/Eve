#pragma once

#include <cstdint>

namespace cordis {
// Zero is invalid. Allocation and overflow checks belong to Kernel.
template<class Tag>
class Id final {
public:
    constexpr Id() noexcept = default;
    explicit constexpr Id(std::uint64_t value) noexcept : value_(value) {}
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0; }
    friend constexpr bool operator==(Id left, Id right) noexcept
    {
        return left.value_ == right.value_;
    }
    friend constexpr bool operator!=(Id left, Id right) noexcept { return !(left == right); }
    friend constexpr bool operator<(Id left, Id right) noexcept
    {
        return left.value_ < right.value_;
    }

private:
    std::uint64_t value_ = 0;
};

struct RuntimeTag;
struct InstanceTag;
struct BindingTag;
struct RealmTag;
struct OperationTag;
struct GenerationTag;
struct EffectTag;
using RuntimeId = Id<RuntimeTag>;
using InstanceId = Id<InstanceTag>;
using BindingId = Id<BindingTag>;
using RealmId = Id<RealmTag>;
using OperationId = Id<OperationTag>;
using GenerationId = Id<GenerationTag>;
using EffectId = Id<EffectTag>;
using Epoch = std::uint64_t;
using ConfigRevision = std::uint64_t;
} // namespace cordis

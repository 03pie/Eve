/**
 * @file identity.hpp
 * @brief 提供带类型标签的整数标识。
 */
#pragma once

#include <cstdint>

namespace cordis {
/**
 * @brief 以标签区分的强类型整数标识。
 *
 * 零值无效；分配及溢出检查由 Kernel 负责。
 * @tparam Tag 用于区分不同标识类别的标签类型。
 */
template<class Tag>
class Id final {
public:
    /**
     * @brief 创建无效的零值标识。
     */
    constexpr Id() noexcept = default;
    /**
     * @brief 使用指定整数值创建标识。
     * @param value 标识的底层整数值。
     */
    explicit constexpr Id(std::uint64_t value) noexcept : value_(value) {}
    /**
     * @brief 获取底层整数值。
     * @return 此标识保存的整数值。
     */
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    /**
     * @brief 判断标识是否有效。
     * @return 底层值非零时返回 true。
     */
    [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0; }
    /**
     * @brief 比较两个标识是否相等。
     * @param left 左侧标识。
     * @param right 右侧标识。
     * @return 两个底层值相等时返回 true。
     */
    friend constexpr bool operator==(Id left, Id right) noexcept
    {
        return left.value_ == right.value_;
    }
    /**
     * @brief 比较两个标识是否不相等。
     * @param left 左侧标识。
     * @param right 右侧标识。
     * @return 两个底层值不相等时返回 true。
     */
    friend constexpr bool operator!=(Id left, Id right) noexcept { return !(left == right); }
    /**
     * @brief 按底层整数值比较两个标识的顺序。
     * @param left 左侧标识。
     * @param right 右侧标识。
     * @return 左侧底层值小于右侧时返回 true。
     */
    friend constexpr bool operator<(Id left, Id right) noexcept
    {
        return left.value_ < right.value_;
    }

private:
    std::uint64_t value_ = 0;
};

/** @brief 运行时标识的类型标签。 */
struct RuntimeTag;
/** @brief 实例标识的类型标签。 */
struct InstanceTag;
/** @brief 绑定标识的类型标签。 */
struct BindingTag;
/** @brief 领域标识的类型标签。 */
struct RealmTag;
/** @brief 操作标识的类型标签。 */
struct OperationTag;
/** @brief 代次标识的类型标签。 */
struct GenerationTag;
/** @brief effect 标识的类型标签。 */
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

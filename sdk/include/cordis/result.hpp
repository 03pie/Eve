/**
 * @file result.hpp
 * @brief 提供携带成功值或错误信息的结果类型。
 */
#pragma once

#include <cordis/error.hpp>
#include <stdexcept>
#include <utility>
#include <variant>

namespace cordis {
/**
 * @brief 访问 Result 中与当前状态不匹配的成员时抛出。
 */
class BadResultAccess : public std::logic_error {
public:
    /** @brief 使用默认错误说明构造异常。 */
    BadResultAccess() : std::logic_error("accessed the wrong Result alternative") {}
};

/**
 * @brief 表示一个成功值或错误信息。
 *
 * 禁止赋值以避免底层 variant 在赋值期间进入无效状态。
 * @tparam T 成功结果的值类型。
 */
template<class T>
class Result {
public:
    /**
     * @brief 构造成功结果。
     * @param value 要保存的成功值。
     * @return 保存该值的结果对象。
     */
    static Result success(T value) { return Result(std::in_place_index<0>, std::move(value)); }
    /**
     * @brief 构造失败结果。
     * @param error 要保存的错误信息。
     * @return 保存该错误的结果对象。
     */
    static Result failure(ErrorInfo error) { return Result(std::in_place_index<1>, std::move(error)); }
    /** @brief 复制结果及其当前值。 */
    Result(const Result&) = default;
    /** @brief 移动结果及其当前值。 */
    Result(Result&&) = default;
    /** @brief 禁止复制赋值，以保持 variant 状态有效。 */
    Result& operator=(const Result&) = delete;
    /** @brief 禁止移动赋值，以保持 variant 状态有效。 */
    Result& operator=(Result&&) = delete;

    /**
     * @brief 查询结果是否包含成功值。
     * @return 包含成功值时返回 true。
     */
    [[nodiscard]] bool hasValue() const noexcept { return storage_.index() == 0; }
    /** @brief 以可变左值引用访问成功值；状态不匹配时抛出 BadResultAccess。 */
    T& value() & { check(true); return std::get<0>(storage_); }
    /** @brief 以常量左值引用访问成功值；状态不匹配时抛出 BadResultAccess。 */
    const T& value() const & { check(true); return std::get<0>(storage_); }
    /** @brief 从右值结果中移动成功值；状态不匹配时抛出 BadResultAccess。 */
    T value() && { check(true); return std::get<0>(std::move(storage_)); }
    /** @brief 从常量右值结果中复制成功值；状态不匹配时抛出 BadResultAccess。 */
    T value() const && { check(true); return std::get<0>(storage_); }
    /** @brief 以常量左值引用访问错误；状态不匹配时抛出 BadResultAccess。 */
    const ErrorInfo& error() const & { check(false); return std::get<1>(storage_); }
    /** @brief 从右值结果中移动错误；状态不匹配时抛出 BadResultAccess。 */
    ErrorInfo error() && { check(false); return std::get<1>(std::move(storage_)); }
    /** @brief 从常量右值结果中复制错误；状态不匹配时抛出 BadResultAccess。 */
    ErrorInfo error() const && { check(false); return std::get<1>(storage_); }

private:
    /** @brief 使用指定的 variant 分支构造结果。 */
    template<std::size_t I, class U>
    Result(std::in_place_index_t<I> index, U&& value)
        : storage_(index, std::forward<U>(value)) {}
    /** @brief 检查当前分支是否与访问操作匹配。 */
    void check(bool success) const { if (hasValue() != success) throw BadResultAccess(); }
    std::variant<T, ErrorInfo> storage_;
};

/**
 * @brief 不携带成功值的 Result 特化。
 */
template<>
class Result<void> {
public:
    /** @brief 构造成功结果。 */
    static Result success() { return Result(std::in_place_index<0>); }
    /**
     * @brief 构造失败结果。
     * @param error 要保存的错误信息。
     * @return 保存该错误的结果对象。
     */
    static Result failure(ErrorInfo error) { return Result(std::in_place_index<1>, std::move(error)); }
    /** @brief 复制结果及其当前状态。 */
    Result(const Result&) = default;
    /** @brief 移动结果及其当前状态。 */
    Result(Result&&) = default;
    /** @brief 禁止复制赋值，以保持 variant 状态有效。 */
    Result& operator=(const Result&) = delete;
    /** @brief 禁止移动赋值，以保持 variant 状态有效。 */
    Result& operator=(Result&&) = delete;
    /** @brief 查询结果是否成功。 */
    [[nodiscard]] bool hasValue() const noexcept { return storage_.index() == 0; }
    /** @brief 验证结果成功；失败时抛出 BadResultAccess。 */
    void value() const { if (!hasValue()) throw BadResultAccess(); }
    /** @brief 以常量左值引用访问错误；成功时抛出 BadResultAccess。 */
    const ErrorInfo& error() const &
    {
        if (hasValue()) throw BadResultAccess();
        return std::get<1>(storage_);
    }
    /** @brief 从右值结果中移动错误；成功时抛出 BadResultAccess。 */
    ErrorInfo error() &&
    {
        if (hasValue()) throw BadResultAccess();
        return std::get<1>(std::move(storage_));
    }
    /** @brief 从常量右值结果中复制错误；成功时抛出 BadResultAccess。 */
    ErrorInfo error() const &&
    {
        if (hasValue()) throw BadResultAccess();
        return std::get<1>(storage_);
    }

private:
    /** @brief 构造成功状态。 */
    explicit Result(std::in_place_index_t<0>) : storage_(std::in_place_index<0>) {}
    /** @brief 使用指定错误构造失败状态。 */
    Result(std::in_place_index_t<1>, ErrorInfo error)
        : storage_(std::in_place_index<1>, std::move(error)) {}
    std::variant<std::monostate, ErrorInfo> storage_;
};

/**
 * @brief 创建带指定错误码和说明的错误信息。
 * @param code 错误分类。
 * @param message 错误说明。
 * @return 初始化完成的错误信息。
 */
inline ErrorInfo makeError(ErrorCode code, std::string message)
{
    ErrorInfo error;
    error.code = code;
    error.message = std::move(message);
    return error;
}
} // namespace cordis

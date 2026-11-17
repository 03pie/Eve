/** @file object_call_p.hpp
 * @brief 宿主同步调用的栈式删除保护；不提供对象查询或另一条清理路径。
 */
#pragma once
#include <cordis/qt/object.hpp>

namespace cordis::qt::detail {
class ObjectCall {
public:
    static Result<ObjectCall> enter(const QObjectResource& resource);
    Result<void> check() const;
    ObjectCall(ObjectCall&&) noexcept = default;
    ObjectCall(const ObjectCall&) = delete;
    ObjectCall& operator=(const ObjectCall&) = delete;
    ObjectCall& operator=(ObjectCall&&) = delete;
    ~ObjectCall();

private:
    explicit ObjectCall(std::shared_ptr<ObjectControl> control) : control_(std::move(control)) {}
    std::shared_ptr<ObjectControl> control_;
};
} // namespace cordis::qt::detail

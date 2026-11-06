/** @file instance_ownership_p.hpp
 * @brief 宿主运行拥有者的实际身份；不保存驱动、回调或第二套实例账目。
 */
#pragma once
#include <cordis/instance.hpp>
#include <thread>
namespace cordis {
class Kernel;
}
namespace cordis::detail {
struct InstanceOwnerIdentity final {
    const std::thread::id thread = std::this_thread::get_id();
};
/** @brief 只接纳已冻结的真实候选子树；宿主先检查 ready，再提交 Core，随后无回调 publish。 */
class PreparedInstanceOwnership {
public:
    PreparedInstanceOwnership(const PreparedInstanceOwnership&) = delete;
    PreparedInstanceOwnership& operator=(const PreparedInstanceOwnership&) = delete;
    PreparedInstanceOwnership(PreparedInstanceOwnership&&) noexcept = default;
    PreparedInstanceOwnership& operator=(PreparedInstanceOwnership&&) = delete;
    bool ready() const noexcept;
    void publish() noexcept;

private:
    friend struct InstanceOwnershipAccess;
    struct Member {
        Instance instance;
        std::shared_ptr<const InstanceOwnerIdentity> previous;
    };
    PreparedInstanceOwnership(std::shared_ptr<const InstanceOwnerIdentity> owner, std::vector<Member> members)
        : owner_(std::move(owner)), members_(std::move(members))
    {
    }
    std::shared_ptr<const InstanceOwnerIdentity> owner_;
    std::vector<Member> members_;
    bool published_ = false;
};
struct InstanceOwnershipAccess {
    static Result<std::shared_ptr<const InstanceOwnerIdentity>> owner(const Instance&);
    static Result<Instance> mount(Kernel&, std::shared_ptr<const InstanceOwnerIdentity>, const Runtime&, std::string,
                                  RealmMap, std::vector<ServiceRequirement>);
    // 准备真实子树的拥有权接纳；不能覆盖另一拥有者，发布时无分配/回调。
    static Result<PreparedInstanceOwnership> prepare(const Instance&, std::shared_ptr<const InstanceOwnerIdentity>);
};
} // namespace cordis::detail

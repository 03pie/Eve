/** @file instance_origin.cpp
 * @brief 两后端共用的真实业务归属；来源记录与可执行实例引用同生共灭。
 */
#include "catalog_p.hpp"
#include "instance_origin_p.hpp"
#include <utility>

namespace cordis::module::detail {
namespace {
ErrorInfo error(ErrorCode code, const char* message)
{
    auto value = makeError(code, message);
    value.phase = "module.instance.origin";
    return value;
}
} // namespace
InstanceOriginLease::InstanceOriginLease(std::shared_ptr<Generation> generation, InstanceOrigins::iterator row,
                                         ModuleLease lease)
    : generation_(std::move(generation)), row_(row), lease_(std::move(lease))
{
}
InstanceOriginLease::InstanceOriginLease(InstanceOriginLease&& other) noexcept
    : generation_(std::move(other.generation_)), row_(std::exchange(other.row_, {})), lease_(std::move(other.lease_))
{
}
InstanceOriginLease& InstanceOriginLease::operator=(InstanceOriginLease&& other) noexcept
{
    if (this != &other) {
        reset();
        generation_ = std::move(other.generation_);
        row_ = std::exchange(other.row_, {});
        lease_ = std::move(other.lease_);
    }
    return *this;
}
InstanceOriginLease::~InstanceOriginLease()
{
    reset();
}
void InstanceOriginLease::reset() noexcept
{
    if (!generation_)
        return;
    {
        std::lock_guard<std::mutex> lock(generation_->mutex);
        generation_->instanceOrigins.erase(*row_);
        row_.reset();
    }
    lease_.reset();
    generation_.reset();
}
Result<InstanceOriginLease> retainInstanceOrigin(GenerationId id, const std::string& plugin, std::string label,
                                                 InstanceId instance, Epoch epoch, const Context* context)
{
    using Output = Result<InstanceOriginLease>;
    if (!instance.valid() || !epoch)
        return Output::failure(error(ErrorCode::InvalidArgument, "invalid business identity"));
    InstanceOrigin origin{instance, {}, epoch, false};
    if (context) {
        auto source = context->source();
        if (!source.hasValue())
            return Output::failure(source.error());
        if (source.value().instance != instance || source.value().epoch != epoch)
            return Output::failure(
                error(ErrorCode::InvalidArgument, "business identity differs from actual Core context"));
        origin.runtime = source.value().runtime;
        origin.managed = true;
        origin.operation = source.value().replacement.value_or(OperationId{});
    }
    const auto generation = findGeneration(id);
    if (!generation)
        return Output::failure(error(ErrorCode::InvalidArgument, "unknown business generation"));
    auto lease = generation->ledger.acquire(ResourceKind::Instance, std::move(label));
    if (!lease.hasValue())
        return Output::failure(lease.error());
    std::lock_guard<std::mutex> lock(generation->mutex);
    if (origin.managed) {
        const auto at = generation->runtimeOrigins.find(origin.runtime);
        if (at == generation->runtimeOrigins.end() || at->second != plugin)
            return Output::failure(
                error(ErrorCode::InvalidArgument, "business factory belongs to another plugin or generation"));
    }
    const auto row = generation->instanceOrigins.emplace(std::make_pair(instance, epoch), origin);
    return Output::success(InstanceOriginLease(generation, row, std::move(lease.value())));
}
} // namespace cordis::module::detail

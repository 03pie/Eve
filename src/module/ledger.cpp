/** @file ledger.cpp
 * @brief 以唯一租约和互斥快照管理宿主账目，不持有插件执行能力。
 */
#include <algorithm>
#include <atomic>
#include <cordis/module/ledger.hpp>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace cordis::module {
namespace detail {
struct LedgerControl {
    explicit LedgerControl(GenerationId value) : generation(value) {}
    std::mutex mutex;
    GenerationId generation;
    bool accepting = true;
    std::uint64_t nextRecord = 1;
    std::map<std::uint64_t, ResourceRecord> records;
    std::vector<std::string> pins;
};
} // namespace detail
namespace {
GenerationId nextGeneration()
{
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load(std::memory_order_relaxed);
    for (;;) {
        if (value == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("module generation identifiers exhausted");
        if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
            return GenerationId{value};
    }
}
ErrorInfo ledgerError(ErrorCode code, std::string message)
{
    auto error = makeError(code, std::move(message));
    error.phase = "module.ledger";
    return error;
}
bool validLabel(const std::string& value)
{
    return !value.empty() && value.size() <= 256 &&
           std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}
} // namespace

ModuleLease::ModuleLease(std::shared_ptr<detail::LedgerControl> control, std::uint64_t id) noexcept
    : control_(std::move(control)), id_(id)
{
}
ModuleLease::ModuleLease(ModuleLease&& other) noexcept
    : control_(std::move(other.control_)), id_(std::exchange(other.id_, 0))
{
}
ModuleLease& ModuleLease::operator=(ModuleLease&& other) noexcept
{
    if (this != &other) {
        reset();
        control_ = std::move(other.control_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}
ModuleLease::~ModuleLease()
{
    reset();
}
void ModuleLease::reset() noexcept
{
    if (!control_)
        return;
    {
        std::lock_guard<std::mutex> lock(control_->mutex);
        control_->records.erase(id_);
        id_ = 0;
    }
    control_.reset();
}
GenerationLedger::GenerationLedger() : control_(std::make_shared<detail::LedgerControl>(nextGeneration())) {}
Result<ModuleLease> GenerationLedger::acquire(ResourceKind kind, std::string label) const
{
    if (kind < ResourceKind::Instance || kind > ResourceKind::ExternalHandle || !validLabel(label))
        return Result<ModuleLease>::failure(ledgerError(ErrorCode::InvalidArgument, "invalid resource kind or label"));
    std::lock_guard<std::mutex> lock(control_->mutex);
    if (!control_->accepting)
        return Result<ModuleLease>::failure(
            ledgerError(ErrorCode::ModulePinned, "generation is draining; new references are closed"));
    if (control_->records.size() >= 4096 || control_->nextRecord == std::numeric_limits<std::uint64_t>::max())
        return Result<ModuleLease>::failure(
            ledgerError(ErrorCode::ModulePinned, "module resource record budget exhausted"));
    const auto id = control_->nextRecord++;
    control_->records.emplace(id, ResourceRecord{id, kind, std::move(label)});
    return Result<ModuleLease>::success(ModuleLease(control_, id));
}
void GenerationLedger::beginDraining() const
{
    std::lock_guard<std::mutex> lock(control_->mutex);
    control_->accepting = false;
}
Result<void> GenerationLedger::pin(std::string reason) const
{
    if (!validLabel(reason))
        return Result<void>::failure(ledgerError(ErrorCode::InvalidArgument, "invalid module pin reason"));
    std::lock_guard<std::mutex> lock(control_->mutex);
    if (std::find(control_->pins.begin(), control_->pins.end(), reason) != control_->pins.end())
        return Result<void>::success();
    if (control_->pins.size() >= 64)
        return Result<void>::failure(ledgerError(ErrorCode::ModulePinned, "module pin reason budget exhausted"));
    control_->pins.push_back(std::move(reason));
    return Result<void>::success();
}
LedgerSnapshot GenerationLedger::snapshot() const
{
    std::lock_guard<std::mutex> lock(control_->mutex);
    LedgerSnapshot result{control_->generation, control_->accepting, {}, control_->pins};
    result.records.reserve(control_->records.size());
    for (const auto& pair : control_->records)
        result.records.push_back(pair.second);
    return result;
}
} // namespace cordis::module

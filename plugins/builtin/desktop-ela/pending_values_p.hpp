/** @file pending_values_p.hpp
 * @brief 原 UI 后端中的有界候选值；发布决定只读 Core 原来源，历史仍归各后端。
 */
#pragma once
#include <algorithm>
#include <atomic>
#include <eve/instance.hpp>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace eve::ui::detail {
/** @brief 同一 owner/epoch 共享原观察，作业不持后端或插件；终态结算后释放。 */
struct PendingSource : std::enable_shared_from_this<PendingSource> {
    explicit PendingSource(ContextIdentity identity) : owner(identity) {}
    ContextIdentity owner;
    std::function<Result<std::optional<bool>>()> publication;
    std::shared_ptr<ScheduledJob> observation;
    std::atomic_bool blocked{false};

    ScheduledJob observe(std::function<void()> changed)
    {
        std::weak_ptr<PendingSource> weak = shared_from_this();
        // 已复制到队列的刷新仍须处理 dirty，即使只读结算已释放来源。
        return {std::move(changed), [weak] {
                    if (auto source = weak.lock())
                        source->blocked.store(true);
                }};
    }
    Result<std::optional<bool>> decision() const
    {
        if (blocked.load())
            return Result<std::optional<bool>>::failure(
                makeError(ErrorCode::SchedulerClosed, "UI value publication observation was discarded"));
        return publication();
    }
};

/** @brief 两种实际值后端共享容量/取出机制；不拥有可见历史，也不执行呈现回调。 */
template <class Value> class PendingValues {
public:
    PendingValues(std::size_t countLimit, std::size_t byteLimit, std::size_t targetLimit)
        : countLimit_(countLimit), byteLimit_(byteLimit), targetLimit_(targetLimit)
    {
    }

    std::shared_ptr<PendingSource> source(ContextIdentity owner) const
    {
        for (const auto& entry : entries_)
            if (entry.source->owner.instance == owner.instance && entry.source->owner.epoch == owner.epoch)
                return entry.source;
        return {};
    }
    Result<void> status() const
    {
        if (failure_)
            return Result<void>::failure(*failure_);
        for (const auto& entry : entries_)
            if (entry.source->blocked.load())
                return Result<void>::failure(
                    makeError(ErrorCode::SchedulerClosed, "UI value publication observation was discarded"));
        return Result<void>::success();
    }
    /** @brief 调用方成功后才消耗序号；拒绝不改变 pending、历史或任何控件。 */
    Result<void> enqueue(Value value, std::shared_ptr<PendingSource> source, std::string target, std::size_t bytes)
    {
        const auto available = status();
        if (!available.hasValue())
            return available;
        std::set<std::string> targets;
        for (const auto& entry : entries_)
            targets.insert(entry.target);
        targets.insert(target);
        if (entries_.size() >= countLimit_ || bytes > byteLimit_ - bytes_ || targets.size() > targetLimit_)
            return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "pending UI value capacity exceeded"));
        entries_.push_back({std::move(value), std::move(source), std::move(target), bytes});
        bytes_ += bytes;
        return Result<void>::success();
    }
    /** @brief 先取出整批并释放来源，再由后端接纳/呈现，防止 Qt 重入重复结算。 */
    std::vector<Value> takeReady()
    {
        std::map<std::shared_ptr<PendingSource>, Result<std::optional<bool>>> decisions;
        for (const auto& entry : entries_)
            if (!decisions.count(entry.source))
                decisions.emplace(entry.source, entry.source->decision());
        std::vector<Value> ready;
        std::vector<Entry> remaining;
        for (auto& entry : entries_) {
            const auto& decision = decisions.at(entry.source);
            if (!decision.hasValue()) {
                if (!failure_)
                    failure_ = decision.error();
            } else if (decision.value().has_value()) {
                bytes_ -= entry.bytes;
                if (*decision.value())
                    ready.push_back(std::move(entry.value));
                continue;
            }
            remaining.push_back(std::move(entry));
        }
        entries_ = std::move(remaining);
        return ready;
    }
    /** @brief 只在宿主关门时丢弃未确认值；已接纳历史不经过此队列。 */
    void clear()
    {
        entries_.clear();
        bytes_ = 0;
        failure_.reset();
    }

private:
    struct Entry {
        Value value;
        std::shared_ptr<PendingSource> source;
        std::string target;
        std::size_t bytes;
    };
    std::vector<Entry> entries_;
    std::size_t bytes_ = 0;
    std::size_t countLimit_, byteLimit_, targetLimit_;
    std::optional<ErrorInfo> failure_;
};
} // namespace eve::ui::detail

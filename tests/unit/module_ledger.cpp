/** @file module_ledger.cpp
 * @brief 验证模块账目与实际资源释放、跨线程租约及排空门禁的一致性。
 */
#include <atomic>
#include <cordis/module/ledger.hpp>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

using namespace cordis;
using namespace cordis::module;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)

void categories()
{
    GenerationLedger ledger, other;
    CHECK(ledger.snapshot().generation.valid());
    CHECK(ledger.snapshot().generation != other.snapshot().generation);
    CHECK(!ledger.snapshot().drained());
    std::vector<ModuleLease> leases;
    for (int i = 0; i <= static_cast<int>(ResourceKind::ExternalHandle); ++i)
        leases.push_back(ledger.acquire(static_cast<ResourceKind>(i), "resource." + std::to_string(i)).value());
    auto snapshot = ledger.snapshot();
    CHECK(snapshot.records.size() == 10);
    for (std::size_t i = 0; i < snapshot.records.size(); ++i) {
        CHECK(snapshot.records[i].kind == static_cast<ResourceKind>(i));
        CHECK(snapshot.records[i].id != 0);
        if (i)
            CHECK(snapshot.records[i].id > snapshot.records[i - 1].id);
    }
    snapshot.records.clear();
    CHECK(ledger.snapshot().records.size() == 10);
    auto same = ledger;
    same.beginDraining();
    CHECK(!ledger.snapshot().accepting && !ledger.snapshot().drained());
    CHECK(!ledger.acquire(ResourceKind::Call, "late").hasValue());
    leases.clear();
    CHECK(ledger.snapshot().drained());
    CHECK(other.snapshot().accepting);
}
void ownership()
{
    static_assert(!std::is_copy_constructible_v<ModuleLease>);
    static_assert(std::is_nothrow_move_constructible_v<ModuleLease>);
    GenerationLedger ledger;
    auto first = ledger.acquire(ResourceKind::Instance, "first").value();
    auto second = ledger.acquire(ResourceKind::Callback, "second").value();
    const auto secondId = ledger.snapshot().records[1].id;
    first = std::move(second);
    CHECK(first && !second && ledger.snapshot().records.size() == 1);
    CHECK(ledger.snapshot().records[0].id == secondId);
    auto& alias = first;
    first = std::move(alias);
    CHECK(first && ledger.snapshot().records.size() == 1);
    first.reset();
    first.reset();
    CHECK(!first && ledger.snapshot().records.empty());
    // 账目句柄先销毁，租约仍能安全退出；未来装载器必须另行持有模块控制块。
    ModuleLease surviving;
    {
        GenerationLedger temporary;
        surviving = temporary.acquire(ResourceKind::QueuePayload, "queued payload").value();
    }
    CHECK(surviving);
    surviving.reset();
}
void pins()
{
    GenerationLedger ledger;
    auto lease = ledger.acquire(ResourceKind::QObject, "Qt root").value();
    CHECK(ledger.pin("untrackedStaticRegistration").hasValue());
    CHECK(ledger.pin("untrackedStaticRegistration").hasValue());
    CHECK(ledger.pin("unsupportedUnloadPolicy").hasValue());
    ledger.beginDraining();
    lease.reset();
    auto state = ledger.snapshot();
    CHECK(state.drained() && state.pinReasons.size() == 2);
    CHECK(ledger.pin("uncertainCleanup").hasValue());
    state.pinReasons.clear();
    CHECK(ledger.snapshot().pinReasons.size() == 3);
}
void guards()
{
    GenerationLedger ledger;
    CHECK(!ledger.acquire(static_cast<ResourceKind>(-1), "bad").hasValue());
    CHECK(!ledger.acquire(static_cast<ResourceKind>(10), "bad").hasValue());
    CHECK(!ledger.acquire(ResourceKind::Call, "").hasValue());
    CHECK(!ledger.acquire(ResourceKind::Call, std::string(257, 'x')).hasValue());
    CHECK(!ledger.pin("\n").hasValue());
    CHECK(!ledger.pin(std::string("a\0b", 3)).hasValue());
    CHECK(ledger.snapshot().records.empty() && ledger.snapshot().pinReasons.empty());
    std::vector<ModuleLease> leases;
    for (int i = 0; i < 4096; ++i)
        leases.push_back(ledger.acquire(ResourceKind::Callback, "callback").value());
    CHECK(!ledger.acquire(ResourceKind::Callback, "overflow").hasValue());
    const auto oldId = ledger.snapshot().records.front().id;
    leases.front().reset();
    auto next = ledger.acquire(ResourceKind::Call, "reused capacity").value();
    CHECK(ledger.snapshot().records.back().id > oldId);
    for (int i = 0; i < 64; ++i)
        CHECK(ledger.pin("pin." + std::to_string(i)).hasValue());
    CHECK(!ledger.pin("overflow").hasValue());
    CHECK(ledger.pin("pin.0").hasValue());
}
void threads()
{
    GenerationLedger ledger;
    std::vector<ModuleLease> references;
    for (int i = 0; i < 8; ++i)
        references.push_back(ledger.acquire(ResourceKind::Worker, "native worker").value());
    std::atomic<int> ready{0};
    std::atomic<bool> finish{false};
    std::vector<std::thread> workers;
    for (auto& reference : references) {
        workers.emplace_back([held = std::move(reference), &ready, &finish]() mutable {
            ready.fetch_add(1, std::memory_order_release);
            while (!finish.load(std::memory_order_acquire))
                std::this_thread::yield();
            held.reset();
        });
    }
    while (ready.load(std::memory_order_acquire) != 8)
        std::this_thread::yield();
    ledger.beginDraining();
    const auto before = ledger.snapshot();
    finish.store(true, std::memory_order_release);
    for (auto& worker : workers)
        worker.join();
    CHECK(before.records.size() == 8 && !before.drained());
    CHECK(ledger.snapshot().drained());
    CHECK(!ledger.acquire(ResourceKind::Worker, "after join").hasValue());
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "categories")
            categories();
        else if (name == "ownership")
            ownership();
        else if (name == "pins")
            pins();
        else if (name == "guards")
            guards();
        else if (name == "threads")
            threads();
        else
            throw std::runtime_error("unknown case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

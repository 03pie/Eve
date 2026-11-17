/** @file state_budget.cpp
 * @brief 原值和转换值共用预算，真实 EpochState 的导入/失败控制持有期。
 */
#include "../../src/core/runtime_state_p.hpp"
#include "../../src/core/state_p.hpp"
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
using namespace cordis;
using namespace cordis::detail;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
namespace {
StateSnapshot value(std::string json = "7")
{
    return {{"counter", 1}, std::move(json)};
}
std::shared_ptr<const StateContract> contract(std::string converted = "1234")
{
    return std::make_shared<const StateContract>(
        StateContract{{"counter", 2}, [converted = std::move(converted)](const StateSnapshot&) {
                          return Result<std::string>::success(converted);
                      }});
}
StateParticipant participant(int& restored, bool reject = false)
{
    return {[] { return Result<std::string>::success("7"); },
            [&, reject](std::string_view json) {
                CHECK(json == "1234");
                ++restored;
                return reject ? Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "restore rejected"))
                              : Result<void>::success();
            }};
}
void limits()
{
    auto budget = StateBudget::create();
    std::string json(maxStateJsonBytes, 'a');
    json.front() = json.back() = '"';
    std::vector<std::shared_ptr<const OwnedStateSnapshot>> originals;
    for (unsigned i = 0; i != 16; ++i)
        originals.push_back(ownStateSnapshot(value(json), budget).value());
    CHECK(budget->used() == maxReplacementStateBytes);
    CHECK(!ownStateSnapshot(value(), budget).hasValue());
    CHECK(!budget->hold(std::numeric_limits<std::size_t>::max()).hasValue());
    originals.pop_back();
    CHECK(budget->used() == 15 * maxStateJsonBytes);
    int restored = 0;
    {
        EpochState state(contract(json), originals.front());
        CHECK(state.prepare().hasValue() && budget->used() == maxReplacementStateBytes);
        CHECK(!state.imported());
        CHECK(state
                  .bind({[] { return Result<std::string>::success("0"); },
                         [&](std::string_view input) {
                             CHECK(input == json);
                             ++restored;
                             return Result<void>::success();
                         }})
                  .hasValue());
        CHECK(state.imported() && state.ready().hasValue() && restored == 1);
        CHECK(budget->used() == 15 * maxStateJsonBytes);
    }
    CHECK(originals.front()->value.schema.version == 1 && originals.front()->value.json == json);
    originals.clear();
    CHECK(budget->used() == 0);
}
void shared()
{
    auto budget = StateBudget::create(1);
    auto original = ownStateSnapshot(value(), budget).value();
    auto copy = original;
    CHECK(copy.get() == original.get() && budget->used() == 1);
    original.reset();
    CHECK(budget->used() == 1 && copy->value.json == "7");
    copy.reset();
    CHECK(budget->used() == 0);
    auto lease = budget->hold(1).value();
    auto moved = std::move(lease);
    lease.reset();
    CHECK(budget->used() == 1);
    moved.reset();
    moved.reset();
    CHECK(budget->used() == 0);
}
void conversion(bool exhausted, bool reject)
{
    auto budget = StateBudget::create(exhausted ? 4 : 5);
    auto original = ownStateSnapshot(value(), budget).value();
    int restored = 0;
    {
        EpochState state(contract(), original);
        auto prepared = state.prepare();
        CHECK(prepared.hasValue() == !exhausted);
        if (exhausted) {
            CHECK(prepared.error().code == ErrorCode::InvalidArgument && budget->used() == 1);
            CHECK(!state.bind(participant(restored)).hasValue() && restored == 0 && !state.imported());
        } else {
            CHECK(budget->used() == 5 && !state.ready().hasValue());
            auto bound = state.bind(participant(restored, reject));
            CHECK(bound.hasValue() == !reject && restored == 1 && state.imported() == !reject);
            CHECK(state.ready().hasValue() == !reject && budget->used() == (reject ? 5U : 1U));
            if (reject)
                CHECK(bound.error().code == ErrorCode::ConfigInvalid);
        }
        CHECK(original->value.json == "7" && original->value.schema.version == 1);
    }
    CHECK(budget->used() == 1);
    original.reset();
    CHECK(budget->used() == 0);
}
void payloads()
{
    auto budget = StateBudget::create();
    CHECK(!ownStateSnapshot(value(std::string(maxStateJsonBytes + 1, '0')), budget).hasValue());
    CHECK(!ownStateSnapshot(value(""), budget).hasValue() && budget->used() == 0);
    auto original = ownStateSnapshot(value(), budget).value();
    EpochState state(contract(std::string(maxStateJsonBytes + 1, '0')), original);
    CHECK(!state.prepare().hasValue() && budget->used() == 1 && !state.imported());
}
void threads()
{
    auto budget = StateBudget::create(7);
    auto lease = budget->hold(7).value();
    std::thread worker([held = std::move(lease)]() mutable { held.reset(); });
    worker.join();
    CHECK(budget->used() == 0);
    std::vector<std::thread> workers;
    for (unsigned i = 0; i != 4; ++i)
        workers.emplace_back([&] {
            for (unsigned j = 0; j != 100; ++j) {
                auto held = budget->hold(1);
                CHECK(held.hasValue());
            }
        });
    for (auto& item : workers)
        item.join();
    CHECK(budget->used() == 0);
}
void factory()
{
    int validated = 0;
    auto runtime = Runtime::stateful([](Context) { return completedTask(); },
                                     {{"counter", 2}, [&](const StateSnapshot& input) {
                                          CHECK(input.schema.version == 1 && input.json == "7");
                                          ++validated;
                                          return Result<std::string>::success("1234");
                                      }});
    auto original = value();
    auto checked = RuntimeStateAccess::validate(runtime, original).value();
    CHECK(validated == 1 && checked.schema.version == 2 && checked.json == "1234");
    CHECK(original.schema.version == 1 && original.json == "7");
    Runtime stateless([](Context) { return completedTask(); });
    CHECK(!RuntimeStateAccess::validate(stateless, original).hasValue());
    auto throwing = Runtime::stateful([](Context) { return completedTask(); },
                                      {{"counter", 2}, [](const StateSnapshot&) -> Result<std::string> {
                                           throw std::runtime_error("validation failed");
                                       }});
    CHECK(RuntimeStateAccess::validate(throwing, original).error().code == ErrorCode::CallbackFailed);
}
std::optional<Runtime> transientDefinition;
bool definitionReleased = false;
struct DefinitionCapture {
    ~DefinitionCapture() { definitionReleased = true; }
};
void factoryLifetime()
{
    auto captured = std::make_shared<DefinitionCapture>();
    transientDefinition.emplace(
        Runtime::stateful([](Context) { return completedTask(); },
                          {{"counter", 1}, [captured = std::move(captured)](const StateSnapshot&) {
                               transientDefinition.reset();
                               CHECK(!definitionReleased);
                               return Result<std::string>::success("7");
                           }}));
    auto checked = RuntimeStateAccess::validate(*transientDefinition, value());
    CHECK(checked.hasValue() && !transientDefinition && definitionReleased);
}
} // namespace
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "limits")
            limits();
        else if (name == "shared")
            shared();
        else if (name == "conversion")
            conversion(false, false);
        else if (name == "exhausted")
            conversion(true, false);
        else if (name == "restore_failure")
            conversion(false, true);
        else if (name == "payloads")
            payloads();
        else if (name == "threads")
            threads();
        else if (name == "factory")
            factory();
        else if (name == "factory_lifetime")
            factoryLifetime();
        else
            CHECK(false);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

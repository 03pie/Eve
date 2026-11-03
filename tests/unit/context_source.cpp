/** @file context_source.cpp
 * @brief 来源从真实 Context 派生，跨激活排队、子实例及作用域退役保持权限边界。
 */
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
using namespace cordis;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
int main()
{
    try {
        auto scheduler = std::make_shared<testing::FakeScheduler>();
        Kernel kernel(scheduler);
        auto run = [&] { scheduler->runUntilIdle(30000).value(); };
        std::optional<Context> ordinary, candidate, childContext;
        std::optional<ContextSource> entered, childEntered;
        auto old = kernel
                       .registerPlugin("target",
                                       [&](Context context) {
                                           ordinary = context;
                                           return completedTask();
                                       })
                       .value();
        auto previous = kernel.mount(old).value();
        run();
        const auto original = ordinary->source().value();
        CHECK(original.instance == previous.snapshot().value().instance && original.runtime == old.id() &&
              original.epoch == 1 && !original.replacement);
        std::optional<ErrorCode> wrong;
        std::thread worker([&] { wrong = ordinary->source().error().code; });
        worker.join();
        CHECK(wrong == ErrorCode::WrongThread);
        Runtime child([&](Context context) {
            childContext = context;
            childEntered = context.source().value();
            return completedTask();
        });
        Runtime next([&](Context context) {
            candidate = context;
            entered = context.source().value();
            context.inject(child).value();
            return completedTask();
        });
        auto scope = kernel.reserveReplacement({old.id()}).value();
        CHECK(!scope.preparation().hasValue());
        const auto operation = scope.snapshot().value().operation;
        CHECK(!ordinary->source().value().replacement);
        scope.prepareDefinitions({{old.id(), next}}).value();
        scope.freeze().value();
        CHECK(!ordinary->source().hasValue());
        auto stopped = scope.stop();
        run();
        CHECK(stopped.tryResult()->value().quiescent);
        auto instance = scope.stage(next).value();
        run();
        CHECK(instance.ready().tryResult()->hasValue() && entered && childEntered);
        CHECK(entered->instance == instance.snapshot().value().instance && entered->runtime == next.id() &&
              entered->replacement == operation && childEntered->runtime == child.id() &&
              childEntered->replacement == operation && entered->instance != childEntered->instance);
        const auto discardedContext = *candidate;
        auto discarded = scope.discard();
        run();
        CHECK(discarded.tryResult()->value().quiescent && !discardedContext.source().hasValue() &&
              !childContext->source().hasValue());
        auto active = scope.stage(next).value();
        run();
        CHECK(active.ready().tryResult()->hasValue() && candidate->source().value().replacement == operation);
        CHECK(!discardedContext.source().hasValue());
        scope.prepareCommit().value();
        CHECK(scope.preparation().value().operation == operation);
        CHECK(!candidate->source().hasValue());
        scope.commitPrepared().value();
        CHECK(!candidate->source().hasValue());
        scope.release().value();
        CHECK(!candidate->source().value().replacement && !childContext->source().value().replacement);
        CHECK(!ordinary->source().hasValue());
        auto closed = kernel.close();
        run();
        CHECK(closed.tryResult()->value().quiescent && !candidate->source().hasValue());
        scheduler->close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

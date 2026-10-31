/** @file native_replacement_cases.hpp
 * @brief 实际 C DLL 的托管/手动消费者归属与 Kernel 闭包。
 */
#pragma once
#include "native_managed_cases.hpp"
namespace native_replacement_test {
using namespace native_managed_test;
bool run(const QString& name)
{
    if (name != "replacement_graph" && name != "replacement_external")
        return false;
    Managed s;
    auto provider = s.mountProvider(true, 11, 8);
    if (name == "replacement_graph") {
        auto consumer = s.kernel->mount(s.consumer).value();
        s.pump();
        auto graph = s.kernel->services().dependencyGraph().value();
        CHECK(graph.bindings.size() == 1 && graph.bindings[0].managedProvider);
        CHECK(graph.bindings[0].provider == provider.snapshot().value().instance);
        CHECK(graph.bindings[0].consumers.size() == 1 && graph.bindings[0].consumers[0].managed);
        auto closure = s.kernel->replacementClosure({s.provider.id()}).value();
        CHECK(closure.members.size() == 2 && closure.stopOrder.front() == consumer.snapshot().value().instance);
        CordisRequestId request = 0;
        CHECK(s.request(request, 1) == CORDIS_PENDING);
        s.pump();
        CHECK(s.stat(1, 3) == CORDIS_OK);
        const auto after = s.kernel->services().dependencyGraph().value();
        CHECK(after.bindings[0].consumers.size() == 1 && after.bindings[0].consumers[0].managed);
        CHECK(s.kernel->replacementClosure({s.provider.id()}).value().members.size() == 2);
    } else {
        auto outside = s.active(99);
        CordisRequestId request = 0;
        CHECK(s.request(request, 1) == CORDIS_PENDING);
        s.pump();
        auto graph = s.kernel->services().dependencyGraph().value();
        CHECK(graph.bindings[0].consumers.size() == 1 && !graph.bindings[0].consumers[0].managed);
        auto rejected = s.kernel->replacementClosure({s.provider.id()});
        CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::ConfigConflict &&
              rejected.error().instance == InstanceId{99});
        CHECK(provider.snapshot().value().state == InstanceState::Active);
        auto stopped = outside.close();
        s.pump();
        CHECK(stopped.tryResult()->value().quiescent);
        CHECK(s.kernel->replacementClosure({s.provider.id()}).value().members.size() == 1);
    }
    s.finishManaged();
    return true;
}
} // namespace native_replacement_test

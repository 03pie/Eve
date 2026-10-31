/** @file native_scope_cases.hpp
 * @brief 真实 C DLL 的冻结、受控停止和迟到外部消费者门禁。
 */
#pragma once
#include "native_managed_cases.hpp"
namespace native_scope_test {
using namespace native_managed_test;
bool run(const QString& name)
{
    if (name != "scope_freeze" && name != "scope_stop" && name != "scope_external")
        return false;
    Managed s;
    auto provider = s.mountProvider(true, 11, 8);
    if (name == "scope_external") {
        auto scope = s.kernel->reserveReplacement({s.provider.id()}).value();
        auto outside = s.active(99);
        CordisRequestId request = 0;
        CHECK(s.request(request) == CORDIS_E_BUSY && request == 0 &&
              s.kernel->services().dependencyGraph().value().bindings[0].consumers.empty());
        scope.release().value();
        CHECK(s.request(request) == CORDIS_PENDING);
        s.pump();
        CHECK(s.stat(1, 3) == CORDIS_OK);
        CHECK(!s.kernel->reserveReplacement({s.provider.id()}).hasValue());
        auto closed = outside.close();
        s.pump();
        CHECK(closed.tryResult()->value().quiescent);
    } else {
        s.kernel->mount(s.consumer).value();
        s.pump();
        auto scope = s.kernel->reserveReplacement({s.provider.id()}).value();
        if (name == "scope_freeze") {
            CordisRequestId request = 0;
            CHECK(s.request(request) == CORDIS_PENDING);
            s.pump();
            scope.freeze().value();
            CHECK(s.request(request) == CORDIS_E_CLOSED && provider.snapshot().value().state == InstanceState::Active);
            scope.release().value();
            CHECK(s.request(request) == CORDIS_PENDING);
            s.pump();
            CHECK(s.stat(1, 3) == CORDIS_OK);
        } else {
            auto stopped = scope.stop();
            s.pump();
            unsigned turns = 0;
            while (s.finishAll()) {
                CHECK(++turns < 8);
                s.pump();
            }
            CHECK(stopped.isReady() && stopped.tryResult()->value().quiescent &&
                  s.kernel->services().snapshot().value().empty());
            CHECK(scope.snapshot().value().state == ReplacementScopeState::Stopped);
            scope.release().value();
        }
    }
    s.finishManaged();
    return true;
}
} // namespace native_scope_test

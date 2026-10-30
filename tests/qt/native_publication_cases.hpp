/** @file native_publication_cases.hpp
 * @brief 同一真实 C DLL 的实例分别使用发布组视图，不能经共享模块泄漏访问权限。
 */
#pragma once
#include "native_managed_cases.hpp"
namespace native_publication_test {
using namespace native_managed_test;
bool run(const QString& name)
{
    if (name != "publication_commit" && name != "publication_abort")
        return false;
    Managed s;
    auto group = s.kernel->holdPublication().value();
    auto provider = s.kernel->mountHeld(group, s.provider, "\"start-pending\"").value();
    s.pump();
    s.providerEffect = s.add(11, 0, true);
    s.provide(s.providerEffect, 7);
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(provider.ready().tryResult()->hasValue());
    CHECK(s.kernel->services().snapshot().value()[0].state == BindingState::Held);
    auto outside = s.active(99);
    auto inside = s.kernel->mountHeld(group, s.consumer).value();
    s.pump();
    CHECK(outside.snapshot().value().state == NativeInstanceState::Started && inside.ready().tryResult()->hasValue());
    CHECK(group.snapshot().value().members == 2);
    CordisRequestId request = 0;
    CHECK(s.request(request, 1) == CORDIS_E_NOT_FOUND && request == 0);
    CHECK(s.request(request, 2) == CORDIS_PENDING);
    s.pump();
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(s.result(2) == "{\"integer\":9007199254740993}");
    CHECK(s.request(request, 1) == CORDIS_E_NOT_FOUND && request == 0);
    if (name == "publication_commit") {
        group.commit().value();
        CHECK(s.request(request, 1) == CORDIS_PENDING);
        s.pump();
        CHECK(s.finishAll() == 1);
        s.pump();
        CHECK(s.result(1) == "{\"integer\":9007199254740993}");
    } else {
        group.abort().value();
        CHECK(!group.commit().hasValue());
        CHECK(s.request(request, 1) == CORDIS_E_NOT_FOUND);
    }
    s.finishManaged();
    return true;
}
} // namespace native_publication_test

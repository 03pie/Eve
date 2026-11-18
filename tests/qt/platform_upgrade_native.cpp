/** @file platform_upgrade_native.cpp
 * @brief 真实 Native A/B 替换、状态继续及独立 Windows 映像/文件证据。
 */
#include "platform_upgrade_p.hpp"
#include <QDir>
#include <QPointer>
#include <QWidget>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace platform_upgrade_test {
namespace {
struct Image {
    QString file;
    std::uintptr_t begin;
    std::size_t bytes;
    explicit Image(const ModuleGenerationSnapshot& row)
        : file(QDir::toNativeSeparators(QString::fromUtf8(row.libraryPath.c_str())))
    {
        const auto handle = GetModuleHandleW(reinterpret_cast<LPCWSTR>(file.utf16()));
        PLATFORM_CHECK(handle);
        begin = reinterpret_cast<std::uintptr_t>(handle);
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(handle);
        PLATFORM_CHECK(dos->e_magic == IMAGE_DOS_SIGNATURE);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(begin + static_cast<std::uintptr_t>(dos->e_lfanew));
        PLATFORM_CHECK(nt->Signature == IMAGE_NT_SIGNATURE);
        bytes = nt->OptionalHeader.SizeOfImage;
        PLATFORM_CHECK(bytes > 0);
    }
    void absent() const
    {
        PLATFORM_CHECK(!GetModuleHandleW(reinterpret_cast<LPCWSTR>(file.utf16())));
        for (auto address = begin; address < begin + bytes;) {
            MEMORY_BASIC_INFORMATION info{};
            PLATFORM_CHECK(VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == sizeof(info));
            PLATFORM_CHECK(info.Type != MEM_IMAGE || reinterpret_cast<std::uintptr_t>(info.AllocationBase) != begin);
            const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            PLATFORM_CHECK(end > address);
            address = end;
        }
        const auto fileHandle =
            CreateFileW(reinterpret_cast<LPCWSTR>(file.utf16()), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        PLATFORM_CHECK(fileHandle != INVALID_HANDLE_VALUE);
        PLATFORM_CHECK(CloseHandle(fileHandle));
    }
};
void reclaimed(Scene& scene, const UpgradePreview& value, const WorkspaceUpgradeReport& report, const Image& old)
{
    PLATFORM_CHECK(report.nativeClose && report.nativeClose->hasValue() && report.nativeClose->value().quiescent &&
                   report.drain && report.drain->hasValue() && report.unload && report.unload->hasValue());
    const auto retired = module(scene, value.previous);
    PLATFORM_CHECK(retired.state == ModuleState::Unloaded && !retired.nativeMapped && !retired.nativePinned &&
                   retired.resources.records.empty() && !retired.unloadFailure &&
                   !take(scene.state->manager->runtimeOrigin(value.previousRuntime)));
    old.absent();
}
void replace(const QString& sourceBin, const std::string& alias, bool roundtrip)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto oldCounter = running(scene, "example.counter");
    const auto oldReporter = running(scene, "example.reporter");
    const auto notes = running(scene, "example.notebook");
    const QPointer<QWidget> widget = scene.page(notes.instance);
    scene.edit(notes.instance, QString::fromUtf8("Native 升级时保留草稿"));
    PLATFORM_CHECK(counterValue(scene) == "1");
    const auto value = preview(scene, sourceBin, alias);
    const Image image(module(scene, value.previous));
    const auto report = perform(scene, value);
    applied(scene, value, report);
    reclaimed(scene, value, report, image);
    const auto nowCounter = running(scene, "example.counter");
    const auto nowReporter = running(scene, "example.reporter");
    const auto nowNotes = running(scene, "example.notebook");
    PLATFORM_CHECK(nowReporter.instance != oldReporter.instance && nowNotes.instance == notes.instance &&
                   nowNotes.epoch == notes.epoch && widget && scene.page(notes.instance) == widget);
    PLATFORM_CHECK((alias == "counter") == (nowCounter.instance != oldCounter.instance));
    const auto expected = alias == "counter" ? "2" : "3";
    PLATFORM_CHECK(counterValue(scene) == expected);
    reporterValue(scene, expected);
    if (roundtrip) {
        const auto back = preview(scene, sourceBin, alias, "a");
        const Image secondImage(module(scene, back.previous));
        const auto second = perform(scene, back);
        applied(scene, back, second);
        reclaimed(scene, back, second, secondImage);
        PLATFORM_CHECK(second.candidate != value.previous && counterValue(scene) == "3");
        reporterValue(scene, "3");
        PLATFORM_CHECK(take(scene.state->upgradeSnapshot()).history.size() == 2 &&
                       scene.page(notes.instance) == widget);
    }
    finish(scene);
}
void retainedFactory(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto value = preview(scene, sourceBin, "counter");
    const Image image(module(scene, value.previous));
    std::optional<Runtime> borrowed(*take(scene.state->counter())->runtime);
    const auto report = perform(scene, value);
    applied(scene, value, report);
    PLATFORM_CHECK(report.replacement->committed && !report.retained && report.drain && !report.drain->hasValue() &&
                   !report.unload && module(scene, value.previous).nativeMapped &&
                   take(scene.state->manager->runtimeOrigin(value.previousRuntime)));
    PLATFORM_CHECK(counterValue(scene) == "2");
    borrowed.reset();
    barrier(scene.scheduler);
    take(scene.state->manager->drain(value.previous));
    take(scene.state->manager->tryUnload(value.previous));
    image.absent();
    PLATFORM_CHECK(module(scene, value.previous).state == ModuleState::Unloaded &&
                   take(scene.state->upgradeSnapshot()).accepting);
    finish(scene);
}
} // namespace
bool nativeCase(const QString& name, const QString& sourceBin)
{
    if (name == "counter")
        replace(sourceBin, "counter", false);
    else if (name == "reporter")
        replace(sourceBin, "reporter", false);
    else if (name == "native_roundtrip")
        replace(sourceBin, "counter", true);
    else if (name == "retained_factory")
        retainedFactory(sourceBin);
    else
        return false;
    return true;
}
} // namespace platform_upgrade_test

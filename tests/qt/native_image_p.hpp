/** @file native_image_p.hpp
 * @brief 真实替换入口共用 Windows 映像离开及文件独占判据。
 */
#pragma once
#include "platform_workspace_p.hpp"
#include <QDir>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace platform_upgrade_test {
struct Image {
    QString file;
    std::uintptr_t begin;
    std::size_t bytes;
    explicit Image(const eve::module::ModuleGenerationSnapshot& row)
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
} // namespace platform_upgrade_test

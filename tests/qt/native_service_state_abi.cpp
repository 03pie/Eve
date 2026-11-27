/** @file native_service_state_abi.cpp
 * @brief 固定真实业务 DLL 的发现与纯状态 ABI；不创建假 HostTable 或测试专用导出。
 */
#include "../../src/module/artifact_p.hpp"
#include <QString>
#include <algorithm>
#include <array>
#include <eve_abi/module_query.h>
#include <eve_abi/plugin.h>
#include <eve_abi/state.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace eve;
using namespace eve::module;
namespace native = eve::module::detail;
#define ABI_CHECK(value)                                                                                               \
    do {                                                                                                               \
        if (!(value))                                                                                                  \
            throw std::runtime_error(std::string(#value) +                                                             \
                                     " at native_service_state_abi.cpp:" + std::to_string(__LINE__));                  \
    } while (false)

template <class T> T take(Result<T> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().phase + ": " + result.error().message);
    return std::move(result).value();
}
struct Artifact {
    const char* manifest;
    const char* component;
    const char* version;
    bool explicitUnload;
    bool state;
};
constexpr std::array<Artifact, 6> artifacts{{
    {"native-upgrade/counter-a/module.json", "counter", "1.1.0", true, true},
    {"native-upgrade/counter-b/module.json", "counter", "1.2.0", true, true},
    {"native-upgrade/reporter-a/module.json", "reporter", "1.1.0", true, false},
    {"native-upgrade/reporter-b/module.json", "reporter", "1.2.0", true, false},
    {"native-services/counter/module.json", "counter", "1.0.0", false, false},
    {"native-services/reporter/module.json", "reporter", "1.0.0", false, false},
}};
struct ImageHandle {
    ImageHandle() = default;
    ImageHandle(const ImageHandle&) = delete;
    ImageHandle& operator=(const ImageHandle&) = delete;
    HMODULE value = nullptr;
    ~ImageHandle()
    {
        if (value)
            FreeLibrary(value);
    }
};
/** @brief 复用生产来源固定，直到纯 ABI 调用完成才释放映像及文件/目录保护。 */
class Image {
public:
    Image(const QString& root, const Artifact& expected)
        : source_(take(native::verifyModuleSource(root.toUtf8().toStdString(), expected.manifest, {})))
    {
        const auto& manifest = source_->artifact.manifest();
        ABI_CHECK(manifest.moduleId == std::string("org.eve.example.") + expected.component);
        ABI_CHECK(manifest.moduleVersion == expected.version && manifest.loader == ModuleLoader::CAbi);
        ABI_CHECK(manifest.plugins == std::vector<std::string>{std::string("example.") + expected.component});
        ABI_CHECK(manifest.unloadPolicy ==
                  (expected.explicitUnload ? ModuleUnloadPolicy::Explicit : ModuleUnloadPolicy::Resident));
        const auto path = native::nativeModulePath(QString::fromUtf8(source_->artifact.libraryPath().c_str()));
        image_.value =
            LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        ABI_CHECK(image_.value);
        const native::OpenedPath mapped(native::mappedModulePath(image_.value), false);
        ABI_CHECK(mapped.valid() && source_->library->sameFile(mapped));
    }
    template <class T> T symbol(const char* name) const
    {
        const auto address = GetProcAddress(image_.value, name);
        T function = nullptr;
        static_assert(sizeof(function) == sizeof(address), "Windows ABI entry pointer");
        std::memcpy(&function, &address, sizeof(function));
        return function;
    }

private:
    std::unique_ptr<native::VerifiedSource> source_;
    ImageHandle image_;
};
constexpr std::uint8_t sentinel = 0xa5;
template <class T> std::array<std::uint8_t, sizeof(T)> objectBytes(const T& value)
{
    std::array<std::uint8_t, sizeof(T)> result{};
    std::memcpy(result.data(), &value, sizeof(value));
    return result;
}
/** @brief 结构始终真实可写；advertised capacity 以外的对象字节同样作为 canary 检查。 */
template <class T> struct Guarded {
    std::array<std::uint8_t, 16> before;
    T value{};
    std::array<std::uint8_t, 16> after;
    Guarded()
    {
        before.fill(sentinel);
        after.fill(sentinel);
    }
    void check() const
    {
        ABI_CHECK(std::all_of(before.begin(), before.end(), [](auto byte) { return byte == sentinel; }));
        ABI_CHECK(std::all_of(after.begin(), after.end(), [](auto byte) { return byte == sentinel; }));
    }
    void checkCapacity(std::size_t capacity, const std::array<std::uint8_t, sizeof(T)>& original) const
    {
        check();
        if (capacity < sizeof(T)) {
            const auto current = objectBytes(value);
            ABI_CHECK(std::equal(current.begin() + capacity, current.end(), original.begin() + capacity));
        }
    }
};
EveBytesV1 bytes(const std::string& value)
{
    return {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
}
void span(EveQueryBytesV1 value, const std::string& expected)
{
    ABI_CHECK(value.size == expected.size() && value.data);
    ABI_CHECK(std::memcmp(value.data, expected.data(), expected.size()) == 0);
}
EveModuleQueryV1 request(std::uint32_t minor = 0)
{
    return {{sizeof(EveModuleQueryV1), 1, minor, 0}, 1, 0, 0};
}
void discovery(const QString& root)
{
    for (const auto& artifact : artifacts) {
        Image image(root, artifact);
        const auto discover = image.symbol<EveModuleQueryFnV1>("eve_module_query_v1");
        const auto business = image.symbol<EveQueryFnV1>("eve_query_v1");
        ABI_CHECK(discover && business);
        ABI_CHECK(static_cast<bool>(image.symbol<EveQueryStateFnV1>("eve_query_state_v1")) == artifact.state);
        for (const std::uint32_t minor : {0u, 1u}) {
            const auto input = request(minor);
            Guarded<EveModuleDescriptorV1> output;
            output.value.header.struct_size = sizeof(output.value);
            const auto status = discover(&input, &output.value);
            output.check();
            if (minor == 1 && !artifact.explicitUnload) {
                ABI_CHECK(status == EVE_QUERY_UNSUPPORTED);
                continue;
            }
            ABI_CHECK(status == EVE_QUERY_OK);
            const auto& value = output.value;
            ABI_CHECK(value.header.struct_size == sizeof(value) && value.header.query_major == 1 &&
                      value.header.query_minor == minor && value.header.reserved == 0);
            ABI_CHECK(value.runtime_abi_major == 1 && value.runtime_abi_minor == 0 && value.reserved == 0);
            ABI_CHECK(value.flags == (minor == 0 ? EVE_MODULE_RESIDENT : EVE_MODULE_EXPLICIT_UNLOAD));
            span(value.module_id, std::string("org.eve.example.") + artifact.component);
            span(value.module_version, artifact.version);
            ABI_CHECK(value.plugin_count == 1 && value.plugins);
            span(value.plugins[0], std::string("example.") + artifact.component);
        }
        for (const auto capacity : {0u, 4u, static_cast<unsigned>(sizeof(EveQueryHeaderV1)),
                                    static_cast<unsigned>(sizeof(EveModuleDescriptorV1) - 1)}) {
            const auto input = request();
            Guarded<EveModuleDescriptorV1> output;
            output.value.header.struct_size = capacity;
            const auto original = objectBytes(output.value);
            ABI_CHECK(discover(&input, &output.value) == EVE_QUERY_INVALID_ARGUMENT);
            output.checkCapacity(capacity, original);
        }
        Guarded<EveModuleDescriptorV1> output;
        output.value.header.struct_size = sizeof(output.value) + 8;
        auto input = request();
        ABI_CHECK(discover(&input, &output.value) == EVE_QUERY_OK);
        output.check();
        ABI_CHECK(output.value.header.struct_size == sizeof(output.value));
        for (unsigned invalid = 0; invalid < 7; ++invalid) {
            auto unsupported = request();
            if (invalid == 0)
                unsupported.header.query_major = 2;
            else if (invalid == 1)
                unsupported.header.query_minor = 2;
            else if (invalid == 2)
                unsupported.runtime_abi_major = 2;
            else if (invalid == 3)
                unsupported.runtime_abi_minor = 1;
            else if (invalid == 4)
                unsupported.header.reserved = 1;
            else if (invalid == 5)
                unsupported.flags = 1;
            else
                unsupported.header.struct_size = sizeof(unsupported) - 1;
            ABI_CHECK(discover(&unsupported, &output.value) == EVE_QUERY_UNSUPPORTED);
            output.check();
        }
        // 成功业务协商由真实 NativeModule 场景验证；此处不伪造满足能力位的 HostTable。
        Guarded<EvePluginApiV1> rejected;
        rejected.value.header.struct_size = sizeof(rejected.value);
        ABI_CHECK(business(EVE_ABI_MAJOR, nullptr, &rejected.value) == EVE_E_UNSUPPORTED);
        rejected.check();
    }
}
EveStateApiV1 stateApi(const Image& image)
{
    const auto query = image.symbol<EveQueryStateFnV1>("eve_query_state_v1");
    ABI_CHECK(query);
    Guarded<EveStateApiV1> output;
    output.value.header.struct_size = sizeof(output.value);
    ABI_CHECK(query(1, &output.value) == EVE_OK);
    output.check();
    ABI_CHECK(output.value.header.struct_size == sizeof(output.value) && output.value.header.abi_major == 1 &&
              output.value.header.abi_minor == 0 && output.value.header.reserved == 0);
    ABI_CHECK(output.value.describe && output.value.validate && output.value.capture && output.value.restore);
    return output.value;
}
EveStateSchemaV1 schema(const EveStateApiV1& api)
{
    Guarded<EveStateSchemaV1> output;
    output.value.struct_size = sizeof(output.value);
    const std::string plugin = "example.counter", expected = "org.eve.example.counter";
    ABI_CHECK(api.describe(bytes(plugin), &output.value) == EVE_OK);
    output.check();
    ABI_CHECK(output.value.struct_size == sizeof(output.value) && output.value.version == 1 &&
              output.value.id_size == expected.size() && output.value.reserved == 0);
    ABI_CHECK(std::memcmp(output.value.id, expected.data(), expected.size()) == 0);
    return output.value;
}
void schemas(const QString& root)
{
    for (const auto& artifact : artifacts) {
        if (!artifact.state)
            continue;
        Image image(root, artifact);
        const auto query = image.symbol<EveQueryStateFnV1>("eve_query_state_v1");
        const auto api = stateApi(image);
        (void)schema(api);
        for (const auto capacity : {0u, 4u, static_cast<unsigned>(sizeof(EveStateApiV1) - 1)}) {
            Guarded<EveStateApiV1> output;
            output.value.header.struct_size = capacity;
            const auto original = objectBytes(output.value);
            ABI_CHECK(query(1, &output.value) == EVE_E_INVALID_ARGUMENT);
            output.checkCapacity(capacity, original);
        }
        Guarded<EveStateApiV1> output;
        output.value.header.struct_size = sizeof(output.value) + 8;
        ABI_CHECK(query(1, &output.value) == EVE_OK);
        output.check();
        ABI_CHECK(output.value.header.struct_size == sizeof(output.value));
        ABI_CHECK(query(2, &output.value) == EVE_E_UNSUPPORTED);
        output.check();
        ABI_CHECK(query(1, nullptr) == EVE_E_INVALID_ARGUMENT);
        for (const auto capacity : {0u, 4u, static_cast<unsigned>(sizeof(EveStateSchemaV1) - 1)}) {
            Guarded<EveStateSchemaV1> described;
            described.value.struct_size = capacity;
            const auto original = objectBytes(described.value);
            const std::string plugin = "example.counter";
            ABI_CHECK(api.describe(bytes(plugin), &described.value) == EVE_E_INVALID_ARGUMENT);
            described.checkCapacity(capacity, original);
        }
        Guarded<EveStateSchemaV1> unknown;
        unknown.value.struct_size = sizeof(unknown.value);
        unknown.value.version = 99;
        unknown.value.id_size = 8;
        std::fill(std::begin(unknown.value.id), std::end(unknown.value.id), sentinel);
        const std::string absent = "example.missing";
        ABI_CHECK(api.describe(bytes(absent), &unknown.value) == EVE_E_NOT_FOUND);
        unknown.check();
        ABI_CHECK(unknown.value.version == 0 && unknown.value.id_size == 0);
        ABI_CHECK(
            std::all_of(std::begin(unknown.value.id), std::end(unknown.value.id), [](auto byte) { return !byte; }));
        ABI_CHECK(api.describe({nullptr, 1}, &unknown.value) == EVE_E_INVALID_ARGUMENT);
        unknown.check();
        Guarded<EveStateSchemaV1> reserved;
        reserved.value.struct_size = sizeof(reserved.value);
        reserved.value.reserved = 1;
        const auto original = objectBytes(reserved.value);
        const std::string plugin = "example.counter";
        ABI_CHECK(api.describe(bytes(plugin), &reserved.value) == EVE_E_INVALID_ARGUMENT);
        reserved.checkCapacity(sizeof(reserved.value), original);
        ABI_CHECK(reserved.value.struct_size == sizeof(reserved.value) && reserved.value.reserved == 1);
    }
}
/** @brief 每个声明的 data 字节都实际可写，canary 紧贴可写区域；短结构不强求不可写的 size 清零。 */
class OutputBuffer {
public:
    explicit OutputBuffer(std::size_t capacity) : storage_(capacity + 32, sentinel)
    {
        output.value = {sizeof(EveStateBufferV1), 0, storage_.data() + 16, capacity,
                        std::numeric_limits<std::uint64_t>::max()};
    }
    EveStateBufferV1* get() { return &output.value; }
    void check(EveStatus status, const EveStateBufferV1& original) const
    {
        output.check();
        ABI_CHECK(std::all_of(storage_.begin(), storage_.begin() + 16, [](auto byte) { return byte == sentinel; }));
        ABI_CHECK(std::all_of(storage_.end() - 16, storage_.end(), [](auto byte) { return byte == sentinel; }));
        ABI_CHECK(output.value.struct_size == original.struct_size && output.value.reserved == original.reserved &&
                  output.value.data == original.data && output.value.capacity == original.capacity);
        ABI_CHECK(status != EVE_PENDING && status <= EVE_OK);
        if (original.struct_size >= sizeof(EveStateBufferV1) && status != EVE_OK)
            ABI_CHECK(output.value.size == 0);
        if (original.struct_size < sizeof(EveStateBufferV1))
            output.checkCapacity(original.struct_size, objectBytes(original));
    }
    std::string text() const
    {
        ABI_CHECK(output.value.size <= output.value.capacity);
        return {reinterpret_cast<const char*>(output.value.data), static_cast<std::size_t>(output.value.size)};
    }
    Guarded<EveStateBufferV1> output;

private:
    std::vector<std::uint8_t> storage_;
};
void values(const QString& root)
{
    for (const auto& artifact : artifacts) {
        if (!artifact.state)
            continue;
        Image image(root, artifact);
        const auto api = stateApi(image);
        const auto current = schema(api);
        const std::string plugin = "example.counter";
        for (const std::string text : {"0", "1", "9007199254740993", "18446744073709551615"}) {
            const auto expected = text;
            OutputBuffer output(text.size());
            const auto initial = *output.get();
            const auto input = objectBytes(current);
            const auto status = api.validate(bytes(plugin), &current, bytes(text), output.get());
            ABI_CHECK(status == EVE_OK);
            output.check(status, initial);
            ABI_CHECK(output.text() == expected && text == expected && input == objectBytes(current));
        }
        const std::vector<std::string> invalid{"",
                                               "-1",
                                               "+1",
                                               "00",
                                               "01",
                                               "1.0",
                                               "1e0",
                                               "1E+1",
                                               " 1",
                                               "1 ",
                                               "\t1",
                                               "1\n",
                                               "18446744073709551616",
                                               "184467440737095516150",
                                               "1x",
                                               "null",
                                               "true",
                                               "[]",
                                               "\"1\"",
                                               std::string("0\0", 2),
                                               "\xff"};
        for (const auto& text : invalid) {
            OutputBuffer output(20);
            const auto initial = *output.get();
            const auto status = api.validate(bytes(plugin), &current, bytes(text), output.get());
            ABI_CHECK(status == EVE_E_INVALID_ARGUMENT);
            output.check(status, initial);
        }
        const std::string zero = "0";
        for (unsigned invalidSchema = 0; invalidSchema < 7; ++invalidSchema) {
            auto malformed = current;
            if (invalidSchema == 0)
                malformed.struct_size = sizeof(malformed) - 1;
            else if (invalidSchema == 1)
                malformed.reserved = 1;
            else if (invalidSchema == 2)
                malformed.version = 0;
            else if (invalidSchema == 3)
                malformed.version = 2;
            else if (invalidSchema == 4)
                malformed.id_size = 0;
            else if (invalidSchema == 5)
                malformed.id_size = EVE_STATE_MAX_ID_BYTES + 1;
            else
                malformed.id[0] = 'x';
            const auto originalSchema = objectBytes(malformed);
            OutputBuffer output(20);
            const auto initial = *output.get();
            const auto status = api.validate(bytes(plugin), &malformed, bytes(zero), output.get());
            ABI_CHECK(status == EVE_E_INVALID_ARGUMENT);
            output.check(status, initial);
            ABI_CHECK(originalSchema == objectBytes(malformed));
        }
        OutputBuffer output(20);
        const auto initial = *output.get();
        const std::string absent = "example.missing";
        const auto status = api.validate(bytes(absent), &current, bytes(zero), output.get());
        ABI_CHECK(status == EVE_E_NOT_FOUND);
        output.check(status, initial);
    }
}
void buffers(const QString& root)
{
    for (const auto& artifact : artifacts) {
        if (!artifact.state)
            continue;
        Image image(root, artifact);
        const auto api = stateApi(image);
        const auto current = schema(api);
        const std::string plugin = "example.counter", maximum = "18446744073709551615", zero = "0";
        for (const std::size_t capacity : {std::size_t(0), std::size_t(1), maximum.size() - 1}) {
            OutputBuffer output(capacity);
            const auto initial = *output.get();
            const auto status = api.validate(bytes(plugin), &current, bytes(maximum), output.get());
            ABI_CHECK(status == EVE_E_LIMIT);
            output.check(status, initial);
        }
        for (unsigned invalid = 0; invalid < 7; ++invalid) {
            OutputBuffer output(invalid == 3 ? static_cast<std::size_t>(EVE_STATE_MAX_JSON_BYTES + 1) : 20);
            if (invalid == 0)
                output.get()->struct_size = sizeof(EveStateBufferV1) - 1;
            else if (invalid == 1)
                output.get()->reserved = 1;
            else if (invalid == 2)
                output.get()->data = nullptr;
            const auto initial = *output.get();
            const auto status = api.validate(invalid == 4 ? EveBytesV1{nullptr, 1} : bytes(plugin),
                                             invalid == 5 ? nullptr : &current,
                                             invalid == 6 ? EveBytesV1{nullptr, 1} : bytes(zero), output.get());
            ABI_CHECK(status < EVE_OK);
            output.check(status, initial);
        }
        ABI_CHECK(api.validate(bytes(plugin), &current, bytes(zero), nullptr) == EVE_E_INVALID_ARGUMENT);
        OutputBuffer empty(0);
        empty.get()->data = nullptr;
        const auto emptyInitial = *empty.get();
        const auto emptyStatus = api.validate(bytes(plugin), &current, bytes(zero), empty.get());
        ABI_CHECK(emptyStatus == EVE_E_LIMIT);
        empty.check(emptyStatus, emptyInitial);
        OutputBuffer capture(20);
        const auto initial = *capture.get();
        const auto status = api.capture(std::numeric_limits<EveHandle>::max(), capture.get());
        ABI_CHECK(status == EVE_E_NOT_FOUND);
        capture.check(status, initial);
        ABI_CHECK(api.restore(std::numeric_limits<EveHandle>::max(), &current, bytes(zero)) == EVE_E_NOT_FOUND);
    }
}
} // namespace

bool nativeServiceStateAbiCase(const QString& name, const QString& root)
{
    if (name == "abi_discovery")
        discovery(root);
    else if (name == "abi_state_schema")
        schemas(root);
    else if (name == "abi_state_values")
        values(root);
    else if (name == "abi_state_buffers")
        buffers(root);
    else
        return false;
    return true;
}

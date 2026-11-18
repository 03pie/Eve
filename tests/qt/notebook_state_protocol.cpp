/** @file notebook_state_protocol.cpp
 * @brief 实际 Qt 工厂的纯状态 IID、canonical Unicode JSON 和可写缓冲区边界。
 */
#include "../../src/module/artifact_p.hpp"
#include "notebook_state_p.hpp"
#include <QPluginLoader>
#include <algorithm>
#include <array>
#include <cordis/module/qt_manifest.hpp>
#include <cordis/module/qt_plugin.hpp>
#include <cordis/module/qt_state_plugin.hpp>
#include <cstring>
#include <iterator>
#include <limits>
#include <vector>

namespace notebook_test {
namespace {
namespace native = cordis::module::detail;
constexpr std::uint8_t sentinel = 0xa5;
CordisBytesV1 bytes(const std::string& value)
{
    return {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
}
/** @brief 只取得标准 root/状态接口并调用纯方法；不直接 create 业务对象。 */
struct Factory {
    std::unique_ptr<native::VerifiedSource> source;
    QPluginLoader loader;
    module::IQtStatePluginV1* state = nullptr;
    Factory(const QString& root, const QString& variant)
        : source(take(native::verifyModuleSource(root.toUtf8().toStdString(), manifest(variant), {}))),
          loader(QString::fromUtf8(source->artifact.libraryPath().c_str()))
    {
        const auto checked = take(module::inspectQtModule(root.toUtf8().toStdString(), manifest(variant)));
        const auto& value = checked.artifact.manifest();
        NOTEBOOK_CHECK(value.moduleId == "org.cordis.example.notebook" &&
                       value.moduleVersion == (variant == "a" ? "1.1.0" : "1.2.0") &&
                       value.plugins == std::vector<std::string>{"example.notebook"} &&
                       value.unloadPolicy == module::ModuleUnloadPolicy::Resident &&
                       checked.artifact.libraryPath() == source->artifact.libraryPath() &&
                       value.sha256 == source->artifact.manifest().sha256);
        loader.setLoadHints(QLibrary::PreventUnloadHint | QLibrary::ResolveAllSymbolsHint);
        auto* object = loader.instance();
        auto* factory = qobject_cast<module::IQtPluginV1*>(object);
        NOTEBOOK_CHECK(object && factory);
        const auto plugins = factory->pluginIds();
        NOTEBOOK_CHECK(plugins.size() == 1 && plugins.front() == "example.notebook");
        NOTEBOOK_CHECK(factory->coreRuntimeIdentity() == cordis::coreRuntimeIdentity());
        state = qobject_cast<module::IQtStatePluginV1*>(object);
        NOTEBOOK_CHECK(state);
    }
};
template <class T> std::array<std::uint8_t, sizeof(T)> copy(const T& value)
{
    std::array<std::uint8_t, sizeof(T)> result{};
    std::memcpy(result.data(), &value, sizeof(value));
    return result;
}
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
        NOTEBOOK_CHECK(std::all_of(before.begin(), before.end(), [](auto byte) { return byte == sentinel; }));
        NOTEBOOK_CHECK(std::all_of(after.begin(), after.end(), [](auto byte) { return byte == sentinel; }));
    }
};
struct Buffer {
    Guarded<CordisStateBufferV1> output;
    std::vector<std::uint8_t> storage;
    explicit Buffer(std::size_t capacity) : storage(capacity + 32, sentinel)
    {
        output.value = {sizeof(CordisStateBufferV1), 0, storage.data() + 16, capacity,
                        std::numeric_limits<std::uint64_t>::max()};
    }
    CordisStateBufferV1* get() { return &output.value; }
    void check(CordisStatus status, const CordisStateBufferV1& original) const
    {
        output.check();
        NOTEBOOK_CHECK(std::all_of(storage.begin(), storage.begin() + 16, [](auto byte) { return byte == sentinel; }));
        NOTEBOOK_CHECK(std::all_of(storage.end() - 16, storage.end(), [](auto byte) { return byte == sentinel; }));
        NOTEBOOK_CHECK(output.value.struct_size == original.struct_size && output.value.data == original.data &&
                       output.value.capacity == original.capacity && output.value.reserved == original.reserved);
        NOTEBOOK_CHECK(status <= CORDIS_OK);
        if (original.struct_size >= sizeof(CordisStateBufferV1) && status != CORDIS_OK) {
            NOTEBOOK_CHECK(output.value.size == 0);
            NOTEBOOK_CHECK(std::all_of(storage.begin(), storage.end(), [](auto byte) { return byte == sentinel; }));
        }
        if (original.struct_size < sizeof(CordisStateBufferV1)) {
            const auto old = copy(original), now = copy(output.value);
            NOTEBOOK_CHECK(
                std::equal(old.begin() + original.struct_size, old.end(), now.begin() + original.struct_size));
        }
    }
    std::string text() const
    {
        NOTEBOOK_CHECK(output.value.data && output.value.size <= output.value.capacity);
        return {reinterpret_cast<const char*>(output.value.data), static_cast<std::size_t>(output.value.size)};
    }
};
void protocol(const QString& root, const QString& variant)
{
    Factory factory(root, variant);
    const std::string plugin = "example.notebook";
    Guarded<CordisStateSchemaV1> described;
    described.value.struct_size = sizeof(described.value);
    NOTEBOOK_CHECK(factory.state->describeState(bytes(plugin), &described.value) == CORDIS_OK);
    described.check();
    NOTEBOOK_CHECK(described.value.version == 1 && described.value.id_size == sizeof(schemaId) - 1 &&
                   described.value.struct_size == sizeof(described.value) && !described.value.reserved &&
                   std::memcmp(described.value.id, schemaId, sizeof(schemaId) - 1) == 0);
    const auto schema = described.value;
    Guarded<CordisStateSchemaV1> missing;
    missing.value.struct_size = sizeof(missing.value);
    missing.value.version = 8;
    missing.value.id_size = 8;
    std::fill(std::begin(missing.value.id), std::end(missing.value.id), sentinel);
    const std::string unknown = "example.unknown";
    NOTEBOOK_CHECK(factory.state->describeState(bytes(unknown), &missing.value) == CORDIS_E_NOT_FOUND);
    missing.check();
    NOTEBOOK_CHECK(!missing.value.version && !missing.value.id_size);
    NOTEBOOK_CHECK(
        std::all_of(std::begin(missing.value.id), std::end(missing.value.id), [](auto byte) { return !byte; }));
    for (const std::uint32_t capacity : {0u, static_cast<std::uint32_t>(sizeof(CordisStateSchemaV1) - 1)}) {
        Guarded<CordisStateSchemaV1> shortSchema;
        shortSchema.value.struct_size = capacity;
        const auto original = copy(shortSchema.value);
        NOTEBOOK_CHECK(factory.state->describeState(bytes(plugin), &shortSchema.value) == CORDIS_E_INVALID_ARGUMENT);
        shortSchema.check();
        const auto now = copy(shortSchema.value);
        NOTEBOOK_CHECK(std::equal(original.begin() + capacity, original.end(), now.begin() + capacity));
    }
    const auto validate = [&](const std::string& json, CordisStatus expected,
                              const CordisStateSchemaV1* sourceSchema = nullptr) {
        Buffer output(json.size() + 16);
        const auto initial = *output.get();
        const auto originalSchema = copy(sourceSchema ? *sourceSchema : schema);
        const auto status = factory.state->validateState(bytes(plugin), sourceSchema ? sourceSchema : &schema,
                                                         bytes(json), output.get());
        NOTEBOOK_CHECK(status == expected);
        output.check(status, initial);
        NOTEBOOK_CHECK(originalSchema == copy(sourceSchema ? *sourceSchema : schema));
        if (status == CORDIS_OK)
            NOTEBOOK_CHECK(output.text() == json);
    };
    for (const auto& json :
         {std::string("{\"draft\":\"\"}"), draftJson(QString::fromUtf8("中文 😀\r\nline \"quote\" \\ end")),
          draftJson(QString::fromUtf8("a\0b", 3)), draftJson(QString(32768, QChar(0x00e9)))})
        validate(json, CORDIS_OK);
    const std::vector<std::string> invalid{"{}",
                                           "[]",
                                           "null",
                                           "{\"draft\":0}",
                                           "{\"draft\":\"x\",\"extra\":1}",
                                           "{\"draft\":\"first\",\"draft\":\"last\"}",
                                           " {\"draft\":\"x\"}",
                                           "{\"draft\":\"x\"}\n",
                                           "{\"draft\":\"\\u0078\"}",
                                           "{\"draft\":\"\\ud800\"}",
                                           "{",
                                           std::string("{\"draft\":\"") + '\xff' + "\"}"};
    for (const auto& json : invalid)
        validate(json, CORDIS_E_INVALID_ARGUMENT);
    validate(draftJson(QString(65537, QLatin1Char('x'))), CORDIS_E_LIMIT);
    for (unsigned invalidSchema = 0; invalidSchema < 4; ++invalidSchema) {
        auto malformed = schema;
        if (invalidSchema == 0)
            malformed.version = 2;
        else if (invalidSchema == 1)
            malformed.id[0] = 'x';
        else if (invalidSchema == 2)
            malformed.reserved = 1;
        else
            malformed.struct_size = sizeof(malformed) - 1;
        validate("{\"draft\":\"x\"}", CORDIS_E_INVALID_ARGUMENT, &malformed);
    }
    const std::string text = "{\"draft\":\"buffer\"}";
    for (const std::size_t capacity : {std::size_t(0), text.size() - 1, text.size()}) {
        Buffer output(capacity);
        const auto initial = *output.get();
        const auto status = factory.state->validateState(bytes(plugin), &schema, bytes(text), output.get());
        NOTEBOOK_CHECK(status == (capacity < text.size() ? CORDIS_E_LIMIT : CORDIS_OK));
        output.check(status, initial);
        if (status == CORDIS_OK)
            NOTEBOOK_CHECK(output.text() == text);
    }
    for (unsigned malformed = 0; malformed < 5; ++malformed) {
        Buffer output(malformed == 3 ? static_cast<std::size_t>(CORDIS_STATE_MAX_JSON_BYTES + 1) : 64);
        if (malformed == 0)
            output.get()->struct_size = sizeof(CordisStateBufferV1) - 1;
        else if (malformed == 1)
            output.get()->reserved = 1;
        else if (malformed == 2)
            output.get()->data = nullptr;
        const auto initial = *output.get();
        const auto status = factory.state->validateState(
            bytes(plugin), &schema, malformed == 4 ? CordisBytesV1{nullptr, 1} : bytes(text), output.get());
        NOTEBOOK_CHECK(status == CORDIS_E_INVALID_ARGUMENT);
        output.check(status, initial);
    }
    Buffer missingOutput(64);
    const auto initial = *missingOutput.get();
    const auto status = factory.state->validateState(bytes(unknown), &schema, bytes(text), missingOutput.get());
    NOTEBOOK_CHECK(status == CORDIS_E_NOT_FOUND);
    missingOutput.check(status, initial);
    NOTEBOOK_CHECK(factory.state->validateState(bytes(plugin), &schema, bytes(text), nullptr) ==
                   CORDIS_E_INVALID_ARGUMENT);
}
} // namespace
bool protocolCase(const QString& name, const QString& root)
{
    if (name != "protocol_a" && name != "protocol_b")
        return false;
    protocol(root, name == "protocol_a" ? "a" : "b");
    return true;
}
} // namespace notebook_test

/** @file qt_state.cpp
 * @brief 真实 Qt 状态插件，独立 IID 与原工厂/实例共同工作。
 */
#include <QCoreApplication>
#include <QObject>
#include <QVariant>
#include <cordis/module/qt_plugin.hpp>
#include <cordis/module/qt_state_plugin.hpp>
#include <cstring>
#include <stdexcept>
#include <vector>
using namespace cordis;
using namespace cordis::module;
namespace {
int counts[8]{};
using Hook = void(CORDIS_CALL*)(int);
Hook hook = nullptr;
std::vector<QObject*> objects;
QString mode()
{
    return QCoreApplication::instance()->property("cordis.qt.stateMode").toString();
}
void invoke(int phase)
{
    if (hook)
        hook(phase);
}
bool id(CordisBytesV1 value, const char* expected)
{
    return value.size == std::strlen(expected) &&
           !std::memcmp(value.data, expected, static_cast<std::size_t>(value.size));
}
CordisStatus output(CordisStateBufferV1* buffer, const QByteArray& value)
{
    if (buffer->capacity < static_cast<std::uint64_t>(value.size()))
        return CORDIS_E_LIMIT;
    std::memcpy(buffer->data, value.constData(), static_cast<std::size_t>(value.size()));
    buffer->size = static_cast<std::uint64_t>(value.size());
    return CORDIS_OK;
}
class Child final : public QObject {
public:
    explicit Child(QObject* parent) : QObject(parent) {}
    ~Child() override { ++counts[6]; }
};
} // namespace
class StateBusiness final : public QObject, public IQtInstanceV1, public IQtStateInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(cordis::module::IQtInstanceV1 cordis::module::IQtStateInstanceV1)
public:
    StateBusiness() : index(objects.size())
    {
        objects.push_back(this);
        new Child(this);
        setProperty("state", "7");
    }
    ~StateBusiness() override
    {
        objects[index] = nullptr;
        ++counts[5];
    }
    Task<void> activate(Context context) override
    {
        ++counts[4];
        setProperty("activatedState", property("state"));
        if (context.config() == "\"need-restore\"" && !restored)
            return failedTask(makeError(ErrorCode::ActivationFailed, "restore was not completed before activate"));
        return completedTask();
    }
    CordisStatus captureState(CordisStateBufferV1* buffer) override
    {
        ++counts[3];
        invoke(3);
        const auto value = mode();
        if (value == "capture_busy")
            return CORDIS_E_BUSY;
        if (value == "capture_pending")
            return CORDIS_PENDING;
        if (value == "capture_throw")
            throw std::runtime_error("capture");
        if (value == "capture_parent")
            setParent(QCoreApplication::instance());
        if (value == "capture_deleted") {
            delete this;
            return CORDIS_E_BUSY;
        }
        if (value == "capture_pointer") {
            buffer->data = nullptr;
            return CORDIS_OK;
        }
        if (value == "capture_json")
            return output(buffer, "{");
        return output(buffer, property("state").toByteArray());
    }
    CordisStatus restoreState(const CordisStateSchemaV1* schema, CordisBytesV1 json) override
    {
        ++counts[2];
        invoke(2);
        const auto value = mode();
        if (value == "restore_pending")
            return CORDIS_PENDING;
        if (value == "restore_throw")
            throw std::runtime_error("restore");
        if (value == "restore_reject")
            return CORDIS_E_INVALID_ARGUMENT;
        if (schema->version != 2 || schema->id_size != 7 || std::memcmp(schema->id, "counter", 7))
            return CORDIS_E_INVALID_ARGUMENT;
        setProperty("state", QByteArray(reinterpret_cast<const char*>(json.data), static_cast<int>(json.size)));
        restored = true;
        return CORDIS_OK;
    }

private:
    const std::size_t index;
    bool restored = false;
};
class MissingState final : public QObject, public IQtInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(cordis::module::IQtInstanceV1)
public:
    Task<void> activate(Context) override
    {
        ++counts[4];
        return completedTask();
    }
};
class StateFactory final : public QObject, public IQtPluginV1, public IQtStatePluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.cordis.IQtPluginV1/1.0" FILE "qt_metadata.json")
    Q_INTERFACES(cordis::module::IQtPluginV1 cordis::module::IQtStatePluginV1)
public:
    QStringList pluginIds() const override { return {"org.cordis.qt.page", "org.cordis.qt.settings"}; }
    QObject* create(const QString&) override
    {
        ++counts[7];
        if (mode() == "missing_iid")
            return new MissingState;
        return new StateBusiness;
    }
    CordisStatus describeState(CordisBytesV1 plugin, CordisStateSchemaV1* schema) override
    {
        ++counts[0];
        if (mode() == "describe_throw")
            throw std::runtime_error("describe");
        if (mode() == "describe_pending")
            return CORDIS_PENDING;
        if (mode() == "describe_bad") {
            schema->reserved = 1;
            return CORDIS_OK;
        }
        if (id(plugin, "org.cordis.qt.settings") || mode() == "stateless")
            return CORDIS_E_UNSUPPORTED;
        schema->version = 2;
        schema->id_size = 7;
        std::memcpy(schema->id, "counter", 7);
        return CORDIS_OK;
    }
    CordisStatus validateState(CordisBytesV1, const CordisStateSchemaV1* schema, CordisBytesV1 json,
                               CordisStateBufferV1* buffer) override
    {
        ++counts[1];
        invoke(1);
        const auto value = mode();
        if (value == "validate_pending")
            return CORDIS_PENDING;
        if (value == "validate_throw")
            throw std::runtime_error("validate");
        if (value == "validate_reject")
            return CORDIS_E_INVALID_ARGUMENT;
        if (schema->id_size != 7 || std::memcmp(schema->id, "counter", 7) || schema->version < 1 || schema->version > 2)
            return CORDIS_E_INVALID_ARGUMENT;
        return output(buffer, QByteArray(reinterpret_cast<const char*>(json.data), static_cast<int>(json.size)));
    }
};
extern "C" __declspec(dllexport) int CORDIS_CALL cordis_qt_state_count(int index)
{
    return index >= 0 && index < 8 ? counts[index] : -1;
}
extern "C" __declspec(dllexport) QObject* CORDIS_CALL cordis_qt_state_object(int index)
{
    return index >= 0 && static_cast<std::size_t>(index) < objects.size() ? objects[index] : nullptr;
}
extern "C" __declspec(dllexport) void CORDIS_CALL cordis_qt_state_hook(Hook value)
{
    hook = value;
}
#include "qt_state.moc"

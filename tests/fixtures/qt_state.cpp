/** @file qt_state.cpp
 * @brief 真实 Qt 状态插件，独立 IID 与原工厂/实例共同工作。
 */
#include <QCoreApplication>
#include <QObject>
#include <QVariant>
#include <eve/module/qt_plugin.hpp>
#include <eve/module/qt_state_plugin.hpp>
#include <cstring>
#include <stdexcept>
#include <vector>
using namespace eve;
using namespace eve::module;
#ifndef FIXTURE_STATE_VERSION
#define FIXTURE_STATE_VERSION 2
#endif
namespace {
int counts[8]{};
using Hook = void(EVE_CALL*)(int);
Hook hook = nullptr;
bool hasMode = false;
QString imageMode;
std::vector<QObject*> objects;
QString mode()
{
    return hasMode ? imageMode : QCoreApplication::instance()->property("eve.qt.stateMode").toString();
}
void invoke(int phase)
{
    if (hook)
        hook(phase);
}
bool id(EveBytesV1 value, const char* expected)
{
    return value.size == std::strlen(expected) &&
           !std::memcmp(value.data, expected, static_cast<std::size_t>(value.size));
}
EveStatus output(EveStateBufferV1* buffer, const QByteArray& value)
{
    if (buffer->capacity < static_cast<std::uint64_t>(value.size()))
        return EVE_E_LIMIT;
    std::memcpy(buffer->data, value.constData(), static_cast<std::size_t>(value.size()));
    buffer->size = static_cast<std::uint64_t>(value.size());
    return EVE_OK;
}
class Child final : public QObject {
public:
    explicit Child(QObject* parent) : QObject(parent) {}
    ~Child() override { ++counts[6]; }
};
} // namespace
class StateBusiness final : public QObject, public IQtInstanceV1, public IQtStateInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(eve::module::IQtInstanceV1 eve::module::IQtStateInstanceV1)
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
        setProperty("activatedRestored", restored);
        const auto identity = context.identity();
        if (!identity.hasValue())
            return failedTask(identity.error());
        setProperty("coreInstance", QVariant::fromValue<qulonglong>(identity.value().instance.value()));
#if FIXTURE_STATE_VERSION >= 3
        invoke(4);
#endif
        if (mode() == "activate_fail")
            return failedTask(makeError(ErrorCode::ActivationFailed, "state fixture activation failed"));
        if (context.config() == "\"need-restore\"" && !restored)
            return failedTask(makeError(ErrorCode::ActivationFailed, "restore was not completed before activate"));
        return completedTask();
    }
    EveStatus captureState(EveStateBufferV1* buffer) override
    {
        ++counts[3];
        setProperty("stateCall", 3);
        invoke(3);
        setProperty("stateCall", 0);
        const auto value = mode();
        if (value == "capture_busy")
            return EVE_E_BUSY;
        if (value == "capture_pending")
            return EVE_PENDING;
        if (value == "capture_throw")
            throw std::runtime_error("capture");
        if (value == "capture_parent")
            setParent(QCoreApplication::instance());
        if (value == "capture_deleted") {
            delete this;
            return EVE_E_BUSY;
        }
        if (value == "capture_pointer") {
            buffer->data = nullptr;
            return EVE_OK;
        }
        if (value == "capture_json")
            return output(buffer, "{");
        return output(buffer, property("state").toByteArray());
    }
    EveStatus restoreState(const EveStateSchemaV1* schema, EveBytesV1 json) override
    {
        ++counts[2];
        setProperty("stateCall", 2);
        invoke(2);
        setProperty("stateCall", 0);
        const auto value = mode();
        if (value == "restore_pending")
            return EVE_PENDING;
        if (value == "restore_throw")
            throw std::runtime_error("restore");
        if (value == "restore_reject")
            return EVE_E_INVALID_ARGUMENT;
        if (schema->version != FIXTURE_STATE_VERSION || schema->id_size != 7 || std::memcmp(schema->id, "counter", 7))
            return EVE_E_INVALID_ARGUMENT;
        setProperty("state", QByteArray(reinterpret_cast<const char*>(json.data), static_cast<int>(json.size)));
        restored = true;
        return EVE_OK;
    }

private:
    const std::size_t index;
    bool restored = false;
};
class MissingState final : public QObject, public IQtInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(eve::module::IQtInstanceV1)
public:
    Task<void> activate(Context) override
    {
        ++counts[4];
        return completedTask();
    }
};
class StateFactory final : public QObject, public IQtPluginV1, public IQtStatePluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.eve.IQtPluginV1/1.0" FILE "qt_metadata.json")
    Q_INTERFACES(eve::module::IQtPluginV1 eve::module::IQtStatePluginV1)
public:
    QStringList pluginIds() const override { return {"org.eve.qt.page", "org.eve.qt.settings"}; }
    QObject* create(const QString&) override
    {
        ++counts[7];
        if (mode() == "missing_iid")
            return new MissingState;
        return new StateBusiness;
    }
    EveStatus describeState(EveBytesV1 plugin, EveStateSchemaV1* schema) override
    {
        ++counts[0];
        if (mode() == "describe_throw")
            throw std::runtime_error("describe");
        if (mode() == "describe_pending")
            return EVE_PENDING;
        if (mode() == "describe_bad") {
            schema->reserved = 1;
            return EVE_OK;
        }
        if (id(plugin, "org.eve.qt.settings") || mode() == "stateless")
            return EVE_E_UNSUPPORTED;
        schema->version = FIXTURE_STATE_VERSION;
        schema->id_size = 7;
        std::memcpy(schema->id, "counter", 7);
        return EVE_OK;
    }
    EveStatus validateState(EveBytesV1, const EveStateSchemaV1* schema, EveBytesV1 json,
                               EveStateBufferV1* buffer) override
    {
        ++counts[1];
        invoke(1);
        const auto value = mode();
        if (value == "validate_pending")
            return EVE_PENDING;
        if (value == "validate_throw")
            throw std::runtime_error("validate");
        if (value == "validate_reject")
            return EVE_E_INVALID_ARGUMENT;
        if (schema->id_size != 7 || std::memcmp(schema->id, "counter", 7) || schema->version < 1 ||
            schema->version > FIXTURE_STATE_VERSION)
            return EVE_E_INVALID_ARGUMENT;
        QByteArray converted(reinterpret_cast<const char*>(json.data), static_cast<int>(json.size));
#if FIXTURE_STATE_VERSION >= 3
        if (schema->version < FIXTURE_STATE_VERSION) {
            const auto start = converted.startsWith('-') ? 1 : 0;
            if (converted.size() == start)
                return EVE_E_INVALID_ARGUMENT;
            for (int index = start; index != converted.size(); ++index)
                if (converted[index] < '0' || converted[index] > '9')
                    return EVE_E_INVALID_ARGUMENT;
            if (converted != "0" && converted != "-0")
                converted.append('0');
        }
#endif
        return output(buffer, converted);
    }
};
extern "C" __declspec(dllexport) int EVE_CALL eve_qt_state_count(int index)
{
    return index >= 0 && index < 8 ? counts[index] : -1;
}
extern "C" __declspec(dllexport) QObject* EVE_CALL eve_qt_state_object(int index)
{
    return index >= 0 && static_cast<std::size_t>(index) < objects.size() ? objects[index] : nullptr;
}
extern "C" __declspec(dllexport) void EVE_CALL eve_qt_state_hook(Hook value)
{
    hook = value;
}
extern "C" __declspec(dllexport) void EVE_CALL eve_qt_state_mode(EveBytesV1 value)
{
    if (value.size > 128 || (value.size && !value.data))
        return;
    imageMode = QString::fromUtf8(reinterpret_cast<const char*>(value.data), static_cast<int>(value.size));
    hasMode = true;
}
#include "qt_state.moc"

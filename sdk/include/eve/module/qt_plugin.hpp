/** @file qt_plugin.hpp
 * @brief 仅限已核对工具链的 Qt C++ 工厂契约；不属于业务 C ABI。
 */
#pragma once
#if defined(_MSC_VER) && !defined(_DLL)
#error "Eve Qt plugin SDK requires the dynamic MSVC CRT (/MD or /MDd)"
#endif
#include <QStringList>
#include <QtPlugin>
#include <eve/build_info.hpp>
#include <eve/instance.hpp>
class QObject;
namespace eve::module {
/** @brief 每次激活的独立业务 QObject 实现此接口。
 * 所有异步任务/页面/连接/工作线程须登记到 context，并在其清理屏障内真正释放。
 * 宿主等待本代资源及服务对象退役后，通过 QObjectResource 完整删除业务对象。
 * 不得外部删除、迁移、重新设置 parent，或让未托管回调使用该对象。
 */
class IQtInstanceV1 {
public:
    virtual ~IQtInstanceV1() = default;
    virtual Task<void> activate(Context context) = 0;
};
/** @brief loader root 仅充当工厂。create 转移一个全新、无 parent、应用线程的 QObject。
 * 不得返回 root 或其他活动业务对象；nullptr 表示本次创建被正常拒绝。
 * 宿主不重入 create，忙碌创建可在静止后重试；创建中失活的对象只清理、不调用 activate。
 * root 的寿命由 QPluginLoader 管理，业务实例不能删除 root。
 */
class IQtPluginV1 {
public:
    virtual ~IQtPluginV1() = default;
    /** @brief 必须在 DLL 的虚调用中取得其实际 Core 身份；使用 SDK 默认实现。 */
    virtual const void* coreRuntimeIdentity() const noexcept { return eve::coreRuntimeIdentity(); }
    virtual QStringList pluginIds() const = 0;
    virtual QObject* create(const QString& pluginId) = 0;
};
} // namespace eve::module
Q_DECLARE_INTERFACE(eve::module::IQtPluginV1, "org.eve.IQtPluginV1/1.0")
Q_DECLARE_INTERFACE(eve::module::IQtInstanceV1, "org.eve.IQtInstanceV1/1.0")

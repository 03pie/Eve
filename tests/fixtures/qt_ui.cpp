/** @file qt_ui.cpp
 * @brief 真实 Ela 页面 DLL；计数与投递入口仅供跨运行库回归。
 */
#include <ElaText.h>
#include <QObject>
#include <QVBoxLayout>
#include <QWidget>
#include <cordis/module/qt_plugin.hpp>
#include <cordis/qt/scheduler.hpp>
#include <cordis/ui/pages.hpp>
#include <optional>
using namespace cordis;
using namespace cordis::module;
using namespace cordis::ui;
namespace {
// business create/activate, page/child/capture/business destruction, order errors, jobs/discards
int counts[9] = {};
struct Lifetime {
    bool childDestroyed = false;
    bool factoryReleased = false;
};
struct FactoryCapture {
    std::shared_ptr<Lifetime> life;
    ~FactoryCapture()
    {
        if (!life->childDestroyed)
            ++counts[6];
        life->factoryReleased = true;
        ++counts[4];
    }
};
class Child final : public QObject {
public:
    Child(QObject* parent, std::shared_ptr<Lifetime> life) : QObject(parent), life_(std::move(life)) {}
    ~Child() override
    {
        life_->childDestroyed = true;
        ++counts[3];
    }

private:
    std::shared_ptr<Lifetime> life_;
};
class Page final : public QWidget {
public:
    Page(const std::shared_ptr<Lifetime>& life, const std::string& config)
    {
        setObjectName(QStringLiteral("cordis.dll.page"));
        auto layout = new QVBoxLayout(this);
        layout->addWidget(new ElaText(QString::fromUtf8("来自插件 DLL 的页面"), 24, this));
        auto text = new ElaText(QString::fromStdString(config), this);
        text->setTextFormat(Qt::PlainText);
        layout->addWidget(text);
        layout->addStretch();
        new Child(this, life);
    }
    ~Page() override { ++counts[2]; }
};
} // namespace
class UiBusiness final : public QObject, public IQtInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(cordis::module::IQtInstanceV1)
public:
    UiBusiness() { ++counts[0]; }
    ~UiBusiness() override
    {
        if (page_ && (!life_->childDestroyed || !life_->factoryReleased))
            ++counts[6];
        ++counts[5];
    }
    Task<void> activate(Context context) override
    {
        ++counts[1];
        auto service = context.consume<UiPagesService>({"ui.pages"});
        if (!service.hasValue())
            return failedTask(service.error());
        auto capture = std::make_shared<FactoryCapture>();
        capture->life = life_;
        auto added = service.value().withService([&](UiPagesService& pages) {
            page_ = pages
                        .add(context, {"dll", "DLL 页面", "dll",
                                       [capture, config = context.config()] {
                                           return std::make_unique<Page>(capture->life, config);
                                       }})
                        .value();
        });
        if (!added.hasValue())
            return failedTask(added.error());
        return page_->ready();
    }

private:
    std::shared_ptr<Lifetime> life_ = std::make_shared<Lifetime>();
    std::optional<PageHandle> page_;
};
class UiFactory final : public QObject, public IQtPluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.cordis.IQtPluginV1/1.0" FILE "qt_metadata.json")
    Q_INTERFACES(cordis::module::IQtPluginV1)
public:
    QStringList pluginIds() const override { return {"org.cordis.qt.page", "org.cordis.qt.settings"}; }
    QObject* create(const QString& id) override { return pluginIds().contains(id) ? new UiBusiness : nullptr; }
};
extern "C" __declspec(dllexport) int __cdecl cordis_qt_ui_count(int index)
{
    return index >= 0 && index < 9 ? counts[index] : -1;
}
/** @brief 通过 final 具体类型调用，真实覆盖 DLL 中可能去虚化的 SDK 调用。 */
extern "C" __declspec(dllexport) int __cdecl cordis_qt_ui_post(qt::QtScheduler* scheduler)
{
    return scheduler->post({[] { ++counts[7]; }, [] { ++counts[8]; }}) ? 1 : 0;
}
#include "qt_ui.moc"

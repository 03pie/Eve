/** @file qt_metadata.cpp
 * @brief 元数据夹具；预检不得加载映像或创建该 QObject。
 */
#include <QCoreApplication>
#include <QObject>
#include <stdexcept>
namespace {
int rootCount = 0;
}
extern "C" __declspec(dllexport) int __cdecl eve_qt_fixture_roots()
{
    return rootCount;
}
class MetadataFixture final : public QObject {
    Q_OBJECT
#if defined(EVE_TEST_LEGACY_IID) && EVE_TEST_LEGACY_IID
    Q_PLUGIN_METADATA(IID "org.cordis.IQtPluginV1/1.0" FILE "qt_metadata.json")
#else
    Q_PLUGIN_METADATA(IID "org.eve.IQtPluginV1/1.0" FILE "qt_metadata.json")
#endif
public:
    MetadataFixture()
    {
        ++rootCount;
#if defined(EVE_QT_ROOT_PARENT)
        setParent(QCoreApplication::instance());
#elif defined(EVE_QT_ROOT_REENTER)
        QCoreApplication::processEvents();
#elif defined(EVE_QT_ROOT_THROW)
        throw std::runtime_error("fixture root construction rejected");
#endif
    }
};
#include "qt_metadata.moc"

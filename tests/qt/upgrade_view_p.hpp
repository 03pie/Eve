/** @file upgrade_view_p.hpp
 * @brief 实际升级控件的共享定位与值断言；装配仍由原 Scene 拥有。
 */
#pragma once
#include "platform_upgrade_p.hpp"
#include <QAbstractProxyModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTableView>
#include <exception>

namespace upgrade_view_test {
using namespace platform_upgrade_test;

struct View {
    Scene& scene;
    QPointer<QWidget> body, window;
    QTableView* table = nullptr;
    QLineEdit* source = nullptr;
    QPushButton *browse = nullptr, *review = nullptr, *start = nullptr, *cancel = nullptr, *save = nullptr;
    QPushButton* refreshButton = nullptr;
    QPlainTextEdit *previewText = nullptr, *reportText = nullptr;
    QLabel *status = nullptr, *saveStatus = nullptr;

    explicit View(Scene& value);
    void refresh();
    void select(const std::string& alias);
    void preview(const std::string& alias, const QString& path);
    OperationId begin();
    WorkspaceUpgradeReport settled(std::size_t previousReports = 0);
    void saveNow();
};
struct Observation {
    QMetaObject::Connection connection;
    ~Observation() { QObject::disconnect(connection); }
};
struct Candidate {
    InstanceSnapshot instance;
    ModuleRuntimeOrigin origin;
};

QString candidatePath(const QString& sourceBin, const std::string& alias, const std::string& variant = "b");
QString copiedCandidate(Scene& scene, const QString& sourceBin, const std::string& alias);
std::string diskText(const Scene& scene);
void writeDisk(const Scene& scene, const QByteArray& text);
QListView* commandList();
std::optional<Candidate> notebookCandidate(const Scene& scene, GenerationId previous, bool held);
void committed(const Scene& scene, const WorkspaceUpgradeReport& report);
void visibleReport(const View& view, const WorkspaceUpgradeReport& report);
bool controlsCase(const QString& name, const QString& sourceBin);
bool operationsCase(const QString& name, const QString& sourceBin);
bool failuresCase(const QString& name, const QString& sourceBin);
} // namespace upgrade_view_test

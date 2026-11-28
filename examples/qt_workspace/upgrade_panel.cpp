/** @file upgrade_panel.cpp
 * @brief 候选输入与原任务值呈现；所有操作经具体 Workspace 视图屏障和原宿主入口。
 */
#include "appearance_p.hpp"
#include "upgrade_panel.hpp"
#include "theme_palette_p.hpp"
#include "modules_panel_p.hpp"
#include <ElaLineEdit.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace notebook {
using namespace eve;
namespace {
void setText(ElaPlainTextEdit* widget, const QString& text)
{
    if (widget->toPlainText() != text)
        widget->setPlainText(text);
}
} // namespace

UpgradePanel::UpgradePanel(std::weak_ptr<Workspace> workspace, QWidget* parent, std::function<void()> refreshOwner)
    : QWidget(parent), workspace_(std::move(workspace)), refreshOwner_(std::move(refreshOwner))
{
    if (!refreshOwner_)
        throw std::invalid_argument("upgrade panel requires its parent refresh");
    setObjectName("EveUpgradePanel");
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    auto hint = new ElaText(QString::fromUtf8("选择模块声明 → 检查影响 → 升级 → 保存已接受配置"), 14, this);
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::PlainText);
    layout->addWidget(hint);
    auto row = new QHBoxLayout;
    source_ = new ElaLineEdit(this);
    source_->setObjectName("EveUpgradeSource");
    source_->setAccessibleName(QString::fromUtf8("候选模块声明绝对路径"));
    source_->setPlaceholderText(QString::fromUtf8("候选 module.json 的绝对路径"));
    row->addWidget(source_, 1);
    browse_ = new eve::ui::detail::ActionButton(QString::fromUtf8("选择文件…"), this);
    browse_->setObjectName("EveUpgradeBrowse");
    row->addWidget(browse_);
    layout->addLayout(row);
    auto actions = new QHBoxLayout;
    const auto button = [&](const char* name, const char* title) {
        auto value = new eve::ui::detail::ActionButton(QString::fromUtf8(title), this);
        value->setObjectName(QString::fromLatin1(name));
        value->setEnabled(false);
        actions->addWidget(value);
        return value;
    };
    previewButton_ = button("EveUpgradePreview", "检查影响");
    start_ = button("EveUpgradeStart", "升级");
    cancel_ = button("EveUpgradeCancel", "请求取消");
    actions->addStretch();
    save_ = button("EveUpgradeSave", "保存已接受配置");
    layout->addLayout(actions);
    status_ = new ElaText(this);
    status_->setObjectName("EveUpgradeStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    previewText_ = new ElaPlainTextEdit(this);
    previewText_->setObjectName("EveUpgradePreviewText");
    eve::ui::detail::bindWorkbenchContentPalette(previewText_);
    previewText_->setReadOnly(true);
    layout->addWidget(previewText_, 1);
    reportText_ = new ElaPlainTextEdit(this);
    reportText_->setObjectName("EveUpgradeReportText");
    eve::ui::detail::bindWorkbenchContentPalette(reportText_);
    reportText_->setReadOnly(true);
    layout->addWidget(reportText_, 1);
    saveStatus_ = new ElaText(this);
    saveStatus_->setObjectName("EveUpgradeSaveStatus");
    saveStatus_->setWordWrap(true);
    saveStatus_->setTextFormat(Qt::PlainText);
    layout->addWidget(saveStatus_);
    QObject::connect(source_, &QLineEdit::textChanged, this, [this](const QString& text) { sourceChanged(text); });
    QObject::connect(browse_, &QPushButton::clicked, this, [this] { browse(); });
    QObject::connect(previewButton_, &QPushButton::clicked, this, [this] { inspect(); });
    QObject::connect(start_, &QPushButton::clicked, this, [this] { start(); });
    QObject::connect(cancel_, &QPushButton::clicked, this, [this] { cancel(); });
    QObject::connect(save_, &QPushButton::clicked, this, [this] { save(); });
}

void UpgradePanel::act(const std::function<void(Workspace&)>& action)
{
    const auto workspace = workspace_.lock();
    if (!workspace)
        return;
    (void)workspace->withModuleView([this, workspace, &action] {
        if (acting_ || rendering_)
            return;
        {
            QScopedValueRollback<bool> acting(acting_, true);
            action(*workspace);
        }
        const auto refresh = refreshOwner_;
        refresh();
    });
}

void UpgradePanel::refresh(const WorkspaceModulesSnapshot& modules, const WorkspaceUpgradeSnapshot& upgrade,
                           const std::string& selection)
{
    if (rendering_)
        return;
    QScopedValueRollback<bool> rendering(rendering_, true);
    const auto alias = upgrade.active ? upgrade.active->alias : selection;
    const auto current = std::find_if(modules.current.begin(), modules.current.end(),
                                      [&](const auto& row) { return row.alias == alias; });
    const auto generation = current == modules.current.end() ? GenerationId{} : current->generation;
    const auto runtime = current == modules.current.end() ? RuntimeId{} : current->runtime;
    const bool supports = current != modules.current.end() && current->supportsUpgrade;
    const bool changed =
        alias_ != alias || generation_ != generation || runtime_ != runtime || revision_ != modules.desiredRevision;
    if (changed) {
        preview_.reset();
        ++inputVersion_;
        if (alias_ != alias) {
            sourceText_.clear();
            QSignalBlocker blocked(source_);
            source_->clear();
            message_.clear();
        }
    }
    alias_ = alias;
    generation_ = generation;
    runtime_ = runtime;
    revision_ = modules.desiredRevision;
    const bool busy = upgrade.active.has_value();
    if ((busy || changed) && chooser_)
        chooser_->reject();
    const bool operationsOpen = modules.open && upgrade.accepting && !busy;
    const bool writable = operationsOpen && supports;
    source_->setEnabled(writable);
    browse_->setEnabled(writable);
    previewButton_->setEnabled(writable && !sourceText_.trimmed().isEmpty());
    const auto actual = upgrade.active && upgrade.active->replacement ? &*upgrade.active->replacement : nullptr;
    operation_ = actual ? actual->operation : OperationId{};
    cancel_->setEnabled(modules.open && actual && actual->operation.valid() && !actual->committed &&
                        !actual->recovered && !actual->resourcesRetained && !upgrade.active->retained &&
                        !upgrade.deliveryBlocked && !upgrade.cancellationRequested &&
                        !(upgrade.phase && detail::upgradePhaseTerminal(*upgrade.phase)));
    const auto saved = saving_ ? saving_->tryResult() : Task<loader::SaveReport>::Snapshot{};
    save_->setEnabled(operationsOpen && modules.desiredRevision != 0 && modules.dirty && !modules.writing &&
                      (!saving_ || saved));
    setText(reportText_, detail::upgradeReportText(upgrade, modules, alias_));
    QString status = detail::upgradePhaseText(upgrade);
    if (!supports && !alias_.empty())
        status = QString::fromUtf8("当前模块不提供运行状态升级能力；候选与代码升级只读。");
    if (!message_.isEmpty())
        status += '\n' + message_;
    if (execution_) {
        const auto result = execution_->tryResult();
        if (result && !result->hasValue())
            status += '\n' + detail::moduleErrorText(result->error());
    }
    status_->setText(status);
    auto saveText = detail::upgradeSaveText(modules, saved, saving_ && !saved);
    if (!saveMessage_.isEmpty())
        saveText += '\n' + saveMessage_;
    saveStatus_->setText(saveText);
    const auto unchecked = QString::fromUtf8("请先检查候选及影响。");
    setText(previewText_, preview_ ? detail::upgradePreviewText(*preview_) : unchecked);
    // 文本信号可改变真实输入；已失效的预览不能被旧呈现栈重新开放。
    if (!preview_)
        setText(previewText_, unchecked);
    start_->setEnabled(writable && preview_.has_value());
}

void UpgradePanel::sourceChanged(const QString& text)
{
    const auto workspace = workspace_.lock();
    if (!workspace)
        return;
    (void)workspace->withModuleView([this, workspace, text] {
        const auto actual = workspace->upgradeSnapshot();
        if (!actual.hasValue() || !actual.value().accepting || actual.value().active) {
            QSignalBlocker blocked(source_);
            source_->setText(sourceText_);
            return;
        }
        sourceText_ = text;
        ++inputVersion_;
        preview_.reset();
        message_.clear();
        start_->setEnabled(false);
        if (!acting_ && !rendering_) {
            const auto refresh = refreshOwner_;
            refresh();
        }
    });
}

void UpgradePanel::browse()
{
    act([this](Workspace& workspace) {
        const auto actual = workspace.upgradeSnapshot();
        const auto modules = workspace.modulesSnapshot();
        if (!actual.hasValue() || !modules.hasValue() || !actual.value().accepting || !modules.value().open)
            return;
        if (chooser_) {
            chooser_->raise();
            if (chooser_)
                chooser_->activateWindow();
            return;
        }
        auto dialog = new QFileDialog(this, QString::fromUtf8("选择候选模块声明"));
        chooser_ = dialog;
        dialog->setObjectName("EveUpgradeFileDialog");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowModality(Qt::NonModal);
        dialog->setModal(false);
        dialog->setFileMode(QFileDialog::ExistingFile);
        dialog->setNameFilter(QString::fromUtf8("模块声明 (*.json)"));
        const auto version = inputVersion_;
        const auto alias = alias_;
        QObject::connect(dialog, &QFileDialog::fileSelected, this, [this, version, alias](const QString& file) {
            const auto owner = workspace_.lock();
            if (!owner)
                return;
            (void)owner->withModuleView([this, owner, version, alias, file] {
                const auto state = owner->upgradeSnapshot();
                if (!state.hasValue() || !state.value().accepting || state.value().active || version != inputVersion_ ||
                    alias != alias_)
                    return;
                if (source_->text() == file)
                    sourceChanged(file);
                else
                    source_->setText(file);
            });
        });
        dialog->show();
    });
}

void UpgradePanel::inspect()
{
    act([this](Workspace& workspace) {
        preview_.reset();
        const auto source = source_->text();
        const auto alias = alias_;
        const auto version = inputVersion_;
        const QFileInfo file(source.trimmed());
        if (!QDir::isAbsolutePath(file.filePath())) {
            message_ = QString::fromUtf8("请输入模块声明的绝对路径。");
            return;
        }
        const auto value = workspace.previewUpgrade(alias, file.absolutePath(), file.fileName().toUtf8().toStdString());
        if (version != inputVersion_ || alias != alias_ || source != sourceText_ || source != source_->text()) {
            message_ = QString::fromUtf8("候选输入已变化，请重新检查。");
            return;
        }
        if (value.hasValue()) {
            preview_.emplace(value.value());
            message_ = QString::fromUtf8("已检查影响；升级时将再次核对。");
        } else
            message_ = detail::moduleErrorText(value.error());
    });
}

void UpgradePanel::start()
{
    act([this](Workspace& workspace) {
        if (!preview_)
            return;
        if (source_->text() != sourceText_) {
            preview_.reset();
            message_ = QString::fromUtf8("候选输入已变化，请重新检查。");
            return;
        }
        const auto checked = *preview_;
        const auto begun = workspace.upgrade(checked);
        if (!begun.hasValue()) {
            message_ = detail::moduleErrorText(begun.error());
            preview_.reset();
            return;
        }
        execution_.emplace(begun.value());
        message_ = QString::fromUtf8("升级请求已接纳；以原阶段和回执为准。");
    });
}

void UpgradePanel::cancel()
{
    act([this](Workspace& workspace) {
        const auto expected = operation_;
        if (!expected.valid())
            return;
        const auto result = workspace.cancelUpgrade(expected);
        message_ = result.hasValue() ? QString::fromUtf8("已请求取消；等待原执行结算。")
                                     : detail::moduleErrorText(result.error());
    });
}

void UpgradePanel::save()
{
    act([this](Workspace& workspace) {
        if (saving_ && !saving_->isReady())
            return;
        const auto saved = workspace.saveConfiguration(revision_);
        if (saved.hasValue()) {
            saving_.emplace(saved.value());
            saveMessage_.clear();
        } else {
            saving_.reset();
            saveMessage_ = QString::fromUtf8("保存请求未接纳：") + detail::moduleErrorText(saved.error());
        }
    });
}
} // namespace notebook

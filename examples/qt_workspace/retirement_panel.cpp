/** @file retirement_panel.cpp
 * @brief 原代次目录的稳定选择及显式重试；不关闭会话、不撤销 PIN 或借用。
 */
#include "retirement_panel.hpp"
#include "modules_panel_p.hpp"
#include "retirement_p.hpp"
#include <ElaComboBox.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaText.h>
#include <QHBoxLayout>
#include <QScopedValueRollback>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace notebook {
namespace {
eve::GenerationId selectedGeneration(const ElaComboBox& selector)
{
    return eve::GenerationId(selector.currentData().toULongLong());
}
QString generationText(const eve::module::ModuleGenerationSnapshot& row)
{
    const char* role = row.role == eve::module::GenerationRole::Current     ? "Current"
                       : row.role == eve::module::GenerationRole::Candidate ? "Candidate"
                                                                               : "Retired";
    return QString("#%1 · %2 · %3 · %4")
        .arg(row.generation.value())
        .arg(QString::fromStdString(row.manifest.moduleId), QString::fromStdString(row.manifest.moduleVersion),
             QString::fromLatin1(role));
}
} // namespace

RetirementPanel::RetirementPanel(std::weak_ptr<Workspace> workspace, QWidget* parent,
                                 std::function<void()> refreshOwner)
    : QWidget(parent), workspace_(std::move(workspace)), refreshOwner_(std::move(refreshOwner))
{
    if (!refreshOwner_)
        throw std::invalid_argument("retirement panel requires its parent refresh");
    setObjectName("EveRetirementPanel");
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    auto hint =
        new ElaText(QString::fromUtf8("高级回收只尝试已退出业务的原生代次；调用方须先释放自己的资源。"), 14, this);
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto row = new QHBoxLayout;
    generations_ = new ElaComboBox(this);
    generations_->setObjectName("EveRetirementGeneration");
    generations_->setAccessibleName(QString::fromUtf8("模块代次"));
    generations_->addItem(QString::fromUtf8("请选择代次"), QVariant::fromValue<qulonglong>(0));
    row->addWidget(generations_, 1);
    retry_ = new ElaPushButton(QString::fromUtf8("尝试物理回收"), this);
    retry_->setObjectName("EveRetirementRetry");
    retry_->setEnabled(false);
    row->addWidget(retry_);
    layout->addLayout(row);
    status_ = new ElaText(this);
    status_->setObjectName("EveRetirementStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    details_ = new ElaPlainTextEdit(this);
    details_->setObjectName("EveModuleDiagnosticsText");
    details_->setReadOnly(true);
    layout->addWidget(details_, 1);
    QObject::connect(generations_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     [this](int) { selectionChanged(); });
    QObject::connect(retry_, &QPushButton::clicked, this, [this] { retry(); });
}

void RetirementPanel::selectionChanged()
{
    const auto workspace = workspace_.lock();
    if (!workspace)
        return;
    (void)workspace->withModuleView([this] {
        selected_ = selectedGeneration(*generations_);
        retry_->setEnabled(false);
        if (!acting_ && !rendering_) {
            const auto refresh = refreshOwner_;
            refresh();
        }
    });
}

void RetirementPanel::refresh(const WorkspaceModulesSnapshot& modules, const WorkspaceUpgradeSnapshot& upgrade)
{
    if (rendering_)
        return;
    QScopedValueRollback<bool> rendering(rendering_, true);
    const auto text = detail::moduleDiagnosticsText(modules);
    if (details_->toPlainText() != text)
        details_->setPlainText(text);
    // 不重建选择器，保留真实 GenerationId；目录淘汰不能把选择静默移到另一代。
    for (const auto& row : modules.generations) {
        const auto generationData = QVariant::fromValue<qulonglong>(row.generation.value());
        const auto index = generations_->findData(generationData);
        const auto label = generationText(row);
        if (index < 0)
            generations_->addItem(label, generationData);
        else if (generations_->itemText(index) != label)
            generations_->setItemText(index, label);
    }
    for (int index = generations_->count() - 1; index > 0; --index) {
        const auto id = eve::GenerationId(generations_->itemData(index).toULongLong());
        const bool present = std::any_of(modules.generations.begin(), modules.generations.end(),
                                         [&](const auto& row) { return row.generation == id; });
        if (present)
            continue;
        if (id == selected_) {
            const auto label = QString::fromUtf8("#%1 · 已不在当前目录").arg(id.value());
            if (generations_->itemText(index) != label)
                generations_->setItemText(index, label);
        } else
            generations_->removeItem(index);
    }
    const auto target = detail::retirementTarget(modules, selected_);
    status_->setText(
        detail::retirementStatusText(modules, selected_, target, attempted_, lastAttempt_ ? &*lastAttempt_ : nullptr));
    // 呈现信号可改选另一代；按钮根据最终真实选择核范围，不复用旧行权限。
    retry_->setEnabled(modules.open && !upgrade.active && selectedGeneration(*generations_) == selected_ &&
                       detail::retirementTarget(modules, selected_).hasValue());
}

void RetirementPanel::retry()
{
    const auto workspace = workspace_.lock();
    if (!workspace)
        return;
    (void)workspace->withModuleView([this, workspace] {
        if (acting_ || rendering_)
            return;
        {
            QScopedValueRollback<bool> acting(acting_, true);
            const auto expected = selected_;
            if (expected.valid() && selectedGeneration(*generations_) == expected) {
                attempted_ = expected;
                lastAttempt_.emplace(workspace->retryRetirement(expected));
            } else
                selected_ = selectedGeneration(*generations_);
        }
        const auto refresh = refreshOwner_;
        refresh();
    });
}
} // namespace notebook

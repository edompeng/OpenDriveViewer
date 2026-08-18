#include "src/ui/widgets/shortcut_settings_dialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QVBoxLayout>

#include "src/ui/widgets/subwindow_style.h"

namespace geoviewer::ui {

ShortcutSettingsDialog::ShortcutSettingsDialog(ShortcutManager* manager,
                                               QWidget* parent)
    : QDialog(parent), manager_(manager) {
  setWindowTitle(tr("Keyboard Shortcuts"));
  resize(680, 520);
  ApplySubwindowStyle(this);

  auto* layout = new QVBoxLayout(this);
  auto* description = new QLabel(
      tr("Empty shortcuts are disabled. Select a shortcut and press Clear to "
         "remove it."),
      this);
  description->setWordWrap(true);
  layout->addWidget(description);

  table_ = new QTableWidget(this);
  table_->setColumnCount(4);
  table_->setHorizontalHeaderLabels(
      {tr("Category"), tr("Command"), tr("Shortcut"), QString()});
  table_->verticalHeader()->hide();
  table_->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(
      3, QHeaderView::ResizeToContents);
  table_->setSelectionMode(QAbstractItemView::NoSelection);
  layout->addWidget(table_, 1);

  validation_label_ = new QLabel(this);
  validation_label_->setStyleSheet("color: #ff6b6b;");
  layout->addWidget(validation_label_);

  auto* footer = new QHBoxLayout();
  auto* restore_button = new QPushButton(tr("Restore Defaults"), this);
  connect(restore_button, &QPushButton::clicked, this,
          &ShortcutSettingsDialog::RestoreDefaults);
  footer->addWidget(restore_button);
  footer->addStretch();

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
      this);
  apply_button_ = buttons->button(QDialogButtonBox::Apply);
  connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked, this,
          [this]() {
            if (ApplyChanges()) accept();
          });
  connect(buttons, &QDialogButtonBox::rejected, this,
          &ShortcutSettingsDialog::reject);
  connect(apply_button_, &QPushButton::clicked, this,
          [this]() { ApplyChanges(); });
  footer->addWidget(buttons);
  layout->addLayout(footer);

  BuildTable();
  UpdateValidation();
}

QString ShortcutSettingsDialog::CategoryText(ShortcutCategory category) const {
  switch (category) {
    case ShortcutCategory::kFile:
      return tr("File");
    case ShortcutCategory::kPanels:
      return tr("Panels");
    case ShortcutCategory::kView:
      return tr("View");
    case ShortcutCategory::kTools:
      return tr("Tools");
    case ShortcutCategory::kSettings:
      return tr("Settings");
  }
  return QString();
}

void ShortcutSettingsDialog::BuildTable() {
  if (!manager_) return;
  table_->setRowCount(manager_->Entries().size());
  const QMap<QString, QKeySequence> sequences = manager_->CurrentSequences();
  int row = 0;
  for (const ShortcutEntry& entry : manager_->Entries()) {
    auto* category_item = new QTableWidgetItem(CategoryText(entry.category));
    auto* command_item =
        new QTableWidgetItem(entry.action->text().remove(QLatin1Char('&')));
    category_item->setFlags(category_item->flags() & ~Qt::ItemIsEditable);
    command_item->setFlags(command_item->flags() & ~Qt::ItemIsEditable);
    table_->setItem(row, 0, category_item);
    table_->setItem(row, 1, command_item);

    auto* editor = new QKeySequenceEdit(sequences.value(entry.id), table_);
    editor->setMaximumSequenceLength(1);
    editors_.insert(entry.id, editor);
    table_->setCellWidget(row, 2, editor);
    connect(editor, &QKeySequenceEdit::keySequenceChanged, this,
            [this](const QKeySequence&) { UpdateValidation(); });

    auto* clear_button = new QPushButton(tr("Clear"), table_);
    table_->setCellWidget(row, 3, clear_button);
    connect(clear_button, &QPushButton::clicked, this,
            [this, row]() { ClearSequence(row); });
    ++row;
  }
}

QMap<QString, QKeySequence> ShortcutSettingsDialog::EditedSequences() const {
  QMap<QString, QKeySequence> result;
  for (auto it = editors_.cbegin(); it != editors_.cend(); ++it) {
    result.insert(it.key(), it.value()->keySequence());
  }
  return result;
}

void ShortcutSettingsDialog::ClearSequence(int row) {
  if (!manager_ || row < 0 || row >= manager_->Entries().size()) return;
  const QString id = manager_->Entries().at(row).id;
  if (editors_.contains(id)) editors_.value(id)->clear();
}

void ShortcutSettingsDialog::RestoreDefaults() {
  if (!manager_) return;
  const auto defaults = manager_->DefaultSequences();
  for (auto it = editors_.begin(); it != editors_.end(); ++it) {
    it.value()->setKeySequence(defaults.value(it.key()));
  }
  UpdateValidation();
}

bool ShortcutSettingsDialog::ApplyChanges() {
  if (!manager_) return false;
  QStringList conflicts;
  if (!manager_->ApplySequences(EditedSequences(), &conflicts)) {
    UpdateValidation();
    return false;
  }
  emit ShortcutsApplied();
  apply_button_->setEnabled(false);
  return true;
}

void ShortcutSettingsDialog::UpdateValidation() {
  if (!manager_) return;
  const QStringList conflicts = manager_->FindConflicts(EditedSequences());
  if (conflicts.isEmpty()) {
    validation_label_->clear();
    apply_button_->setEnabled(true);
    return;
  }
  validation_label_->setText(
      tr("Conflicting shortcuts: %1").arg(conflicts.join(", ")));
  apply_button_->setEnabled(false);
}

}  // namespace geoviewer::ui

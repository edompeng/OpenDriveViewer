#pragma once

#include <QDialog>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QTableWidget>

#include "src/ui/widgets/shortcut_manager.h"

namespace geoviewer::ui {

class ShortcutSettingsDialog : public QDialog {
  Q_OBJECT

 public:
  explicit ShortcutSettingsDialog(ShortcutManager* manager,
                                  QWidget* parent = nullptr);

 signals:
  void ShortcutsApplied();

 private:
  QString CategoryText(ShortcutCategory category) const;
  QMap<QString, QKeySequence> EditedSequences() const;
  void BuildTable();
  void ClearSequence(int row);
  void RestoreDefaults();
  bool ApplyChanges();
  void UpdateValidation();

  ShortcutManager* manager_ = nullptr;
  QTableWidget* table_ = nullptr;
  QLabel* validation_label_ = nullptr;
  QPushButton* apply_button_ = nullptr;
  QMap<QString, QKeySequenceEdit*> editors_;
};

}  // namespace geoviewer::ui

#pragma once

#include <QAction>
#include <QKeySequence>
#include <QMap>
#include <QString>
#include <QVector>

#include <map>
#include <string>

namespace geoviewer::ui {

enum class ShortcutCategory { kFile, kPanels, kView, kTools, kSettings };

struct ShortcutEntry {
  QString id;
  ShortcutCategory category = ShortcutCategory::kTools;
  QAction* action = nullptr;
  QKeySequence default_sequence;
};

class ShortcutManager {
 public:
  void RegisterAction(const QString& id, ShortcutCategory category,
                      QAction* action,
                      const QKeySequence& default_sequence = QKeySequence());

  const QVector<ShortcutEntry>& Entries() const { return entries_; }
  QMap<QString, QKeySequence> CurrentSequences() const;
  QMap<QString, QKeySequence> DefaultSequences() const;
  QStringList FindConflicts(const QMap<QString, QKeySequence>& sequences) const;
  bool ApplySequences(const QMap<QString, QKeySequence>& sequences,
                      QStringList* conflicts = nullptr);
  void Load(const std::map<std::string, std::string>& shortcuts);
  std::map<std::string, std::string> Save() const;

 private:
  QVector<ShortcutEntry> entries_;
};

}  // namespace geoviewer::ui

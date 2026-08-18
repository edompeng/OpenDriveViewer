#include "src/ui/widgets/shortcut_manager.h"

#include <QSet>

namespace geoviewer::ui {

void ShortcutManager::RegisterAction(const QString& id,
                                     ShortcutCategory category, QAction* action,
                                     const QKeySequence& default_sequence) {
  if (!action || id.isEmpty()) return;
  action->setShortcutContext(Qt::ApplicationShortcut);
  action->setShortcut(default_sequence);
  entries_.append({id, category, action, default_sequence});
}

QMap<QString, QKeySequence> ShortcutManager::CurrentSequences() const {
  QMap<QString, QKeySequence> result;
  for (const ShortcutEntry& entry : entries_) {
    result.insert(entry.id, entry.action->shortcut());
  }
  return result;
}

QMap<QString, QKeySequence> ShortcutManager::DefaultSequences() const {
  QMap<QString, QKeySequence> result;
  for (const ShortcutEntry& entry : entries_) {
    result.insert(entry.id, entry.default_sequence);
  }
  return result;
}

QStringList ShortcutManager::FindConflicts(
    const QMap<QString, QKeySequence>& sequences) const {
  QStringList conflicts;
  const QList<QString> ids = sequences.keys();
  for (int i = 0; i < ids.size(); ++i) {
    const QKeySequence lhs = sequences.value(ids[i]);
    if (lhs.isEmpty()) continue;
    for (int j = i + 1; j < ids.size(); ++j) {
      const QKeySequence rhs = sequences.value(ids[j]);
      if (rhs.isEmpty()) continue;
      if (lhs.matches(rhs) != QKeySequence::NoMatch ||
          rhs.matches(lhs) != QKeySequence::NoMatch) {
        conflicts.append(ids[i]);
        conflicts.append(ids[j]);
      }
    }
  }
  conflicts.removeDuplicates();
  return conflicts;
}

bool ShortcutManager::ApplySequences(
    const QMap<QString, QKeySequence>& sequences, QStringList* conflicts) {
  const QStringList found = FindConflicts(sequences);
  if (conflicts) *conflicts = found;
  if (!found.isEmpty()) return false;

  for (const ShortcutEntry& entry : entries_) {
    entry.action->setShortcut(sequences.value(entry.id));
  }
  return true;
}

void ShortcutManager::Load(
    const std::map<std::string, std::string>& shortcuts) {
  QMap<QString, QKeySequence> sequences = DefaultSequences();
  for (const ShortcutEntry& entry : entries_) {
    const auto it = shortcuts.find(entry.id.toStdString());
    if (it == shortcuts.end()) continue;
    sequences[entry.id] = QKeySequence::fromString(
        QString::fromStdString(it->second), QKeySequence::PortableText);
  }

  QStringList conflicts;
  if (!ApplySequences(sequences, &conflicts)) {
    ApplySequences(DefaultSequences());
  }
}

std::map<std::string, std::string> ShortcutManager::Save() const {
  std::map<std::string, std::string> result;
  for (const ShortcutEntry& entry : entries_) {
    result[entry.id.toStdString()] = entry.action->shortcut()
                                         .toString(QKeySequence::PortableText)
                                         .toStdString();
  }
  return result;
}

}  // namespace geoviewer::ui

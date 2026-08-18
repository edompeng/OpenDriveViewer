#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>

#include <memory>

#include "src/ui/widgets/shortcut_manager.h"

namespace geoviewer::ui {

class ShortcutManagerTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    static int argc = 1;
    static char app_name[] = "shortcut_manager_test";
    static char* argv[] = {app_name, nullptr};
    if (!QApplication::instance()) {
      application_ = std::make_unique<QApplication>(argc, argv);
    }
  }

  static void TearDownTestSuite() { application_.reset(); }

 private:
  static std::unique_ptr<QApplication> application_;
};

std::unique_ptr<QApplication> ShortcutManagerTest::application_;

TEST_F(ShortcutManagerTest, DefaultsAreEmptyAndCanBeCleared) {
  QAction open_action(nullptr);
  QAction panel_action(nullptr);
  ShortcutManager manager;
  manager.RegisterAction("open_map", ShortcutCategory::kFile, &open_action);
  manager.RegisterAction("toggle_panel", ShortcutCategory::kPanels,
                         &panel_action);

  EXPECT_TRUE(open_action.shortcut().isEmpty());
  EXPECT_TRUE(panel_action.shortcut().isEmpty());

  QMap<QString, QKeySequence> edited = manager.CurrentSequences();
  edited["open_map"] = QKeySequence("Ctrl+O");
  EXPECT_TRUE(manager.ApplySequences(edited));
  EXPECT_FALSE(open_action.shortcut().isEmpty());

  edited["open_map"] = QKeySequence();
  EXPECT_TRUE(manager.ApplySequences(edited));
  EXPECT_TRUE(open_action.shortcut().isEmpty());
}

TEST_F(ShortcutManagerTest, RejectsDuplicateShortcutsWithoutPartialApply) {
  QAction first_action(nullptr);
  QAction second_action(nullptr);
  ShortcutManager manager;
  manager.RegisterAction("first", ShortcutCategory::kTools, &first_action);
  manager.RegisterAction("second", ShortcutCategory::kTools, &second_action);

  QMap<QString, QKeySequence> edited;
  edited["first"] = QKeySequence("Ctrl+K");
  edited["second"] = QKeySequence("Ctrl+K");
  QStringList conflicts;
  EXPECT_FALSE(manager.ApplySequences(edited, &conflicts));
  EXPECT_EQ(conflicts.size(), 2);
  EXPECT_TRUE(first_action.shortcut().isEmpty());
  EXPECT_TRUE(second_action.shortcut().isEmpty());
}

TEST_F(ShortcutManagerTest, RoundTripsPortableSequencesIncludingEmptyValues) {
  QAction open_action(nullptr);
  QAction panel_action(nullptr);
  ShortcutManager manager;
  manager.RegisterAction("open_map", ShortcutCategory::kFile, &open_action);
  manager.RegisterAction("toggle_panel", ShortcutCategory::kPanels,
                         &panel_action);

  manager.Load({{"open_map", "Ctrl+O"}, {"toggle_panel", ""}});
  EXPECT_EQ(open_action.shortcut(), QKeySequence("Ctrl+O"));
  EXPECT_TRUE(panel_action.shortcut().isEmpty());

  const auto saved = manager.Save();
  EXPECT_EQ(saved.at("open_map"), "Ctrl+O");
  EXPECT_EQ(saved.at("toggle_panel"), "");
}

}  // namespace geoviewer::ui

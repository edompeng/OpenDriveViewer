#pragma once

#include <QDialog>
#include <QTreeWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QString>
#include <string>
#include "pugixml/pugixml.hpp"
#include "src/ui/widgets/xml_syntax_highlighter.h"
#include "src/ui/widgets/xml_editor_types.h"

namespace geoviewer::ui {

class XmlEditorDialog : public QDialog {
  Q_OBJECT

 public:
  explicit XmlEditorDialog(QWidget* parent = nullptr);
  ~XmlEditorDialog() override = default;

  void SetXml(const QString& xml_text, const XmlTarget& target);
  XmlTarget GetTarget() const { return target_; }

 signals:
  void XmlSaved(const XmlTarget& target, const QString& xml_text);

 private slots:
  void HandleSave();
  void HandleTreeItemClicked(QTreeWidgetItem* item, int column);

 private:
  void PopulateTree(const pugi::xml_node& node, QTreeWidgetItem* parent_item);
  void HighlightNodeInText(const QString& tag_name, const QString& id_attr);

  QTreeWidget* tree_widget_ = nullptr;
  QTextEdit* text_edit_ = nullptr;
  XmlSyntaxHighlighter* highlighter_ = nullptr;
  QPushButton* save_button_ = nullptr;
  QPushButton* close_button_ = nullptr;
  QLabel* status_label_ = nullptr;

  XmlTarget target_;
  QString current_xml_;
};

}  // namespace geoviewer::ui

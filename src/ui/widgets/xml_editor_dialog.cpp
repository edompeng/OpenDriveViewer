#include "src/ui/widgets/xml_editor_dialog.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTextCursor>
#include <QVBoxLayout>

#include "src/ui/widgets/subwindow_style.h"

namespace geoviewer::ui {

XmlEditorDialog::XmlEditorDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("XML Element Editor"));
  resize(900, 650);

  // Set non-modal behavior by default, but allow dragging/resizing
  setWindowFlags(windowFlags() | Qt::Window);
  ApplySubwindowStyle(this);

  QVBoxLayout* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(10, 10, 10, 10);

  QSplitter* splitter = new QSplitter(Qt::Horizontal, this);

  // Left outline tree view
  tree_widget_ = new QTreeWidget(this);
  tree_widget_->setHeaderLabel(tr("XML Outline"));
  tree_widget_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  splitter->addWidget(tree_widget_);

  // Right text editor
  text_edit_ = new QTextEdit(this);
  text_edit_->setFontFamily("Courier New");
  text_edit_->setFontPointSize(10);
  text_edit_->setAcceptRichText(false);
  highlighter_ = new XmlSyntaxHighlighter(text_edit_->document());
  splitter->addWidget(text_edit_);

  // Set initial splitter sizes
  splitter->setSizes(QList<int>({250, 650}));
  main_layout->addWidget(splitter);

  // Bottom action bar
  QHBoxLayout* bottom_layout = new QHBoxLayout();
  status_label_ = new QLabel(this);
  status_label_->setStyleSheet("color: #66cc66; font-weight: bold;");
  bottom_layout->addWidget(status_label_);
  bottom_layout->addStretch();

  save_button_ = new QPushButton(tr("Save"), this);
  close_button_ = new QPushButton(tr("Close"), this);
  bottom_layout->addWidget(save_button_);
  bottom_layout->addWidget(close_button_);

  main_layout->addLayout(bottom_layout);

  // Connect actions
  connect(save_button_, &QPushButton::clicked, this,
          &XmlEditorDialog::HandleSave);
  connect(close_button_, &QPushButton::clicked, this, &XmlEditorDialog::reject);
  connect(tree_widget_, &QTreeWidget::itemClicked, this,
          &XmlEditorDialog::HandleTreeItemClicked);
}

void XmlEditorDialog::SetXml(const QString& xml_text, const XmlTarget& target) {
  target_ = target;
  current_xml_ = xml_text;

  // Set text in editor
  text_edit_->setPlainText(xml_text);

  // Update window title based on target
  QString target_info;
  switch (target_.type) {
    case XmlTargetType::kRoad:
      target_info =
          QString("Road ID: %1").arg(QString::fromStdString(target_.road_id));
      break;
    case XmlTargetType::kLane:
      target_info = QString("Lane: Road %1 / s0 %2 / ID %3")
                        .arg(QString::fromStdString(target_.road_id))
                        .arg(target_.lane_s0)
                        .arg(target_.lane_id);
      break;
    case XmlTargetType::kJunction:
      target_info = QString("Junction ID: %1")
                        .arg(QString::fromStdString(target_.element_id));
      break;
    case XmlTargetType::kObject:
      target_info = QString("Object ID: %1 (Road: %2)")
                        .arg(QString::fromStdString(target_.element_id))
                        .arg(QString::fromStdString(target_.road_id));
      break;
    case XmlTargetType::kSignal:
      target_info = QString("Signal ID: %1 (Road: %2)")
                        .arg(QString::fromStdString(target_.element_id))
                        .arg(QString::fromStdString(target_.road_id));
      break;
  }
  setWindowTitle(
      QString("%1 - %2").arg(tr("XML Element Editor")).arg(target_info));

  // Clear tree and rebuild it
  tree_widget_->clear();
  status_label_->clear();

  pugi::xml_document doc;
  pugi::xml_parse_result result =
      doc.load_string(xml_text.toStdString().c_str());
  if (result) {
    PopulateTree(doc.first_child(), nullptr);
    tree_widget_->expandAll();
  }
}

void XmlEditorDialog::PopulateTree(const pugi::xml_node& node,
                                   QTreeWidgetItem* parent_item) {
  if (node.type() != pugi::node_element) return;

  QTreeWidgetItem* item = nullptr;
  if (parent_item) {
    item = new QTreeWidgetItem(parent_item);
  } else {
    item = new QTreeWidgetItem(tree_widget_);
  }

  // Format node label
  QString node_name = QString::fromStdString(node.name());
  QString id_val = QString::fromStdString(node.attribute("id").value());
  QString s_val = QString::fromStdString(node.attribute("s").value());

  QString label = node_name;
  if (!id_val.isEmpty()) {
    label += QString(" [id=\"%1\"]").arg(id_val);
  } else if (!s_val.isEmpty()) {
    label += QString(" [s=\"%1\"]").arg(s_val);
  }

  item->setText(0, label);
  item->setData(0, Qt::UserRole, node_name);
  item->setData(0, Qt::UserRole + 1, id_val);

  // Add child elements
  for (pugi::xml_node child : node.children()) {
    PopulateTree(child, item);
  }
}

void XmlEditorDialog::HandleTreeItemClicked(QTreeWidgetItem* item, int column) {
  Q_UNUSED(column);
  if (!item) return;

  QString tag_name = item->data(0, Qt::UserRole).toString();
  QString id_attr = item->data(0, Qt::UserRole + 1).toString();

  HighlightNodeInText(tag_name, id_attr);
}

void XmlEditorDialog::HighlightNodeInText(const QString& tag_name,
                                          const QString& id_attr) {
  if (tag_name.isEmpty()) return;

  QTextDocument* doc = text_edit_->document();
  QTextCursor cursor(doc);

  // Construct a search pattern.
  // We can search for "<tag_name" or "<tag_name ... id="id_attr""
  QRegularExpression regex;
  if (!id_attr.isEmpty()) {
    regex = QRegularExpression(QString("<%1\\b[^>]*\\bid\\s*=\\s*[\"']%2[\"']")
                                   .arg(tag_name)
                                   .arg(QRegularExpression::escape(id_attr)));
  } else {
    regex = QRegularExpression(QString("<%1\\b").arg(tag_name));
  }

  QRegularExpressionMatch match = regex.match(doc->toPlainText());
  if (match.hasMatch()) {
    cursor.setPosition(match.capturedStart());
    cursor.setPosition(match.capturedEnd(), QTextCursor::KeepAnchor);
    text_edit_->setTextCursor(cursor);
    text_edit_->ensureCursorVisible();
  }
}

void XmlEditorDialog::HandleSave() {
  QString xml_text = text_edit_->toPlainText();

  // Validate XML syntax
  pugi::xml_document doc;
  pugi::xml_parse_result result =
      doc.load_string(xml_text.toStdString().c_str());
  if (!result) {
    QMessageBox::critical(this, tr("XML Validation Error"),
                          tr("Invalid XML format:\n%1\nOffset: %2")
                              .arg(result.description())
                              .arg(result.offset));
    return;
  }

  // Ensure root node matches target element name
  pugi::xml_node root = doc.first_child();
  QString root_name = QString::fromStdString(root.name());
  QString expected_root;

  switch (target_.type) {
    case XmlTargetType::kRoad:
      expected_root = "road";
      break;
    case XmlTargetType::kLane:
      expected_root = "lane";
      break;
    case XmlTargetType::kJunction:
      expected_root = "junction";
      break;
    case XmlTargetType::kObject:
      expected_root = "object";
      break;
    case XmlTargetType::kSignal:
      expected_root = "signal";
      break;
  }

  if (root_name != expected_root) {
    QMessageBox::critical(this, tr("XML Validation Error"),
                          tr("Root element must be '%1', but found '%2'.")
                              .arg(expected_root)
                              .arg(root_name));
    return;
  }

  // Check if ID changed (if applicable) and matches expected target ID
  if (target_.type == XmlTargetType::kRoad ||
      target_.type == XmlTargetType::kJunction) {
    QString root_id = QString::fromStdString(root.attribute("id").value());
    QString expected_id = QString::fromStdString(
        target_.type == XmlTargetType::kRoad ? target_.road_id
                                             : target_.element_id);
    if (root_id != expected_id) {
      auto response = QMessageBox::warning(
          this, tr("XML Validation Warning"),
          tr("You changed the element ID from '%1' to '%2'. Are you sure?")
              .arg(expected_id)
              .arg(root_id),
          QMessageBox::Yes | QMessageBox::No);
      if (response == QMessageBox::No) {
        return;
      }
    }
  }

  // Update outline tree
  tree_widget_->clear();
  PopulateTree(root, nullptr);
  tree_widget_->expandAll();

  // Save successful
  current_xml_ = xml_text;
  status_label_->setText(tr("XML saved and updated successfully!"));
  emit XmlSaved(target_, xml_text);
}

}  // namespace geoviewer::ui

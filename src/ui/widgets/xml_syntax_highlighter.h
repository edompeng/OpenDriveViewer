#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QVector>

namespace geoviewer::ui {

class XmlSyntaxHighlighter : public QSyntaxHighlighter {
  Q_OBJECT

 public:
  explicit XmlSyntaxHighlighter(QTextDocument* parent = nullptr);

 protected:
  void highlightBlock(const QString& text) override;

 private:
  struct HighlightingRule {
    QRegularExpression pattern;
    QTextCharFormat format;
  };

  QVector<HighlightingRule> highlighting_rules_;

  QTextCharFormat tag_format_;
  QTextCharFormat attribute_format_;
  QTextCharFormat attribute_value_format_;
  QTextCharFormat comment_format_;
  QTextCharFormat text_format_;
};

}  // namespace geoviewer::ui

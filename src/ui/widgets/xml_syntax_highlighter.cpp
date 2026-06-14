#include "src/ui/widgets/xml_syntax_highlighter.h"

namespace geoviewer::ui {

XmlSyntaxHighlighter::XmlSyntaxHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent) {
  // Define text formats
  tag_format_.setForeground(QColor("#0066cc"));
  tag_format_.setFontWeight(QFont::Bold);

  attribute_format_.setForeground(QColor("#b200b2"));
  attribute_format_.setFontItalic(true);

  attribute_value_format_.setForeground(QColor("#990000"));

  comment_format_.setForeground(QColor("#808080"));
  comment_format_.setFontItalic(true);

  HighlightingRule rule;

  // Rule for XML Tags: <tag, </tag, >
  rule.pattern = QRegularExpression(QStringLiteral("(<\\/?[a-zA-Z_:][a-zA-Z0-9._:-]*)|(>|\\/>)"));
  rule.format = tag_format_;
  highlighting_rules_.append(rule);

  // Rule for XML Attributes: attr=
  rule.pattern = QRegularExpression(QStringLiteral("\\b[a-zA-Z0-9._:-]+(?=\\s*=)"));
  rule.format = attribute_format_;
  highlighting_rules_.append(rule);

  // Rule for Attribute Values (Quoted strings)
  rule.pattern = QRegularExpression(QStringLiteral("(\"[^\"]*\")|('[^']*')"));
  rule.format = attribute_value_format_;
  highlighting_rules_.append(rule);
}

void XmlSyntaxHighlighter::highlightBlock(const QString& text) {
  // 1. Highlight tags, attributes, and strings
  for (const HighlightingRule& rule : highlighting_rules_) {
    QRegularExpressionMatchIterator match_iterator =
        rule.pattern.globalMatch(text);
    while (match_iterator.hasNext()) {
      QRegularExpressionMatch match = match_iterator.next();
      setFormat(match.capturedStart(), match.capturedLength(), rule.format);
    }
  }

  // 2. Multi-line comment highlighting
  setCurrentBlockState(0);
  int start_index = 0;
  if (previousBlockState() != 1) {
    start_index = text.indexOf(QRegularExpression(QStringLiteral("<!--")));
  }

  while (start_index >= 0) {
    QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("-->")).match(text, start_index);
    int end_index = match.capturedStart();
    int comment_length = 0;

    if (end_index == -1) {
      setCurrentBlockState(1);
      comment_length = text.length() - start_index;
    } else {
      comment_length = end_index - start_index + match.capturedLength();
    }

    setFormat(start_index, comment_length, comment_format_);
    start_index = text.indexOf(QRegularExpression(QStringLiteral("<!--")),
                               start_index + comment_length);
  }
}

}  // namespace geoviewer::ui

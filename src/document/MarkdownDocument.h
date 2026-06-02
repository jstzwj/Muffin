#pragma once

#include "document/NodeIndex.h"

#include <QObject>

namespace muffin {

class MarkdownDocument : public QObject {
  Q_OBJECT

public:
  explicit MarkdownDocument(QObject* parent = nullptr);

  MarkdownNode& root();
  const MarkdownNode& root() const;

  NodeIndex& index();
  const NodeIndex& index() const;

  const QString& markdownText() const;
  void setMarkdownText(QString text, std::unique_ptr<MarkdownNode> root);
  void replaceTopLevelRange(
      qsizetype first,
      qsizetype count,
      std::vector<std::unique_ptr<MarkdownNode>> replacements,
      qsizetype sourceStart,
      qsizetype sourceEnd,
      const QString& replacementText);

  quint64 revision() const;
  bool isModified() const;
  void setModified(bool modified);

  MarkdownNode* node(NodeId id) const;
  void replaceRoot(std::unique_ptr<MarkdownNode> root);

signals:
  void documentReset();
  void modifiedChanged(bool modified);

private:
  QString markdownText_;
  std::unique_ptr<MarkdownNode> root_;
  NodeIndex index_;
  quint64 revision_ = 0;
  bool modified_ = false;
};

}  // namespace muffin

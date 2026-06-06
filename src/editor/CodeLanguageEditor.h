#pragma once

#include "document/NodeId.h"

#include <QObject>
#include <QStringList>

class QCompleter;
class QLineEdit;
class QWidget;

namespace muffin {

class BlockLayout;
struct CursorPosition;
class DocumentLayout;
struct HitTestResult;
class MarkdownDocument;

class CodeLanguageEditor : public QObject {
  Q_OBJECT

public:
  explicit CodeLanguageEditor(QWidget* viewport, QObject* parent = nullptr);

  void setSuggestions(QStringList languages);
  void update(const CursorPosition& cursor, const HitTestResult& hit,
              const DocumentLayout* layout, const MarkdownDocument* doc,
              qreal scrollY, int viewportHeight);
  void forceHide();

signals:
  void languageCommitted(NodeId codeId, QString language);

private:
  void ensureEditor();
  void show(const BlockLayout& block, const MarkdownDocument& doc,
            qreal scrollY, int viewportHeight);

  QWidget* viewport_;
  QLineEdit* editor_ = nullptr;
  QCompleter* completer_ = nullptr;
  QStringList suggestions_;
  NodeId nodeId_;
  bool updating_ = false;
};

}  // namespace muffin

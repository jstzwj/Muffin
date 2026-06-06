#include "editor/CodeLanguageEditor.h"

#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "editor/CursorPosition.h"
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"

#include <QCompleter>
#include <QGraphicsDropShadowEffect>
#include <QLineEdit>
#include <QListView>
#include <QStringListModel>

namespace muffin {

CodeLanguageEditor::CodeLanguageEditor(QWidget* viewport, QObject* parent)
    : QObject(parent), viewport_(viewport) {}

void CodeLanguageEditor::setSuggestions(QStringList languages) {
  languages.removeDuplicates();
  languages.sort(Qt::CaseInsensitive);
  suggestions_ = std::move(languages);
  if (completer_) {
    completer_->setModel(new QStringListModel(suggestions_, completer_));
  }
}

void CodeLanguageEditor::update(const CursorPosition& cursor, const HitTestResult& hit,
                                const DocumentLayout* layout, const MarkdownDocument* doc,
                                qreal scrollY, int viewportHeight) {
  if (updating_) {
    return;
  }

  if (!layout || !doc || !cursor.isValid() || cursor.blockId != hit.blockId ||
      hit.zone != HitTestResult::Zone::Code) {
    forceHide();
    return;
  }

  const BlockLayout* block = layout->block(cursor.blockId);
  if (!block || block->type() != BlockType::CodeFence) {
    forceHide();
    return;
  }
  show(*block, *doc, scrollY, viewportHeight);
}

void CodeLanguageEditor::forceHide() {
  nodeId_ = {};
  if (editor_) {
    editor_->hide();
  }
}

void CodeLanguageEditor::ensureEditor() {
  if (editor_) {
    return;
  }

  editor_ = new QLineEdit(viewport_);
  editor_->setObjectName(QStringLiteral("codeLanguageEditor"));
  editor_->setPlaceholderText(QStringLiteral("text"));
  editor_->setClearButtonEnabled(false);
  editor_->setFrame(false);
  editor_->setFixedWidth(116);
  editor_->hide();
  editor_->setStyleSheet(QStringLiteral(
      "QLineEdit#codeLanguageEditor {"
      "  background:rgba(255,255,255,235);"
      "  color:#222222;"
      "  border:1px solid #d7dce2;"
      "  border-radius:4px;"
      "  padding:2px 8px;"
      "  selection-background-color:#2f80ed;"
      "  selection-color:#ffffff;"
      "  font-size:12px;"
      "}"));
  auto* shadow = new QGraphicsDropShadowEffect(editor_);
  shadow->setBlurRadius(14.0);
  shadow->setOffset(0.0, 3.0);
  shadow->setColor(QColor(15, 23, 42, 35));
  editor_->setGraphicsEffect(shadow);

  completer_ = new QCompleter(suggestions_, editor_);
  completer_->setCaseSensitivity(Qt::CaseInsensitive);
  completer_->setFilterMode(Qt::MatchContains);
  completer_->setCompletionMode(QCompleter::PopupCompletion);
  auto* popup = new QListView(editor_);
  popup->setObjectName(QStringLiteral("codeLanguagePopup"));
  popup->setUniformItemSizes(true);
  popup->setAlternatingRowColors(false);
  popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  popup->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  popup->setStyleSheet(QStringLiteral(
      "QListView#codeLanguagePopup {"
      "  background:#ffffff;"
      "  color:#333333;"
      "  border:1px solid #e1e4e8;"
      "  border-radius:5px;"
      "  padding:6px 0;"
      "  outline:0;"
      "  font-size:13px;"
      "}"
      "QListView#codeLanguagePopup::item {"
      "  min-height:28px;"
      "  padding:4px 12px;"
      "}"
      "QListView#codeLanguagePopup::item:selected {"
      "  background:#f1f6ff;"
      "  color:#111111;"
      "}"));
  completer_->setPopup(popup);
  editor_->setCompleter(completer_);

  connect(editor_, &QLineEdit::editingFinished, this, [this] {
    if (updating_ || !editor_ || !editor_->isVisible() || !nodeId_.isValid()) {
      return;
    }
    QString language = editor_->text().trimmed();
    if (language == QStringLiteral("text")) {
      language.clear();
    }
    emit languageCommitted(nodeId_, language);
  });
  connect(editor_, &QLineEdit::returnPressed, this, [this] {
    if (updating_ || !editor_ || !editor_->isVisible() || !nodeId_.isValid()) {
      return;
    }
    QString language = editor_->text().trimmed();
    if (language == QStringLiteral("text")) {
      language.clear();
    }
    emit languageCommitted(nodeId_, language);
  });
  connect(editor_, &QLineEdit::textEdited, this, [this] {
    if (completer_) {
      QRect rect = editor_->rect();
      rect.setWidth(140);
      completer_->complete(rect);
    }
  });
}

void CodeLanguageEditor::show(const BlockLayout& block, const MarkdownDocument& doc,
                              qreal scrollY, int viewportHeight) {
  ensureEditor();
  if (!editor_) {
    return;
  }

  const MarkdownNode* node = doc.node(block.nodeId());
  if (!node || node->type() != BlockType::CodeFence) {
    forceHide();
    return;
  }

  updating_ = true;
  nodeId_ = block.nodeId();
  if (!editor_->hasFocus()) {
    const QString language = node->codeLanguage().isEmpty() ? QStringLiteral("text") : node->codeLanguage();
    if (editor_->text() != language) {
      editor_->setText(language);
    }
    editor_->deselect();
    editor_->setCursorPosition(editor_->text().size());
  }

  const QRectF blockRect = block.rect().translated(0, -scrollY);
  editor_->resize(116, 26);
  const int margin = 10;
  const int x = qRound(blockRect.right()) - editor_->width() - margin;
  int y = qRound(blockRect.bottom()) - editor_->height() / 2;
  if (y + editor_->height() + margin > viewportHeight) {
    y = qRound(blockRect.bottom()) - editor_->height() - margin;
  }
  editor_->move(qMax(0, x), qMax(0, y));
  editor_->show();
  editor_->raise();
  updating_ = false;
}

}  // namespace muffin

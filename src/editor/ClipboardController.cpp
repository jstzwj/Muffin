#include "editor/ClipboardController.h"

#include "document/DocumentSession.h"
#include "document/ImageSyntaxOps.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "document/MarkdownNode.h"
#include "projection/HtmlMarkdownImporter.h"
#include "image/ImageInsertionPolicy.h"

#include <QApplication>
#include <QClipboard>
#include <QImage>
#include <QMimeData>
#include <QSettings>

namespace {
QString clipboardHtmlMarkdown(QString html) {
  // Windows browser/Office clipboards may contain the surrounding page too.
  const auto first = html.indexOf(QStringLiteral("<!--StartFragment-->"));
  const auto last = html.indexOf(QStringLiteral("<!--EndFragment-->"));
  if (first >= 0 && last > first)
    html = html.mid(first + 20, last - first - 20);
  return muffin::importHtmlMarkdown(html);
}

QString clipboardTableMarkdown(QString text) {
  if (!text.contains('\t'))
    return {};
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  // Spreadsheet TSV quotes cells containing tabs/newlines and doubles quotes.
  QList<QStringList> rows;
  QStringList row;
  QString cell;
  bool quoted = false;
  for (qsizetype i = 0; i < text.size(); ++i) {
    const auto c = text[i];
    if (c == '"' && (quoted || cell.isEmpty())) {
      if (quoted && i + 1 < text.size() && text[i + 1] == '"') {
        cell += '"';
        ++i;
      } else
        quoted = !quoted;
    } else if (!quoted && (c == '\t' || c == '\n')) {
      row.append(std::move(cell));
      cell.clear();
      if (c == '\n') {
        rows.append(std::move(row));
        row.clear();
      }
    } else
      cell += c;
  }
  if (quoted)
    return {};
  if (!cell.isEmpty() || !row.isEmpty()) {
    row.append(std::move(cell));
    rows.append(std::move(row));
  }
  if (rows.size() < 2 || rows.first().size() < 2)
    return {};
  QString result;
  for (qsizetype i = 0; i < rows.size(); ++i) {
    if (rows[i].size() != rows.first().size())
      return {};
    result += '|';
    for (QString value : rows[i]) {
      QString escaped;
      for (const auto c : value) {
        if (QStringLiteral("\\`*_{}[]<>()!#|~").contains(c))
          escaped += '\\';
        escaped += c;
      }
      escaped.replace('\n', QStringLiteral("<br>"));
      result += ' ' + escaped + QStringLiteral(" |");
    }
    result += '\n';
    if (i == 0)
      result += '|' + QStringLiteral(" --- |").repeated(rows.first().size()) + '\n';
  }
  result.chop(1);
  return result;
}
} // namespace

namespace muffin {

ClipboardController::ClipboardController(QObject* parent) : QObject(parent) {
}

void ClipboardController::setInputController(InputController* inputController) {
  inputController_ = inputController;
}

bool ClipboardController::copy() {
  if (!ctx_.hasSession() || !ctx_.hasCursor() || ctx_.selection->selection().isCollapsed()) {
    return false;
  }

  const SelectionExportResult markdown = selectionSerializer_.exportSelection(
      SelectionExportRequest{&ctx_.session->document(), ctx_.selection->selection(), SelectionExportFormat::Markdown});
  const SelectionExportResult plainText = selectionSerializer_.exportSelection(
      SelectionExportRequest{&ctx_.session->document(), ctx_.selection->selection(), SelectionExportFormat::PlainText});
  if (markdown.text.isEmpty() && plainText.text.isEmpty()) {
    return false;
  }

  // Plain-text preference only controls plain targets. Rich editors receive
  // HTML, while Muffin-to-Muffin paste always retains the exact Markdown.
  const bool preferMarkdown = QSettings().value(QStringLiteral("editor/copyAsMarkdown"), true).toBool();
  auto* mimeData = new QMimeData();
  if (!markdown.text.isEmpty())
    mimeData->setData(QStringLiteral("text/markdown"), markdown.mimeData);
  const auto html = selectionSerializer_.exportSelection(
      SelectionExportRequest{&ctx_.session->document(), ctx_.selection->selection(), SelectionExportFormat::Html});
  if (!html.text.isEmpty())
    mimeData->setHtml(html.text);
  if (preferMarkdown) {
    mimeData->setText(markdown.text.isEmpty() ? plainText.text : markdown.text);
    if (!markdown.text.isEmpty()) {
      mimeData->setData(QStringLiteral("text/markdown"), markdown.mimeData);
    }
    if (markdown.text.isEmpty() && !plainText.text.isEmpty()) {
      mimeData->setData(QStringLiteral("text/plain"), plainText.mimeData);
    }
  } else {
    mimeData->setText(plainText.text.isEmpty() ? markdown.text : plainText.text);
    if (!plainText.text.isEmpty()) {
      mimeData->setData(QStringLiteral("text/plain"), plainText.mimeData);
    }
  }
  QApplication::clipboard()->setMimeData(mimeData);
  return true;
}

bool ClipboardController::cut() {
  if (!ctx_.hasSession() || !ctx_.session->isEditingAllowed())
    return false;
  if (!copy()) {
    return false;
  }
  return inputController_->deleteSelection();
}

bool ClipboardController::paste() {
  if (!ctx_.hasSession() || !ctx_.session->isEditingAllowed())
    return false;
  if (!inputController_) {
    return false;
  }

  const QMimeData* mimeData = QApplication::clipboard()->mimeData();
  if (!mimeData)
    return false;
  // Literal editors must receive text exactly as copied, including indentation
  // and tabs. The source editor has its own plain-text clipboard path.
  const auto* node = ctx_.hasCursor() && ctx_.hasSession()
                         ? ctx_.session->document().node(ctx_.selection->cursorPosition().blockId)
                         : nullptr;
  const bool literal = node && (node->type() == BlockType::CodeFence || node->type() == BlockType::HtmlBlock ||
                                node->type() == BlockType::MathBlock || node->type() == BlockType::FrontMatter);
  if (literal)
    return pasteAsPlainText();
  if (mimeData->hasFormat(QStringLiteral("text/markdown"))) {
    const auto markdown = QString::fromUtf8(mimeData->data(QStringLiteral("text/markdown")));
    if (!markdown.isEmpty())
      return inputController_->insertText(markdown);
  }
  if (mimeData->hasHtml()) {
    const auto markdown = clipboardHtmlMarkdown(mimeData->html());
    if (!markdown.isEmpty())
      return inputController_->insertText(markdown);
  }
  // A genuine bitmap still follows the user's image insertion policy.
  if (mimeData && mimeData->hasImage() && ctx_.hasSession()) {
    const QImage image = qvariant_cast<QImage>(mimeData->imageData());
    if (!image.isNull()) {
      muffin::ImageInsertRequest req;
      req.pastedImage = image;
      req.documentPath = ctx_.session->filePath();
      req.documentText = ctx_.session->markdownText().toString();
      QSettings settings;
      const muffin::ImageInsertResult res = muffin::ImageInsertionPolicy::resolveHref(req, settings, nullptr);
      if (res.ok) {
        return inputController_->insertText(image_syntax::markdownImage(QStringLiteral("image"), res.href));
      }
    }
  }

  const QString text = QApplication::clipboard()->text();
  const auto table = clipboardTableMarkdown(text);
  if (!table.isEmpty())
    return inputController_->insertText(table);
  return text.isEmpty() ? false : inputController_->insertText(text);
}

bool ClipboardController::copyAsPlainText() {
  if (!ctx_.hasSession() || !ctx_.hasCursor() || ctx_.selection->selection().isCollapsed()) {
    return false;
  }

  const SelectionExportResult result = selectionSerializer_.exportSelection(
      SelectionExportRequest{&ctx_.session->document(), ctx_.selection->selection(), SelectionExportFormat::PlainText});
  if (result.text.isEmpty()) {
    return false;
  }

  auto* mimeData = new QMimeData();
  mimeData->setText(result.text);
  QApplication::clipboard()->setMimeData(mimeData);
  return true;
}

bool ClipboardController::copyAsMarkdown() {
  if (!ctx_.hasSession() || !ctx_.hasCursor() || ctx_.selection->selection().isCollapsed()) {
    return false;
  }

  const SelectionExportResult result = selectionSerializer_.exportSelection(
      SelectionExportRequest{&ctx_.session->document(), ctx_.selection->selection(), SelectionExportFormat::Markdown});
  if (result.text.isEmpty()) {
    return false;
  }

  auto* mimeData = new QMimeData();
  mimeData->setText(result.text);
  mimeData->setData(QStringLiteral("text/markdown"), result.mimeData);
  QApplication::clipboard()->setMimeData(mimeData);
  return true;
}

bool ClipboardController::copyAsHtml() {
  if (!ctx_.hasSession() || !ctx_.hasCursor() || ctx_.selection->selection().isCollapsed()) {
    return false;
  }

  const SelectionExportResult result = selectionSerializer_.exportSelection(
      SelectionExportRequest{&ctx_.session->document(), ctx_.selection->selection(), SelectionExportFormat::Html});
  if (result.text.isEmpty()) {
    return false;
  }

  auto* mimeData = new QMimeData();
  mimeData->setHtml(result.text);
  mimeData->setText(result.text);
  QApplication::clipboard()->setMimeData(mimeData);
  return true;
}

bool ClipboardController::pasteAsPlainText() {
  if (!inputController_) {
    return false;
  }

  const QString text = QApplication::clipboard()->text();
  return text.isEmpty() ? false : inputController_->insertText(text);
}

} // namespace muffin

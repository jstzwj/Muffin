#include "app/DocumentSession.h"

#include <QFileInfo>

#include <utility>

namespace muffin {

DocumentSession::DocumentSession(QObject* parent) : QObject(parent) {
  connect(&document_, &MarkdownDocument::modifiedChanged, this, &DocumentSession::modifiedChanged);
  newDocument();
}

MarkdownDocument& DocumentSession::document() {
  return document_;
}

const MarkdownDocument& DocumentSession::document() const {
  return document_;
}

QString DocumentSession::filePath() const {
  return filePath_;
}

QString DocumentSession::displayName() const {
  if (filePath_.isEmpty()) {
    return tr("Untitled");
  }
  return QFileInfo(filePath_).fileName();
}

QString DocumentSession::markdownText() const {
  return document_.markdownText();
}

qint64 DocumentSession::lastParseElapsedMs() const {
  return lastParseElapsedMs_;
}

void DocumentSession::newDocument() {
  filePath_.clear();
  emit filePathChanged(filePath_);
  parseAndStore(QString(), false);
  emit documentTextChanged(QString());
}

void DocumentSession::setFilePath(QString path) {
  if (filePath_ == path) {
    return;
  }
  filePath_ = std::move(path);
  emit filePathChanged(filePath_);
}

void DocumentSession::setMarkdownText(QString text, bool modified) {
  parseAndStore(std::move(text), modified);
  emit documentTextChanged(document_.markdownText());
}

void DocumentSession::updateFromEditor(QString text) {
  parseAndStore(std::move(text), true);
}

void DocumentSession::parseAndStore(QString text, bool modified) {
  ParseResult result = parser_.parseDocument(QStringView(text), parseOptions_);
  lastParseElapsedMs_ = result.elapsedMs;
  document_.setMarkdownText(std::move(text), std::move(result.root));
  document_.setModified(modified);
  emit parsed(lastParseElapsedMs_);
}

}  // namespace muffin

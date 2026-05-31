#pragma once

#include "document/MarkdownDocument.h"
#include "parser/CmarkGfmParser.h"

#include <QObject>
#include <QString>

namespace muffin {

class DocumentSession final : public QObject {
  Q_OBJECT

public:
  explicit DocumentSession(QObject* parent = nullptr);

  MarkdownDocument& document();
  const MarkdownDocument& document() const;

  QString filePath() const;
  QString displayName() const;
  QString markdownText() const;
  qint64 lastParseElapsedMs() const;

  void newDocument();
  void setFilePath(QString path);
  void setMarkdownText(QString text, bool modified);
  void updateFromEditor(QString text);

signals:
  void documentTextChanged(QString text);
  void filePathChanged(QString path);
  void parsed(qint64 elapsedMs);
  void modifiedChanged(bool modified);

private:
  void parseAndStore(QString text, bool modified);

  MarkdownDocument document_;
  CmarkGfmParser parser_;
  ParseOptions parseOptions_;
  QString filePath_;
  qint64 lastParseElapsedMs_ = 0;
};

}  // namespace muffin

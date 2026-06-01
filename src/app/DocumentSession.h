#pragma once

#include "document/MarkdownDocument.h"
#include "parser/CmarkGfmParser.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace muffin {

struct LocalEditNodeHint {
  NodeId nodeId;
  qsizetype targetSourceOffset = -1;
  BlockType type = BlockType::Unknown;
};

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
  void applyMarkdownText(QString text, bool modified);
  bool applyLocalMarkdownEdit(
      qsizetype sourceStart,
      qsizetype sourceEnd,
      QString replacementText,
      bool modified,
      QVector<LocalEditNodeHint> nodeHints = {});

signals:
  void documentTextChanged(QString text);
  void filePathChanged(QString path);
  void parsed(qint64 elapsedMs);
  void modifiedChanged(bool modified);

private:
  void parseAndStore(QString text, bool modified);
  bool tryApplyTopLevelLocalEdit(
      qsizetype sourceStart,
      qsizetype sourceEnd,
      QString replacementText,
      bool modified,
      const QVector<LocalEditNodeHint>& nodeHints);

  MarkdownDocument document_;
  CmarkGfmParser parser_;
  ParseOptions parseOptions_;
  QString filePath_;
  qint64 lastParseElapsedMs_ = 0;
};

}  // namespace muffin

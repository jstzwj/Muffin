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
  const QString& markdownText() const;
  qint64 lastParseElapsedMs() const;
  bool lastParseWasLocalEdit() const;

  void newDocument();
  void setFilePath(QString path);
  void setMarkdownText(QString text, bool modified);
  void updateFromEditor(QString text);
  void applyMarkdownText(QString text, bool modified);
  bool applyTextDelta(
      qsizetype sourceStart,
      qsizetype removedLength,
      QString insertedText,
      bool modified,
      QVector<LocalEditNodeHint> nodeHints = {});
  bool applyTableSnapshot(NodeId tableId, int tableIndex, const MarkdownNode& tableSnapshot, bool modified);
  bool applyNodeSnapshot(NodeId nodeId, BlockType nodeType, int nodeIndex, const MarkdownNode& nodeSnapshot, bool modified);
  bool applyInsertedNode(
      NodeId nodeId,
      BlockType nodeType,
      qsizetype sourceStart,
      qsizetype targetSourceOffset,
      qsizetype removedLength,
      QString insertedText,
      bool modified);

signals:
  void documentTextChanged(QString text);
  void documentLocallyEdited(qsizetype start, qsizetype removedLength, QString insertedText);
  void filePathChanged(QString path);
  void parsed(qint64 elapsedMs);
  void modifiedChanged(bool modified);

private:
  void parseAndStore(QString text, bool modified);
  bool tryApplyTopLevelLocalEdit(
      qsizetype sourceStart,
      qsizetype sourceEnd,
      const QString& replacementText,
      bool modified,
      const QVector<LocalEditNodeHint>& nodeHints);

  MarkdownDocument document_;
  CmarkGfmParser parser_;
  ParseOptions parseOptions_;
  QString filePath_;
  qint64 lastParseElapsedMs_ = 0;
  bool lastParseWasLocalEdit_ = false;
};

}  // namespace muffin

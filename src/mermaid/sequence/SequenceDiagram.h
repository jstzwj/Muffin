#pragma once

#include "mermaid/sequence/SequenceTokenizer.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::sequence {

enum class SequenceErrorStage { Detector, Lexer, Parser, Semantic, Resource };
enum class SequenceErrorCode {
  Generic,
  MissingHeader,
  UnexpectedToken,
  MissingEnd,
  UnexpectedEnd,
  InvalidArrow,
  InactiveParticipant,
  InvalidCreateMessage,
  InvalidDestroyMessage,
  DuplicateParticipant,
  LimitExceeded,
};

struct SequenceSourceSpan {
  qsizetype offset = -1;
  qsizetype length = 0;
  int line = 0;
  int column = 0;
};

struct SequenceDiagnostic {
  SequenceErrorStage stage = SequenceErrorStage::Parser;
  SequenceErrorCode code = SequenceErrorCode::Generic;
  SequenceSourceSpan span;
  QString production;
  QString actual;
  QStringList expected;
  QString detail;
};

class SequenceParseError final : public std::runtime_error {
public:
  explicit SequenceParseError(SequenceDiagnostic diagnostic);
  const SequenceDiagnostic& diagnostic() const noexcept { return diagnostic_; }

private:
  SequenceDiagnostic diagnostic_;
};

struct SequenceActor {
  QString id;
  QString name;
  QString description;
  bool wrap = false;
  QString prevActor;
  QString nextActor;
  QString type = QStringLiteral("participant");
  QString box;
  int boxIndex = -1;
  QJsonObject links;
  QJsonObject properties;
};

struct SequenceMessage {
  QString id;
  QString from;
  QString to;
  QJsonValue message = QString{};
  bool wrap = false;
  int type = 0;
  bool activate = false;
  int centralConnection = 0;
  int placement = -1;
};

struct SequenceBox {
  QString name;
  QString fill = QStringLiteral("transparent");
  bool wrap = false;
  QStringList actorKeys;
};

struct SequenceData {
  QString title;
  QString accTitle;
  QString accDescription;
  bool sequenceNumbers = false;
  QVector<SequenceActor> actors;
  QVector<SequenceMessage> messages;
  QVector<SequenceBox> boxes;
  QMap<QString, int> createdActors;
  QMap<QString, int> destroyedActors;
};

struct SequenceLimits {
  int maxActors = 500;
  int maxMessages = 5000;
  int maxFragmentDepth = 64;
  int maxTextSize = 50000;
};

class SequenceDiagram {
public:
  static SequenceDiagram parse(const QString& source, SequenceLimits limits = {});

  const SequenceData& data() const { return data_; }
  QJsonObject toJson() const;

private:
  SequenceData data_;
};

QString sequenceErrorStageName(SequenceErrorStage stage);
QString sequenceErrorCodeName(SequenceErrorCode code);
QString formatSequenceDiagnostic(const SequenceDiagnostic& diagnostic);

}  // namespace muffin::mermaid::sequence

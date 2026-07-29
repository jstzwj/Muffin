#pragma once

#include "mermaid/state/StateTokenizer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::state {

enum class StateErrorStage { Detector, Lexer, Parser, Semantic, Resource };
enum class StateErrorCode {
  MissingHeader, UnexpectedToken, MissingClosingBrace, MissingEndNote,
  InvalidDirection, InvalidStateName, LimitExceeded,
};

struct StateSourceSpan {
  qsizetype offset = -1;
  qsizetype length = 0;
  int line = 1;
  int column = 0;
};
struct StateDiagnostic {
  StateErrorStage stage = StateErrorStage::Parser;
  StateErrorCode code = StateErrorCode::UnexpectedToken;
  StateSourceSpan span;
  QString production;
  QString actual;
  QStringList expected;
  QString detail;
};
class StateParseError final : public std::runtime_error {
public:
  explicit StateParseError(StateDiagnostic diagnostic);
  const StateDiagnostic& diagnostic() const noexcept { return diagnostic_; }
private:
  StateDiagnostic diagnostic_;
};

struct StateNode {
  QString id;
  QString type = QStringLiteral("default");
  QStringList descriptions;
  QJsonValue document = QJsonValue::Null;
  QJsonValue note = QJsonValue::Null;
  QStringList classes;
  QStringList styles;
  QStringList textStyles;
};
struct StateRelation {
  QString id1;
  QString id2;
  QJsonValue relationTitle = QJsonValue::Null;
  QStringList linkStyles;  // linkStyle declarations (key:value), applied at paint
};
struct StateStyleClass {
  QString id;
  QStringList styles;
  QStringList textStyles;
};
struct StateLink {
  QJsonValue id;
  QString url;
  QString tooltip;
};
struct StateDiagramData {
  QJsonArray root;
  QString direction = QStringLiteral("TB");
  QString accTitle;
  QString accDescription;
  QVector<StateNode> states;
  QVector<StateRelation> relations;
  QVector<StateStyleClass> styleClasses;
  QVector<StateLink> links;
};
struct StateLimits {
  int maxStates = 1000;
  int maxRelations = 5000;
  int maxCompositeDepth = 64;
  int maxTextSize = 100000;
};

class StateDiagram {
public:
  static StateDiagram parse(const QString& source, StateLimits limits = {});
  const StateDiagramData& data() const { return data_; }
  QJsonObject toJson() const;
private:
  StateDiagramData data_;
};

struct StateProductionMapping {
  int id = 0;
  QString parserFunction;
  QString oracleCase;
};
QVector<StateProductionMapping> stateProductionMappings();
QString stateErrorStageName(StateErrorStage stage);
QString stateErrorCodeName(StateErrorCode code);
QString formatStateDiagnostic(const StateDiagnostic& diagnostic);

}  // namespace muffin::mermaid::state

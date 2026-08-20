#include "mermaid/state/StateDiagram.h"

#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>

namespace muffin::mermaid::state {
namespace {

StateSourceSpan spanOf(const StateToken& token) {
  return {token.offset, std::max<qsizetype>(1, token.length), token.line, token.column};
}
StateParseError error(const StateToken& token, StateErrorStage stage,
                      StateErrorCode code, QString production,
                      QStringList expected = {}, QString detail = {}) {
  return StateParseError({stage, code, spanOf(token), std::move(production),
      token.text.isEmpty() ? stateTokenName(token.kind) : token.text,
      std::move(expected), std::move(detail)});
}
QString unquote(QString value) {
  value = value.trimmed();
  if (value.size() >= 2 && value.front() == QLatin1Char('"') &&
      value.back() == QLatin1Char('"'))
    return value.mid(1, value.size() - 2);
  return value;
}
QJsonObject stateStatement(QString id, QString type = QStringLiteral("default"),
                           QString description = {}) {
  return {{QStringLiteral("stmt"), QStringLiteral("state")},
          {QStringLiteral("id"), std::move(id)},
          {QStringLiteral("type"), std::move(type)},
          {QStringLiteral("description"), std::move(description)}};
}
QString statementId(const QJsonValue& value) {
  return value.isObject() ? value.toObject().value(QStringLiteral("id")).toString()
                          : value.toString();
}

class Parser {
public:
  Parser(QString source, StateLimits limits)
      : source_(std::move(source)), limits_(limits),
        tokens_(StateTokenizer(source_).tokenize()), cursor_(tokens_) {}

  StateDiagramData parse() {
    if (source_.size() > limits_.maxTextSize)
      throw error(cursor_.peek(), StateErrorStage::Resource,
                  StateErrorCode::LimitExceeded, QStringLiteral("start"), {},
                  QStringLiteral("state source exceeds maxTextSize"));
    cursor_.skipSeparators();
    StateTokenCursor header = cursor_.consumeLine();
    if (header.atEnd() || header.peek().kind != StateTokenKind::Header)
      throw error(header.atEnd() ? cursor_.peek() : header.peek(),
                  StateErrorStage::Detector, StateErrorCode::MissingHeader,
                  QStringLiteral("start"), {QStringLiteral("stateDiagram-v2")});
    header.consume();
    if (!header.atEnd())
      throw error(header.peek(), StateErrorStage::Parser,
                  StateErrorCode::UnexpectedToken, QStringLiteral("start"),
                  {QStringLiteral("NL")});
    cursor_.skipSeparators();
    data_.root = parseDocument(false, 0);
    translateDocument(data_.root, QStringLiteral("root"));
    extract(data_.root);
    return data_;
  }

private:
  QJsonArray parseDocument(bool stopAtBrace, int depth) {
    if (depth > limits_.maxCompositeDepth)
      throw error(cursor_.peek(), StateErrorStage::Resource,
                  StateErrorCode::LimitExceeded, QStringLiteral("document"), {},
                  QStringLiteral("state composite depth exceeds maxCompositeDepth"));
    QJsonArray document;
    while (!cursor_.atEnd()) {
      cursor_.skipSeparators();
      if (cursor_.atEnd()) break;
      if (cursor_.peek().kind == StateTokenKind::RBrace) {
        if (!stopAtBrace) {
          cursor_.consume();
          continue;
        }
        cursor_.consume();
        return document;
      }
      StateTokenCursor line = cursor_.consumeLine();
      if (!line.atEnd()) {
        const QJsonValue statement = parseStatement(line, depth);
        if (statement.isArray())
          for (const QJsonValue& item : statement.toArray()) document.append(item);
        else if (!statement.isUndefined())
          document.append(statement);
      }
      cursor_.skipSeparators();
    }
    if (stopAtBrace)
      throw error(tokens_.last(), StateErrorStage::Parser,
                  StateErrorCode::MissingClosingBrace, QStringLiteral("statement"),
                  {QStringLiteral("}")});
    return document;
  }

  QJsonValue parseStatement(StateTokenCursor line, int depth) {
    const StateToken first = line.peek();
    switch (first.kind) {
      case StateTokenKind::Direction: return parseDirection(line, depth == 0);
      case StateTokenKind::AccTitle: return parseAccessibility(line, true);
      case StateTokenKind::AccDescr: return parseAccessibility(line, false);
      case StateTokenKind::State: return parseState(line, depth);
      case StateTokenKind::Note: return parseNote(line);
      case StateTokenKind::ClassDef: return parseClassDef(line);
      case StateTokenKind::Class: return parseApplyClass(line);
      case StateTokenKind::Style: return parseStyle(line);
      case StateTokenKind::Click: return parseClick(line);
      case StateTokenKind::HideEmpty:
      case StateTokenKind::Scale: return line.raw(source_);
      case StateTokenKind::Concurrent:
        line.consume();
        if (!line.atEnd()) throw unexpected(line, QStringLiteral("statement"));
        return QJsonObject{{QStringLiteral("stmt"), QStringLiteral("state")},
                           {QStringLiteral("id"), QStringLiteral("divider-id-%1").arg(++divider_)},
                           {QStringLiteral("type"), QStringLiteral("divider")}};
      default: return parseIdStatement(line);
    }
  }

  StateParseError unexpected(const StateTokenCursor& line, QString production) const {
    const StateToken& token = line.atEnd()
        ? tokens_.at(std::min(line.endPosition(), tokens_.size() - 1))
        : line.peek();
    return error(token, token.kind == StateTokenKind::Invalid
                                  ? StateErrorStage::Lexer : StateErrorStage::Parser,
                 StateErrorCode::UnexpectedToken, std::move(production));
  }

  QJsonValue parseDirection(StateTokenCursor line, bool rootDirection) {
    const QString original = line.raw(source_);
    line.consume();
    if (line.atEnd()) throw unexpected(line, QStringLiteral("direction"));
    const StateToken valueToken = line.consume();
    const QString value = valueToken.text.toUpper();
    if (!line.atEnd() || !QStringList{QStringLiteral("TB"), QStringLiteral("BT"),
                                      QStringLiteral("RL"), QStringLiteral("LR")}.contains(value))
      return original;
    if (rootDirection) data_.direction = value;
    return QJsonObject{{QStringLiteral("stmt"), QStringLiteral("dir")},
                       {QStringLiteral("value"), value}};
  }

  QJsonValue parseAccessibility(StateTokenCursor line, bool title) {
    line.consume();
    line.match(StateTokenKind::Colon);
    QString value;
    if (!title && line.match(StateTokenKind::LBrace)) {
      QStringList values;
      cursor_.skipSeparators();
      while (!cursor_.atEnd() && cursor_.peek().kind != StateTokenKind::RBrace) {
        StateTokenCursor content = cursor_.consumeLine();
        values.append(content.raw(source_).trimmed());
        cursor_.skipSeparators();
      }
      if (!cursor_.match(StateTokenKind::RBrace))
        throw error(cursor_.peek(), StateErrorStage::Parser,
                    StateErrorCode::MissingClosingBrace, QStringLiteral("acc_descr"),
                    {QStringLiteral("}")});
      value = values.join(QLatin1Char('\n')).trimmed();
    } else if (!line.atEnd()) value = line.raw(source_).trimmed();
    if (title) data_.accTitle = value; else data_.accDescription = value;
    return value;
  }

  QJsonValue parseState(StateTokenCursor line, int depth) {
    line.consume();
    if (line.atEnd()) throw unexpected(line, QStringLiteral("statement"));
    QString description;
    QString id;
    if (line.peek().kind == StateTokenKind::String) {
      description = unquote(line.consume().text).trimmed();
      if (!line.match(StateTokenKind::As))
        throw unexpected(line, QStringLiteral("statement"));
      id = line.atEnd() ? QString{} : line.consume().text.trimmed();
    } else {
      id = line.consume().text.trimmed();
    }
    QString type = QStringLiteral("default");
    if (!line.atEnd() && line.peek().kind == StateTokenKind::Identifier) {
      const QString marker = line.peek().text.toLower();
      if (marker == QLatin1String("<<fork>>") || marker == QLatin1String("[[fork]]")) type = QStringLiteral("fork");
      else if (marker == QLatin1String("<<join>>") || marker == QLatin1String("[[join]]")) type = QStringLiteral("join");
      else if (marker == QLatin1String("<<choice>>") || marker == QLatin1String("[[choice]]")) type = QStringLiteral("choice");
      if (type != QLatin1String("default")) line.consume();
    }
    const bool composite = line.match(StateTokenKind::LBrace);
    if (!line.atEnd()) throw unexpected(line, QStringLiteral("statement"));
    if (!composite && description.isEmpty() && type == QLatin1String("default"))
      return id;
    QJsonObject result = stateStatement(id, type, description);
    if (type != QLatin1String("default")) result.remove(QStringLiteral("description"));
    if (composite) result.insert(QStringLiteral("doc"), parseDocument(true, depth + 1));
    return result;
  }

  // The 11.16 grammar accepts several statements on one line (newlines are
  // plain whitespace to it), so unknown keywords such as
  // `linkStyle 0 stroke:red` lex as ids and become plain states — "linkStyle",
  // "0", and "stroke" carrying the raw remainder as its description. Bare ids
  // wrap into {stmt:state,…} objects (jison cases 44/45), unlike the
  // `state X` form which yields the bare string (browser-verified).
  QJsonValue parseIdStatement(StateTokenCursor line) {
    QJsonArray statements;
    while (!line.atEnd()) {
      QJsonObject left = parseEndpoint(line);
      if (line.match(StateTokenKind::Arrow)) {
        if (line.atEnd()) throw unexpected(line, QStringLiteral("statement"));
        QJsonObject right = parseEndpoint(line);
        QJsonObject relation{{QStringLiteral("stmt"), QStringLiteral("relation")},
                             {QStringLiteral("state1"), left},
                             {QStringLiteral("state2"), right}};
        if (line.match(StateTokenKind::Colon)) {
          relation.insert(QStringLiteral("description"), line.raw(source_).trimmed());
          while (!line.atEnd()) line.consume();
        }
        if (!line.atEnd()) throw unexpected(line, QStringLiteral("statement"));
        statements.append(relation);
        return statements.size() == 1 ? statements.first() : QJsonValue(statements);
      }
      if (line.match(StateTokenKind::Colon)) {
        left.insert(QStringLiteral("description"), line.raw(source_).trimmed());
        while (!line.atEnd()) line.consume();
        statements.append(left);
        return statements.size() == 1 ? statements.first() : QJsonValue(statements);
      }
      if (line.atEnd()) {
        statements.append(left);
        return statements.size() == 1 ? statements.first() : QJsonValue(statements);
      }
      statements.append(left);
    }
    return statements.size() == 1 ? statements.first() : QJsonValue(statements);
  }

  QJsonObject parseEndpoint(StateTokenCursor& line) {
    if (line.atEnd()) throw unexpected(line, QStringLiteral("idStatement"));
    const StateToken token = line.consume();
    if (token.kind != StateTokenKind::Identifier && token.kind != StateTokenKind::StartEnd &&
        token.kind != StateTokenKind::Default)
      throw error(token, StateErrorStage::Parser, StateErrorCode::UnexpectedToken,
                  QStringLiteral("idStatement"), {QStringLiteral("ID"), QStringLiteral("[*]")});
    QJsonObject result = stateStatement(token.text.trimmed());
    if (line.match(StateTokenKind::StyleSeparator)) {
      if (line.atEnd()) throw unexpected(line, QStringLiteral("idStatement"));
      result.insert(QStringLiteral("classes"), QJsonArray{line.consume().text.trimmed()});
    }
    return result;
  }

  QJsonValue parseNote(StateTokenCursor line) {
    line.consume();
    if (!line.atEnd() && line.peek().kind == StateTokenKind::String) {
      const QString text = unquote(line.consume().text);
      if (!line.match(StateTokenKind::As) || line.atEnd())
        throw unexpected(line, QStringLiteral("note"));
      return QJsonObject{{QStringLiteral("stmt"), QStringLiteral("state")},
                         {QStringLiteral("id"), line.consume().text.trimmed()},
                         {QStringLiteral("note"), QJsonObject{{QStringLiteral("text"), text}}}};
    }
    if (line.atEnd() || (line.peek().kind != StateTokenKind::Left &&
                         line.peek().kind != StateTokenKind::Right))
      throw unexpected(line, QStringLiteral("notePosition"));
    const QString position = line.consume().text.toLower() + QStringLiteral(" of");
    if (!line.match(StateTokenKind::Of) || line.atEnd())
      throw unexpected(line, QStringLiteral("notePosition"));
    const QString id = line.consume().text.trimmed();
    QString text;
    if (line.match(StateTokenKind::Colon)) {
      text = line.raw(source_).trimmed();
    } else {
      QStringList values;
      bool closed = false;
      cursor_.skipSeparators();
      while (!cursor_.atEnd()) {
        StateTokenCursor content = cursor_.consumeLine();
        if (!content.atEnd() && content.peek().kind == StateTokenKind::End &&
            content.peek(1).kind == StateTokenKind::Note) { closed = true; break; }
        values.append(content.raw(source_).trimmed());
        cursor_.skipSeparators();
      }
      if (!closed)
        throw error(cursor_.peek(), StateErrorStage::Lexer,
                    StateErrorCode::MissingEndNote, QStringLiteral("note"),
                    {QStringLiteral("end note")});
      text = values.join(QLatin1Char('\n')).trimmed();
    }
    return QJsonObject{{QStringLiteral("stmt"), QStringLiteral("state")},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("note"), QJsonObject{
                           {QStringLiteral("position"), position},
                           {QStringLiteral("text"), text}}}};
  }

  QJsonValue parseClassDef(StateTokenCursor line) {
    line.consume();
    if (line.atEnd()) throw unexpected(line, QStringLiteral("classDefStatement"));
    const QString id = line.consume().text.trimmed();
    return QJsonObject{{QStringLiteral("stmt"), QStringLiteral("classDef")},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("classes"), line.raw(source_).trimmed()}};
  }
  QJsonValue parseApplyClass(StateTokenCursor line) {
    line.consume();
    if (line.atEnd()) throw unexpected(line, QStringLiteral("cssClassStatement"));
    const qsizetype start = line.position();
    const qsizetype end = line.endPosition();
    if (end - start < 2) throw unexpected(line, QStringLiteral("cssClassStatement"));
    StateTokenCursor ids(tokens_, start, end - 1);
    return QJsonObject{{QStringLiteral("stmt"), QStringLiteral("applyClass")},
                       {QStringLiteral("id"), ids.raw(source_).trimmed()},
                       {QStringLiteral("styleClass"), tokens_.at(end - 1).text.trimmed()}};
  }
  QJsonValue parseStyle(StateTokenCursor line) {
    line.consume();
    if (line.atEnd()) throw unexpected(line, QStringLiteral("styleStatement"));
    const QString id = line.consume().text.trimmed();
    return QJsonObject{{QStringLiteral("stmt"), QStringLiteral("style")},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("styleClass"), line.raw(source_).trimmed()}};
  }
  QJsonValue parseClick(StateTokenCursor line) {
    line.consume();
    QJsonObject id = parseEndpoint(line);
    line.match(StateTokenKind::Href);
    if (line.atEnd()) throw unexpected(line, QStringLiteral("statement"));
    const QString url = line.consume().text;
    const QString tooltip = line.atEnd() ? QString{} : line.consume().text;
    if (!line.atEnd()) throw unexpected(line, QStringLiteral("statement"));
    return QJsonObject{{QStringLiteral("stmt"), QStringLiteral("click")},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("url"), url},
                       {QStringLiteral("tooltip"), tooltip}};
  }

  void translateStatement(QJsonObject& statement, const QString& parent, bool first) {
    const QString kind = statement.value(QStringLiteral("stmt")).toString();
    if (kind == QLatin1String("relation")) {
      QJsonObject left = statement.value(QStringLiteral("state1")).toObject();
      QJsonObject right = statement.value(QStringLiteral("state2")).toObject();
      translateStatement(left, parent, true);
      translateStatement(right, parent, false);
      statement.insert(QStringLiteral("state1"), left);
      statement.insert(QStringLiteral("state2"), right);
      return;
    }
    if (kind != QLatin1String("state")) return;
    if (statement.value(QStringLiteral("id")).toString() == QLatin1String("[*]")) {
      statement.insert(QStringLiteral("id"), parent + (first ? QStringLiteral("_start")
                                                              : QStringLiteral("_end")));
      statement.insert(QStringLiteral("start"), first);
    }
    if (!statement.value(QStringLiteral("doc")).isArray()) return;
    QJsonArray doc = statement.value(QStringLiteral("doc")).toArray();
    QJsonArray partitions, current;
    for (const QJsonValue& value : doc) {
      const QJsonObject child = value.toObject();
      if (child.value(QStringLiteral("type")).toString() == QLatin1String("divider")) {
        QJsonObject divider = child;
        divider.insert(QStringLiteral("doc"), current);
        partitions.append(divider);
        current = {};
      } else current.append(value);
    }
    if (!partitions.isEmpty() && !current.isEmpty()) {
      partitions.append(QJsonObject{{QStringLiteral("stmt"), QStringLiteral("state")},
          {QStringLiteral("id"), QStringLiteral("$generated-%1").arg(++generated_)},
          {QStringLiteral("type"), QStringLiteral("divider")},
          {QStringLiteral("doc"), current}});
      doc = partitions;
      statement.insert(QStringLiteral("doc"), doc);
    }
    const QString id = statement.value(QStringLiteral("id")).toString().trimmed();
    for (qsizetype i = 0; i < doc.size(); ++i) {
      if (!doc.at(i).isObject()) continue;
      QJsonObject child = doc.at(i).toObject();
      translateStatement(child, id, true);
      doc.replace(i, child);
    }
    statement.insert(QStringLiteral("doc"), doc);
  }
  void translateDocument(QJsonArray& document, const QString& parent) {
    for (qsizetype i = 0; i < document.size(); ++i) {
      if (!document.at(i).isObject()) continue;
      QJsonObject statement = document.at(i).toObject();
      translateStatement(statement, parent, true);
      document.replace(i, statement);
    }
  }

  StateNode& addState(const QString& id, const QString& type = QStringLiteral("default")) {
    const QString trimmed = id.trimmed();
    auto it = stateIndex_.find(trimmed);
    if (it == stateIndex_.end()) {
      if (data_.states.size() >= limits_.maxStates)
        throw error(tokens_.last(), StateErrorStage::Resource,
                    StateErrorCode::LimitExceeded, QStringLiteral("extract"));
      stateIndex_.insert(trimmed, data_.states.size());
      data_.states.append({trimmed, type});
      return data_.states.last();
    }
    StateNode& state = data_.states[it.value()];
    if (state.type.isEmpty()) state.type = type;
    return state;
  }
  void extract(const QJsonArray& document) {
    for (const QJsonValue& value : document) {
      if (!value.isObject()) continue;
      const QJsonObject item = value.toObject();
      const QString kind = item.value(QStringLiteral("stmt")).toString();
      if (kind == QLatin1String("state")) {
        StateNode& state = addState(item.value(QStringLiteral("id")).toString(),
                                    item.value(QStringLiteral("type")).toString(QStringLiteral("default")));
        if (item.contains(QStringLiteral("doc"))) state.document = item.value(QStringLiteral("doc"));
        const QString description = item.value(QStringLiteral("description")).toString().trimmed();
        if (!description.isEmpty()) state.descriptions.append(description.startsWith(QLatin1Char(':'))
            ? description.mid(1).trimmed() : description);
        if (item.contains(QStringLiteral("note"))) state.note = item.value(QStringLiteral("note"));
        for (const QJsonValue& css : item.value(QStringLiteral("classes")).toArray())
          state.classes.append(css.toString());
      } else if (kind == QLatin1String("relation")) {
        if (data_.relations.size() >= limits_.maxRelations)
          throw error(tokens_.last(), StateErrorStage::Resource,
                      StateErrorCode::LimitExceeded, QStringLiteral("extract"));
        const QJsonObject left = item.value(QStringLiteral("state1")).toObject();
        const QJsonObject right = item.value(QStringLiteral("state2")).toObject();
        addState(left.value(QStringLiteral("id")).toString(), left.value(QStringLiteral("type")).toString());
        addState(right.value(QStringLiteral("id")).toString(), right.value(QStringLiteral("type")).toString());
        data_.relations.append({left.value(QStringLiteral("id")).toString(),
                                right.value(QStringLiteral("id")).toString(),
                                item.contains(QStringLiteral("description"))
                                    ? QJsonValue(item.value(QStringLiteral("description")).toString())
                                    : QJsonValue(QString{})});
      } else if (kind == QLatin1String("classDef")) {
        StateStyleClass valueClass;
        valueClass.id = item.value(QStringLiteral("id")).toString();
        for (QString style : item.value(QStringLiteral("classes")).toString().split(QLatin1Char(','))) {
          style.remove(QRegularExpression(QStringLiteral(R"(;$)")));
          style = style.trimmed();
          if (style.contains(QStringLiteral("color"))) {
            QString text = style;
            text.replace(QStringLiteral("fill"), QStringLiteral("bgFill"));
            text.replace(QStringLiteral("color"), QStringLiteral("fill"));
            valueClass.textStyles.append(text);
          }
          valueClass.styles.append(style);
        }
        data_.styleClasses.append(valueClass);
      } else if (kind == QLatin1String("applyClass")) {
        for (const QString& id : item.value(QStringLiteral("id")).toString().split(QLatin1Char(',')))
          addState(id.trimmed()).classes.append(item.value(QStringLiteral("styleClass")).toString());
      } else if (kind == QLatin1String("style")) {
        StateNode& state = addState(item.value(QStringLiteral("id")).toString());
        for (const QString& style : item.value(QStringLiteral("styleClass")).toString().split(QLatin1Char(',')))
          state.styles.append(style.trimmed());
      } else if (kind == QLatin1String("click")) {
        data_.links.append({item.value(QStringLiteral("id")),
                            item.value(QStringLiteral("url")).toString(),
                            item.value(QStringLiteral("tooltip")).toString()});
      }
    }
  }

  QString source_;
  StateLimits limits_;
  QVector<StateToken> tokens_;
  StateTokenCursor cursor_;
  StateDiagramData data_;
  QMap<QString, qsizetype> stateIndex_;
  int divider_ = 0;
  int generated_ = 0;
};

QJsonArray strings(const QStringList& values) {
  QJsonArray result;
  for (const QString& value : values) result.append(value);
  return result;
}
}

StateParseError::StateParseError(StateDiagnostic diagnostic)
    : std::runtime_error(formatStateDiagnostic(diagnostic).toUtf8().constData()),
      diagnostic_(std::move(diagnostic)) {}

StateDiagram StateDiagram::parse(const QString& source, StateLimits limits) {
  StateDiagram result;
  result.data_ = Parser(source, limits).parse();
  return result;
}

QJsonObject StateDiagram::toJson() const {
  QJsonArray states;
  for (const StateNode& state : data_.states) {
    states.append(QJsonObject{{QStringLiteral("id"), state.id},
        {QStringLiteral("type"), state.type},
        {QStringLiteral("descriptions"), strings(state.descriptions)},
        {QStringLiteral("doc"), state.document}, {QStringLiteral("note"), state.note},
        {QStringLiteral("classes"), strings(state.classes)},
        {QStringLiteral("styles"), strings(state.styles)},
        {QStringLiteral("textStyles"), strings(state.textStyles)}});
  }
  QJsonArray relations;
  for (const StateRelation& relation : data_.relations)
    relations.append(QJsonObject{{QStringLiteral("id1"), relation.id1},
        {QStringLiteral("id2"), relation.id2},
        {QStringLiteral("relationTitle"), relation.relationTitle}});
  QJsonArray classes;
  for (const StateStyleClass& style : data_.styleClasses)
    classes.append(QJsonObject{{QStringLiteral("id"), style.id},
        {QStringLiteral("styles"), strings(style.styles)},
        {QStringLiteral("textStyles"), strings(style.textStyles)}});
  QJsonArray links;
  for (const StateLink& link : data_.links)
    links.append(QJsonObject{{QStringLiteral("id"), link.id},
        {QStringLiteral("url"), link.url}, {QStringLiteral("tooltip"), link.tooltip}});
  return {{QStringLiteral("root"), data_.root},
          {QStringLiteral("direction"), data_.direction},
          {QStringLiteral("accTitle"), data_.accTitle},
          {QStringLiteral("accDescription"), data_.accDescription},
          {QStringLiteral("states"), states}, {QStringLiteral("relations"), relations},
          {QStringLiteral("classes"), classes}, {QStringLiteral("links"), links}};
}

QVector<StateProductionMapping> stateProductionMappings() {
  static const QStringList functions = {
      "parse", "parse", "parse", "parseDocument", "parseDocument", "parseStatement",
      "parseStatement", "parseStatement", "parseStatement", "parseClassDef", "parseStyle",
      "parseApplyClass", "parseIdStatement", "parseIdStatement", "parseIdStatement",
      "parseIdStatement", "parseStatement", "parseStatement", "parseState", "parseState",
      "parseState", "parseState", "parseState", "parseState", "parseStatement", "parseNote",
      "parseNote", "parseDirection", "parseAccessibility", "parseAccessibility",
      "parseAccessibility", "parseClick", "parseClick", "parseClassDef", "parseClassDef",
      "parseStyle", "parseApplyClass", "parseDirection", "parseDirection", "parseDirection",
      "parseDirection", "parseDocument", "parseDocument", "parseIdStatement",
      "parseIdStatement", "parseIdStatement", "parseIdStatement", "parseNote", "parseNote"};
  QVector<StateProductionMapping> result;
  for (int i = 0; i < functions.size(); ++i)
    result.append({i + 1, functions.at(i), QStringLiteral("state-db")});
  return result;
}

QString stateErrorStageName(StateErrorStage stage) {
  switch (stage) {
    case StateErrorStage::Detector: return QStringLiteral("detector");
    case StateErrorStage::Lexer: return QStringLiteral("lexer");
    case StateErrorStage::Parser: return QStringLiteral("parser");
    case StateErrorStage::Semantic: return QStringLiteral("semantic");
    case StateErrorStage::Resource: return QStringLiteral("resource");
  }
  return {};
}
QString stateErrorCodeName(StateErrorCode code) {
  switch (code) {
    case StateErrorCode::MissingHeader: return QStringLiteral("missing-header");
    case StateErrorCode::UnexpectedToken: return QStringLiteral("unexpected-token");
    case StateErrorCode::MissingClosingBrace: return QStringLiteral("missing-closing-brace");
    case StateErrorCode::MissingEndNote: return QStringLiteral("missing-end-note");
    case StateErrorCode::InvalidDirection: return QStringLiteral("invalid-direction");
    case StateErrorCode::InvalidStateName: return QStringLiteral("invalid-state-name");
    case StateErrorCode::LimitExceeded: return QStringLiteral("limit-exceeded");
  }
  return {};
}
QString formatStateDiagnostic(const StateDiagnostic& diagnostic) {
  return QStringLiteral("%1:%2: %3 [%4]")
      .arg(diagnostic.span.line).arg(diagnostic.span.column)
      .arg(stateErrorCodeName(diagnostic.code), diagnostic.actual);
}

}  // namespace muffin::mermaid::state

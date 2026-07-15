#include "mermaid/sequence/SequenceDiagram.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

namespace muffin::mermaid::sequence {
namespace {

enum LineType {
  Solid = 0,
  Dotted = 1,
  Note = 2,
  SolidCross = 3,
  DottedCross = 4,
  SolidOpen = 5,
  DottedOpen = 6,
  LoopStart = 10,
  LoopEnd = 11,
  AltStart = 12,
  AltElse = 13,
  AltEnd = 14,
  OptStart = 15,
  OptEnd = 16,
  ActiveStart = 17,
  ActiveEnd = 18,
  ParStart = 19,
  ParAnd = 20,
  ParEnd = 21,
  RectStart = 22,
  RectEnd = 23,
  SolidPoint = 24,
  DottedPoint = 25,
  Autonumber = 26,
  CriticalStart = 27,
  CriticalOption = 28,
  CriticalEnd = 29,
  BreakStart = 30,
  BreakEnd = 31,
  ParOverStart = 32,
  BidirectionalSolid = 33,
  BidirectionalDotted = 34,
  SolidTop = 41,
  SolidBottom = 42,
  StickTop = 43,
  StickBottom = 44,
  SolidReverseTop = 45,
  SolidReverseBottom = 46,
  StickReverseTop = 47,
  StickReverseBottom = 48,
  SolidTopDotted = 51,
  SolidBottomDotted = 52,
  StickTopDotted = 53,
  StickBottomDotted = 54,
  SolidReverseTopDotted = 55,
  SolidReverseBottomDotted = 56,
  StickReverseTopDotted = 57,
  StickReverseBottomDotted = 58,
  CentralConnection = 59,
  CentralConnectionReverse = 60,
  CentralConnectionDual = 61,
};

SequenceSourceSpan spanOf(const SequenceToken& token) {
  return {token.offset, std::max<qsizetype>(1, token.length), token.line, token.column};
}

SequenceParseError parseError(const SequenceToken& token, SequenceErrorStage stage,
                              SequenceErrorCode code, QString production,
                              QStringList expected = {}, QString detail = {}) {
  SequenceDiagnostic diagnostic;
  diagnostic.stage = stage;
  diagnostic.code = code;
  diagnostic.span = spanOf(token);
  diagnostic.production = std::move(production);
  diagnostic.actual = token.text.isEmpty() ? sequenceTokenName(token.kind) : token.text;
  diagnostic.expected = std::move(expected);
  diagnostic.detail = std::move(detail);
  return SequenceParseError(std::move(diagnostic));
}

struct MessageText {
  QString text;
  bool wrap = false;
};

MessageText parseMessageText(QString text) {
  text = text.trimmed();
  MessageText result;
  static const QRegularExpression prefix(QStringLiteral(R"(^:?(wrap|nowrap):)"),
                                          QRegularExpression::CaseInsensitiveOption);
  const auto match = prefix.match(text);
  if (match.hasMatch()) {
    result.wrap = match.captured(1).compare(QStringLiteral("wrap"), Qt::CaseInsensitive) == 0;
    text.remove(0, match.capturedLength());
  }
  result.text = text.trimmed();
  return result;
}

class TokenCursor {
public:
  explicit TokenCursor(QVector<SequenceToken> tokens) : tokens_(std::move(tokens)) {}

  const SequenceToken& peek(qsizetype lookahead = 0) const {
    return tokens_.at(std::min(position_ + lookahead, tokens_.size() - 1));
  }
  bool atEnd() const { return peek().kind == SequenceTokenKind::Eof; }
  const SequenceToken& consume() { return tokens_.at(position_++); }
  bool consumeIf(SequenceTokenKind kind) {
    if (peek().kind != kind) return false;
    ++position_;
    return true;
  }
  void skipSeparators() {
    while (peek().kind == SequenceTokenKind::Newline || peek().kind == SequenceTokenKind::Semi)
      ++position_;
  }
  QVector<SequenceToken> parseList() {
    QVector<SequenceToken> result;
    while (!atEnd() && peek().kind != SequenceTokenKind::Newline &&
           peek().kind != SequenceTokenKind::Semi)
      result.append(consume());
    return result;
  }

private:
  QVector<SequenceToken> tokens_;
  qsizetype position_ = 0;
};

struct Frame {
  SequenceTokenKind kind = SequenceTokenKind::Invalid;
  int endType = -1;
  SequenceToken opening;
};

class Parser {
public:
  Parser(QString source, SequenceLimits limits)
      : source_(std::move(source)), limits_(limits), cursor_(SequenceTokenizer(source_).tokenize()) {}

  SequenceData parse() {
    if (source_.size() > limits_.maxTextSize)
      throw parseError(cursor_.peek(), SequenceErrorStage::Resource,
                       SequenceErrorCode::LimitExceeded, QStringLiteral("start"), {},
                       QStringLiteral("sequence source exceeds maxTextSize"));
    cursor_.skipSeparators();
    const QVector<SequenceToken> header = cursor_.parseList();
    if (header.size() != 1 || header.first().kind != SequenceTokenKind::Header)
      throw parseError(header.isEmpty() ? cursor_.peek() : header.first(),
                       SequenceErrorStage::Detector, SequenceErrorCode::MissingHeader,
                       QStringLiteral("start"), {QStringLiteral("sequenceDiagram")});
    cursor_.skipSeparators();
    while (!cursor_.atEnd()) {
      const QVector<SequenceToken> statement = cursor_.parseList();
      if (!statement.isEmpty() && statement.first().kind == SequenceTokenKind::AccDescr &&
          raw(statement).contains(QLatin1Char('{'))) {
        QStringList lines;
        bool closed = false;
        cursor_.skipSeparators();
        while (!cursor_.atEnd()) {
          const QVector<SequenceToken> line = cursor_.parseList();
          if (raw(line) == QLatin1String("}")) { closed = true; break; }
          if (!line.isEmpty()) lines.append(raw(line).trimmed());
          cursor_.skipSeparators();
        }
        if (!closed)
          throw parseError(statement.first(), SequenceErrorStage::Parser,
                           SequenceErrorCode::MissingEnd, QStringLiteral("acc_descr"),
                           {QStringLiteral("}")});
        data_.accDescription = lines.join(QLatin1Char('\n')).trimmed();
      } else if (!statement.isEmpty()) {
        parseStatement(statement);
      }
      cursor_.skipSeparators();
    }
    if (!frames_.isEmpty())
      throw parseError(frames_.last().opening, SequenceErrorStage::Parser,
                       SequenceErrorCode::MissingEnd, QStringLiteral("statement"),
                       {QStringLiteral("end")});
    if (!pendingCreated_.isEmpty())
      throw parseError(lastToken_, SequenceErrorStage::Semantic,
                       SequenceErrorCode::InvalidCreateMessage, QStringLiteral("signal"), {},
                       QStringLiteral("created participant requires the next message to target it"));
    return data_;
  }

private:
  QString raw(const QVector<SequenceToken>& tokens, qsizetype first = 0) const {
    if (first >= tokens.size()) return {};
    const qsizetype start = tokens[first].offset;
    const SequenceToken& last = tokens.last();
    return source_.mid(start, last.offset + last.length - start).trimmed();
  }

  QString rawAfter(const QVector<SequenceToken>& tokens, qsizetype index) const {
    return raw(tokens, index + 1);
  }

  void requireCapacity(const SequenceToken& token, int current, int maximum,
                       QStringView resource) const {
    if (current >= maximum)
      throw parseError(token, SequenceErrorStage::Resource, SequenceErrorCode::LimitExceeded,
                       QStringLiteral("statement"), {},
                       QStringLiteral("maximum %1 count exceeded").arg(resource));
  }

  int actorIndex(const QString& id) const { return actorIndices_.value(id, -1); }

  SequenceActor& ensureActor(QString id, const SequenceToken& token,
                             QString description = {}, QString type = {}) {
    id = id.trimmed();
    int index = actorIndex(id);
    if (index >= 0) {
      SequenceActor& actor = data_.actors[index];
      if (!description.isEmpty()) actor.description = parseMessageText(description).text;
      if (!type.isEmpty()) actor.type = type;
      return actor;
    }
    requireCapacity(token, data_.actors.size(), limits_.maxActors, QStringLiteral("actor"));
    SequenceActor actor;
    actor.id = actor.name = id;
    const MessageText parsedDescription = parseMessageText(description.isEmpty() ? id : description);
    actor.description = parsedDescription.text;
    actor.wrap = parsedDescription.wrap;
    if (!type.isEmpty()) actor.type = type;
    if (!data_.actors.isEmpty()) {
      actor.prevActor = data_.actors.last().id;
      data_.actors.last().nextActor = id;
    }
    if (currentBox_ >= 0) {
      actor.box = data_.boxes[currentBox_].name;
      data_.boxes[currentBox_].actorKeys.append(id);
    }
    data_.actors.append(actor);
    actorIndices_.insert(id, data_.actors.size() - 1);
    return data_.actors.last();
  }

  void addMessage(QString from, QString to, QJsonValue message, bool wrap, int type,
                  bool activate = false, int placement = -1, int central = 0) {
    requireCapacity(lastToken_, data_.messages.size(), limits_.maxMessages,
                    QStringLiteral("message"));
    SequenceMessage result;
    result.id = QString::number(data_.messages.size());
    result.from = std::move(from);
    result.to = std::move(to);
    result.message = std::move(message);
    result.wrap = wrap;
    result.type = type;
    result.activate = activate;
    result.placement = placement;
    result.centralConnection = central;
    data_.messages.append(result);
  }

  int arrowType(const SequenceToken& token) const {
    static const QMap<QString, int> types = {
        {QStringLiteral("->>"), Solid}, {QStringLiteral("-->>"), Dotted},
        {QStringLiteral("->"), SolidOpen}, {QStringLiteral("-->"), DottedOpen},
        {QStringLiteral("-x"), SolidCross}, {QStringLiteral("--x"), DottedCross},
        {QStringLiteral("-)"), SolidPoint}, {QStringLiteral("--)"), DottedPoint},
        {QStringLiteral("<<->>"), BidirectionalSolid},
        {QStringLiteral("<<-->>"), BidirectionalDotted},
        {QStringLiteral("--|\\"), SolidTopDotted},
        {QStringLiteral("--|/"), SolidBottomDotted},
        {QStringLiteral("--\\\\"), StickTopDotted},
        {QStringLiteral("--//"), StickBottomDotted},
        {QStringLiteral("/|--"), SolidReverseTopDotted},
        {QStringLiteral("\\|--"), SolidReverseBottomDotted},
        {QStringLiteral("//--"), StickReverseTopDotted},
        {QStringLiteral("\\\\--"), StickReverseBottomDotted},
        {QStringLiteral("-|\\"), SolidTop}, {QStringLiteral("-|/"), SolidBottom},
        {QStringLiteral("-\\\\"), StickTop}, {QStringLiteral("-//"), StickBottom},
        {QStringLiteral("/|-"), SolidReverseTop},
        {QStringLiteral("\\|-"), SolidReverseBottom},
        {QStringLiteral("//-"), StickReverseTop},
        {QStringLiteral("\\\\-"), StickReverseBottom},
    };
    const auto found = types.constFind(token.text);
    if (found == types.constEnd())
      throw parseError(token, SequenceErrorStage::Parser, SequenceErrorCode::InvalidArrow,
                       QStringLiteral("signaltype"));
    return found.value();
  }

  void parseParticipant(const QVector<SequenceToken>& tokens, bool create) {
    qsizetype index = create ? 2 : 1;
    if (tokens.size() <= index || tokens[index].kind != SequenceTokenKind::Word)
      throw parseError(tokens.last(), SequenceErrorStage::Parser,
                       SequenceErrorCode::UnexpectedToken, QStringLiteral("participant"),
                       {QStringLiteral("ACTOR")});
    QString id = tokens[index].text;
    QString description;
    if (index + 1 < tokens.size() && tokens[index + 1].kind == SequenceTokenKind::As)
      description = raw(tokens, index + 2);
    QString type = tokens[create ? 1 : 0].kind == SequenceTokenKind::Actor
                       ? QStringLiteral("actor") : QStringLiteral("participant");
    const QString statement = raw(tokens);
    const qsizetype configStart = statement.indexOf(QStringLiteral("@{"));
    if (configStart >= 0) {
      const qsizetype configEnd = statement.lastIndexOf(QLatin1Char('}'));
      if (configEnd <= configStart) {
        SequenceToken opening = tokens[index];
        const qsizetype local = opening.text.indexOf(QStringLiteral("@{"));
        if (local >= 0) {
          opening.offset += local;
          opening.column += static_cast<int>(local);
          opening.length = 2;
          opening.text = QStringLiteral("@{");
        }
        throw parseError(opening, SequenceErrorStage::Parser,
                         SequenceErrorCode::UnexpectedToken, QStringLiteral("config_object"),
                         {QStringLiteral("}")});
      } else {
        const QString prefix = statement.left(configStart).trimmed();
        id = prefix.section(QRegularExpression(QStringLiteral("\\s+")), -1);
        const QJsonObject config = QJsonDocument::fromJson(
            (QLatin1Char('{') + statement.mid(configStart + 2,
                                              configEnd - configStart - 2) + QLatin1Char('}')).toUtf8()).object();
        if (!config.value(QStringLiteral("type")).toString().isEmpty())
          type = config.value(QStringLiteral("type")).toString();
        if (description.isEmpty()) description = config.value(QStringLiteral("alias")).toString();
        const QString suffix = statement.mid(configEnd + 1).trimmed();
        if (suffix.startsWith(QStringLiteral("as "), Qt::CaseInsensitive))
          description = suffix.mid(3).trimmed();
      }
    }
    if (create && actorIndex(id) >= 0)
      throw parseError(tokens[index], SequenceErrorStage::Semantic,
                       SequenceErrorCode::DuplicateParticipant, QStringLiteral("create"));
    ensureActor(id, tokens[index], description, type);
    if (create) {
      pendingCreated_ = id;
      data_.createdActors.insert(id, data_.messages.size());
    }
  }

  void parseSignal(const QVector<SequenceToken>& tokens) {
    qsizetype arrow = -1, colon = -1;
    for (qsizetype i = 0; i < tokens.size(); ++i) {
      if (tokens[i].kind == SequenceTokenKind::Arrow && arrow < 0) arrow = i;
      if (tokens[i].kind == SequenceTokenKind::Colon && arrow >= 0) { colon = i; break; }
    }
    if (arrow <= 0)
      throw parseError(tokens.first(), SequenceErrorStage::Parser,
                       SequenceErrorCode::UnexpectedToken, QStringLiteral("signal"));
    if (colon < 0) {
      qsizetype target = arrow + 1;
      if (target < tokens.size() && tokens[target].kind == SequenceTokenKind::Central) ++target;
      if (target < tokens.size() && (tokens[target].kind == SequenceTokenKind::Plus ||
                                     tokens[target].kind == SequenceTokenKind::Minus)) ++target;
      SequenceToken end = target < tokens.size() ? tokens[target] : tokens[arrow];
      end.offset += end.length;
      end.column += static_cast<int>(end.length);
      end.length = 1;
      end.text = QStringLiteral("statement end");
      throw parseError(end, SequenceErrorStage::Parser,
                       SequenceErrorCode::UnexpectedToken, QStringLiteral("signal"),
                       {QStringLiteral(":")});
    }
    if (colon <= arrow + 1)
      throw parseError(tokens[colon], SequenceErrorStage::Parser,
                       SequenceErrorCode::UnexpectedToken, QStringLiteral("actor"));
    const bool centralBefore = arrow > 0 && tokens[arrow - 1].kind == SequenceTokenKind::Central;
    const qsizetype fromEnd = centralBefore ? tokens[arrow - 1].offset : tokens[arrow].offset;
    const QString from = source_.mid(tokens.first().offset,
                                     fromEnd - tokens.first().offset).trimmed();
    qsizetype targetIndex = arrow + 1;
    const bool centralAfter = targetIndex < tokens.size() &&
                              tokens[targetIndex].kind == SequenceTokenKind::Central;
    if (centralAfter) ++targetIndex;
    bool activate = false, deactivate = false;
    if (tokens[targetIndex].kind == SequenceTokenKind::Plus) { activate = true; ++targetIndex; }
    else if (tokens[targetIndex].kind == SequenceTokenKind::Minus) { deactivate = true; ++targetIndex; }
    if (targetIndex >= colon)
      throw parseError(tokens[arrow], SequenceErrorStage::Parser,
                       SequenceErrorCode::UnexpectedToken, QStringLiteral("actor"));
    const QString to = source_.mid(tokens[targetIndex].offset,
                                   tokens[colon].offset - tokens[targetIndex].offset).trimmed();
    ensureActor(from, tokens.first());
    ensureActor(to, tokens[targetIndex]);
    if (!pendingCreated_.isEmpty()) {
      if (to != pendingCreated_)
        throw parseError(tokens[targetIndex], SequenceErrorStage::Semantic,
                         SequenceErrorCode::InvalidCreateMessage, QStringLiteral("signal"));
      pendingCreated_.clear();
    } else if (!pendingDestroyed_.isEmpty()) {
      if (from != pendingDestroyed_ && to != pendingDestroyed_)
        throw parseError(tokens[targetIndex], SequenceErrorStage::Semantic,
                         SequenceErrorCode::InvalidDestroyMessage, QStringLiteral("signal"));
      pendingDestroyed_.clear();
    }
    const MessageText message = parseMessageText(raw(tokens, colon + 1));
    const bool central = centralBefore || centralAfter;
    const int centralType = centralBefore && centralAfter ? CentralConnectionDual
        : centralAfter ? CentralConnection : centralBefore ? CentralConnectionReverse : 0;
    addMessage(from, to, message.text, message.wrap, arrowType(tokens[arrow]),
               centralAfter || activate, -1, centralType);
    if (centralAfter) {
      addMessage(to, {}, QString{}, false, CentralConnection);
    }
    if (centralBefore) {
      addMessage(from, {}, QString{}, false, CentralConnectionReverse);
    }
    if (activate && !central) {
      addMessage(to, {}, QString{}, false, ActiveStart);
      ++activations_[to];
    }
    if (deactivate) {
      if (activations_.value(from) < 1)
        throw parseError(tokens[arrow], SequenceErrorStage::Semantic,
                         SequenceErrorCode::InactiveParticipant, QStringLiteral("signal"));
      addMessage(from, {}, QString{}, false, ActiveEnd);
      --activations_[from];
    }
  }

  void parseNote(const QVector<SequenceToken>& tokens) {
    qsizetype colon = -1;
    for (qsizetype i = 1; i < tokens.size(); ++i)
      if (tokens[i].kind == SequenceTokenKind::Colon) { colon = i; break; }
    if (colon < 0)
      throw parseError(tokens.first(), SequenceErrorStage::Parser,
                       SequenceErrorCode::UnexpectedToken, QStringLiteral("note"));
    int placement = -1;
    qsizetype actorsStart = 0;
    if (tokens.size() > 3 && tokens[1].kind == SequenceTokenKind::Left &&
        tokens[2].kind == SequenceTokenKind::Of) { placement = 0; actorsStart = 3; }
    else if (tokens.size() > 3 && tokens[1].kind == SequenceTokenKind::Right &&
             tokens[2].kind == SequenceTokenKind::Of) { placement = 1; actorsStart = 3; }
    else if (tokens.size() > 2 && tokens[1].kind == SequenceTokenKind::Over) {
      placement = 2; actorsStart = 2;
    }
    if (placement < 0 || actorsStart >= colon)
      throw parseError(tokens.first(), SequenceErrorStage::Parser,
                       SequenceErrorCode::UnexpectedToken, QStringLiteral("placement"));
    QString actorText = source_.mid(tokens[actorsStart].offset,
                                    tokens[colon].offset - tokens[actorsStart].offset).trimmed();
    QStringList actors = actorText.split(QLatin1Char(','));
    for (QString& actor : actors) actor = actor.trimmed();
    if (actors.size() > 2 || std::any_of(actors.cbegin(), actors.cend(),
                                         [](const QString& actor) { return actor.isEmpty(); })) {
      const auto duplicate = std::adjacent_find(tokens.cbegin() + actorsStart,
                                                tokens.cbegin() + colon,
                                                [](const SequenceToken& left,
                                                   const SequenceToken& right) {
                                                  return left.kind == SequenceTokenKind::Comma &&
                                                         right.kind == SequenceTokenKind::Comma;
                                                });
      throw parseError(duplicate == tokens.cbegin() + colon ? tokens[actorsStart] : *(duplicate + 1),
                       SequenceErrorStage::Parser, SequenceErrorCode::UnexpectedToken,
                       QStringLiteral("actor_pair"));
    }
    ensureActor(actors.first(), tokens[actorsStart]);
    if (actors.size() > 1) ensureActor(actors[1], tokens[actorsStart]);
    const MessageText message = parseMessageText(raw(tokens, colon + 1));
    addMessage(actors.first(), actors.size() > 1 ? actors[1] : actors.first(),
               message.text, message.wrap, Note, false, placement);
  }

  void startFragment(const QVector<SequenceToken>& tokens, SequenceTokenKind kind,
                     int startType, int endType) {
    if (frames_.size() >= limits_.maxFragmentDepth)
      throw parseError(tokens.first(), SequenceErrorStage::Resource,
                       SequenceErrorCode::LimitExceeded, QStringLiteral("statement"));
    const MessageText text = parseMessageText(rawAfter(tokens, 0));
    addMessage({}, {}, text.text, text.wrap, startType);
    frames_.append({kind, endType, tokens.first()});
  }

  void parseStatement(const QVector<SequenceToken>& tokens) {
    lastToken_ = tokens.last();
    const auto invalid = std::find_if(tokens.cbegin(), tokens.cend(), [](const SequenceToken& token) {
      return token.kind == SequenceTokenKind::Invalid;
    });
    if (invalid != tokens.cend())
      throw parseError(*invalid, SequenceErrorStage::Parser,
                       SequenceErrorCode::UnexpectedToken, QStringLiteral("statement"));
    const SequenceTokenKind kind = tokens.first().kind;
    if (kind == SequenceTokenKind::Participant || kind == SequenceTokenKind::Actor) {
      parseParticipant(tokens, false);
    } else if (kind == SequenceTokenKind::Create) {
      if (tokens.size() < 3 || (tokens[1].kind != SequenceTokenKind::Participant &&
                                tokens[1].kind != SequenceTokenKind::Actor))
        throw parseError(tokens.first(), SequenceErrorStage::Parser,
                         SequenceErrorCode::UnexpectedToken, QStringLiteral("create"));
      parseParticipant(tokens, true);
    } else if (kind == SequenceTokenKind::Destroy) {
      if (tokens.size() != 2)
        throw parseError(tokens.first(), SequenceErrorStage::Parser,
                         SequenceErrorCode::UnexpectedToken, QStringLiteral("destroy"));
      pendingDestroyed_ = tokens[1].text;
      data_.destroyedActors.insert(pendingDestroyed_, data_.messages.size());
    } else if (kind == SequenceTokenKind::Activate || kind == SequenceTokenKind::Deactivate) {
      if (tokens.size() != 2)
        throw parseError(tokens.first(), SequenceErrorStage::Parser,
                         SequenceErrorCode::UnexpectedToken, QStringLiteral("activate"));
      const QString actor = tokens[1].text;
      ensureActor(actor, tokens[1]);
      if (kind == SequenceTokenKind::Deactivate && activations_.value(actor) < 1)
        throw parseError(tokens[1], SequenceErrorStage::Semantic,
                         SequenceErrorCode::InactiveParticipant, QStringLiteral("deactivate"));
      addMessage(actor, {}, QString{}, false,
                 kind == SequenceTokenKind::Activate ? ActiveStart : ActiveEnd);
      activations_[actor] += kind == SequenceTokenKind::Activate ? 1 : -1;
    } else if (kind == SequenceTokenKind::Note) {
      parseNote(tokens);
    } else if (kind == SequenceTokenKind::Loop) {
      startFragment(tokens, kind, LoopStart, LoopEnd);
    } else if (kind == SequenceTokenKind::Rect) {
      startFragment(tokens, kind, RectStart, RectEnd);
    } else if (kind == SequenceTokenKind::Opt) {
      startFragment(tokens, kind, OptStart, OptEnd);
    } else if (kind == SequenceTokenKind::Alt) {
      startFragment(tokens, kind, AltStart, AltEnd);
    } else if (kind == SequenceTokenKind::Par || kind == SequenceTokenKind::ParOver) {
      startFragment(tokens, kind, kind == SequenceTokenKind::Par ? ParStart : ParOverStart, ParEnd);
    } else if (kind == SequenceTokenKind::Critical) {
      startFragment(tokens, kind, CriticalStart, CriticalEnd);
    } else if (kind == SequenceTokenKind::Break) {
      startFragment(tokens, kind, BreakStart, BreakEnd);
    } else if (kind == SequenceTokenKind::Else || kind == SequenceTokenKind::And ||
               kind == SequenceTokenKind::Option) {
      const SequenceTokenKind required = kind == SequenceTokenKind::Else ? SequenceTokenKind::Alt
          : kind == SequenceTokenKind::Option ? SequenceTokenKind::Critical
                                               : SequenceTokenKind::Par;
      const bool parMatch = required == SequenceTokenKind::Par && !frames_.isEmpty() &&
                            (frames_.last().kind == SequenceTokenKind::Par ||
                             frames_.last().kind == SequenceTokenKind::ParOver);
      if (frames_.isEmpty() || (!parMatch && frames_.last().kind != required))
        throw parseError(tokens.first(), SequenceErrorStage::Parser,
                         SequenceErrorCode::UnexpectedToken, QStringLiteral("section"));
      const MessageText text = parseMessageText(rawAfter(tokens, 0));
      addMessage({}, {}, text.text, text.wrap,
                 kind == SequenceTokenKind::Else ? AltElse
                     : kind == SequenceTokenKind::And ? ParAnd : CriticalOption);
    } else if (kind == SequenceTokenKind::End) {
      if (frames_.isEmpty())
        throw parseError(tokens.first(), SequenceErrorStage::Parser,
                         SequenceErrorCode::UnexpectedEnd, QStringLiteral("statement"));
      const Frame frame = frames_.takeLast();
      if (frame.kind == SequenceTokenKind::Box) currentBox_ = -1;
      else addMessage({}, {}, QString{}, false, frame.endType);
    } else if (kind == SequenceTokenKind::Box) {
      SequenceBox box;
      const QString boxData = rawAfter(tokens, 0);
      static const QRegularExpression color(
          QStringLiteral(R"(^((?:rgba?|hsla?)\s*\([^)]*\)|[A-Za-z]+)(?:\s+(.*))?$)"));
      const auto match = color.match(boxData);
      if (match.hasMatch()) {
        const QString candidate = match.captured(1);
        const bool functionalColor = candidate.startsWith(QStringLiteral("rgb"), Qt::CaseInsensitive) ||
                                     candidate.startsWith(QStringLiteral("hsl"), Qt::CaseInsensitive);
        if (functionalColor || QColor(candidate).isValid()) {
          box.fill = candidate;
          box.name = parseMessageText(match.captured(2)).text;
        } else {
          box.name = parseMessageText(boxData).text;
        }
      } else {
        box.name = parseMessageText(boxData).text;
      }
      data_.boxes.append(box);
      currentBox_ = data_.boxes.size() - 1;
      frames_.append({SequenceTokenKind::Box, -1, tokens.first()});
    } else if (kind == SequenceTokenKind::Autonumber) {
      QJsonObject value;
      if (tokens.size() > 1 && tokens[1].kind == SequenceTokenKind::Off) {
        value.insert(QStringLiteral("visible"), false);
      } else {
        if (tokens.size() > 1) {
          value.insert(QStringLiteral("start"), tokens[1].text.toDouble());
          value.insert(QStringLiteral("step"),
                       tokens.size() > 2 ? tokens[2].text.toDouble() : 1.0);
        }
        value.insert(QStringLiteral("visible"), true);
      }
      addMessage({}, {}, value, false, Autonumber);
    } else if (kind == SequenceTokenKind::Links || kind == SequenceTokenKind::Link ||
               kind == SequenceTokenKind::Properties || kind == SequenceTokenKind::Details) {
      qsizetype colon = -1;
      for (qsizetype i = 1; i < tokens.size(); ++i)
        if (tokens[i].kind == SequenceTokenKind::Colon) { colon = i; break; }
      if (colon < 0 || tokens.size() < 3)
        throw parseError(tokens.first(), SequenceErrorStage::Parser,
                         SequenceErrorCode::UnexpectedToken, QStringLiteral("actor property"));
      const QString actorId = raw(tokens, 1).section(QLatin1Char(':'), 0, 0).trimmed();
      SequenceActor& actor = ensureActor(actorId, tokens[1]);
      const QString payload = raw(tokens, colon + 1);
      if (kind == SequenceTokenKind::Links || kind == SequenceTokenKind::Properties) {
        const QJsonObject object = QJsonDocument::fromJson(payload.toUtf8()).object();
        QJsonObject& target = kind == SequenceTokenKind::Links ? actor.links : actor.properties;
        for (auto it = object.begin(); it != object.end(); ++it) target.insert(it.key(), it.value());
      } else if (kind == SequenceTokenKind::Link) {
        const qsizetype separator = payload.indexOf(QLatin1Char('@'));
        if (separator > 0)
          actor.links.insert(payload.left(separator).trimmed(),
                             payload.mid(separator + 1).trimmed());
      }
    } else if (kind == SequenceTokenKind::Title || kind == SequenceTokenKind::AccTitle ||
               kind == SequenceTokenKind::AccDescr) {
      QString value = rawAfter(tokens, 0);
      if (value.startsWith(QLatin1Char(':'))) value.remove(0, 1);
      value = value.trimmed();
      if (kind == SequenceTokenKind::Title) data_.title = value;
      else if (kind == SequenceTokenKind::AccTitle) data_.accTitle = value;
      else data_.accDescription = value;
    } else {
      parseSignal(tokens);
    }
  }

  QString source_;
  SequenceLimits limits_;
  TokenCursor cursor_;
  SequenceData data_;
  QMap<QString, int> actorIndices_;
  QMap<QString, int> activations_;
  QVector<Frame> frames_;
  int currentBox_ = -1;
  QString pendingCreated_;
  QString pendingDestroyed_;
  SequenceToken lastToken_;
};

QJsonArray actorArray(const QVector<SequenceActor>& actors) {
  QJsonArray result;
  for (const SequenceActor& actor : actors) {
    QJsonObject value;
    value.insert(QStringLiteral("id"), actor.id);
    value.insert(QStringLiteral("name"), actor.name);
    value.insert(QStringLiteral("description"), actor.description);
    value.insert(QStringLiteral("wrap"), actor.wrap);
    value.insert(QStringLiteral("prevActor"), actor.prevActor.isEmpty()
                     ? QJsonValue(QJsonValue::Null) : QJsonValue(actor.prevActor));
    value.insert(QStringLiteral("nextActor"), actor.nextActor.isEmpty()
                     ? QJsonValue(QJsonValue::Null) : QJsonValue(actor.nextActor));
    value.insert(QStringLiteral("type"), actor.type);
    value.insert(QStringLiteral("box"), actor.box.isEmpty()
                     ? QJsonValue(QJsonValue::Null) : QJsonValue(actor.box));
    value.insert(QStringLiteral("links"), actor.links);
    value.insert(QStringLiteral("properties"), actor.properties);
    result.append(value);
  }
  return result;
}

QJsonArray messageArray(const QVector<SequenceMessage>& messages) {
  QJsonArray result;
  for (const SequenceMessage& message : messages) {
    QJsonObject value;
    value.insert(QStringLiteral("id"), message.id);
    value.insert(QStringLiteral("from"), message.from.isEmpty()
                     ? QJsonValue(QJsonValue::Null) : QJsonValue(message.from));
    value.insert(QStringLiteral("to"), message.to.isEmpty()
                     ? QJsonValue(QJsonValue::Null) : QJsonValue(message.to));
    value.insert(QStringLiteral("message"), message.message);
    value.insert(QStringLiteral("wrap"), message.wrap);
    value.insert(QStringLiteral("type"), message.type);
    value.insert(QStringLiteral("activate"), message.activate);
    value.insert(QStringLiteral("centralConnection"), message.centralConnection);
    value.insert(QStringLiteral("placement"), message.placement < 0
                     ? QJsonValue(QJsonValue::Null) : QJsonValue(message.placement));
    result.append(value);
  }
  return result;
}

}  // namespace

SequenceDiagram SequenceDiagram::parse(const QString& source, SequenceLimits limits) {
  SequenceDiagram diagram;
  diagram.data_ = Parser(source, limits).parse();
  return diagram;
}

QJsonObject SequenceDiagram::toJson() const {
  QJsonObject root;
  root.insert(QStringLiteral("title"), data_.title);
  root.insert(QStringLiteral("accTitle"), data_.accTitle);
  root.insert(QStringLiteral("accDescription"), data_.accDescription);
  root.insert(QStringLiteral("sequenceNumbers"), data_.sequenceNumbers);
  root.insert(QStringLiteral("actors"), actorArray(data_.actors));
  root.insert(QStringLiteral("messages"), messageArray(data_.messages));
  QJsonArray boxes;
  for (const SequenceBox& box : data_.boxes) {
    QJsonObject value;
    value.insert(QStringLiteral("name"), box.name.isEmpty()
                     ? QJsonValue(QJsonValue::Null) : QJsonValue(box.name));
    value.insert(QStringLiteral("fill"), box.fill);
    value.insert(QStringLiteral("wrap"), box.wrap);
    QJsonArray actors;
    for (const QString& actor : box.actorKeys) actors.append(actor);
    value.insert(QStringLiteral("actorKeys"), actors);
    boxes.append(value);
  }
  root.insert(QStringLiteral("boxes"), boxes);
  QJsonObject created, destroyed;
  for (auto it = data_.createdActors.cbegin(); it != data_.createdActors.cend(); ++it)
    created.insert(it.key(), it.value());
  for (auto it = data_.destroyedActors.cbegin(); it != data_.destroyedActors.cend(); ++it)
    destroyed.insert(it.key(), it.value());
  root.insert(QStringLiteral("createdActors"), created);
  root.insert(QStringLiteral("destroyedActors"), destroyed);
  return root;
}

QString sequenceErrorStageName(SequenceErrorStage stage) {
  switch (stage) {
    case SequenceErrorStage::Detector: return QStringLiteral("detector");
    case SequenceErrorStage::Lexer: return QStringLiteral("lexer");
    case SequenceErrorStage::Parser: return QStringLiteral("parser");
    case SequenceErrorStage::Semantic: return QStringLiteral("semantic");
    case SequenceErrorStage::Resource: return QStringLiteral("resource");
  }
  return QStringLiteral("parser");
}

QString sequenceErrorCodeName(SequenceErrorCode code) {
  switch (code) {
    case SequenceErrorCode::Generic: return QStringLiteral("generic");
    case SequenceErrorCode::MissingHeader: return QStringLiteral("missing-header");
    case SequenceErrorCode::UnexpectedToken: return QStringLiteral("unexpected-token");
    case SequenceErrorCode::MissingEnd: return QStringLiteral("missing-end");
    case SequenceErrorCode::UnexpectedEnd: return QStringLiteral("unexpected-end");
    case SequenceErrorCode::InvalidArrow: return QStringLiteral("invalid-arrow");
    case SequenceErrorCode::InactiveParticipant: return QStringLiteral("inactive-participant");
    case SequenceErrorCode::InvalidCreateMessage: return QStringLiteral("invalid-create-message");
    case SequenceErrorCode::InvalidDestroyMessage: return QStringLiteral("invalid-destroy-message");
    case SequenceErrorCode::DuplicateParticipant: return QStringLiteral("duplicate-participant");
    case SequenceErrorCode::LimitExceeded: return QStringLiteral("limit-exceeded");
  }
  return QStringLiteral("generic");
}

QString formatSequenceDiagnostic(const SequenceDiagnostic& diagnostic) {
  QString result = QStringLiteral("sequence %1/%2 at %3:%4")
                       .arg(sequenceErrorStageName(diagnostic.stage),
                            sequenceErrorCodeName(diagnostic.code))
                       .arg(diagnostic.span.line)
                       .arg(diagnostic.span.column);
  if (!diagnostic.production.isEmpty())
    result += QStringLiteral(" in %1").arg(diagnostic.production);
  if (!diagnostic.actual.isEmpty()) result += QStringLiteral(": got %1").arg(diagnostic.actual);
  if (!diagnostic.detail.isEmpty()) result += QStringLiteral(" (%1)").arg(diagnostic.detail);
  return result;
}

SequenceParseError::SequenceParseError(SequenceDiagnostic diagnostic)
    : std::runtime_error(formatSequenceDiagnostic(diagnostic).toUtf8().constData()),
      diagnostic_(std::move(diagnostic)) {}

}  // namespace muffin::mermaid::sequence

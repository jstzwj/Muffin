#include "mermaid/gitgraph/GitGraphDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <QRegularExpression>
#include <QHash>

#include <algorithm>

namespace muffin::mermaid::gitgraph {
namespace {

struct Token {
  QString text;
  bool quoted = false;
  int column = 1;
};

QString sanitize(const QString& value) {
  return HtmlSanitizer().sanitizedMermaidText(value.trimmed());
}

[[noreturn]] void error(GitGraphErrorKind kind, int line, int column,
                        const QString& token, const QString& message) {
  throw GitGraphParseError(kind, kind == GitGraphErrorKind::Runtime ? 0 : line,
                           kind == GitGraphErrorKind::Runtime ? 0 : column,
                           token, message);
}

QVector<Token> tokenize(const QString& text, int line) {
  QVector<Token> result;
  qsizetype i = 0;
  while (i < text.size()) {
    while (i < text.size() && (text.at(i) == u' ' || text.at(i) == u'\t')) ++i;
    if (i >= text.size()) break;
    const int column = static_cast<int>(i) + 1;
    const QChar ch = text.at(i);
    if (ch == u';')
      error(GitGraphErrorKind::Lexer, line, column, QString(ch),
            QStringLiteral("unexpected character: ->;<-"));
    if (ch == u'\'' || ch == u'\"') {
      const QChar quote = ch;
      ++i;
      QString value;
      bool closed = false;
      while (i < text.size()) {
        const QChar current = text.at(i++);
        if (current == quote) {
          closed = true;
          break;
        }
        if (current == u'\\' && i < text.size()) {
          const QChar escaped = text.at(i++);
          switch (escaped.unicode()) {
            case 'n': value += u'\n'; break;
            case 'r': value += u'\r'; break;
            case 't': value += u'\t'; break;
            default: value += escaped; break;
          }
        } else {
          value += current;
        }
      }
      if (!closed)
        error(GitGraphErrorKind::Lexer, line, column, text.mid(column - 1),
              QStringLiteral("unterminated string"));
      result.push_back({value, true, column});
      continue;
    }
    qsizetype end = i;
    while (end < text.size() && text.at(end) != u' ' && text.at(end) != u'\t' &&
           text.at(end) != u';') ++end;
    result.push_back({text.mid(i, end - i), false, column});
    i = end;
  }
  return result;
}

QString optionValue(QVector<Token>& tokens, qsizetype& index, const QString& option,
                    int line) {
  if (index >= tokens.size() || !tokens.at(index).quoted)
    error(GitGraphErrorKind::Parser, line,
          index < tokens.size() ? tokens.at(index).column : 1,
          index < tokens.size() ? tokens.at(index).text : QString(),
          QStringLiteral("Expected STRING after %1").arg(option));
  return tokens.at(index++).text;
}

CommitType parseType(const QString& value, int line, int column) {
  if (value == QLatin1String("NORMAL")) return CommitType::Normal;
  if (value == QLatin1String("REVERSE")) return CommitType::Reverse;
  if (value == QLatin1String("HIGHLIGHT")) return CommitType::Highlight;
  error(GitGraphErrorKind::Parser, line, column, value,
        QStringLiteral("Expected NORMAL, REVERSE, or HIGHLIGHT"));
}

class Parser {
public:
  Parser(QString source, GitGraphParseConfig config)
      : source_(std::move(source)), config_(std::move(config)) {
    GitBranch main;
    main.name = config_.mainBranchName;
    data_.branches.push_back(main);
    explicitOrders_.insert(main.name, config_.mainBranchOrder);
    data_.currentBranch = main.name;
  }

  GitGraphData run() {
    QString normalized = source_;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QStringList lines = normalized.split(u'\n', Qt::KeepEmptyParts);
    int headerLine = -1;
    QString remainder;
    for (int i = 0; i < lines.size(); ++i) {
      const QString trimmed = lines.at(i).trimmed();
      if (trimmed.isEmpty() || trimmed.startsWith(QLatin1String("%%"))) continue;
      headerLine = i;
      if (!trimmed.startsWith(QLatin1String("gitGraph")) ||
          (trimmed.size() > 8 && !trimmed.at(8).isSpace() &&
           trimmed.at(8) != u':'))
        error(GitGraphErrorKind::Parser, i + 1, 1, trimmed,
              QStringLiteral("Expected gitGraph header"));
      QString tail = trimmed.mid(8);
      if (tail.startsWith(u':')) tail.remove(0, 1);
      tail = tail.trimmed();
      static const QRegularExpression directionHeader(
          QStringLiteral(R"(^(LR|TB|BT)\s*:(?:\s*(.*))?$)"));
      const QRegularExpressionMatch directionMatch = directionHeader.match(tail);
      if (directionMatch.hasMatch()) {
        const QString value = directionMatch.captured(1);
        if (value == QLatin1String("TB"))
          data_.direction = Direction::TopToBottom;
        else if (value == QLatin1String("BT"))
          data_.direction = Direction::BottomToTop;
        remainder = directionMatch.captured(2);
      } else {
        remainder = tail;
        if (!remainder.isEmpty()) {
          const QString first = remainder.section(
              QRegularExpression(QStringLiteral("\\s+")), 0, 0);
          static const QStringList statements = {
              QStringLiteral("commit"), QStringLiteral("branch"),
              QStringLiteral("checkout"), QStringLiteral("switch"),
              QStringLiteral("merge"), QStringLiteral("cherry-pick"),
              QStringLiteral("title"), QStringLiteral("accTitle:"),
              QStringLiteral("accDescr:")};
          if (!statements.contains(first)) {
            const int column = static_cast<int>(trimmed.indexOf(remainder)) + 1;
            error(GitGraphErrorKind::Parser, i + 1, column, first,
                  QStringLiteral("Unexpected token after gitGraph header"));
          }
        }
      }
      break;
    }
    if (headerLine < 0)
      error(GitGraphErrorKind::Parser, 1, 1, QString(),
            QStringLiteral("Expected gitGraph header"));
    if (!remainder.isEmpty()) statement(remainder, headerLine + 1);
    for (int i = headerLine + 1; i < lines.size(); ++i) {
      const QString trimmed = lines.at(i).trimmed();
      if (trimmed.isEmpty() || trimmed.startsWith(QLatin1String("%%"))) continue;
      if (trimmed.startsWith(QLatin1String("accDescr")) && trimmed.contains(u'{')) {
        QString block = trimmed.mid(trimmed.indexOf(u'{') + 1);
        while (!block.contains(u'}') && ++i < lines.size()) block += u'\n' + lines.at(i);
        if (!block.contains(u'}'))
          error(GitGraphErrorKind::Parser, i + 1, 1, trimmed,
                QStringLiteral("Unclosed accDescr block"));
        data_.accDescr = sanitize(block.left(block.indexOf(u'}')).replace(
            QRegularExpression(QStringLiteral("\n\\s+")), QStringLiteral("\n")));
        continue;
      }
      statement(trimmed, i + 1);
    }
    finalizeBranches();
    return data_;
  }

private:
  GitBranch* branch(const QString& name) {
    for (GitBranch& value : data_.branches) if (value.name == name) return &value;
    return nullptr;
  }
  const GitCommit* commitById(const QString& id) const {
    for (const GitCommit& value : data_.commits) if (value.id == id) return &value;
    return nullptr;
  }
  GitCommit* commitById(const QString& id) {
    for (GitCommit& value : data_.commits) if (value.id == id) return &value;
    return nullptr;
  }
  QString headId() const {
    for (const GitBranch& value : data_.branches)
      if (value.name == data_.currentBranch) return value.hasHead ? value.head : QString();
    return {};
  }
  void setHead(const QString& id) {
    GitBranch* value = branch(data_.currentBranch);
    if (value) { value->head = id; value->hasHead = true; }
  }
  void pushCommit(GitCommit value) {
    const QString id = value.id;
    if (GitCommit* existing = commitById(id)) *existing = value;
    else data_.commits.push_back(value);
    setHead(id);
  }

  void statement(const QString& text, int line) {
    if (text.startsWith(QLatin1String("title")) &&
        (text.size() == 5 || text.at(5).isSpace())) {
      data_.title = sanitize(text.mid(5)); return;
    }
    if (text.startsWith(QLatin1String("accTitle"))) {
      const int colon = text.indexOf(u':');
      if (colon < 0) error(GitGraphErrorKind::Parser, line, 1, text, QStringLiteral("Expected ':'"));
      data_.accTitle = sanitize(text.mid(colon + 1)); return;
    }
    if (text.startsWith(QLatin1String("accDescr"))) {
      const int colon = text.indexOf(u':');
      if (colon < 0) error(GitGraphErrorKind::Parser, line, 1, text, QStringLiteral("Expected ':'"));
      data_.accDescr = sanitize(text.mid(colon + 1)); return;
    }
    QVector<Token> tokens = tokenize(text, line);
    if (tokens.isEmpty()) return;
    const QString command = tokens.front().text;
    if (command == QLatin1String("commit")) parseCommit(tokens, line);
    else if (command == QLatin1String("branch")) parseBranch(tokens, line);
    else if (command == QLatin1String("checkout") || command == QLatin1String("switch")) parseCheckout(tokens, line);
    else if (command == QLatin1String("merge")) parseMerge(tokens, line);
    else if (command == QLatin1String("cherry-pick")) parseCherry(tokens, line);
    else error(GitGraphErrorKind::Parser, line, tokens.front().column, command,
               QStringLiteral("Unexpected statement %1").arg(command));
  }

  void parseCommit(QVector<Token>& tokens, int line) {
    GitCommit value;
    value.seq = seq_++;
    value.branch = data_.currentBranch;
    value.type = CommitType::Normal;
    const QString head = headId();
    if (!head.isEmpty()) value.parents.push_back(head);
    for (qsizetype i = 1; i < tokens.size();) {
      const Token token = tokens.at(i++);
      if (token.text == QLatin1String("id:")) value.id = optionValue(tokens, i, token.text, line);
      else if (token.text == QLatin1String("msg:")) value.message = optionValue(tokens, i, token.text, line);
      else if (token.text == QLatin1String("tag:")) value.tags.push_back(optionValue(tokens, i, token.text, line));
      else if (token.text == QLatin1String("type:")) {
        if (i >= tokens.size()) error(GitGraphErrorKind::Parser, line, token.column, token.text, QStringLiteral("Expected commit type"));
        value.type = parseType(tokens.at(i).text, line, tokens.at(i).column); ++i;
      } else if (token.quoted) value.message = token.text;
      else error(GitGraphErrorKind::Parser, line, token.column, token.text, QStringLiteral("Unexpected commit option"));
    }
    value.id = sanitize(value.id);
    value.message = sanitize(value.message);
    for (QString& tag : value.tags) tag = sanitize(tag);
    if (value.id.isEmpty()) value.id = QStringLiteral("@generated:%1").arg(value.seq);
    pushCommit(std::move(value));
  }

  void parseBranch(QVector<Token>& tokens, int line) {
    if (tokens.size() < 2)
      error(GitGraphErrorKind::Parser, line, 1, QStringLiteral("branch"), QStringLiteral("Expected branch name"));
    const QString name = sanitize(tokens.at(1).text);
    if (!tokens.at(1).quoted && !QRegularExpression(QStringLiteral(R"(^\w(?:[-\w]*\w)?$)")).match(name).hasMatch())
      error(GitGraphErrorKind::Lexer, line, tokens.at(1).column, name, QStringLiteral("Invalid branch name"));
    std::optional<qreal> order;
    if (tokens.size() > 2) {
      if (tokens.size() != 4 || tokens.at(2).text != QLatin1String("order:") ||
          !QRegularExpression(QStringLiteral(R"(^(0|[1-9][0-9]*)$)")).match(tokens.at(3).text).hasMatch())
        error(GitGraphErrorKind::Parser, line,
              tokens.size() > 3 ? tokens.at(3).column : tokens.at(2).column,
              tokens.size() > 3 ? tokens.at(3).text : tokens.at(2).text,
              QStringLiteral("Invalid branch order"));
      order = tokens.at(3).text.toDouble();
    }
    if (branch(name))
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens),
            QStringLiteral("Trying to create an existing branch. (Help: Either use a new name if you want create a new branch or try using \"checkout %1\")").arg(name));
    GitBranch value; value.name = name;
    const QString head = headId(); if (!head.isEmpty()) { value.head = head; value.hasHead = true; }
    data_.branches.push_back(value);
    if (order) explicitOrders_.insert(name, *order);
    data_.currentBranch = name;
  }

  void parseCheckout(const QVector<Token>& tokens, int line) {
    if (tokens.size() != 2)
      error(GitGraphErrorKind::Parser, line, 1, textFor(tokens), QStringLiteral("Expected branch name"));
    const QString name = sanitize(tokens.at(1).text);
    if (!branch(name))
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens),
            QStringLiteral("Trying to checkout branch which is not yet created. (Help try using \"branch %1\")").arg(name));
    data_.currentBranch = name;
  }

  void parseMerge(QVector<Token>& tokens, int line) {
    if (tokens.size() < 2)
      error(GitGraphErrorKind::Parser, line, 1, QStringLiteral("merge"), QStringLiteral("Expected branch name"));
    const QString other = sanitize(tokens.at(1).text);
    QString customId; QVector<QString> tags; std::optional<CommitType> customType;
    for (qsizetype i = 2; i < tokens.size();) {
      const Token token = tokens.at(i++);
      if (token.text == QLatin1String("id:")) customId = optionValue(tokens, i, token.text, line);
      else if (token.text == QLatin1String("tag:")) tags.push_back(optionValue(tokens, i, token.text, line));
      else if (token.text == QLatin1String("type:")) {
        if (i >= tokens.size()) error(GitGraphErrorKind::Parser, line, token.column, token.text, QStringLiteral("Expected merge type"));
        customType = parseType(tokens.at(i).text, line, tokens.at(i).column); ++i;
      } else error(GitGraphErrorKind::Parser, line, token.column, token.text, QStringLiteral("Unexpected merge option"));
    }
    const GitBranch* current = branch(data_.currentBranch);
    const GitBranch* otherBranch = branch(other);
    const GitCommit* currentCommit = current && current->hasHead ? commitById(current->head) : nullptr;
    const GitCommit* otherCommit = otherBranch && otherBranch->hasHead ? commitById(otherBranch->head) : nullptr;
    if (currentCommit && otherCommit && currentCommit->branch == other)
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens), QStringLiteral("Cannot merge branch '%1' into itself.").arg(other));
    if (data_.currentBranch == other)
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens), QStringLiteral("Incorrect usage of \"merge\". Cannot merge a branch to itself"));
    if (!currentCommit)
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens), QStringLiteral("Incorrect usage of \"merge\". Current branch (%1)has no commits").arg(data_.currentBranch));
    if (!otherBranch)
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens), QStringLiteral("Incorrect usage of \"merge\". Branch to be merged (%1) does not exist").arg(other));
    if (!otherCommit)
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens), QStringLiteral("Incorrect usage of \"merge\". Branch to be merged (%1) has no commits").arg(other));
    if (currentCommit->id == otherCommit->id)
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens), QStringLiteral("Incorrect usage of \"merge\". Both branches have same head"));
    customId = sanitize(customId);
    if (!customId.isEmpty() && commitById(customId))
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens), QStringLiteral("Incorrect usage of \"merge\". Commit with id:%1 already exists, use different custom id").arg(customId));
    GitCommit value; value.seq = seq_++; value.id = customId.isEmpty() ? QStringLiteral("@generated:%1").arg(value.seq) : customId;
    value.message = QStringLiteral("merged branch %1 into %2").arg(other, data_.currentBranch);
    value.parents = {currentCommit->id, otherCommit->id}; value.branch = data_.currentBranch;
    value.type = CommitType::Merge; value.customType = customType; value.customId = !customId.isEmpty();
    for (QString tag : tags) value.tags.push_back(sanitize(tag));
    pushCommit(std::move(value));
  }

  void parseCherry(QVector<Token>& tokens, int line) {
    QString sourceId; QString parent; QVector<QString> tags;
    for (qsizetype i = 1; i < tokens.size();) {
      const Token token = tokens.at(i++);
      if (token.text == QLatin1String("id:")) sourceId = optionValue(tokens, i, token.text, line);
      else if (token.text == QLatin1String("tag:")) tags.push_back(optionValue(tokens, i, token.text, line));
      else if (token.text == QLatin1String("parent:")) parent = optionValue(tokens, i, token.text, line);
      else error(GitGraphErrorKind::Parser, line, token.column, token.text, QStringLiteral("Unexpected cherry-pick option"));
    }
    sourceId = sanitize(sourceId); parent = sanitize(parent);
    const GitCommit* source = commitById(sourceId);
    if (!source)
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens), QStringLiteral("Incorrect usage of \"cherryPick\". Source commit id should exist and provided"));
    if (!parent.isEmpty() && !source->parents.contains(parent))
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens), QStringLiteral("Invalid operation: The specified parent commit is not an immediate parent of the cherry-picked commit."));
    if (source->type == CommitType::Merge && parent.isEmpty())
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens), QStringLiteral("Incorrect usage of cherry-pick: If the source commit is a merge commit, an immediate parent commit must be specified."));
    if (source->branch == data_.currentBranch)
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens), QStringLiteral("Incorrect usage of \"cherryPick\". Source commit is already on current branch"));
    const GitCommit* current = commitById(headId());
    if (!current)
      error(GitGraphErrorKind::Runtime, line, 1, textFor(tokens), QStringLiteral("Incorrect usage of \"cherry-pick\". Current branch (%1)has no commits").arg(data_.currentBranch));
    GitCommit value; value.seq = seq_++; value.id = QStringLiteral("@generated:%1").arg(value.seq);
    value.message = QStringLiteral("cherry-picked %1 into %2").arg(source->message, data_.currentBranch);
    value.parents = {current->id, source->id}; value.branch = data_.currentBranch; value.type = CommitType::CherryPick;
    if (tags.isEmpty()) value.tags = {QStringLiteral("cherry-pick:%1%2").arg(source->id, source->type == CommitType::Merge ? QStringLiteral("|parent:%1").arg(parent) : QString())};
    else for (QString tag : tags) if (!tag.isEmpty()) value.tags.push_back(sanitize(tag));
    pushCommit(std::move(value));
  }

  static QString textFor(const QVector<Token>& tokens) {
    QStringList parts; for (const Token& token : tokens) parts.push_back(token.text); return parts.join(u' ');
  }
  void finalizeBranches() {
    QVector<const GitBranch*> ordered;
    for (const GitBranch& value : data_.branches) ordered.push_back(&value);
    std::stable_sort(ordered.begin(), ordered.end(), [this](const GitBranch* a, const GitBranch* b) {
      auto effective = [this](const GitBranch* branchValue) {
        const auto explicitOrder = explicitOrders_.constFind(branchValue->name);
        if (explicitOrder != explicitOrders_.constEnd()) return *explicitOrder;
        for (qsizetype i = 0; i < data_.branches.size(); ++i)
          if (&data_.branches.at(i) == branchValue)
            return QStringLiteral("0.%1").arg(i).toDouble();
        return 0.0;
      };
      return effective(a) < effective(b);
    });
    for (const GitBranch* value : ordered) data_.orderedBranches.push_back(value->name);
  }

  QString source_;
  GitGraphParseConfig config_;
  GitGraphData data_;
  QHash<QString, qreal> explicitOrders_;
  int seq_ = 0;
};

}  // namespace

GitGraphParseError::GitGraphParseError(GitGraphErrorKind kindValue,
                                       int lineValue, int columnValue,
                                       QString tokenValue,
                                       const QString& message)
    : std::runtime_error(message.toStdString()), kind(kindValue),
      line(lineValue), column(columnValue), token(std::move(tokenValue)) {}

GitGraphData GitGraphDiagram::parse(const QString& source,
                                    const GitGraphParseConfig& config) {
  return Parser(source, config).run();
}

}  // namespace muffin::mermaid::gitgraph

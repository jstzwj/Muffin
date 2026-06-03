#include "math/MathMacroExpander.h"

#include "math/MathParseError.h"

#include <QStringList>
#include <QtGlobal>

#include <algorithm>

namespace muffin::math {
namespace {

using MacroToken = MathMacroExpander::MacroToken;

bool isCommandStart(const QString& text, qsizetype pos) {
  return pos < text.size() && text.at(pos) == QLatin1Char('\\');
}

QVector<MacroToken> tokenize(const QString& input) {
  QVector<MacroToken> tokens;
  for (qsizetype pos = 0; pos < input.size();) {
    const qsizetype start = pos;
    const QChar ch = input.at(pos++);
    if (ch == QLatin1Char('\\')) {
      if (pos < input.size() && (input.at(pos).isLetter() || input.at(pos) == QLatin1Char('@'))) {
        while (pos < input.size() && (input.at(pos).isLetter() || input.at(pos) == QLatin1Char('@'))) {
          ++pos;
        }
      } else if (pos < input.size()) {
        ++pos;
      }
      tokens.push_back({input.mid(start, pos - start), start, pos});
    } else {
      tokens.push_back({QString(ch), start, pos});
    }
  }
  return tokens;
}

QString tokensToString(const QVector<MacroToken>& tokens) {
  QString result;
  for (const MacroToken& token : tokens) {
    if (!token.treatAsRelax) {
      result += token.text;
    }
  }
  return result;
}

QVector<MacroToken> reversedTokensFromString(const QString& input, qsizetype position = 0) {
  QVector<MacroToken> tokens = tokenize(input);
  for (MacroToken& token : tokens) {
    if (token.position < 0) {
      token.position = position;
      token.endPosition = position;
    } else if (token.endPosition < token.position) {
      token.endPosition = token.position + token.text.size();
    }
  }
  std::reverse(tokens.begin(), tokens.end());
  return tokens;
}

}  // namespace

class MathMacroExpander::TokenStream {
public:
  explicit TokenStream(QString input) {
    QVector<MacroToken> tokens = tokenize(input);
    for (int i = tokens.size() - 1; i >= 0; --i) {
      stack_.push_back(tokens.at(i));
    }
  }

  bool empty() const {
    return stack_.isEmpty();
  }

  MacroToken future() {
    return stack_.isEmpty() ? MacroToken{QStringLiteral("EOF"), -1, -1} : stack_.last();
  }

  MacroToken popToken() {
    if (stack_.isEmpty()) {
      return {QStringLiteral("EOF"), -1, -1};
    }
    return stack_.takeLast();
  }

  void pushToken(MacroToken token) {
    stack_.push_back(std::move(token));
  }

  void pushTokens(const QVector<MacroToken>& reversedTokens) {
    for (const MacroToken& token : reversedTokens) {
      stack_.push_back(token);
    }
  }

  void consumeSpaces() {
    while (!stack_.isEmpty() && stack_.last().text.trimmed().isEmpty()) {
      stack_.removeLast();
    }
  }

  QVector<MacroToken> consumeArg() {
    consumeSpaces();
    QVector<MacroToken> result;
    if (stack_.isEmpty()) {
      return result;
    }
    MacroToken first = popToken();
    if (first.text != QStringLiteral("{")) {
      result.push_back(first);
      return result;
    }
    int depth = 1;
    while (!stack_.isEmpty() && depth > 0) {
      MacroToken token = popToken();
      if (token.text == QStringLiteral("{")) {
        ++depth;
        result.push_back(token);
      } else if (token.text == QStringLiteral("}")) {
        --depth;
        if (depth > 0) {
          result.push_back(token);
        }
      } else {
        result.push_back(token);
      }
    }
    return result;
  }

  QString consumeArgText() {
    return tokensToString(consumeArg());
  }

  QString consumeCommandArgument() {
    QVector<MacroToken> arg = consumeArg();
    if (arg.size() == 1 && arg.first().text.startsWith(QLatin1Char('\\'))) {
      return arg.first().text;
    }
    if (!arg.isEmpty() && arg.first().text.startsWith(QLatin1Char('\\'))) {
      return arg.first().text;
    }
    return {};
  }

  QString consumeBracketText() {
    consumeSpaces();
    if (future().text != QStringLiteral("[")) {
      return {};
    }
    popToken();
    QVector<MacroToken> result;
    int depth = 1;
    while (!stack_.isEmpty() && depth > 0) {
      MacroToken token = popToken();
      if (token.text == QStringLiteral("[")) {
        ++depth;
        result.push_back(token);
      } else if (token.text == QStringLiteral("]")) {
        --depth;
        if (depth > 0) {
          result.push_back(token);
        }
      } else {
        result.push_back(token);
      }
    }
    return tokensToString(result);
  }

  QString consumeUntilGroupStart() {
    QString text;
    while (!stack_.isEmpty() && future().text != QStringLiteral("{")) {
      text += popToken().text;
    }
    return text;
  }

private:
  QVector<MacroToken> stack_;
};

namespace {

int countDefArgs(const QString& parameterText) {
  int maxArg = 0;
  for (qsizetype i = 0; i + 1 < parameterText.size(); ++i) {
    if (parameterText.at(i) == QLatin1Char('#') && parameterText.at(i + 1).isDigit()) {
      maxArg = qMax(maxArg, parameterText.at(i + 1).digitValue());
    }
  }
  return qBound(0, maxArg, 9);
}

QVector<MacroToken> applyArgs(QVector<MacroToken> body, const QVector<QVector<MacroToken>>& args) {
  QVector<MacroToken> expanded;
  for (int i = 0; i < body.size(); ++i) {
    const MacroToken& token = body.at(i);
    if (token.text == QStringLiteral("#") && i + 1 < body.size()) {
      const MacroToken& next = body.at(++i);
      if (next.text == QStringLiteral("#")) {
        expanded.push_back(token);
      } else {
        bool ok = false;
        const int index = next.text.toInt(&ok);
        if (ok && index >= 1 && index <= args.size()) {
          expanded += args.at(index - 1);
        }
      }
    } else {
      expanded.push_back(token);
    }
  }
  std::reverse(expanded.begin(), expanded.end());
  return expanded;
}

}  // namespace

MathMacroExpander::MathMacroExpander(MathSettings settings) : settings_(std::move(settings)) {
  defineMacro(QStringLiteral("\\lt"), QStringLiteral("<"));
  defineMacro(QStringLiteral("\\gt"), QStringLiteral(">"));
  defineMacro(QStringLiteral("\\ne"), QStringLiteral("\\neq"));
  defineMacro(QStringLiteral("\\land"), QStringLiteral("\\wedge"));
  defineMacro(QStringLiteral("\\lor"), QStringLiteral("\\vee"));
  defineMacro(QStringLiteral("\\gets"), QStringLiteral("\\leftarrow"));
  defineMacro(QStringLiteral("\\to"), QStringLiteral("\\rightarrow"));
  defineMacro(QStringLiteral("\\notag"), QStringLiteral(""));
  defineMacro(QStringLiteral("\\nonumber"), QStringLiteral(""));
  defineMacro(QStringLiteral("\\bgroup"), QStringLiteral("{"));
  defineMacro(QStringLiteral("\\egroup"), QStringLiteral("}"));
  defineMacro(QStringLiteral("\\lq"), QStringLiteral("`"));
  defineMacro(QStringLiteral("\\rq"), QStringLiteral("'"));
  defineMacro(QStringLiteral("\\aa"), QStringLiteral("\\r a"));
  defineMacro(QStringLiteral("\\AA"), QStringLiteral("\\r A"));
  defineMacro(QStringLiteral("\\alef"), QStringLiteral("\\aleph"));
  defineMacro(QStringLiteral("\\alefsym"), QStringLiteral("\\aleph"));
  defineMacro(QStringLiteral("\\And"), QStringLiteral("\\mathbin{\\&}"));
  defineMacro(QStringLiteral("\\implies"), QStringLiteral("\\DOTSB\\;\\Longrightarrow\\;"));
  defineMacro(QStringLiteral("\\impliedby"), QStringLiteral("\\DOTSB\\;\\Longleftarrow\\;"));
  defineMacro(QStringLiteral("\\iff"), QStringLiteral("\\DOTSB\\;\\Longleftrightarrow\\;"));
  defineMacro(QStringLiteral("\\bmod"), QStringLiteral("\\mathbin{\\rm mod}"));
  defineMacro(QStringLiteral("\\pod"), QStringLiteral("\\allowbreak\\mathchoice{\\mkern18mu}{\\mkern8mu}{\\mkern8mu}{\\mkern8mu}(#1)"), 1);
  defineMacro(QStringLiteral("\\pmod"), QStringLiteral("\\pod{\\rm mod\\mkern6mu #1}"), 1);
  defineMacro(QStringLiteral("\\mod"), QStringLiteral("\\allowbreak\\mathchoice{\\mkern18mu}{\\mkern12mu}{\\mkern12mu}{\\mkern12mu}{\\rm mod}\\mkern6mu #1"), 1);
  defineMacro(QStringLiteral("\\cr"), QStringLiteral("\\\\"));
  defineMacro(QStringLiteral("\\crcr"), QStringLiteral("\\\\"));
  defineMacro(QStringLiteral("\\DOTSB"), QStringLiteral(""));
  defineMacro(QStringLiteral("\\allowbreak"), QStringLiteral(""));
  defineMacro(QStringLiteral("\\@firstoftwo"), QStringLiteral("#1"), 2);
  defineMacro(QStringLiteral("\\@secondoftwo"), QStringLiteral("#2"), 2);
  defineMacro(QStringLiteral("\\@ifstar"), QStringLiteral("\\@ifnextchar *{\\@firstoftwo{#1}}"), 1);

  builtins_ = macros_;
}

void MathMacroExpander::defineMacro(QString name, QString replacement) {
  defineMacro(std::move(name), std::move(replacement), 0);
}

void MathMacroExpander::defineMacro(QString name, QString replacement, int numArgs) {
  macros_.insert(std::move(name), Macro{std::move(replacement), qBound(0, numArgs, 9), false});
}

void MathMacroExpander::beginGroup() {
  undoStack_.push_back({});
}

void MathMacroExpander::endGroup() {
  if (undoStack_.isEmpty()) {
    throw MathParseError(QStringLiteral("Unbalanced namespace destruction: attempt to pop global namespace"));
  }
  const QHash<QString, std::optional<Macro>> undo = undoStack_.takeLast();
  for (auto it = undo.cbegin(); it != undo.cend(); ++it) {
    if (it.value().has_value()) {
      macros_.insert(it.key(), it.value().value());
    } else {
      macros_.remove(it.key());
    }
  }
}

void MathMacroExpander::endGroups() {
  while (!undoStack_.isEmpty()) {
    endGroup();
  }
}

bool MathMacroExpander::hasMacro(const QString& name) const {
  return macros_.contains(name) || builtins_.contains(name);
}

MathMacroExpander::Macro MathMacroExpander::macro(const QString& name) const {
  if (macros_.contains(name)) {
    return macros_.value(name);
  }
  return builtins_.value(name);
}

void MathMacroExpander::setMacro(const QString& name, const Macro& macro, bool global) {
  if (global) {
    for (QHash<QString, std::optional<Macro>>& undo : undoStack_) {
      undo.remove(name);
    }
    if (!undoStack_.isEmpty()) {
      undoStack_.last().insert(name, macro);
    }
  } else if (!undoStack_.isEmpty() && !undoStack_.last().contains(name)) {
    if (macros_.contains(name)) {
      undoStack_.last().insert(name, macros_.value(name));
    } else {
      undoStack_.last().insert(name, std::nullopt);
    }
  }
  macros_.insert(name, macro);
}

void MathMacroExpander::undefineMacro(const QString& name, bool global) {
  if (global) {
    for (QHash<QString, std::optional<Macro>>& undo : undoStack_) {
      undo.remove(name);
    }
  } else if (!undoStack_.isEmpty() && !undoStack_.last().contains(name)) {
    if (macros_.contains(name)) {
      undoStack_.last().insert(name, macros_.value(name));
    } else {
      undoStack_.last().insert(name, std::nullopt);
    }
  }
  macros_.remove(name);
}

void MathMacroExpander::countExpansion(int amount, qsizetype position, qsizetype endPosition) {
  expansionCount_ += amount;
  if (settings_.maxExpand >= 0 && expansionCount_ > settings_.maxExpand) {
    throw MathParseError(QStringLiteral("Too many expansions: infinite loop or need to increase maxExpand setting"), {}, position, endPosition);
  }
}

QString MathMacroExpander::expand(QString input) {
  expansionCount_ = 0;
  TokenStream stream(std::move(input));
  QVector<MacroToken> output;
  while (!stream.empty()) {
    MacroToken token = expandNextToken(stream);
    if (token.text == QStringLiteral("EOF")) {
      break;
    }
    if (token.text == QStringLiteral("{")) {
      beginGroup();
    } else if (token.text == QStringLiteral("}") && !undoStack_.isEmpty()) {
      endGroup();
    }
    output.push_back(std::move(token));
  }
  endGroups();
  return tokensToString(output);
}

QString MathMacroExpander::expandOnce(QString input, bool* changed) {
  TokenStream stream(std::move(input));
  *changed = expandOnce(stream) != false;
  QVector<MacroToken> output;
  while (!stream.empty()) {
    output.push_back(stream.popToken());
  }
  return tokensToString(output);
}

MacroToken MathMacroExpander::expandNextToken(TokenStream& stream) {
  for (;;) {
    if (!expandOnce(stream)) {
      MacroToken token = stream.popToken();
      if (token.treatAsRelax) {
        token.text = QStringLiteral("\\relax");
        token.noExpand = false;
        token.treatAsRelax = false;
      }
      return token;
    }
  }
}

bool MathMacroExpander::expandOnce(TokenStream& stream) {
  MacroToken token = stream.popToken();
  const QString command = token.text;
  if (command == QStringLiteral("EOF")) {
    stream.pushToken(token);
    return false;
  }

  if (command == QStringLiteral("\\noexpand")) {
    MacroToken next = stream.popToken();
    if (next.text.startsWith(QLatin1Char('\\')) && hasMacro(next.text)) {
      next.noExpand = true;
      next.treatAsRelax = true;
    }
    stream.pushToken(next);
    countExpansion(1, token.position, token.endPosition);
    return true;
  }

  if (command == QStringLiteral("\\expandafter")) {
    MacroToken held = stream.popToken();
    expandOnce(stream);
    stream.pushToken(held);
    countExpansion(1, token.position, token.endPosition);
    return true;
  }

  if (command == QStringLiteral("\\@ifnextchar")) {
    QVector<MacroToken> symbol = stream.consumeArg();
    QVector<MacroToken> ifBranch = stream.consumeArg();
    QVector<MacroToken> elseBranch = stream.consumeArg();
    stream.consumeSpaces();
    const QString expected = symbol.isEmpty() ? QString() : symbol.first().text;
    QVector<MacroToken> chosen = stream.future().text == expected ? ifBranch : elseBranch;
    std::reverse(chosen.begin(), chosen.end());
    stream.pushTokens(chosen);
    countExpansion(1, token.position, token.endPosition);
    return true;
  }

  bool globalPrefix = false;
  QString actualCommand = command;
  if (actualCommand == QStringLiteral("\\global")) {
    globalPrefix = true;
    token = stream.popToken();
    actualCommand = token.text;
  }

  if (actualCommand == QStringLiteral("\\begingroup")) {
    beginGroup();
    countExpansion(1, token.position, token.endPosition);
    return true;
  }
  if (actualCommand == QStringLiteral("\\endgroup")) {
    if (!undoStack_.isEmpty()) {
      endGroup();
    }
    countExpansion(1, token.position, token.endPosition);
    return true;
  }

  if (actualCommand == QStringLiteral("\\newcommand") || actualCommand == QStringLiteral("\\renewcommand") ||
      actualCommand == QStringLiteral("\\providecommand")) {
    const QString name = stream.consumeCommandArgument();
    int numArgs = 0;
    const QString bracket = stream.consumeBracketText();
    if (!bracket.isEmpty()) {
      numArgs = qBound(0, bracket.toInt(), 9);
    }
    const QString replacement = stream.consumeArgText();
    if (!name.isEmpty() && (actualCommand != QStringLiteral("\\providecommand") || !hasMacro(name))) {
      setMacro(name, Macro{replacement, numArgs, false}, globalPrefix);
    }
    countExpansion(1, token.position, token.endPosition);
    return true;
  }

  if (actualCommand == QStringLiteral("\\def") || actualCommand == QStringLiteral("\\gdef") || actualCommand == QStringLiteral("\\edef") ||
      actualCommand == QStringLiteral("\\xdef")) {
    const bool global = globalPrefix || actualCommand == QStringLiteral("\\gdef") || actualCommand == QStringLiteral("\\xdef");
    const QString name = stream.consumeCommandArgument();
    const QString parameterText = stream.consumeUntilGroupStart();
    QString replacement = stream.consumeArgText();
    if (actualCommand == QStringLiteral("\\edef") || actualCommand == QStringLiteral("\\xdef")) {
      replacement = MathMacroExpander(settings_).expand(replacement);
    }
    if (!name.isEmpty()) {
      setMacro(name, Macro{replacement, countDefArgs(parameterText), false}, global);
    }
    countExpansion(1, token.position, token.endPosition);
    return true;
  }

  if (actualCommand == QStringLiteral("\\let")) {
    const QString name = stream.consumeCommandArgument();
    stream.consumeSpaces();
    if (stream.future().text == QStringLiteral("=")) {
      stream.popToken();
    }
    const MacroToken target = stream.popToken();
    if (!name.isEmpty()) {
      if (hasMacro(target.text)) {
        setMacro(name, macro(target.text), globalPrefix);
      } else {
        setMacro(name, Macro{target.text, 0, false}, globalPrefix);
      }
    }
    countExpansion(1, token.position, token.endPosition);
    return true;
  }

  if (actualCommand == QStringLiteral("\\futurelet")) {
    const QString name = stream.consumeCommandArgument();
    MacroToken first = stream.popToken();
    MacroToken second = stream.future();
    if (!name.isEmpty()) {
      setMacro(name, Macro{second.text, 0, false}, globalPrefix);
    }
    stream.pushToken(first);
    countExpansion(1, token.position, token.endPosition);
    return true;
  }

  if (actualCommand == QStringLiteral("\\char")) {
    MacroToken next = stream.popToken();
    int base = 10;
    if (next.text == QStringLiteral("'")) {
      base = 8;
      next = stream.popToken();
    } else if (next.text == QStringLiteral("\"")) {
      base = 16;
      next = stream.popToken();
    } else if (next.text == QStringLiteral("`")) {
      next = stream.popToken();
      const QChar ch = next.text.startsWith(QLatin1Char('\\')) && next.text.size() > 1 ? next.text.at(1) : (next.text.isEmpty() ? QChar() : next.text.at(0));
      stream.pushTokens(reversedTokensFromString(QStringLiteral("\\text{%1}").arg(ch), token.position));
      countExpansion(1, token.position, token.endPosition);
      return true;
    }
    bool ok = false;
    int value = next.text.toInt(&ok, base);
    if (!ok) {
      throw MathParseError(QStringLiteral("Invalid character code"), next.text, next.position);
    }
    stream.pushTokens(reversedTokensFromString(QStringLiteral("\\text{%1}").arg(QChar(value)), token.position));
    countExpansion(1, token.position, token.endPosition);
    return true;
  }

  if (token.noExpand || !hasMacro(actualCommand)) {
    token.text = actualCommand;
    stream.pushToken(token);
    return false;
  }

  const Macro expansion = macro(actualCommand);
  if (expansion.unexpandable) {
    token.text = actualCommand;
    stream.pushToken(token);
    return false;
  }
  QVector<QVector<MacroToken>> args;
  for (int i = 0; i < expansion.numArgs; ++i) {
    args.push_back(stream.consumeArg());
  }
  QVector<MacroToken> body = tokenize(expansion.replacement);
  for (MacroToken& bodyToken : body) {
    bodyToken.position = token.position;
    bodyToken.endPosition = token.endPosition;
  }
  stream.pushTokens(applyArgs(std::move(body), args));
  countExpansion(1, token.position, token.endPosition);
  return true;
}

}  // namespace muffin::math

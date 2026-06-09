#include "math/MathMacroExpander.h"

#include "math/MathParseError.h"

#include <QSet>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>

namespace muffin::math {
namespace {

bool spaceAfterDotsToken(const QString& token) {
  static const QSet<QString> tokens{
      QStringLiteral(")"),        QStringLiteral("]"),       QStringLiteral("\\rbrack"),
      QStringLiteral("\\}"),      QStringLiteral("\\rbrace"), QStringLiteral("\\rangle"),
      QStringLiteral("\\rceil"),  QStringLiteral("\\rfloor"), QStringLiteral("\\rgroup"),
      QStringLiteral("\\rmoustache"), QStringLiteral("\\right"), QStringLiteral("\\bigr"),
      QStringLiteral("\\biggr"),  QStringLiteral("\\Bigr"), QStringLiteral("\\Biggr"),
      QStringLiteral("$"),        QStringLiteral(";"),       QStringLiteral("."),
      QStringLiteral(",")};
  return tokens.contains(token);
}

// KaTeX macros.ts:394-450: \dots dispatches to the appropriate dots variant
// based on the following token.
QString dotsVariantForNext(const QString& next) {
  static const QHash<QString, QString> dotsByToken{
      {QStringLiteral(","), QStringLiteral("\\dotsc")},
      {QStringLiteral("\\not"), QStringLiteral("\\dotsb")},
      {QStringLiteral("+"), QStringLiteral("\\dotsb")},
      {QStringLiteral("="), QStringLiteral("\\dotsb")},
      {QStringLiteral("<"), QStringLiteral("\\dotsb")},
      {QStringLiteral(">"), QStringLiteral("\\dotsb")},
      {QStringLiteral("-"), QStringLiteral("\\dotsb")},
      {QStringLiteral("*"), QStringLiteral("\\dotsb")},
      {QStringLiteral(":"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\DOTSB"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\coprod"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\bigvee"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\bigwedge"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\biguplus"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\bigcap"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\bigcup"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\prod"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\sum"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\bigotimes"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\bigoplus"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\bigodot"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\bigsqcup"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\And"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\longrightarrow"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\Longrightarrow"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\longleftarrow"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\Longleftarrow"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\longleftrightarrow"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\Longleftrightarrow"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\mapsto"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\longmapsto"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\hookrightarrow"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\doteq"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\mathbin"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\mathrel"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\relbar"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\Relbar"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\xrightarrow"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\xleftarrow"), QStringLiteral("\\dotsb")},
      {QStringLiteral("\\DOTSI"), QStringLiteral("\\dotsi")},
      {QStringLiteral("\\int"), QStringLiteral("\\dotsi")},
      {QStringLiteral("\\oint"), QStringLiteral("\\dotsi")},
      {QStringLiteral("\\iint"), QStringLiteral("\\dotsi")},
      {QStringLiteral("\\iiint"), QStringLiteral("\\dotsi")},
      {QStringLiteral("\\iiiint"), QStringLiteral("\\dotsi")},
      {QStringLiteral("\\idotsint"), QStringLiteral("\\dotsi")},
      {QStringLiteral("\\DOTSX"), QStringLiteral("\\dotsx")},
  };
  const auto it = dotsByToken.constFind(next);
  if (it != dotsByToken.constEnd()) {
    return it.value();
  }
  // KaTeX also checks if next starts with \not → \dotsb
  if (next.startsWith(QStringLiteral("\\not"))) {
    return QStringLiteral("\\dotsb");
  }
  return QStringLiteral("\\dotso");
}

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
  // Simple aliases (not duplicated in sections below)
  defineMacro(QStringLiteral("\\lt"), QStringLiteral("<"));
  defineMacro(QStringLiteral("\\gt"), QStringLiteral(">"));
  defineMacro(QStringLiteral("\\land"), QStringLiteral("\\wedge"));
  defineMacro(QStringLiteral("\\lor"), QStringLiteral("\\vee"));
  defineMacro(QStringLiteral("\\gets"), QStringLiteral("\\leftarrow"));
  defineMacro(QStringLiteral("\\to"), QStringLiteral("\\rightarrow"));
  defineMacro(QStringLiteral("\\notag"), QStringLiteral(""));
  defineMacro(QStringLiteral("\\nonumber"), QStringLiteral(""));
  defineMacro(QStringLiteral("\\bgroup"), QStringLiteral("{"));
  defineMacro(QStringLiteral("\\egroup"), QStringLiteral("}"));
  defineMacro(QStringLiteral("\\cr"), QStringLiteral("\\\\"));
  defineMacro(QStringLiteral("\\crcr"), QStringLiteral("\\\\"));
  defineMacro(QStringLiteral("\\DOTSB"), QStringLiteral(""));
  defineMacro(QStringLiteral("\\DOTSI"), QStringLiteral(""));
  defineMacro(QStringLiteral("\\DOTSX"), QStringLiteral(""));
  defineMacro(QStringLiteral("\\@firstoftwo"), QStringLiteral("#1"), 2);
  defineMacro(QStringLiteral("\\@secondoftwo"), QStringLiteral("#2"), 2);
  defineMacro(QStringLiteral("\\@ifstar"), QStringLiteral("\\@ifnextchar *{\\@firstoftwo{#1}}"), 1);

  //////////////////////////////////////////////////////////////////////
  // \html@mathml — NOT defined as a macro here.
  // In KaTeX, \html@mathml{HTML}{MATHML} renders only the HTML part.
  // Our function registry already handles it with numArgs=1 (text handler).
  // Defining it as a 2-arg macro here would conflict with the function
  // registry and break fixtures that use \html@mathml directly.

  // \TextOrMath{TEXT}{MATH} — not supported; macros that need it
  // should be defined with just the math-mode branch.

  // No-op directives
  defineMacro(QStringLiteral("\\nobreak"), QStringLiteral(""));
  defineMacro(QStringLiteral("\\relax"), QStringLiteral(""));

  //////////////////////////////////////////////////////////////////////
  // Grouping (KaTeX macros.ts:229-243)
  // NOTE: ~ is NOT defined as a macro here — it's handled directly by
  // the builder (MathBuilder.cpp) as a 0.25em space. Defining it as
  // \nobreakspace would require \nobreakspace to be in the symbol table,
  // and may conflict with text-mode ~ handling.
  defineMacro(QStringLiteral("\\lq"), QStringLiteral("`"));
  defineMacro(QStringLiteral("\\rq"), QStringLiteral("'"));
  defineMacro(QStringLiteral("\\aa"), QStringLiteral("\\r a"));
  defineMacro(QStringLiteral("\\AA"), QStringLiteral("\\r A"));

  //////////////////////////////////////////////////////////////////////
  // Copyright/registered (KaTeX macros.ts:250-254)
  defineMacro(QStringLiteral("\\textcopyright"), QStringLiteral("\\textcircled{c}"));
  defineMacro(QStringLiteral("\\copyright"), QStringLiteral("\\text{\\textcircled{c}}"));
  defineMacro(QStringLiteral("\\textregistered"), QStringLiteral("\\textcircled{\\scriptsize R}"));

  //////////////////////////////////////////////////////////////////////
  // Negation (KaTeX macros.ts:288-301)
  defineMacro(QStringLiteral("\\not"), QStringLiteral("\\mathrel{\\mathrlap\\@not}"));
  defineMacro(QStringLiteral("\\neq"), QStringLiteral("\\mathrel{\\not=}"));
  defineMacro(QStringLiteral("\\ne"), QStringLiteral("\\neq"));
  defineMacro(QString(QChar(0x2260)), QStringLiteral("\\neq"));
  defineMacro(QStringLiteral("\\notin"), QStringLiteral("\\mathrel{{\\in}\\mathllap{/\\mskip1mu}}"));
  defineMacro(QString(QChar(0x2209)), QStringLiteral("\\notin"));

  //////////////////////////////////////////////////////////////////////
  // \vdots (KaTeX macros.ts:348)
  defineMacro(QStringLiteral("\\vdots"), QStringLiteral("{\\varvdots\\rule{0pt}{15pt}}"));
  defineMacro(QString(QChar(0x22EE)), QStringLiteral("\\vdots"));

  //////////////////////////////////////////////////////////////////////
  // Italic Greek capitals (KaTeX macros.ts:357-367)
  defineMacro(QStringLiteral("\\varGamma"), QStringLiteral("\\mathit{\\Gamma}"));
  defineMacro(QStringLiteral("\\varDelta"), QStringLiteral("\\mathit{\\Delta}"));
  defineMacro(QStringLiteral("\\varTheta"), QStringLiteral("\\mathit{\\Theta}"));
  defineMacro(QStringLiteral("\\varLambda"), QStringLiteral("\\mathit{\\Lambda}"));
  defineMacro(QStringLiteral("\\varXi"), QStringLiteral("\\mathit{\\Xi}"));
  defineMacro(QStringLiteral("\\varPi"), QStringLiteral("\\mathit{\\Pi}"));
  defineMacro(QStringLiteral("\\varSigma"), QStringLiteral("\\mathit{\\Sigma}"));
  defineMacro(QStringLiteral("\\varUpsilon"), QStringLiteral("\\mathit{\\Upsilon}"));
  defineMacro(QStringLiteral("\\varPhi"), QStringLiteral("\\mathit{\\Phi}"));
  defineMacro(QStringLiteral("\\varPsi"), QStringLiteral("\\mathit{\\Psi}"));
  defineMacro(QStringLiteral("\\varOmega"), QStringLiteral("\\mathit{\\Omega}"));

  //////////////////////////////////////////////////////////////////////
  // amsmath (KaTeX macros.ts:369-540)
  defineMacro(QStringLiteral("\\substack"), QStringLiteral("\\begin{subarray}{c}#1\\end{subarray}"), 1);
  defineMacro(QStringLiteral("\\colon"),
              QStringLiteral("\\nobreak\\mskip2mu\\mathpunct{}"
                             "\\mathchoice{\\mkern-3mu}{\\mkern-3mu}{}{}{:}\\mskip6mu"));
  defineMacro(QStringLiteral("\\iff"), QStringLiteral("\\DOTSB\\;\\Longleftrightarrow\\;"));
  defineMacro(QStringLiteral("\\implies"), QStringLiteral("\\DOTSB\\;\\Longrightarrow\\;"));
  defineMacro(QStringLiteral("\\impliedby"), QStringLiteral("\\DOTSB\\;\\Longleftarrow\\;"));
  defineMacro(QStringLiteral("\\dddot"), QStringLiteral("{\\overset{\\raisebox{-0.1ex}{\\normalsize ...}}{#1}}"), 1);
  defineMacro(QStringLiteral("\\ddddot"), QStringLiteral("{\\overset{\\raisebox{-0.1ex}{\\normalsize ....}}{#1}}"), 1);
  defineMacro(QStringLiteral("\\dotsb"), QStringLiteral("\\cdots"));
  defineMacro(QStringLiteral("\\dotsm"), QStringLiteral("\\cdots"));
  defineMacro(QStringLiteral("\\dotsi"), QStringLiteral("\\!\\cdots"));
  defineMacro(QStringLiteral("\\dotsx"), QStringLiteral("\\ldots\\,"));
  defineMacro(QStringLiteral("\\bmod"),
              QStringLiteral("\\mathchoice{\\mskip1mu}{\\mskip1mu}{\\mskip5mu}{\\mskip5mu}"
                             "\\mathbin{\\rm mod}"
                             "\\mathchoice{\\mskip1mu}{\\mskip1mu}{\\mskip5mu}{\\mskip5mu}"));
  defineMacro(QStringLiteral("\\pod"), QStringLiteral("\\allowbreak\\mathchoice{\\mkern18mu}{\\mkern8mu}{\\mkern8mu}{\\mkern8mu}(#1)"), 1);
  defineMacro(QStringLiteral("\\pmod"), QStringLiteral("\\pod{{\\rm mod}\\mkern6mu#1}"), 1);
  defineMacro(QStringLiteral("\\mod"),
              QStringLiteral("\\allowbreak\\mathchoice{\\mkern18mu}{\\mkern12mu}{\\mkern12mu}{\\mkern12mu}"
                             "{\\rm mod}\\,\\,#1"),
              1);

  //////////////////////////////////////////////////////////////////////
  // LaTeX source2e (KaTeX macros.ts:618)
  defineMacro(QStringLiteral("\\newline"), QStringLiteral("\\\\\\relax"));

  //////////////////////////////////////////////////////////////////////
  // \hspace and helpers (KaTeX macros.ts:653-659)
  defineMacro(QStringLiteral("\\hspace"), QStringLiteral("\\@ifstar\\@hspacer\\@hspace"));
  defineMacro(QStringLiteral("\\@hspace"), QStringLiteral("\\hskip #1\\relax"), 1);
  defineMacro(QStringLiteral("\\@hspacer"), QStringLiteral("\\rule{0pt}{0pt}\\hskip #1\\relax"), 1);

  //////////////////////////////////////////////////////////////////////
  // \underbar, \mathstrut, \llap/\rlap/\clap (KaTeX macros.ts:273-281)
  defineMacro(QStringLiteral("\\underbar"), QStringLiteral("\\underline{\\text{#1}}"), 1);
  defineMacro(QStringLiteral("\\mathstrut"), QStringLiteral("\\vphantom{(}"));
  defineMacro(QStringLiteral("\\llap"), QStringLiteral("\\mathllap{\\textrm{#1}}"), 1);
  defineMacro(QStringLiteral("\\rlap"), QStringLiteral("\\mathrlap{\\textrm{#1}}"), 1);
  defineMacro(QStringLiteral("\\clap"), QStringLiteral("\\mathclap{\\textrm{#1}}"), 1);

  //////////////////////////////////////////////////////////////////////
  // \Bbbk (KaTeX macros.ts:270)
  defineMacro(QStringLiteral("\\Bbbk"), QStringLiteral("\\Bbb{k}"));

  //////////////////////////////////////////////////////////////////////
  // Custom Khan Academy colors (KaTeX macros.ts:976-1032)
  defineMacro(QStringLiteral("\\blue"), QStringLiteral("\\textcolor{##6495ed}{#1}"), 1);
  defineMacro(QStringLiteral("\\orange"), QStringLiteral("\\textcolor{##ffa500}{#1}"), 1);
  defineMacro(QStringLiteral("\\pink"), QStringLiteral("\\textcolor{##ff00af}{#1}"), 1);
  defineMacro(QStringLiteral("\\red"), QStringLiteral("\\textcolor{##df0030}{#1}"), 1);
  defineMacro(QStringLiteral("\\green"), QStringLiteral("\\textcolor{##28ae7b}{#1}"), 1);
  defineMacro(QStringLiteral("\\gray"), QStringLiteral("\\textcolor{gray}{#1}"), 1);
  defineMacro(QStringLiteral("\\purple"), QStringLiteral("\\textcolor{##9d38bd}{#1}"), 1);
  defineMacro(QStringLiteral("\\blueA"), QStringLiteral("\\textcolor{##ccfaff}{#1}"), 1);
  defineMacro(QStringLiteral("\\blueB"), QStringLiteral("\\textcolor{##80f6ff}{#1}"), 1);
  defineMacro(QStringLiteral("\\blueC"), QStringLiteral("\\textcolor{##63d9ea}{#1}"), 1);
  defineMacro(QStringLiteral("\\blueD"), QStringLiteral("\\textcolor{##11accd}{#1}"), 1);
  defineMacro(QStringLiteral("\\blueE"), QStringLiteral("\\textcolor{##0c7f99}{#1}"), 1);
  defineMacro(QStringLiteral("\\tealA"), QStringLiteral("\\textcolor{##94fff5}{#1}"), 1);
  defineMacro(QStringLiteral("\\tealB"), QStringLiteral("\\textcolor{##26edd5}{#1}"), 1);
  defineMacro(QStringLiteral("\\tealC"), QStringLiteral("\\textcolor{##01d1c1}{#1}"), 1);
  defineMacro(QStringLiteral("\\tealD"), QStringLiteral("\\textcolor{##01a995}{#1}"), 1);
  defineMacro(QStringLiteral("\\tealE"), QStringLiteral("\\textcolor{##208170}{#1}"), 1);
  defineMacro(QStringLiteral("\\greenA"), QStringLiteral("\\textcolor{##b6ffb0}{#1}"), 1);
  defineMacro(QStringLiteral("\\greenB"), QStringLiteral("\\textcolor{##8af281}{#1}"), 1);
  defineMacro(QStringLiteral("\\greenC"), QStringLiteral("\\textcolor{##74cf70}{#1}"), 1);
  defineMacro(QStringLiteral("\\greenD"), QStringLiteral("\\textcolor{##1fab54}{#1}"), 1);
  defineMacro(QStringLiteral("\\greenE"), QStringLiteral("\\textcolor{##0d923f}{#1}"), 1);
  defineMacro(QStringLiteral("\\goldA"), QStringLiteral("\\textcolor{##ffd0a9}{#1}"), 1);
  defineMacro(QStringLiteral("\\goldB"), QStringLiteral("\\textcolor{##ffbb71}{#1}"), 1);
  defineMacro(QStringLiteral("\\goldC"), QStringLiteral("\\textcolor{##ff9c39}{#1}"), 1);
  defineMacro(QStringLiteral("\\goldD"), QStringLiteral("\\textcolor{##e07d10}{#1}"), 1);
  defineMacro(QStringLiteral("\\goldE"), QStringLiteral("\\textcolor{##a75a05}{#1}"), 1);
  defineMacro(QStringLiteral("\\redA"), QStringLiteral("\\textcolor{##fca9a9}{#1}"), 1);
  defineMacro(QStringLiteral("\\redB"), QStringLiteral("\\textcolor{##ff8482}{#1}"), 1);
  defineMacro(QStringLiteral("\\redC"), QStringLiteral("\\textcolor{##f9685d}{#1}"), 1);
  defineMacro(QStringLiteral("\\redD"), QStringLiteral("\\textcolor{##e84d39}{#1}"), 1);
  defineMacro(QStringLiteral("\\redE"), QStringLiteral("\\textcolor{##bc2612}{#1}"), 1);
  defineMacro(QStringLiteral("\\maroonA"), QStringLiteral("\\textcolor{##ffbde0}{#1}"), 1);
  defineMacro(QStringLiteral("\\maroonB"), QStringLiteral("\\textcolor{##ff92c6}{#1}"), 1);
  defineMacro(QStringLiteral("\\maroonC"), QStringLiteral("\\textcolor{##ed5fa6}{#1}"), 1);
  defineMacro(QStringLiteral("\\maroonD"), QStringLiteral("\\textcolor{##ca337c}{#1}"), 1);
  defineMacro(QStringLiteral("\\maroonE"), QStringLiteral("\\textcolor{##9e034e}{#1}"), 1);
  defineMacro(QStringLiteral("\\purpleA"), QStringLiteral("\\textcolor{##ddd7ff}{#1}"), 1);
  defineMacro(QStringLiteral("\\purpleB"), QStringLiteral("\\textcolor{##c6b9fc}{#1}"), 1);
  defineMacro(QStringLiteral("\\purpleC"), QStringLiteral("\\textcolor{##aa87ff}{#1}"), 1);
  defineMacro(QStringLiteral("\\purpleD"), QStringLiteral("\\textcolor{##7854ab}{#1}"), 1);
  defineMacro(QStringLiteral("\\purpleE"), QStringLiteral("\\textcolor{##543b78}{#1}"), 1);
  defineMacro(QStringLiteral("\\mintA"), QStringLiteral("\\textcolor{##f5f9e8}{#1}"), 1);
  defineMacro(QStringLiteral("\\mintB"), QStringLiteral("\\textcolor{##edf2df}{#1}"), 1);
  defineMacro(QStringLiteral("\\mintC"), QStringLiteral("\\textcolor{##e0e5cc}{#1}"), 1);
  defineMacro(QStringLiteral("\\grayA"), QStringLiteral("\\textcolor{##f6f7f7}{#1}"), 1);
  defineMacro(QStringLiteral("\\grayB"), QStringLiteral("\\textcolor{##f0f1f2}{#1}"), 1);
  defineMacro(QStringLiteral("\\grayC"), QStringLiteral("\\textcolor{##e3e5e6}{#1}"), 1);
  defineMacro(QStringLiteral("\\grayD"), QStringLiteral("\\textcolor{##d6d8da}{#1}"), 1);
  defineMacro(QStringLiteral("\\grayE"), QStringLiteral("\\textcolor{##babec2}{#1}"), 1);
  defineMacro(QStringLiteral("\\grayF"), QStringLiteral("\\textcolor{##888d93}{#1}"), 1);
  defineMacro(QStringLiteral("\\grayG"), QStringLiteral("\\textcolor{##626569}{#1}"), 1);
  defineMacro(QStringLiteral("\\grayH"), QStringLiteral("\\textcolor{##3b3e40}{#1}"), 1);
  defineMacro(QStringLiteral("\\grayI"), QStringLiteral("\\textcolor{##21242c}{#1}"), 1);
  defineMacro(QStringLiteral("\\kaBlue"), QStringLiteral("\\textcolor{##314453}{#1}"), 1);
  defineMacro(QStringLiteral("\\kaGreen"), QStringLiteral("\\textcolor{##71B307}{#1}"), 1);

  //////////////////////////////////////////////////////////////////////
  // statmath.sty (KaTeX macros.ts:906-908)
  defineMacro(QStringLiteral("\\argmin"), QStringLiteral("\\DOTSB\\operatorname*{arg\\,min}"));
  defineMacro(QStringLiteral("\\argmax"), QStringLiteral("\\DOTSB\\operatorname*{arg\\,max}"));
  defineMacro(QStringLiteral("\\plim"), QStringLiteral("\\DOTSB\\mathop{\\operatorname{plim}}\\limits"));

  //////////////////////////////////////////////////////////////////////
  // amsopn.sty (KaTeX macros.ts:765-770)
  defineMacro(QStringLiteral("\\limsup"), QStringLiteral("\\DOTSB\\operatorname*{lim\\,sup}"));
  defineMacro(QStringLiteral("\\liminf"), QStringLiteral("\\DOTSB\\operatorname*{lim\\,inf}"));
  defineMacro(QStringLiteral("\\injlim"), QStringLiteral("\\DOTSB\\operatorname*{inj\\,lim}"));
  defineMacro(QStringLiteral("\\projlim"), QStringLiteral("\\DOTSB\\operatorname*{proj\\,lim}"));
  defineMacro(QStringLiteral("\\varlimsup"), QStringLiteral("\\DOTSB\\operatorname*{\\overline{lim}}"));
  defineMacro(QStringLiteral("\\varliminf"), QStringLiteral("\\DOTSB\\operatorname*{\\underline{lim}}"));
  defineMacro(QStringLiteral("\\varinjlim"), QStringLiteral("\\DOTSB\\operatorname*{\\underrightarrow{lim}}"));
  defineMacro(QStringLiteral("\\varprojlim"), QStringLiteral("\\DOTSB\\operatorname*{\\underleftarrow{lim}}"));

  //////////////////////////////////////////////////////////////////////
  // texvc.sty aliases (KaTeX macros.ts:837-900)
  defineMacro(QStringLiteral("\\darr"), QStringLiteral("\\downarrow"));
  defineMacro(QStringLiteral("\\dArr"), QStringLiteral("\\Downarrow"));
  defineMacro(QStringLiteral("\\Darr"), QStringLiteral("\\Downarrow"));
  defineMacro(QStringLiteral("\\lang"), QStringLiteral("\\langle"));
  defineMacro(QStringLiteral("\\rang"), QStringLiteral("\\rangle"));
  defineMacro(QStringLiteral("\\uarr"), QStringLiteral("\\uparrow"));
  defineMacro(QStringLiteral("\\uArr"), QStringLiteral("\\Uparrow"));
  defineMacro(QStringLiteral("\\Uarr"), QStringLiteral("\\Uparrow"));
  defineMacro(QStringLiteral("\\N"), QStringLiteral("\\mathbb{N}"));
  defineMacro(QStringLiteral("\\R"), QStringLiteral("\\mathbb{R}"));
  defineMacro(QStringLiteral("\\Z"), QStringLiteral("\\mathbb{Z}"));
  defineMacro(QStringLiteral("\\alef"), QStringLiteral("\\aleph"));
  defineMacro(QStringLiteral("\\alefsym"), QStringLiteral("\\aleph"));
  defineMacro(QStringLiteral("\\Alpha"), QStringLiteral("\\mathrm{A}"));
  defineMacro(QStringLiteral("\\Beta"), QStringLiteral("\\mathrm{B}"));
  defineMacro(QStringLiteral("\\bull"), QStringLiteral("\\bullet"));

  //////////////////////////////////////////////////////////////////////
  // actuarialangle.dtx (KaTeX macros.ts:974)
  defineMacro(QStringLiteral("\\angln"), QStringLiteral("{\\angl n}"));

  //////////////////////////////////////////////////////////////////////
  // Dotless i/j (KaTeX macros.ts:357-358)
  // \@imath/\@jmath are loaded from KaTeX symbols.ts as PUA characters.
  // \imath/\jmath are macros that reference them.
  defineMacro(QStringLiteral("\\imath"), QStringLiteral("\\@imath"));
  defineMacro(QStringLiteral("\\jmath"), QStringLiteral("\\@jmath"));
  defineMacro(QStringLiteral("\\Chi"), QStringLiteral("\\mathrm{X}"));
  defineMacro(QStringLiteral("\\clubs"), QStringLiteral("\\clubsuit"));
  defineMacro(QStringLiteral("\\cnums"), QStringLiteral("\\mathbb{C}"));
  defineMacro(QStringLiteral("\\Complex"), QStringLiteral("\\mathbb{C}"));
  defineMacro(QStringLiteral("\\Dagger"), QStringLiteral("\\ddagger"));
  defineMacro(QStringLiteral("\\diamonds"), QStringLiteral("\\diamondsuit"));
  defineMacro(QStringLiteral("\\empty"), QStringLiteral("\\emptyset"));
  defineMacro(QStringLiteral("\\Epsilon"), QStringLiteral("\\mathrm{E}"));
  defineMacro(QStringLiteral("\\Eta"), QStringLiteral("\\mathrm{H}"));
  defineMacro(QStringLiteral("\\exist"), QStringLiteral("\\exists"));
  defineMacro(QStringLiteral("\\harr"), QStringLiteral("\\leftrightarrow"));
  defineMacro(QStringLiteral("\\hArr"), QStringLiteral("\\Leftrightarrow"));
  defineMacro(QStringLiteral("\\Harr"), QStringLiteral("\\Leftrightarrow"));
  defineMacro(QStringLiteral("\\hearts"), QStringLiteral("\\heartsuit"));
  defineMacro(QStringLiteral("\\image"), QStringLiteral("\\Im"));
  defineMacro(QStringLiteral("\\infin"), QStringLiteral("\\infty"));
  defineMacro(QStringLiteral("\\Iota"), QStringLiteral("\\mathrm{I}"));
  defineMacro(QStringLiteral("\\isin"), QStringLiteral("\\in"));
  defineMacro(QStringLiteral("\\Kappa"), QStringLiteral("\\mathrm{K}"));
  defineMacro(QStringLiteral("\\larr"), QStringLiteral("\\leftarrow"));
  defineMacro(QStringLiteral("\\lArr"), QStringLiteral("\\Leftarrow"));
  defineMacro(QStringLiteral("\\Larr"), QStringLiteral("\\Leftarrow"));
  defineMacro(QStringLiteral("\\lrarr"), QStringLiteral("\\leftrightarrow"));
  defineMacro(QStringLiteral("\\lrArr"), QStringLiteral("\\Leftrightarrow"));
  defineMacro(QStringLiteral("\\Lrarr"), QStringLiteral("\\Leftrightarrow"));
  defineMacro(QStringLiteral("\\Mu"), QStringLiteral("\\mathrm{M}"));
  defineMacro(QStringLiteral("\\natnums"), QStringLiteral("\\mathbb{N}"));
  defineMacro(QStringLiteral("\\Nu"), QStringLiteral("\\mathrm{N}"));
  defineMacro(QStringLiteral("\\Omicron"), QStringLiteral("\\mathrm{O}"));
  defineMacro(QStringLiteral("\\plusmn"), QStringLiteral("\\pm"));
  defineMacro(QStringLiteral("\\rarr"), QStringLiteral("\\rightarrow"));
  defineMacro(QStringLiteral("\\rArr"), QStringLiteral("\\Rightarrow"));
  defineMacro(QStringLiteral("\\Rarr"), QStringLiteral("\\Rightarrow"));
  defineMacro(QStringLiteral("\\real"), QStringLiteral("\\Re"));
  defineMacro(QStringLiteral("\\reals"), QStringLiteral("\\mathbb{R}"));
  defineMacro(QStringLiteral("\\Reals"), QStringLiteral("\\mathbb{R}"));
  defineMacro(QStringLiteral("\\Rho"), QStringLiteral("\\mathrm{P}"));
  defineMacro(QStringLiteral("\\sdot"), QStringLiteral("\\cdot"));
  defineMacro(QStringLiteral("\\sect"), QStringLiteral("\\S"));
  defineMacro(QStringLiteral("\\spades"), QStringLiteral("\\spadesuit"));
  defineMacro(QStringLiteral("\\sub"), QStringLiteral("\\subset"));
  defineMacro(QStringLiteral("\\sube"), QStringLiteral("\\subseteq"));
  defineMacro(QStringLiteral("\\supe"), QStringLiteral("\\supseteq"));
  defineMacro(QStringLiteral("\\Tau"), QStringLiteral("\\mathrm{T}"));
  defineMacro(QStringLiteral("\\thetasym"), QStringLiteral("\\vartheta"));
  defineMacro(QStringLiteral("\\weierp"), QStringLiteral("\\wp"));
  defineMacro(QStringLiteral("\\Zeta"), QStringLiteral("\\mathrm{Z}"));

  // Logo macros, matching KaTeX src/macros.ts:624-651.
  // Use \mathrm (typeName="font" → parseRequiredGroup) so nested \kern/\raisebox
  // are properly parsed. KaTeX uses \textrm{\html@mathml{...}{...}} but our
  // \textrm handler uses parseRawGroupText which doesn't parse commands.
  // latexRaiseA = Main-Regular T.height - 0.7*A.height ≈ 0.205em.
  defineMacro(QStringLiteral("\\TeX"),
              QStringLiteral("\\mathrm{T\\kern-.1667em\\raisebox{-.5ex}{E}\\kern-.125emX}"));
  defineMacro(QStringLiteral("\\LaTeX"),
              QStringLiteral("\\mathrm{L\\kern-.36em\\raisebox{0.205em}{\\scriptstyle A}\\kern-.15em\\TeX}"));
  defineMacro(QStringLiteral("\\KaTeX"),
              QStringLiteral("\\mathrm{K\\kern-.17em\\raisebox{0.205em}{\\scriptstyle A}\\kern-.15em\\TeX}"));

  // braket.sty macros, matching KaTeX src/macros.ts:911-968.
  // Simplified: | inside the body is treated as a regular ord symbol,
  // not a \middle delimiter (KaTeX dynamically redefines | via braketHelper).
  defineMacro(QStringLiteral("\\bra"), QStringLiteral("\\mathinner{\\langle{#1}|}"), 1);
  defineMacro(QStringLiteral("\\ket"), QStringLiteral("\\mathinner{|{#1}\\rangle}"), 1);
  defineMacro(QStringLiteral("\\braket"), QStringLiteral("\\mathinner{\\langle{#1}\\rangle}"), 1);
  defineMacro(QStringLiteral("\\Bra"), QStringLiteral("\\left\\langle #1 \\right|"), 1);
  defineMacro(QStringLiteral("\\Ket"), QStringLiteral("\\left| #1 \\right\\rangle"), 1);
  // \Set and \Braket use braketHelper pattern (KaTeX macros.ts:919-968):
  // dynamically redefine | as \middle\vert within the group.
  defineMacro(QStringLiteral("\\Set"), QStringLiteral("\\bra@set{\\{\\,}{\\mid}{}{\\,\\}}"));
  defineMacro(QStringLiteral("\\Braket"), QStringLiteral("\\bra@ket{\\left\\langle}{\\,\\middle\\vert\\,}{\\,\\middle\\vert\\,}{\\right\\rangle}"));
  // \bra@ket and \bra@set are handled specially in expandOnce()
  defineMacro(QStringLiteral("\\bra@ket"), QString());
  defineMacro(QStringLiteral("\\bra@set"), QString());
  defineMacro(QStringLiteral("\\set"), QStringLiteral("\\{\\, #1 \\,\\}"), 1);

  // stmaryrd \minuso (KaTeX src/macros.ts:820-826).
  // KaTeX renders as \circ overlaid with - via \mathrlap, producing 2 glyphs.
  defineMacro(QStringLiteral("\\minuso"), QStringLiteral("\\mathbin{\\mathrlap{\\circ}{-}}"));

  // Apply user-defined macros from settings, overriding builtins.
  for (auto it = settings_.macros.cbegin(); it != settings_.macros.cend(); ++it) {
    defineMacro(it.key(), it.value());
  }

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

  // braket.sty braketHelper (KaTeX macros.ts:893-968):
  // braket.sty braketHelper (KaTeX macros.ts:893-968):
  // \bra@ket and \bra@set replace | with \middle\vert and \| with
  // \middle\Vert inside the body. We do direct string replacement since
  // Muffin's macro expansion works on text.
  if (actualCommand == QStringLiteral("\\bra@ket") || actualCommand == QStringLiteral("\\bra@set")) {
    const QString left = stream.consumeArgText();
    const QString mid = stream.consumeArgText();
    const QString midDouble = stream.consumeArgText();
    const QString right = stream.consumeArgText();

    QString body = stream.consumeArgText();
    if (!midDouble.isEmpty()) {
      body.replace(QStringLiteral("\\|"), midDouble);
    }
    body.replace(QStringLiteral("|"), mid);

    stream.pushTokens(reversedTokensFromString(right, token.position));
    stream.pushTokens(reversedTokensFromString(body, token.position));
    stream.pushTokens(reversedTokensFromString(left, token.position));
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

  // KaTeX macros.ts:452: \dots is context-sensitive, dispatches to
  // \dotso/\dotsb/\dotsi/\dotsx based on the following token.
  if (actualCommand == QStringLiteral("\\dots")) {
    const QString variant = dotsVariantForNext(stream.future().text);
    stream.pushTokens(reversedTokensFromString(variant, token.position));
    countExpansion(1, token.position, token.endPosition);
    return true;
  }

  if (actualCommand == QStringLiteral("\\dotso") || actualCommand == QStringLiteral("\\dotsc") || actualCommand == QStringLiteral("\\cdots")) {
    const QString next = stream.future().text;
    QString replacement;
    if (actualCommand == QStringLiteral("\\cdots")) {
      replacement = spaceAfterDotsToken(next) ? QStringLiteral("\\@cdots\\,") : QStringLiteral("\\@cdots");
    } else if (actualCommand == QStringLiteral("\\dotsc")) {
      replacement = spaceAfterDotsToken(next) && next != QStringLiteral(",") ? QStringLiteral("\\ldots\\,") : QStringLiteral("\\ldots");
    } else {
      replacement = spaceAfterDotsToken(next) ? QStringLiteral("\\ldots\\,") : QStringLiteral("\\ldots");
    }
    stream.pushTokens(reversedTokensFromString(replacement, token.position));
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

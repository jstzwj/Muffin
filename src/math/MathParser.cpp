#include "math/MathParser.h"

#include "math/MathDimension.h"
#include "math/MathFunctionRegistry.h"
#include "math/MathMacroExpander.h"
#include "math/MathParseError.h"
#include "math/MathSymbols.h"

#include <QSet>

#include <numeric>

namespace muffin::math {

MathParser::MathParser(QString input, MathSettings settings)
    : lexer_(MathMacroExpander(settings).expand(std::move(input))), settings_(std::move(settings)) {}

QVector<MathParseNode> MathParser::parse() {
  return parseExpression();
}

QVector<MathParseNode> MathParser::parseExpression(const QString& breakOn) {
  QVector<MathParseNode> nodes;
  while (true) {
    const MathToken next = lexer_.peek();
    if (next.text == QStringLiteral("EOF") || (!breakOn.isEmpty() && next.text == breakOn)) {
      break;
    }
    if (next.text == QStringLiteral("}")) {
      break;
    }
    if (const MathFunctionSpec* function = MathFunctionRegistry::lookup(next.text); function != nullptr && function->infix) {
      const MathToken infix = lexer_.next();
      nodes = QVector<MathParseNode>{parseInfixFraction(infix, std::move(nodes), breakOn)};
      continue;
    }
    nodes.push_back(parseAtom());
  }
  return nodes;
}

QVector<MathParseNode> MathParser::parseExpressionUntilAny(const QVector<QString>& breakTokens) {
  QVector<MathParseNode> nodes;
  while (true) {
    const MathToken next = lexer_.peek();
    if (next.text == QStringLiteral("EOF") || next.text == QStringLiteral("}")) {
      break;
    }
    if (const MathFunctionSpec* function = MathFunctionRegistry::lookup(next.text); function != nullptr && function->infix) {
      const MathToken infix = lexer_.next();
      nodes = QVector<MathParseNode>{parseInfixFractionUntilAny(infix, std::move(nodes), breakTokens)};
      continue;
    }
    bool shouldBreak = false;
    for (const QString& token : breakTokens) {
      if (next.text == token) {
        shouldBreak = true;
        break;
      }
    }
    if (shouldBreak) {
      break;
    }
    nodes.push_back(parseAtom());
  }
  return nodes;
}

namespace {

bool isUnicodeToken(const QString& text) {
  return text.size() == 1 && text.at(0).unicode() >= 0x80;
}

bool isSupportedUnicodeCodepoint(QChar ch) {
  if (ch.isSurrogate() || ch.category() == QChar::Other_Control || ch.category() == QChar::Other_NotAssigned ||
      ch.category() == QChar::Other_PrivateUse) {
    return false;
  }
  const ushort codepoint = ch.unicode();
  return codepoint < 0xfdd0 || codepoint > 0xfdef;
}

bool isBigOperatorCommand(const QString& token) {
  static const QSet<QString> commands{
      QStringLiteral("\\coprod"),    QStringLiteral("\\bigvee"),    QStringLiteral("\\bigwedge"), QStringLiteral("\\biguplus"),
      QStringLiteral("\\bigcap"),    QStringLiteral("\\bigcup"),    QStringLiteral("\\intop"),    QStringLiteral("\\prod"),
      QStringLiteral("\\sum"),       QStringLiteral("\\bigotimes"), QStringLiteral("\\bigoplus"), QStringLiteral("\\bigodot"),
      QStringLiteral("\\bigsqcup"),  QStringLiteral("\\smallint"),  QStringLiteral("\u220f"),     QStringLiteral("\u2210"),
      QStringLiteral("\u2211"),      QStringLiteral("\u22c0"),      QStringLiteral("\u22c1"),     QStringLiteral("\u22c2"),
      QStringLiteral("\u22c3"),      QStringLiteral("\u2a00"),      QStringLiteral("\u2a01"),     QStringLiteral("\u2a02"),
      QStringLiteral("\u2a04"),      QStringLiteral("\u2a06")};
  return commands.contains(token);
}

bool isIntegralOperatorCommand(const QString& token) {
  static const QSet<QString> commands{
      QStringLiteral("\\int"),   QStringLiteral("\\iint"),   QStringLiteral("\\iiint"), QStringLiteral("\\oint"),
      QStringLiteral("\\oiint"), QStringLiteral("\\oiiint"), QStringLiteral("\u222b"),  QStringLiteral("\u222c"),
      QStringLiteral("\u222d"),  QStringLiteral("\u222e"),   QStringLiteral("\u222f"),  QStringLiteral("\u2230")};
  return commands.contains(token);
}

bool isLimitOperatorCommand(const QString& token) {
  static const QSet<QString> commands{
      QStringLiteral("\\det"),  QStringLiteral("\\gcd"), QStringLiteral("\\inf"), QStringLiteral("\\lim"),
      QStringLiteral("\\max"),  QStringLiteral("\\min"), QStringLiteral("\\Pr"),  QStringLiteral("\\sup"),
      QStringLiteral("\\arg"),  QStringLiteral("\\dim"), QStringLiteral("\\hom"), QStringLiteral("\\ker")};
  return commands.contains(token);
}

QString canonicalOperatorName(const QString& token) {
  static const QHash<QString, QString> bigOps{
      {QStringLiteral("\u220f"), QStringLiteral("\\prod")},      {QStringLiteral("\u2210"), QStringLiteral("\\coprod")},
      {QStringLiteral("\u2211"), QStringLiteral("\\sum")},       {QStringLiteral("\u22c0"), QStringLiteral("\\bigwedge")},
      {QStringLiteral("\u22c1"), QStringLiteral("\\bigvee")},    {QStringLiteral("\u22c2"), QStringLiteral("\\bigcap")},
      {QStringLiteral("\u22c3"), QStringLiteral("\\bigcup")},    {QStringLiteral("\u2a00"), QStringLiteral("\\bigodot")},
      {QStringLiteral("\u2a01"), QStringLiteral("\\bigoplus")},  {QStringLiteral("\u2a02"), QStringLiteral("\\bigotimes")},
      {QStringLiteral("\u2a04"), QStringLiteral("\\biguplus")},  {QStringLiteral("\u2a06"), QStringLiteral("\\bigsqcup")},
      {QStringLiteral("\u222b"), QStringLiteral("\\int")},       {QStringLiteral("\u222c"), QStringLiteral("\\iint")},
      {QStringLiteral("\u222d"), QStringLiteral("\\iiint")},     {QStringLiteral("\u222e"), QStringLiteral("\\oint")},
      {QStringLiteral("\u222f"), QStringLiteral("\\oiint")},     {QStringLiteral("\u2230"), QStringLiteral("\\oiiint")}};
  return bigOps.value(token, token);
}

QString unitFromSizeText(const QString& sizeText) {
  qsizetype i = sizeText.size() - 1;
  while (i >= 0 && sizeText.at(i).isLetter()) {
    --i;
  }
  return sizeText.mid(i + 1).toLower();
}

bool isKnownSizeUnit(const QString& unit) {
  static const QSet<QString> units{
      QStringLiteral("em"), QStringLiteral("ex"), QStringLiteral("mu"), QStringLiteral("pt"), QStringLiteral("mm"),
      QStringLiteral("cm"), QStringLiteral("in"), QStringLiteral("px"), QStringLiteral("bp"), QStringLiteral("pc"),
      QStringLiteral("dd"), QStringLiteral("cc"), QStringLiteral("sp")};
  return units.contains(unit.toLower());
}

QString longestKnownSizeUnitPrefix(const QString& text) {
  for (int len = qMin(2, text.size()); len >= 1; --len) {
    const QString unit = text.left(len).toLower();
    if (isKnownSizeUnit(unit)) {
      return text.left(len);
    }
  }
  return {};
}

}  // namespace

MathParseNode MathParser::parseInfixFraction(const MathToken& token, QVector<MathParseNode> numerator, const QString& breakOn) {
  qreal lineThickness = -1.0;
  if (token.text == QStringLiteral("\\above")) {
    lineThickness = sizeTextToEm(parseSizeText(token.text));
  }
  QVector<MathParseNode> denominator = parseExpression(breakOn);
  return makeInfixFraction(token, std::move(numerator), std::move(denominator), lineThickness);
}

MathParseNode MathParser::parseInfixFractionUntilAny(const MathToken& token, QVector<MathParseNode> numerator, const QVector<QString>& breakTokens) {
  qreal lineThickness = -1.0;
  if (token.text == QStringLiteral("\\above")) {
    lineThickness = sizeTextToEm(parseSizeText(token.text));
  }
  QVector<MathParseNode> denominator = parseExpressionUntilAny(breakTokens);
  return makeInfixFraction(token, std::move(numerator), std::move(denominator), lineThickness);
}

MathParseNode MathParser::makeInfixFraction(const MathToken& token,
                                            QVector<MathParseNode> numerator,
                                            QVector<MathParseNode> denominator,
                                            qreal lineThickness) {
  MathParseNode frac;
  frac.type = MathNodeType::Fraction;
  frac.numerator = std::move(numerator);
  frac.denominator = std::move(denominator);

  QString leftDelim;
  QString rightDelim;
  bool wrap = false;
  if (token.text == QStringLiteral("\\choose")) {
    leftDelim = QStringLiteral("(");
    rightDelim = QStringLiteral(")");
    frac.lineThickness = 0.0;
    wrap = true;
  } else if (token.text == QStringLiteral("\\atop")) {
    frac.lineThickness = 0.0;
  } else if (token.text == QStringLiteral("\\brace")) {
    leftDelim = QStringLiteral("\\lbrace");
    rightDelim = QStringLiteral("\\rbrace");
    frac.lineThickness = 0.0;
    wrap = true;
  } else if (token.text == QStringLiteral("\\brack")) {
    leftDelim = QStringLiteral("[");
    rightDelim = QStringLiteral("]");
    frac.lineThickness = 0.0;
    wrap = true;
  } else if (token.text == QStringLiteral("\\above")) {
    frac.lineThickness = lineThickness;
  }

  if (wrap) {
    frac.leftDelim = leftDelim;
    frac.rightDelim = rightDelim;
  }
  return parseScripts(std::move(frac));
}

MathParseNode MathParser::parseAtom() {
  const MathToken token = lexer_.next();
  if (token.text == QStringLiteral("\\\\")) {
    return parseCr(token);
  }
  if (token.text == QStringLiteral("{")) {
    MathParseNode group;
    group.type = MathNodeType::Group;
    group.body = parseExpression(QStringLiteral("}"));
    expect(QStringLiteral("}"), QStringLiteral("group"));
    return parseScripts(std::move(group));
  }

  if (const MathFunctionSpec* function = MathFunctionRegistry::lookup(token.text)) {
    return parseFunction(token, *function);
  }

  return parseScripts(parseSymbol(token));
}

MathParseNode MathParser::parseFunction(const MathToken& token, const MathFunctionSpec& function) {
  reportFunctionPolicy(token, function);
  switch (function.handlerKind) {
  case MathFunctionHandlerKind::Fraction: {
    MathParseNode frac;
    frac.type = MathNodeType::Fraction;
    if (token.text == QStringLiteral("\\genfrac")) {
      const QString left = delimiterReplacement(parseRawGroupText(token.text));
      const QString right = delimiterReplacement(parseRawGroupText(token.text));
      const QString thickness = parseRawGroupText(token.text).trimmed();
      frac.lineThickness = thickness.isEmpty() ? -1.0 : sizeTextToEm(thickness);
      const QString styleText = parseRawGroupText(token.text).trimmed();
      if (styleText == QStringLiteral("0")) frac.style = QStringLiteral("\\displaystyle");
      else if (styleText == QStringLiteral("1")) frac.style = QStringLiteral("\\textstyle");
      else if (styleText == QStringLiteral("2")) frac.style = QStringLiteral("\\scriptstyle");
      else if (styleText == QStringLiteral("3")) frac.style = QStringLiteral("\\scriptscriptstyle");
      frac.numerator = parseRequiredGroup(token.text);
      frac.denominator = parseRequiredGroup(token.text);
      if (!left.isEmpty() || !right.isEmpty()) {
        frac.leftDelim = left.isEmpty() ? QStringLiteral(".") : left;
        frac.rightDelim = right.isEmpty() ? QStringLiteral(".") : right;
      }
      return parseScripts(std::move(frac));
    }
    if (token.text == QStringLiteral("\\cfrac")) {
      frac.style = QStringLiteral("\\displaystyle");
      frac.continuedFraction = true;
    } else if (token.text == QStringLiteral("\\dfrac") || token.text == QStringLiteral("\\dbinom")) {
      frac.style = QStringLiteral("\\displaystyle");
    } else if (token.text == QStringLiteral("\\tfrac") || token.text == QStringLiteral("\\tbinom")) {
      frac.style = QStringLiteral("\\textstyle");
    }
    frac.numerator = parseRequiredGroup(token.text);
    frac.denominator = parseRequiredGroup(token.text);
    if (token.text.contains(QStringLiteral("binom"))) {
      frac.leftDelim = QStringLiteral("(");
      frac.rightDelim = QStringLiteral(")");
    }
    return parseScripts(std::move(frac));
  }

  case MathFunctionHandlerKind::Sqrt: {
    MathParseNode sqrt;
    sqrt.type = MathNodeType::Sqrt;
    if (lexer_.peek().text == QStringLiteral("[")) {
      lexer_.consume();
      sqrt.rootIndex = parseExpression(QStringLiteral("]"));
      expect(QStringLiteral("]"), token.text);
    }
    sqrt.body = parseRequiredGroup(token.text);
    return parseScripts(std::move(sqrt));
  }

  case MathFunctionHandlerKind::Accent: {
    MathParseNode accent;
    accent.type = MathNodeType::Accent;
    accent.label = token.text;
    accent.base = parseRequiredGroup(token.text);
    return parseScripts(std::move(accent));
  }

  case MathFunctionHandlerKind::AccentUnder: {
    MathParseNode accent;
    accent.type = MathNodeType::AccentUnder;
    accent.label = token.text;
    accent.base = parseRequiredGroup(token.text);
    return parseScripts(std::move(accent));
  }

  case MathFunctionHandlerKind::HorizBrace: {
    MathParseNode brace;
    brace.type = MathNodeType::HorizBrace;
    brace.label = token.text;
    brace.isOver = token.text.contains(QStringLiteral("\\over"));
    brace.base = parseRequiredGroup(token.text);
    return parseScripts(std::move(brace));
  }

  case MathFunctionHandlerKind::XArrow: {
    MathParseNode arrow;
    arrow.type = MathNodeType::XArrow;
    arrow.label = token.text;
    if (lexer_.peek().text == QStringLiteral("[")) {
      lexer_.consume();
      arrow.sub = parseExpression(QStringLiteral("]"));
      expect(QStringLiteral("]"), token.text);
    }
    arrow.body = parseRequiredGroup(token.text);
    return parseScripts(std::move(arrow));
  }

  case MathFunctionHandlerKind::Underline: {
    MathParseNode underline;
    underline.type = MathNodeType::Underline;
    underline.base = parseRequiredGroup(token.text);
    return parseScripts(std::move(underline));
  }

  case MathFunctionHandlerKind::Overline: {
    MathParseNode overline;
    overline.type = MathNodeType::Overline;
    overline.base = parseRequiredGroup(token.text);
    return parseScripts(std::move(overline));
  }

  case MathFunctionHandlerKind::Phantom: {
    MathParseNode phantom;
    phantom.type = MathNodeType::Phantom;
    phantom.label = token.text;
    phantom.base = parseRequiredGroup(token.text);
    return parseScripts(std::move(phantom));
  }

  case MathFunctionHandlerKind::Smash: {
    MathParseNode smash;
    smash.type = MathNodeType::Smash;
    const QString option = parseOptionalBracketText();
    if (option.isEmpty()) {
      smash.smashHeight = true;
      smash.smashDepth = true;
    } else {
      for (QChar ch : option) {
        if (ch == QLatin1Char('t')) smash.smashHeight = true;
        else if (ch == QLatin1Char('b')) smash.smashDepth = true;
      }
    }
    smash.base = parseRequiredGroup(token.text);
    return parseScripts(std::move(smash));
  }

  case MathFunctionHandlerKind::Rule: {
    MathParseNode rule;
    rule.type = MathNodeType::Rule;
    rule.shift = parseOptionalBracketText();
    rule.width = parseSizeText(token.text);
    rule.height = parseSizeText(token.text);
    return parseScripts(std::move(rule));
  }

  case MathFunctionHandlerKind::Kern: {
    MathParseNode kern;
    kern.type = MathNodeType::Kern;
    kern.width = parseSizeText(token.text);
    reportKernUnitPolicy(token, kern.width);
    return parseScripts(std::move(kern));
  }

  case MathFunctionHandlerKind::RaiseBox: {
    MathParseNode raise;
    raise.type = MathNodeType::RaiseBox;
    raise.shift = parseSizeText(token.text);
    raise.base = parseRequiredGroup(token.text);
    return parseScripts(std::move(raise));
  }

  case MathFunctionHandlerKind::VCenter: {
    MathParseNode vcenter;
    vcenter.type = MathNodeType::VCenter;
    vcenter.base = parseRequiredGroup(token.text);
    return parseScripts(std::move(vcenter));
  }

  case MathFunctionHandlerKind::Lap: {
    MathParseNode lap;
    lap.type = MathNodeType::Lap;
    lap.label = token.text;
    lap.base = parseRequiredGroup(token.text);
    return parseScripts(std::move(lap));
  }

  case MathFunctionHandlerKind::Enclose: {
    MathParseNode enclose;
    enclose.type = MathNodeType::Enclose;
    enclose.label = token.text;
    if (token.text == QStringLiteral("\\colorbox")) {
      enclose.backgroundColor = parseRawGroupText(token.text);
      enclose.base = parseRequiredGroup(token.text);
    } else if (token.text == QStringLiteral("\\fcolorbox")) {
      enclose.borderColor = parseRawGroupText(token.text);
      enclose.backgroundColor = parseRawGroupText(token.text);
      enclose.base = parseRequiredGroup(token.text);
    } else {
      enclose.base = parseRequiredGroup(token.text);
    }
    return parseScripts(std::move(enclose));
  }

  case MathFunctionHandlerKind::IncludeGraphics: {
    MathParseNode graphics;
    graphics.type = MathNodeType::IncludeGraphics;
    const QString options = parseOptionalBracketText();
    for (const QString& part : options.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
      const QStringList keyValue = part.split(QLatin1Char('='));
      if (keyValue.size() != 2) {
        continue;
      }
      const QString key = keyValue.at(0).trimmed();
      const QString value = keyValue.at(1).trimmed();
      if (key == QStringLiteral("width")) graphics.width = value;
      else if (key == QStringLiteral("height")) graphics.height = value;
      else if (key == QStringLiteral("totalheight")) graphics.totalHeight = value;
      else if (key == QStringLiteral("alt")) graphics.alt = value;
    }
    graphics.href = parseRawGroupText(token.text);
    if (!ensureTrusted(token, function, trustContextForNode(token, function, graphics))) {
      return parseScripts(errorNode(QStringLiteral("Function %1 is not trusted").arg(token.text), &token));
    }
    return parseScripts(std::move(graphics));
  }

  case MathFunctionHandlerKind::MathChoice: {
    MathParseNode choice;
    choice.type = MathNodeType::MathChoice;
    choice.display = parseRequiredGroup(token.text);
    choice.body = parseRequiredGroup(token.text);
    choice.script = parseRequiredGroup(token.text);
    choice.scriptScript = parseRequiredGroup(token.text);
    return parseScripts(std::move(choice));
  }

  case MathFunctionHandlerKind::Href: {
    MathParseNode href;
    href.type = MathNodeType::Href;
    href.href = parseRawGroupText(token.text);
    href.body = parseRequiredGroup(token.text);
    if (!ensureTrusted(token, function, trustContextForNode(token, function, href))) {
      return parseScripts(errorNode(QStringLiteral("Function %1 is not trusted").arg(token.text), &token));
    }
    return parseScripts(std::move(href));
  }

  case MathFunctionHandlerKind::Url: {
    MathParseNode url;
    url.type = MathNodeType::Href;
    url.href = parseRawGroupText(token.text);
    url.text = url.href;
    if (!ensureTrusted(token, function, trustContextForNode(token, function, url))) {
      return parseScripts(errorNode(QStringLiteral("Function %1 is not trusted").arg(token.text), &token));
    }
    return parseScripts(std::move(url));
  }

  case MathFunctionHandlerKind::Html: {
    MathParseNode html;
    html.type = MathNodeType::Html;
    html.label = token.text;
    html.text = parseRawGroupText(token.text);
    html.body = parseRequiredGroup(token.text);
    if (!ensureTrusted(token, function, trustContextForNode(token, function, html))) {
      return parseScripts(errorNode(QStringLiteral("Function %1 is not trusted").arg(token.text), &token));
    }
    return parseScripts(std::move(html));
  }

  case MathFunctionHandlerKind::Tag: {
    MathParseNode tag;
    tag.type = MathNodeType::Tag;
    tag.tag = parseRequiredGroup(token.text);
    return tag;
  }

  case MathFunctionHandlerKind::Verb: {
    MathParseNode verb;
    verb.type = MathNodeType::Verb;
    verb.label = token.text;
    bool starred = false;
    bool ok = false;
    MathToken delimiter;
    const QString body = lexer_.readVerbBody(starred, ok, delimiter);
    if (delimiter.text == QStringLiteral("EOF")) {
      return parseScripts(errorNode(QStringLiteral("\\verb ended by end of line instead of matching delimiter"), &delimiter));
    }
    if (!ok) {
      return parseScripts(errorNode(QStringLiteral("\\verb ended by end of line instead of matching delimiter"), &delimiter));
    }
    if (starred) {
      verb.label = QStringLiteral("\\verb*");
    }
    verb.text = body;
    return parseScripts(std::move(verb));
  }

  case MathFunctionHandlerKind::Styling: {
    MathParseNode styling;
    styling.type = MathNodeType::Styling;
    styling.style = token.text;
    styling.body = parseExpression();
    return styling;
  }

  case MathFunctionHandlerKind::Sizing: {
    MathParseNode sizing;
    sizing.type = MathNodeType::Sizing;
    sizing.size = token.text;
    sizing.body = parseExpression();
    return sizing;
  }

  case MathFunctionHandlerKind::MathClass: {
    if (token.text == QStringLiteral("\\@binrel")) {
      MathParseNode klass;
      klass.type = MathNodeType::Class;
      const QVector<MathParseNode> source = parseRequiredGroup(token.text);
      klass.sourceMathClass = source.isEmpty() ? QString() : source.first().mathClass;
      if (source.isEmpty()) {
        klass.mathClass = QStringLiteral("\\mathord");
      } else {
        const MathNodeType sourceType = source.first().type == MathNodeType::Class ? classNodeType(source.first().mathClass) : source.first().type;
        if (sourceType == MathNodeType::Binary) klass.mathClass = QStringLiteral("\\mathbin");
        else if (sourceType == MathNodeType::Relation) klass.mathClass = QStringLiteral("\\mathrel");
        else klass.mathClass = QStringLiteral("\\mathord");
      }
      klass.body = parseRequiredGroup(token.text);
      return parseScripts(std::move(klass));
    }
    MathParseNode klass;
    klass.type = MathNodeType::Class;
    klass.mathClass = (token.text == QStringLiteral("\\boldsymbol") || token.text == QStringLiteral("\\bm")) ? QStringLiteral("\\mathord") : token.text;
    klass.body = parseRequiredGroup(token.text);
    if (token.text == QStringLiteral("\\boldsymbol") || token.text == QStringLiteral("\\bm")) {
      klass.fontClass = QStringLiteral("mathbf");
      if (!klass.body.isEmpty()) {
        const MathNodeType bodyType = klass.body.first().type == MathNodeType::Class ? classNodeType(klass.body.first().mathClass) : klass.body.first().type;
        if (bodyType == MathNodeType::Binary) klass.mathClass = QStringLiteral("\\mathbin");
        else if (bodyType == MathNodeType::Relation) klass.mathClass = QStringLiteral("\\mathrel");
      }
    }
    return parseScripts(std::move(klass));
  }

  case MathFunctionHandlerKind::Stack: {
    MathParseNode shifted;
    shifted.type = MathNodeType::SupSub;
    const QVector<MathParseNode> annotation = parseRequiredGroup(token.text);
    MathParseNode op;
    op.type = MathNodeType::Operator;
    op.limits = true;
    op.alwaysHandleSupSub = true;
    QVector<MathParseNode> baseBody = parseRequiredGroup(token.text);
    op.body = baseBody;
    shifted.base.push_back(std::move(op));
    if (token.text == QStringLiteral("\\underset")) {
      shifted.sub = annotation;
    } else {
      shifted.sup = annotation;
    }
    MathParseNode klass;
    klass.type = MathNodeType::Class;
    klass.mathClass = QStringLiteral("\\mathord");
    if (token.text == QStringLiteral("\\stackrel")) {
      klass.mathClass = QStringLiteral("\\mathrel");
    } else if (!baseBody.isEmpty()) {
      const MathNodeType baseType = baseBody.first().type == MathNodeType::Class ? classNodeType(baseBody.first().mathClass) : baseBody.first().type;
      if (baseType == MathNodeType::Binary) klass.mathClass = QStringLiteral("\\mathbin");
      else if (baseType == MathNodeType::Relation) klass.mathClass = QStringLiteral("\\mathrel");
    }
    klass.body.push_back(std::move(shifted));
    return parseScripts(std::move(klass));
  }

  case MathFunctionHandlerKind::Text: {
    if (token.text == QStringLiteral("\\@char")) {
      MathParseNode text;
      text.type = MathNodeType::Text;
      text.label = token.text;
      text.fontClass = QStringLiteral("main");
      const QString codeText = parseRawGroupText(token.text).trimmed();
      bool ok = false;
      const uint codepoint = codeText.toUInt(&ok);
      text.text = ok ? QString(QChar(codepoint)) : QString();
      return parseScripts(std::move(text));
    }
    if (token.text == QStringLiteral("\\html@mathml")) {
      MathParseNode node;
      node.type = MathNodeType::Group;
      node.label = token.text;
      node.body = parseRequiredGroup(token.text);
      parseRequiredGroup(token.text);
      return parseScripts(std::move(node));
    }
    if (token.text == QStringLiteral("\\hbox")) {
      MathParseNode node;
      node.type = MathNodeType::Group;
      node.label = token.text;
      node.body = parseRequiredGroup(token.text);
      return parseScripts(std::move(node));
    }
    const auto fontClassForCommand = [](const QString& command) {
      if (command == QStringLiteral("\\mathbf") || command == QStringLiteral("\\textbf") || command == QStringLiteral("\\bold") ||
          command == QStringLiteral("\\bf") || command == QStringLiteral("\\boldsymbol") || command == QStringLiteral("\\bm")) {
        return QStringLiteral("mathbf");
      }
      if (command == QStringLiteral("\\mathit") || command == QStringLiteral("\\textit") || command == QStringLiteral("\\it")) {
        return QStringLiteral("mathit");
      }
      if (command == QStringLiteral("\\mathnormal")) {
        return QStringLiteral("mathnormal");
      }
      if (command == QStringLiteral("\\mathsf") || command == QStringLiteral("\\mathsfit") || command == QStringLiteral("\\sf")) {
        return QStringLiteral("sans");
      }
      if (command == QStringLiteral("\\mathtt") || command == QStringLiteral("\\tt")) {
        return QStringLiteral("typewriter");
      }
      if (command == QStringLiteral("\\mathbb") || command == QStringLiteral("\\Bbb")) {
        return QStringLiteral("amsrm");
      }
      if (command == QStringLiteral("\\mathcal") || command == QStringLiteral("\\cal") || command == QStringLiteral("\\mathscr")) {
        return QStringLiteral("mathcal");
      }
      if (command == QStringLiteral("\\mathfrak") || command == QStringLiteral("\\frak")) {
        return QStringLiteral("mathfrak");
      }
      return QStringLiteral("main");
    };
    if (function.numArgs == 0) {
      MathParseNode text;
      text.type = MathNodeType::Text;
      text.label = token.text;
      text.fontClass = fontClassForCommand(token.text);
      text.body = parseExpressionUntilAny(
          {QStringLiteral("&"), QStringLiteral("\\\\"), QStringLiteral("\\cr"), QStringLiteral("\\end"), QStringLiteral("\\hline"), QStringLiteral("\\hdashline")});
      return parseScripts(std::move(text));
    }
    MathParseNode text;
    text.type = MathNodeType::Text;
    text.label = token.text;
    text.fontClass = fontClassForCommand(token.text);
    if (function.typeName == QStringLiteral("font")) {
      text.body = parseRequiredGroup(token.text);
    } else {
      text.text = parseRawGroupText(token.text);
    }
    return parseScripts(std::move(text));
  }

  case MathFunctionHandlerKind::Color: {
    MathParseNode color;
    color.type = MathNodeType::Color;
    if (token.text == QStringLiteral("\\textcolor")) {
      color.color = parseRawGroupText(token.text);
      color.body = parseRequiredGroup(token.text);
    } else {
      color.color = parseRawGroupText(token.text);
      color.body = parseExpressionUntilAny(
          {QStringLiteral("&"), QStringLiteral("\\\\"), QStringLiteral("\\cr"), QStringLiteral("\\end"), QStringLiteral("\\hline"), QStringLiteral("\\hdashline")});
    }
    return parseScripts(std::move(color));
  }

  case MathFunctionHandlerKind::DelimSizing: {
    MathParseNode delim;
    delim.type = MathNodeType::DelimSizing;
    delim.delimiterSize = function.delimiterSize;
    MathToken delimiter = lexer_.next();
    delim.text = delimiterReplacement(delimiter.text);
    delim.label = token.text;
    delim.body.push_back(parseSymbol(MathToken{delim.text, delimiter.position, delimiter.endPosition}));
    delim.body.first().type = function.delimiterNodeType;
    return parseScripts(std::move(delim));
  }

  case MathFunctionHandlerKind::OperatorName: {
    MathParseNode op;
    op.type = MathNodeType::Operator;
    bool limits = token.text == QStringLiteral("\\operatornamewithlimits");
    if (token.text == QStringLiteral("\\operatorname") && lexer_.peek().text == QStringLiteral("*")) {
      lexer_.consume();
      limits = true;
    }
    op.text = parseRawGroupText(token.text);
    op.label = token.text;
    op.limits = limits;
    op.explicitLimits = limits;
    op.alwaysHandleSupSub = true;
    op.opSymbol = false;
    return parseScripts(std::move(op));
  }

  case MathFunctionHandlerKind::Operator: {
    MathParseNode op;
    op.type = MathNodeType::Operator;
    op.label = token.text;
    op.body = parseRequiredGroup(token.text);
    op.limits = false;
    op.opSymbol = false;
    return parseScripts(std::move(op));
  }

  case MathFunctionHandlerKind::BeginEnvironment:
    return parseScripts(parseBeginEnvironment());

  case MathFunctionHandlerKind::LeftRight:
    return parseScripts(parseLeftRight());
  }

  return parseScripts(parseSymbol(token));
}

QVector<MathParseNode> MathParser::parseGroup() {
  if (lexer_.peek().text == QStringLiteral("{")) {
    lexer_.consume();
    QVector<MathParseNode> group = parseExpression(QStringLiteral("}"));
    expect(QStringLiteral("}"), QStringLiteral("group"));
    return group;
  }
  return QVector<MathParseNode>{parseAtom()};
}

QVector<MathParseNode> MathParser::parseScriptGroup() {
  if (lexer_.peek().text == QStringLiteral("{")) {
    return parseGroup();
  }

  const MathToken token = lexer_.next();
  if (token.text == QStringLiteral("EOF") || token.text == QStringLiteral("^") || token.text == QStringLiteral("_") || token.text == QStringLiteral("}") ||
      token.text == QStringLiteral("&")) {
    return QVector<MathParseNode>{errorNode(QStringLiteral("Expected script argument"), &token)};
  }
  if (token.text == QStringLiteral("{")) {
    QVector<MathParseNode> group = parseExpression(QStringLiteral("}"));
    expect(QStringLiteral("}"), QStringLiteral("script"));
    return group;
  }
  if (const MathFunctionSpec* function = MathFunctionRegistry::lookup(token.text)) {
    return QVector<MathParseNode>{parseFunction(token, *function)};
  }
  return QVector<MathParseNode>{parseSymbol(token)};
}

QVector<MathParseNode> MathParser::parseRequiredGroup(const QString& command) {
  if (lexer_.peek().text == QStringLiteral("{")) {
    return parseGroup();
  }
  const MathToken token = lexer_.peek();
  if (!canStartRequiredArgument(token)) {
    return QVector<MathParseNode>{errorNode(QStringLiteral("%1 expects a group").arg(command), &token)};
  }
  return QVector<MathParseNode>{parseAtom()};
}

bool MathParser::canStartRequiredArgument(const MathToken& token) const {
  static const QSet<QString> invalidStarts{
      QStringLiteral("EOF"),
      QStringLiteral("}"),
      QStringLiteral("]"),
      QStringLiteral("&"),
      QStringLiteral("\\\\"),
      QStringLiteral("\\cr"),
      QStringLiteral("\\end"),
      QStringLiteral("\\right"),
      QStringLiteral("^"),
      QStringLiteral("_")};
  return !invalidStarts.contains(token.text);
}

QString MathParser::parseRawGroupText(const QString& command) {
  if (lexer_.peek().text != QStringLiteral("{")) {
    const MathToken token = lexer_.peek();
    return errorNode(QStringLiteral("%1 expects a group").arg(command), &token).text;
  }
  lexer_.consume();
  QString text;
  int depth = 1;
  while (depth > 0 && lexer_.peek().text != QStringLiteral("EOF")) {
    const QString token = lexer_.next().text;
    if (token == QStringLiteral("{")) {
      ++depth;
      text += token;
    } else if (token == QStringLiteral("}")) {
      --depth;
      if (depth > 0) {
        text += token;
      }
    } else if (token.startsWith(QLatin1Char('\\'))) {
      const MathSymbolInfo symbol = lookupSymbol(token);
      text += symbol.known ? symbol.replacement : token.mid(1);
    } else {
      text += token;
    }
  }
  return text;
}

QString MathParser::parseOptionalBracketText() {
  if (lexer_.peek().text != QStringLiteral("[")) {
    return {};
  }
  lexer_.consume();
  QString text;
  int depth = 1;
  while (depth > 0 && lexer_.peek().text != QStringLiteral("EOF")) {
    const QString token = lexer_.next().text;
    if (token == QStringLiteral("[")) {
      ++depth;
      text += token;
    } else if (token == QStringLiteral("]")) {
      --depth;
      if (depth > 0) {
        text += token;
      }
    } else {
      text += token;
    }
  }
  return text;
}

QString MathParser::parseSizeText(const QString& command) {
  if (lexer_.peek().text == QStringLiteral("{")) {
    return parseRawGroupText(command);
  }
  QString text;
  enum class Part { SignOrNumber, Unit };
  Part part = Part::SignOrNumber;
  bool sawDigit = false;
  bool sawDot = false;
  while (lexer_.peek().text != QStringLiteral("EOF")) {
    const MathToken next = lexer_.peek();
    const QString token = next.text;
    if (token.startsWith(QLatin1Char('\\')) || token == QStringLiteral("{") || token == QStringLiteral("}") ||
        token == QStringLiteral("^") || token == QStringLiteral("_") || token == QStringLiteral("&")) {
      break;
    }
    if (token.size() == 1) {
      const QChar ch = token.at(0);
      if (part == Part::SignOrNumber) {
        if ((ch == QLatin1Char('-') || ch == QLatin1Char('+')) && text.isEmpty()) {
          text += lexer_.next().text;
          continue;
        }
        if (ch.isDigit()) {
          sawDigit = true;
          text += lexer_.next().text;
          continue;
        }
        if (ch == QLatin1Char('.') && !sawDot) {
          sawDot = true;
          text += lexer_.next().text;
          continue;
        }
        if (ch.isLetter() && sawDigit) {
          part = Part::Unit;
        } else {
          break;
        }
      }
      if (part == Part::Unit && ch.isLetter()) {
        text += lexer_.next().text;
        const QString unit = unitFromSizeText(text);
        if (isKnownSizeUnit(unit)) {
          break;
        }
        continue;
      }
    }
    if (part == Part::Unit && token.size() > 1 && token.at(0).isLetter()) {
      const QString unitPrefix = longestKnownSizeUnitPrefix(token);
      if (!unitPrefix.isEmpty()) {
        lexer_.consume();
        text += unitPrefix;
        const QString remainder = token.mid(unitPrefix.size());
        if (!remainder.isEmpty()) {
          lexer_.pushFront(MathToken{remainder, next.position + unitPrefix.size(), next.endPosition});
        }
      }
      break;
    }
    break;
  }
  if (text.isEmpty()) {
    const MathToken token{command, lexer_.peek().position, lexer_.peek().position + command.size()};
    return errorNode(QStringLiteral("%1 expects a size").arg(command), &token).text;
  }
  return text;
}

MathParseNode MathParser::parseCr(const MathToken& token) {
  MathParseNode cr;
  cr.type = MathNodeType::Cr;
  cr.label = token.text;
  if (lexer_.peek().text == QStringLiteral("[")) {
    cr.height = parseOptionalBracketText();
  }
  if (settings_.displayMode) {
    settings_.shouldApplyStrictBehavior(QStringLiteral("newLineInDisplayMode"),
                                        QStringLiteral("In LaTeX, \\\\ or \\newline does nothing in display mode"),
                                        &token);
  }
  return cr;
}

MathParseNode MathParser::parseLeftRight() {
  MathParseNode leftRight;
  leftRight.type = MathNodeType::LeftRight;
  leftRight.leftDelim = delimiterReplacement(lexer_.next().text);
  leftRight.body = parseExpression(QStringLiteral("\\right"));
  expect(QStringLiteral("\\right"), QStringLiteral("\\left"));
  if (lexer_.peek().text == QStringLiteral("EOF")) {
    leftRight.rightDelim = QStringLiteral(".");
    return leftRight;
  }
  leftRight.rightDelim = delimiterReplacement(lexer_.next().text);
  return leftRight;
}

QVector<MathParseNode> MathParser::parseOptionalGroupExpression(const QString& command) {
  if (lexer_.peek().text == QStringLiteral("{")) {
    return parseGroup();
  }
  Q_UNUSED(command);
  return {};
}

void MathParser::reportFunctionPolicy(const MathToken& token, const MathFunctionSpec& function) {
  if (function.strictCategory.isEmpty() || !settings_.strictEnabled()) {
    return;
  }

  if (function.strictCategory == QStringLiteral("htmlExtension")) {
    settings_.reportNonstrict(function.strictCategory, QStringLiteral("HTML extension is disabled on strict mode"), &token);
  } else if (function.strictCategory == QStringLiteral("mathVsTextAccents")) {
    settings_.reportNonstrict(function.strictCategory, QStringLiteral("LaTeX's accent %1 works only in text mode").arg(token.text), &token);
  } else if (function.strictCategory == QStringLiteral("mathVsSout") && token.text == QStringLiteral("\\sout")) {
    settings_.reportNonstrict(function.strictCategory, QStringLiteral("\\sout is a text-mode extension"), &token);
  }
}

void MathParser::reportKernUnitPolicy(const MathToken& token, const QString& sizeText) {
  if (!settings_.strictEnabled()) {
    return;
  }

  const QString unit = unitFromSizeText(sizeText);
  const bool mathFunction = token.text == QStringLiteral("\\mkern") || token.text == QStringLiteral("\\mskip");
  const bool muUnit = unit == QStringLiteral("mu");
  if (mathFunction && !muUnit) {
    settings_.reportNonstrict(QStringLiteral("mathVsTextUnits"),
                              QStringLiteral("LaTeX's %1 supports only mu units, not %2 units").arg(token.text, unit),
                              &token);
  } else if (!mathFunction && muUnit) {
    settings_.reportNonstrict(QStringLiteral("mathVsTextUnits"),
                              QStringLiteral("LaTeX's %1 doesn't support mu units").arg(token.text),
                              &token);
  }
}

bool MathParser::ensureTrusted(const MathToken& token, const MathFunctionSpec& function, const MathTrustContext& context) {
  if (!function.requiresTrust || settings_.isTrusted(context)) {
    return true;
  }
  if (settings_.throwOnError) {
    throw MathParseError(QStringLiteral("Function %1 is not trusted").arg(token.text), token.text, token.position, token.endPosition);
  }
  return false;
}

MathTrustContext MathParser::trustContextForNode(const MathToken& token, const MathFunctionSpec& function, const MathParseNode& node) const {
  MathTrustContext context;
  context.command = token.text;
  if (function.handlerKind == MathFunctionHandlerKind::Href || function.handlerKind == MathFunctionHandlerKind::Url ||
      function.handlerKind == MathFunctionHandlerKind::IncludeGraphics) {
    context.url = node.href;
    const int colon = context.url.indexOf(QLatin1Char(':'));
    if (colon > 0) {
      context.protocol = context.url.left(colon).toLower();
    }
  } else if (function.handlerKind == MathFunctionHandlerKind::Html) {
    if (token.text == QStringLiteral("\\htmlClass")) {
      context.className = node.text;
    } else if (token.text == QStringLiteral("\\htmlId")) {
      context.id = node.text;
    } else if (token.text == QStringLiteral("\\htmlStyle")) {
      context.style = node.text;
    } else if (token.text == QStringLiteral("\\htmlData")) {
      const QStringList items = node.text.split(QLatin1Char(','), Qt::SkipEmptyParts);
      for (const QString& item : items) {
        const qsizetype equals = item.indexOf(QLatin1Char('='));
        if (equals < 0) {
          if (settings_.throwOnError) {
            throw MathParseError(QStringLiteral("\\htmlData key/value '%1' missing equals sign").arg(item), token.text, token.position, token.endPosition);
          }
          context.attributes.insert(QStringLiteral("data-error"), item);
          continue;
        }
        const QString key = QStringLiteral("data-") + item.left(equals).trimmed();
        const QString value = item.mid(equals + 1);
        context.attributes.insert(key, value);
        if (context.attribute.isEmpty()) {
          context.attribute = key;
          context.value = value;
        }
      }
    }
  }
  return context;
}

bool MathParser::isAccentCommand(const QString& token) const {
  const MathFunctionSpec* function = MathFunctionRegistry::lookup(token);
  return function != nullptr && function->handlerKind == MathFunctionHandlerKind::Accent;
}

bool MathParser::isAccentUnderCommand(const QString& token) const {
  const MathFunctionSpec* function = MathFunctionRegistry::lookup(token);
  return function != nullptr && function->handlerKind == MathFunctionHandlerKind::AccentUnder;
}

bool MathParser::isHorizBraceCommand(const QString& token) const {
  const MathFunctionSpec* function = MathFunctionRegistry::lookup(token);
  return function != nullptr && function->handlerKind == MathFunctionHandlerKind::HorizBrace;
}

bool MathParser::isUnderlineCommand(const QString& token) const {
  const MathFunctionSpec* function = MathFunctionRegistry::lookup(token);
  return function != nullptr && function->handlerKind == MathFunctionHandlerKind::Underline;
}

bool MathParser::isSizingCommand(const QString& token) const {
  const MathFunctionSpec* function = MathFunctionRegistry::lookup(token);
  return function != nullptr && function->handlerKind == MathFunctionHandlerKind::Sizing;
}

bool MathParser::isMathClassCommand(const QString& token) const {
  const MathFunctionSpec* function = MathFunctionRegistry::lookup(token);
  return function != nullptr && function->handlerKind == MathFunctionHandlerKind::MathClass;
}

bool MathParser::isEncloseCommand(const QString& token) const {
  const MathFunctionSpec* function = MathFunctionRegistry::lookup(token);
  return function != nullptr && function->handlerKind == MathFunctionHandlerKind::Enclose;
}

bool MathParser::isHtmlCommand(const QString& token) const {
  const MathFunctionSpec* function = MathFunctionRegistry::lookup(token);
  return function != nullptr && function->handlerKind == MathFunctionHandlerKind::Html;
}

MathNodeType MathParser::classNodeType(const QString& mathClass) const {
  if (mathClass == QStringLiteral("\\mathbin")) return MathNodeType::Binary;
  if (mathClass == QStringLiteral("\\mathrel")) return MathNodeType::Relation;
  if (mathClass == QStringLiteral("\\mathopen")) return MathNodeType::Open;
  if (mathClass == QStringLiteral("\\mathclose")) return MathNodeType::Close;
  if (mathClass == QStringLiteral("\\mathpunct")) return MathNodeType::Punct;
  if (mathClass == QStringLiteral("\\mathinner")) return MathNodeType::Inner;
  return MathNodeType::Ord;
}

bool MathParser::isDelimiterSizingCommand(const QString& token) const {
  const MathFunctionSpec* function = MathFunctionRegistry::lookup(token);
  return function != nullptr && function->handlerKind == MathFunctionHandlerKind::DelimSizing;
}

int MathParser::delimiterSizingCommandSize(const QString& token) const {
  const MathFunctionSpec* function = MathFunctionRegistry::lookup(token);
  return function == nullptr ? 0 : function->delimiterSize;
}

MathNodeType MathParser::delimiterSizingCommandType(const QString& token) const {
  const MathFunctionSpec* function = MathFunctionRegistry::lookup(token);
  return function == nullptr ? MathNodeType::Ord : function->delimiterNodeType;
}

QString MathParser::delimiterReplacement(const QString& token) const {
  if (token == QStringLiteral("\\{") || token == QStringLiteral("\\lbrace")) return QStringLiteral("\\lbrace");
  if (token == QStringLiteral("\\}") || token == QStringLiteral("\\rbrace")) return QStringLiteral("\\rbrace");
  if (token == QStringLiteral("\\langle")) return QStringLiteral("\u27e8");
  if (token == QStringLiteral("\\rangle")) return QStringLiteral("\u27e9");
  if (token == QStringLiteral("\\lvert") || token == QStringLiteral("\\rvert") || token == QStringLiteral("\\vert")) return QStringLiteral("|");
  if (token == QStringLiteral("\\lVert") || token == QStringLiteral("\\rVert") || token == QStringLiteral("\\Vert") || token == QStringLiteral("\\|")) {
    return QStringLiteral("\u2016");
  }
  if (token == QStringLiteral("\\lfloor")) return QStringLiteral("\u230a");
  if (token == QStringLiteral("\\rfloor")) return QStringLiteral("\u230b");
  if (token == QStringLiteral("\\lceil")) return QStringLiteral("\u2308");
  if (token == QStringLiteral("\\rceil")) return QStringLiteral("\u2309");
  if (token == QStringLiteral("\\backslash")) return QStringLiteral("\\");
  return token;
}

MathParseNode MathParser::parseSymbol(const MathToken& token) {
  const MathSymbolInfo symbol = lookupSymbol(token.text);
  MathParseNode node;
  node.type = symbol.known ? symbol.type : MathNodeType::Error;
  node.text = symbol.replacement;
  if (!symbol.known || symbol.type != MathNodeType::Ord) {
    node.fontClass = symbol.fontClass;
  }
  node.label = canonicalOperatorName(token.text);
  if (node.type == MathNodeType::Operator) {
    node.opSymbol = isBigOperatorCommand(token.text) || isIntegralOperatorCommand(token.text);
    node.limits = isBigOperatorCommand(token.text) || isLimitOperatorCommand(token.text);
    if (token.text == QStringLiteral("\\smallint")) {
      node.limits = false;
    }
  }
  if (!symbol.known) {
    node.error = QStringLiteral("Unsupported command %1").arg(token.text);
    if (token.text.startsWith(QLatin1Char('\\')) && settings_.strictEnabled()) {
      settings_.reportNonstrict(QStringLiteral("unknownSymbol"),
                                QStringLiteral("Unrecognized command %1").arg(token.text),
                                &token);
    }
    if (settings_.throwOnError) {
      throw MathParseError(node.error, token.text, token.position, token.endPosition);
    }
  } else if (isUnicodeToken(token.text) && settings_.strictEnabled()) {
    const QChar ch = token.text.at(0);
    if (!isSupportedUnicodeCodepoint(ch)) {
      settings_.reportNonstrict(QStringLiteral("unknownSymbol"),
                                QStringLiteral("Unrecognized Unicode character \"%1\" (%2)").arg(token.text).arg(ch.unicode()),
                                &token);
    } else {
      settings_.reportNonstrict(QStringLiteral("unicodeTextInMathMode"),
                                QStringLiteral("Unicode text character \"%1\" used in math mode").arg(token.text),
                                &token);
    }
  }
  return node;
}

MathParseNode MathParser::applyOperatorLimitsModifier(MathParseNode base, const MathToken& token) {
  MathParseNode* op = nullptr;
  if (base.type == MathNodeType::Operator) {
    op = &base;
  } else if (base.type == MathNodeType::Class && !base.body.isEmpty() && base.body.first().type == MathNodeType::Operator) {
    op = &base.body.first();
  }
  if (op == nullptr) {
    if (settings_.throwOnError) {
      throw MathParseError(QStringLiteral("%1 must follow a math operator").arg(token.text), token.text, token.position, token.endPosition);
    }
    return base;
  }
  op->limits = token.text == QStringLiteral("\\limits");
  op->explicitLimits = true;
  op->alwaysHandleSupSub = token.text == QStringLiteral("\\limits");
  return base;
}

MathParseNode MathParser::parseScripts(MathParseNode base) {
  while (lexer_.peek().text == QStringLiteral("\\limits") || lexer_.peek().text == QStringLiteral("\\nolimits")) {
    base = applyOperatorLimitsModifier(std::move(base), lexer_.next());
  }
  QVector<MathParseNode> sup;
  QVector<MathParseNode> sub;
  while (lexer_.peek().text == QStringLiteral("^") || lexer_.peek().text == QStringLiteral("_")) {
    const QString marker = lexer_.next().text;
    if (marker == QStringLiteral("^")) {
      sup = parseScriptGroup();
    } else {
      sub = parseScriptGroup();
    }
  }
  if (sup.isEmpty() && sub.isEmpty()) {
    return base;
  }
  MathParseNode node;
  node.type = MathNodeType::SupSub;
  node.base.push_back(std::move(base));
  node.sup = std::move(sup);
  node.sub = std::move(sub);
  return node;
}

void MathParser::expect(const QString& token, const QString& context) {
  if (lexer_.peek().text == token) {
    lexer_.consume();
    return;
  }
  if (settings_.throwOnError) {
    const MathToken next = lexer_.peek();
    throw MathParseError(QStringLiteral("Expected %1 in %2").arg(token, context), next.text, next.position, next.endPosition);
  }
}

MathParseNode MathParser::errorNode(QString message, const MathToken* token) {
  if (settings_.throwOnError) {
    if (token != nullptr) {
      throw MathParseError(message, token->text, token->position, token->endPosition);
    }
    throw MathParseError(message);
  }
  MathParseNode node;
  node.type = MathNodeType::Error;
  node.text = std::move(message);
  node.error = node.text;
  return node;
}

}  // namespace muffin::math

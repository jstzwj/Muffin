#pragma once

#include "math/MathLexer.h"
#include "math/MathFunctionRegistry.h"
#include "math/MathParseNode.h"
#include "math/MathSettings.h"

#include <QString>
#include <QVector>

namespace muffin::math {

class MathParser {
public:
  MathParser(QString input, MathSettings settings = {});

  QVector<MathParseNode> parse();

private:
  class DepthGuard;  // RAII recursion-depth guard, defined in the .cpp
  QVector<MathParseNode> parseExpression(const QString& breakOn = {});
  QVector<MathParseNode> parseExpressionUntilAny(const QVector<QString>& breakTokens);
  MathParseNode parseInfixFraction(const MathToken& token, QVector<MathParseNode> numerator, const QString& breakOn);
  MathParseNode parseInfixFractionUntilAny(const MathToken& token, QVector<MathParseNode> numerator, const QVector<QString>& breakTokens);
  MathParseNode makeInfixFraction(const MathToken& token, QVector<MathParseNode> numerator, QVector<MathParseNode> denominator, qreal lineThickness = -1.0);
  MathParseNode parseAtom();
  MathParseNode parseFunction(const MathToken& token, const MathFunctionSpec& function);
  // LaTeX function-command handlers — one per MathFunctionHandlerKind. parseFunction is a thin
  // dispatch over these; each consumes its command's arguments and returns the parsed node.
  MathParseNode parseFraction(const MathToken& token);
  MathParseNode parseSqrt(const MathToken& token);
  MathParseNode parseAccent(const MathToken& token);
  MathParseNode parseAccentUnder(const MathToken& token);
  MathParseNode parseHorizBrace(const MathToken& token);
  MathParseNode parseXArrow(const MathToken& token);
  MathParseNode parseUnderline(const MathToken& token);
  MathParseNode parseOverline(const MathToken& token);
  MathParseNode parsePhantom(const MathToken& token);
  MathParseNode parseSmash(const MathToken& token);
  MathParseNode parseRule(const MathToken& token);
  MathParseNode parseKern(const MathToken& token);
  MathParseNode parseRaiseBox(const MathToken& token);
  MathParseNode parseVCenter(const MathToken& token);
  MathParseNode parseLap(const MathToken& token);
  MathParseNode parseEnclose(const MathToken& token);
  MathParseNode parseIncludeGraphics(const MathToken& token, const MathFunctionSpec& function);
  MathParseNode parseMathChoice(const MathToken& token);
  MathParseNode parseHref(const MathToken& token, const MathFunctionSpec& function);
  MathParseNode parseUrl(const MathToken& token, const MathFunctionSpec& function);
  MathParseNode parseHtml(const MathToken& token, const MathFunctionSpec& function);
  MathParseNode parseTag(const MathToken& token);
  MathParseNode parseVerb(const MathToken& token);
  MathParseNode parseStyling(const MathToken& token);
  MathParseNode parseSizing(const MathToken& token);
  MathParseNode parseMathClass(const MathToken& token);
  MathParseNode parseStack(const MathToken& token);
  MathParseNode parseText(const MathToken& token, const MathFunctionSpec& function);
  MathParseNode parseColor(const MathToken& token);
  MathParseNode parseDelimSizing(const MathToken& token, const MathFunctionSpec& function);
  MathParseNode parseOperatorName(const MathToken& token);
  MathParseNode parseOperator(const MathToken& token);
  QVector<MathParseNode> parseGroup();
  QVector<MathParseNode> parseScriptGroup();
  QVector<MathParseNode> parseRequiredGroup(const QString& command);
  bool canStartRequiredArgument(const MathToken& token) const;
  QString parseRawGroupText(const QString& command);
  QString parseRawGroupTextArgument();
  QString applyTextAccent(const QString& accent, const QString& base) const;
  QString parseOptionalBracketText();
  QString parseSizeText(const QString& command);
  MathParseNode parseBeginEnvironment();
  MathParseNode parseCr(const MathToken& token);
  MathParseNode parseArrayEnvironment(const QString& name);
  MathParseNode parseCDEnvironment();
  void parseArrayPreamble(MathParseNode& array, const QString& preamble);
  void consumeArrayHLines(MathParseNode& array, int beforeRow);
  void configureArrayEnvironment(MathParseNode& array, const QString& name);
  MathParseNode parseLeftRight();
  QVector<MathParseNode> parseOptionalGroupExpression(const QString& command);
  void reportFunctionPolicy(const MathToken& token, const MathFunctionSpec& function);
  void reportKernUnitPolicy(const MathToken& token, const QString& sizeText);
  bool ensureTrusted(const MathToken& token, const MathFunctionSpec& function, const MathTrustContext& context);
  MathTrustContext trustContextForNode(const MathToken& token, const MathFunctionSpec& function, const MathParseNode& node) const;
  bool isAccentCommand(const QString& token) const;
  bool isAccentUnderCommand(const QString& token) const;
  bool isHorizBraceCommand(const QString& token) const;
  bool isUnderlineCommand(const QString& token) const;
  bool isSizingCommand(const QString& token) const;
  bool isMathClassCommand(const QString& token) const;
  bool isEncloseCommand(const QString& token) const;
  bool isHtmlCommand(const QString& token) const;
  MathNodeType classNodeType(const QString& mathClass) const;
  bool isDelimiterSizingCommand(const QString& token) const;
  int delimiterSizingCommandSize(const QString& token) const;
  MathNodeType delimiterSizingCommandType(const QString& token) const;
  QString delimiterReplacement(const QString& token) const;
  MathParseNode parseSymbol(const MathToken& token);
  MathParseNode applyOperatorLimitsModifier(MathParseNode base, const MathToken& token);
  MathParseNode parseScripts(MathParseNode base);

  void expect(const QString& token, const QString& context);
  MathParseNode errorNode(QString message, const MathToken* token = nullptr);

  MathLexer lexer_;
  MathSettings settings_;

  // Break tokens from the enclosing parseExpressionUntilAny context.
  // Used by Styling/Sizing/Color handlers to respect array cell boundaries
  // (&, \\, \end) and \left-right boundaries (\right) instead of consuming everything.
  QVector<QString> outerBreakTokens_;

  // When true, $ is treated as a math-mode switch inside text body parsing.
  // KaTeX registers $ as a text-mode function that switches to inline math.
  bool inTextBody_ = false;

  // Active recursive-parse frame count (RAII-bumped by DepthGuard). Caps nesting so deeply
  // nested {{{...}}} / \sqrt{...} / \left..\right / \text{...} input can't overflow the
  // GUI-thread stack — same class of crash NodeCssElement was fixed to avoid.
  int depth_ = 0;
};

}  // namespace muffin::math

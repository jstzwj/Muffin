#pragma once

#include "math/MathLexer.h"
#include "math/MathParseTask.h"
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
  class DepthGuard;  // logical nesting budget, defined in the .cpp
  MathParseTask<QVector<MathParseNode>> parseExpression(const QString& breakOn = {});
  MathParseTask<QVector<MathParseNode>> parseExpressionUntilAny(const QVector<QString>& breakTokens);
  MathParseTask<MathParseNode> parseInfixFraction(const MathToken& token, QVector<MathParseNode> numerator, const QString& breakOn);
  MathParseTask<MathParseNode> parseInfixFractionUntilAny(const MathToken& token, QVector<MathParseNode> numerator, const QVector<QString>& breakTokens);
  MathParseTask<MathParseNode> makeInfixFraction(const MathToken& token, QVector<MathParseNode> numerator, QVector<MathParseNode> denominator, qreal lineThickness = -1.0);
  MathParseTask<MathParseNode> parseAtom();
  MathParseTask<MathParseNode> parseFunction(const MathToken& token, const MathFunctionSpec& function);
  // LaTeX function-command handlers — one per MathFunctionHandlerKind. parseFunction is a thin
  // dispatch over these; each consumes its command's arguments and returns the parsed node.
  MathParseTask<MathParseNode> parseFraction(const MathToken& token);
  MathParseTask<MathParseNode> parseSqrt(const MathToken& token);
  MathParseTask<MathParseNode> parseAccent(const MathToken& token);
  MathParseTask<MathParseNode> parseAccentUnder(const MathToken& token);
  MathParseTask<MathParseNode> parseHorizBrace(const MathToken& token);
  MathParseTask<MathParseNode> parseXArrow(const MathToken& token);
  MathParseTask<MathParseNode> parseUnderline(const MathToken& token);
  MathParseTask<MathParseNode> parseOverline(const MathToken& token);
  MathParseTask<MathParseNode> parsePhantom(const MathToken& token);
  MathParseTask<MathParseNode> parseSmash(const MathToken& token);
  MathParseTask<MathParseNode> parseRule(const MathToken& token);
  MathParseTask<MathParseNode> parseKern(const MathToken& token);
  MathParseTask<MathParseNode> parseRaiseBox(const MathToken& token);
  MathParseTask<MathParseNode> parseVCenter(const MathToken& token);
  MathParseTask<MathParseNode> parseLap(const MathToken& token);
  MathParseTask<MathParseNode> parseEnclose(const MathToken& token);
  MathParseTask<MathParseNode> parseIncludeGraphics(const MathToken& token, const MathFunctionSpec& function);
  MathParseTask<MathParseNode> parseMathChoice(const MathToken& token);
  MathParseTask<MathParseNode> parseHref(const MathToken& token, const MathFunctionSpec& function);
  MathParseTask<MathParseNode> parseUrl(const MathToken& token, const MathFunctionSpec& function);
  MathParseTask<MathParseNode> parseHtml(const MathToken& token, const MathFunctionSpec& function);
  MathParseTask<MathParseNode> parseTag(const MathToken& token);
  MathParseTask<MathParseNode> parseVerb(const MathToken& token);
  MathParseTask<MathParseNode> parseStyling(const MathToken& token);
  MathParseTask<MathParseNode> parseSizing(const MathToken& token);
  MathParseTask<MathParseNode> parseMathClass(const MathToken& token);
  MathParseTask<MathParseNode> parseStack(const MathToken& token);
  MathParseTask<MathParseNode> parseText(const MathToken& token, const MathFunctionSpec& function);
  MathParseTask<MathParseNode> parseColor(const MathToken& token);
  MathParseTask<MathParseNode> parseDelimSizing(const MathToken& token, const MathFunctionSpec& function);
  MathParseTask<MathParseNode> parseOperatorName(const MathToken& token);
  MathParseTask<MathParseNode> parseOperator(const MathToken& token);
  MathParseTask<QVector<MathParseNode>> parseGroup();
  MathParseTask<QVector<MathParseNode>> parseScriptGroup();
  MathParseTask<QVector<MathParseNode>> parseRequiredGroup(const QString& command);
  bool canStartRequiredArgument(const MathToken& token) const;
  MathParseTask<QString> parseRawGroupText(const QString& command);
  MathParseTask<QString> parseRawGroupTextArgument();
  QString applyTextAccent(const QString& accent, const QString& base) const;
  QString parseOptionalBracketText();
  MathParseTask<QString> parseSizeText(const QString& command);
  MathParseTask<MathParseNode> parseBeginEnvironment();
  MathParseNode parseCr(const MathToken& token);
  MathParseTask<MathParseNode> parseArrayEnvironment(const QString& name);
  MathParseTask<MathParseNode> parseCDEnvironment();
  void parseArrayPreamble(MathParseNode& array, const QString& preamble);
  void consumeArrayHLines(MathParseNode& array, int beforeRow);
  void configureArrayEnvironment(MathParseNode& array, const QString& name);
  MathParseTask<MathParseNode> parseLeftRight();
  MathParseTask<QVector<MathParseNode>> parseOptionalGroupExpression(const QString& command);
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
  MathParseTask<MathParseNode> parseScripts(MathParseNode base);

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

  // Active logical parse depth. Grammar continuations live on the heap, while
  // DepthGuard preserves the resource budget for the resulting parse tree.
  int depth_ = 0;
};

}  // namespace muffin::math

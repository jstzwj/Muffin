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
  QVector<MathParseNode> parseExpression(const QString& breakOn = {});
  QVector<MathParseNode> parseExpressionUntilAny(const QVector<QString>& breakTokens);
  MathParseNode parseInfixFraction(const MathToken& token, QVector<MathParseNode> numerator, const QString& breakOn);
  MathParseNode parseInfixFractionUntilAny(const MathToken& token, QVector<MathParseNode> numerator, const QVector<QString>& breakTokens);
  MathParseNode makeInfixFraction(const MathToken& token, QVector<MathParseNode> numerator, QVector<MathParseNode> denominator, qreal lineThickness = -1.0);
  MathParseNode parseAtom();
  MathParseNode parseFunction(const MathToken& token, const MathFunctionSpec& function);
  QVector<MathParseNode> parseGroup();
  QVector<MathParseNode> parseScriptGroup();
  QVector<MathParseNode> parseRequiredGroup(const QString& command);
  bool canStartRequiredArgument(const MathToken& token) const;
  QString parseRawGroupText(const QString& command);
  QString parseOptionalBracketText();
  QString parseSizeText(const QString& command);
  MathParseNode parseBeginEnvironment();
  MathParseNode parseCr(const MathToken& token);
  MathParseNode parseArrayEnvironment(const QString& name);
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
};

}  // namespace muffin::math

#include "mermaid/editor/MermaidDiagrams.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/gitgraph/GitGraphDiagram.h"
#include "mermaid/gitgraph/GitGraphScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QHash>

#include <QJsonObject>
#include <QSize>

#include <cmath>
#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue gitGraphValue(const QJsonObject& object, const char* key,
                         const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() ? fallback : value;
}

qreal finiteNumber(const QJsonValue& value, qreal fallback) {
  const qreal number = jsNumberValue(value);
  return std::isfinite(number) ? number : fallback;
}

// Live subset of gitGraph getStyles (Mermaid 11.16.0). The leading
// `.commit-id, .commit-msg, .branch-label` rule matches no element of the
// current DOM (class tokens differ) but stays for faithfulness; the neo
// `.commit-label-bkg { opacity: ; }` declaration is dropped by the CSSOM and
// is simply not emitted; `.commit-cherry-pick${i}` never matches the
// classless cherry-pick token, exactly like upstream.
QString gitGraphBaseCss(const gitgraph::GitGraphSceneStyle& style,
                        int ruleCount, const QString& noteFontWeight) {
  const QString theme = style.themeName;
  const auto has = [&theme](const char* needle) {
    return theme.contains(QLatin1String(needle));
  };
  const bool neoColorGen =
      has("neo") || has("redux");  // NEO_COLOR_GEN_THEMES
  const bool reduxGeometry = has("redux");  // REDUX_GEOMETRY_THEMES
  const bool colorTheme = has("redux-color");  // COLOR_THEMES (-color suffix)
  const bool neoTheme = theme == QLatin1String("neo") ||
                        theme == QLatin1String("neo-dark");
  const auto arrayAt = [](const QVector<QString>& values, int index) {
    return index >= 0 && index < values.size() ? values.at(index)
                                               : QString();
  };
  QString css = QStringLiteral(
      ".commit-id, .commit-msg, .branch-label { fill: lightgrey; "
      "color: lightgrey; font-family: 'trebuchet ms', verdana, arial, "
      "sans-serif; font-family: var(--mermaid-font-family); }\n");
  const auto weightDecl = [reduxGeometry, noteFontWeight]() {
    return reduxGeometry
        ? QStringLiteral(" font-weight: %1;").arg(noteFontWeight)
        : QString();
  };
  for (int i = 0; i < ruleCount; ++i) {
    if (neoColorGen && neoTheme) {
      if (i == 0) {
        css += QStringLiteral(
            ".branch-label0 { fill: %1; }\n.commit0 { stroke: %1; }\n"
            ".commit-highlight0 { stroke: %1; fill: %1; }\n"
            ".arrow0 { stroke: %1; }\n.commit-bullets { fill: %1; }\n"
            ".commit-cherry-pick0 { stroke: %1; }\n")
            .arg(style.nodeBorder);
      } else {
        const int ci = i % 8;
        const QString git = arrayAt(style.gitColors, ci);
        const QString inv = arrayAt(style.gitInvColors, ci);
        if (!git.isEmpty()) {
          css += QStringLiteral(".branch-label%1 { fill: %2; }\n"
                                ".commit%1 { stroke: %2; fill: %2; }\n"
                                ".arrow%1 { stroke: %2; }\n").arg(i).arg(git);
        }
        if (!inv.isEmpty())
          css += QStringLiteral(".commit-highlight%1 { stroke: %2; fill: %2; }\n")
                     .arg(i)
                     .arg(inv);
      }
    } else if (neoColorGen && !colorTheme) {
      css += QStringLiteral(
                 ".branch-label%1 { fill: %2;%3 }\n.commit%1 { stroke: %2; }\n"
                 ".commit-highlight%1 { stroke: %2; fill: %2; }\n"
                 ".label%1 { fill: %4; stroke: %2; stroke-width: %5;%3 }\n"
                 ".arrow%1 { stroke: %2; }\n"
                 ".commit-bullets { fill: %2; }\n"
                 ".commit-cherry-pick%1 { stroke: %2; }\n")
                 .arg(QString::number(i), style.nodeBorder, weightDecl(),
                      style.mainBkg, QString::number(style.strokeWidth));
    } else if (colorTheme) {
      if (i == 0) {
        css += QStringLiteral(
                   ".branch-label0 { fill: %1;%2 }\n.commit0 { stroke: %1; }\n"
                   ".commit-highlight0 { stroke: %1; fill: %3; }\n"
                   ".label0 { fill: %3; stroke: %1; stroke-width: %4;%2 }\n"
                   ".arrow0 { stroke: %1; }\n.commit-bullets { fill: %1; }\n")
                   .arg(style.nodeBorder, weightDecl(), style.mainBkg,
                        QString::number(style.strokeWidth));
      } else {
        const int ci = i % std::max<qsizetype>(1, style.borderColors.size());
        const QString border = arrayAt(style.borderColors, ci);
        const QString labelFill = theme.contains(QLatin1String("dark"))
                                      ? style.mainBkg
                                      : border;
        if (!border.isEmpty())
          css += QStringLiteral(
                     ".branch-label%1 { fill: %2;%3 }\n"
                     ".commit%1 { stroke: %4; fill: %4; }\n"
                     ".commit-highlight%1 { stroke: %4; fill: %4; }\n"
                     ".label%1 { fill: %5; stroke: %4; stroke-width: %6; }\n"
                     ".arrow%1 { stroke: %4; }\n")
                     .arg(QString::number(i), style.nodeBorder, weightDecl(),
                          border, labelFill,
                          QString::number(style.strokeWidth));
      }
    } else {
      const int ci = i % 8;
      const QString git = arrayAt(style.gitColors, ci);
      const QString inv = arrayAt(style.gitInvColors, ci);
      const QString label = arrayAt(style.branchLabelColors, ci);
      if (!label.isEmpty())
        css += QStringLiteral(".branch-label%1 { fill: %2; }\n").arg(i).arg(label);
      if (!git.isEmpty())
        css += QStringLiteral(".commit%1 { stroke: %2; fill: %2; }\n"
                              ".label%1 { fill: %2; }\n"
                              ".arrow%1 { stroke: %2; }\n")
                     .arg(i)
                     .arg(git);
      if (!inv.isEmpty())
        css += QStringLiteral(".commit-highlight%1 { stroke: %2; fill: %2; }\n")
                     .arg(i)
                     .arg(inv);
    }
  }
  const QString commitLine = !style.commitLineColor.isEmpty()
                                 ? style.commitLineColor
                                 : style.lineColor;
  css += QStringLiteral(
             ".branch { stroke-width: %1; stroke: %2; stroke-dasharray: %3; }\n")
             .arg(QString::number(style.strokeWidth), commitLine,
                  neoColorGen ? QStringLiteral("4 2") : QStringLiteral("2"));
  css += QStringLiteral(".commit-label { font-size: %1px; fill: %2;%3 }\n")
             .arg(QString::number(style.commitLabelFontSize),
                  neoColorGen ? style.nodeBorder : style.commitLabelColor,
                  neoColorGen
                      ? QStringLiteral(" font-weight: %1;")
                            .arg(noteFontWeight)
                      : QString());
  css += QStringLiteral(
             ".commit-label-bkg { font-size: %1px; fill: %2;%3 }\n")
             .arg(QString::number(style.commitLabelFontSize),
                  neoColorGen ? QStringLiteral("transparent")
                              : style.commitLabelBackground,
                  neoColorGen ? QString()
                              : QStringLiteral(" opacity: 0.5;"));
  css += QStringLiteral(".tag-label { font-size: %1px; fill: %2; }\n")
             .arg(QString::number(style.tagLabelFontSize),
                  style.tagLabelColor);
  // The neo `.tag-label-bkg { filter: dropShadow }` glow stays unpainted by
  // the native renderer (same trade as timeline's eventWrapper brightness).
  css += QStringLiteral(".tag-label-bkg { fill: %1; stroke: %2; }\n")
             .arg(neoColorGen ? style.mainBkg : style.tagLabelBackground,
                  neoColorGen ? style.nodeBorder : style.tagLabelBorder);
  css += QStringLiteral(".tag-hole { fill: %1; }\n").arg(style.textColor);
  const QString inner = neoColorGen ? style.mainBkg : style.primaryColor;
  css += QStringLiteral(".commit-merge { stroke: %1; fill: %1; }\n"
                        ".commit-reverse { stroke: %1; fill: %1; "
                        "stroke-width: %2; }\n"
                        ".commit-highlight-outer { }\n"
                        ".commit-highlight-inner { stroke: %1; fill: %1; }\n")
             .arg(inner,
                  neoColorGen ? QString::number(style.strokeWidth)
                              : QStringLiteral("3"));
  css += QStringLiteral(
             ".arrow { stroke-width: %1; stroke-linecap: round; fill: none }\n"
             ".gitTitleText { text-anchor: middle; font-size: 18px; "
             "fill: %2; }\n")
             .arg(reduxGeometry ? QString::number(style.strokeWidth)
                                : QStringLiteral("8"),
                  style.textColor);
  return css;
}

struct GitGraphDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("gitGraph")}; }
  QString cssClass() const override { return QStringLiteral("gitGraph"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeId themeId = themeIdFromName(effectiveTheme);
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeId, themeOverrides(pre.config));

    const QJsonObject family =
        pre.config.value(QStringLiteral("gitGraph")).toObject();
    gitgraph::GitGraphParseConfig parseConfig;
    const QJsonValue mainName =
        gitGraphValue(family, "mainBranchName", QStringLiteral("main"));
    parseConfig.mainBranchName = mainName.isString()
                                     ? mainName.toString()
                                     : QStringLiteral("main");
    parseConfig.mainBranchOrder = finiteNumber(
        gitGraphValue(family, "mainBranchOrder", 0.0), 0.0);

    gitgraph::GitGraphData data =
        gitgraph::GitGraphDiagram::parse(pre.code, parseConfig);
    if (data.title.isEmpty() && !pre.title.isEmpty())
      data.title = HtmlSanitizer().sanitizedMermaidText(pre.title);

    gitgraph::GitGraphConfig config;
    config.useMaxWidth = truthyConfigValue(
        gitGraphValue(family, "useMaxWidth", true));
    config.titleTopMargin = finiteNumber(
        gitGraphValue(family, "titleTopMargin", 25.0), 25.0);
    config.diagramPadding = finiteNumber(
        gitGraphValue(family, "diagramPadding", 8.0), 8.0);
    config.showCommitLabel = truthyConfigValue(
        gitGraphValue(family, "showCommitLabel", true));
    config.showBranches = truthyConfigValue(
        gitGraphValue(family, "showBranches", true));
    config.rotateCommitLabel = truthyConfigValue(
        gitGraphValue(family, "rotateCommitLabel", true));
    config.parallelCommits = truthyConfigValue(
        gitGraphValue(family, "parallelCommits", false));

    gitgraph::GitGraphSceneStyle style;
    style.themeName = effectiveTheme;
    const QJsonValue look = pre.config.value(QStringLiteral("look"));
    style.look = look.isString() ? look.toString() : QStringLiteral("classic");
    style.fontFamily = themeVars.fontFamily;
    const CssLengthContext rootContext =
        pieCssLengthContext(style.fontFamily, 16.0);
    style.fontSize = cssFontSizePx(themeVars.fontSize, rootContext);
    style.textColor = themeVars.textColor;
    style.lineColor = themeVars.lineColor;
    style.commitLineColor = themeVars.commitLineColor;
    const QJsonObject rawThemeVariables =
        pre.config.value(QStringLiteral("themeVariables")).toObject();
    if (rawThemeVariables.contains(QStringLiteral("commitLineColor")))
      style.commitLineColor = flowtheme::resolveFlowTheme(themeId, {}).commitLineColor;
    style.nodeBorder = themeVars.nodeBorder;
    style.mainBkg = themeVars.mainBkg;
    style.primaryColor = themeVars.primaryColor;
    style.commitLabelColor = themeVars.commitLabelColor;
    style.commitLabelBackground = themeVars.commitLabelBackground;
    style.commitLabelFontSize = cssFontSizePx(
        themeVars.commitLabelFontSize, rootContext);
    style.tagLabelColor = themeVars.tagLabelColor;
    style.tagLabelBackground = themeVars.tagLabelBackground;
    style.tagLabelBorder = themeVars.tagLabelBorder;
    style.tagLabelFontSize = cssFontSizePx(
        themeVars.tagLabelFontSize, rootContext);
    style.strokeWidth = themeVars.strokeWidth;
    style.useGradient = themeVars.useGradient;
    style.gradientStart = themeVars.gradientStart;
    style.gradientStop = themeVars.gradientStop;
    for (const QString& value : themeVars.git) style.gitColors.push_back(value);
    for (const QString& value : themeVars.gitInv)
      style.gitInvColors.push_back(value);
    for (const QString& value : themeVars.gitBranchLabel)
      style.branchLabelColors.push_back(value);
    for (const QString& value : themeVars.borderColorArray)
      style.borderColors.push_back(value);

    // themeCSS: resolve the user sheet against a faithful model of the
    // gitGraph DOM (two empty measurement-pass groups, the branch scaffold,
    // arrows, then the content bullets/labels and the title). Base rules come
    // first so `.commitN`/`.labelN` keep overriding presentation attributes
    // exactly as upstream's stylesheet does.
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    gitgraph::GitGraphCssOverrides overrides;
    const bool themeCssActive = !themeCss.trimmed().isEmpty();
    if (themeCssActive) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      const bool reduxGeometry =
          effectiveTheme.contains(QStringLiteral("redux"));
      const bool colorTree = effectiveTheme == QLatin1String("redux-color") ||
                             effectiveTheme ==
                                 QLatin1String("redux-dark-color");
      const int ruleCount =
          jsThemeColorLimit(pre.config).value_or(themeVars.themeColorLimit);
      const QString baseCss = gitGraphBaseCss(
          style, ruleCount,
          themeVars.fontWeight.isEmpty() ? QStringLiteral("400")
                                         : themeVars.fontWeight);
      // Mermaid paints `#id { font-family; font-size; fill: textColor }`
      // directly on the svg element; `svg {}` user rules are scoped to
      // `#id svg` and can never reach the root itself.
      ElementStyle rootStyle;
      rootStyle.fill = style.textColor;
      rootStyle.stroke = QStringLiteral("none");
      rootStyle.strokeWidth = QStringLiteral("1px");
      rootStyle.color = QStringLiteral("black");
      rootStyle.fontFamily = style.fontFamily;
      rootStyle.fontSize =
          QString::number(style.fontSize) + QStringLiteral("px");
      rootStyle.fontWeight = QStringLiteral("400");
      // Empty members inherit the parent's resolved value via project().
      const ElementStyle inheritAll;
      const bool dark = effectiveTheme.contains(QStringLiteral("dark"));
      const QString eye = dark ? QStringLiteral("#000000")
                               : QStringLiteral("#fff");
      // Class digits follow calcColorIndex: color themes skip slot 0, the
      // wrap limit is THEME_COLOR_LIMIT (the redux geometry limit is the
      // themeVariables override). Slot count matches the native colorIndex
      // for the themes the fixture exercises.
      const auto classDigit = [colorTree, reduxGeometry, ruleCount](int index) {
        if (colorTree && index > 0) return (index - 1) % 7 + 1;
        return index % (reduxGeometry ? ruleCount : 12);
      };

      // Pass 1 — branch spacing is probed through a transient classless
      // `g.label.branch-label` wrapper, so `.branch-labelN` font rules move
      // only the drawn label.
      const QHash<QString, ElementStyle> probe = csscascade::resolveElements(
          themeCss,
          {ElementInput{QStringLiteral("svg"), {}, QStringLiteral("svg"),
                        QStringLiteral("diagram-root"), {}, {}, rootStyle, {}},
           ElementInput{QStringLiteral("probe-wrap"), QStringLiteral("svg"),
                        QStringLiteral("g"), {}, {}, {}, inheritAll, {}},
           ElementInput{QStringLiteral("probe-label"),
                        QStringLiteral("probe-wrap"), QStringLiteral("g"), {},
                        {QStringLiteral("branchLabel")}, {}, inheritAll, {}},
           ElementInput{QStringLiteral("probe"), QStringLiteral("probe-label"),
                        QStringLiteral("g"), {},
                        {QStringLiteral("label"),
                         QStringLiteral("branch-label")},
                        {}, inheritAll, {}},
           ElementInput{QStringLiteral("probe-text"), QStringLiteral("probe"),
                        QStringLiteral("text"), {}, {}, {}, inheritAll, {}}},
          baseCss);
      const CssLengthContext familyCtx =
          pieCssLengthContext(style.fontFamily, style.fontSize);
      const auto fontSizePx = [&familyCtx](const QString& css) {
        return cssFontSizePx(css, familyCtx);
      };
      const auto convert = [&](const ElementStyle& resolved) {
        gitgraph::GitGraphElementCss out;
        out.fill = resolved.fill;
        out.stroke = resolved.stroke;
        out.strokeWidth = resolved.strokeWidth;
        out.color = resolved.color;
        // The engine only resolves font-family through matching rules or the
        // inherit keyword; with no declaration the value stays empty and the
        // element keeps the root font chain.
        if (!resolved.fontFamily.trimmed().isEmpty())
          out.fontFamily = firstFontFamily(resolved.fontFamily);
        out.fontSize = fontSizePx(resolved.fontSize);
        out.fontWeight = resolved.fontWeight;
        out.opacity = resolved.effectiveOpacity;
        out.visible = resolved.displayed();
        out.hasBox = resolved.hasBox();
        out.measures =
            resolved.display.compare(QStringLiteral("none"),
                                      Qt::CaseInsensitive) != 0;
        return out;
      };
      overrides.active = true;
      overrides.branchProbe = convert(probe.value(QStringLiteral("probe-text")));

      // Pass 2 — the complete element tree, siblings in document order.
      QVector<ElementInput> tree;
      const auto push = [&tree](ElementInput input) {
        tree.append(std::move(input));
      };
      push({QStringLiteral("svg"), {}, QStringLiteral("svg"),
            QStringLiteral("diagram-root"), {}, {}, rootStyle, {}});
      push({QStringLiteral("styleEl"), QStringLiteral("svg"),
            QStringLiteral("style"), {}, {}, {}, inheritAll, {}});
      // drawCommits(modifyGraph=false) leaves two empty groups behind.
      push({QStringLiteral("bullets-empty"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {QStringLiteral("commit-bullets")}, {},
            inheritAll, {}});
      push({QStringLiteral("labels-empty"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {QStringLiteral("commit-labels")}, {},
            inheritAll, {}});
      push({QStringLiteral("scaffold"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
      const auto bkey = [](qsizetype i, const char* suffix) {
        return QStringLiteral("b%1-%2").arg(i).arg(QLatin1String(suffix));
      };
      for (qsizetype i = 0; i < data.orderedBranches.size(); ++i) {
        const int digit = classDigit(static_cast<int>(i));
        push({bkey(i, "line"), QStringLiteral("scaffold"),
              QStringLiteral("line"), {},
              {QStringLiteral("branch"),
               QStringLiteral("branch%1").arg(digit)},
              {}, inheritAll, {}});
        QHash<QString, QString> bkgAttrs;
        bkgAttrs.insert(QStringLiteral("rx"),
                        reduxGeometry ? QStringLiteral("0")
                                      : QStringLiteral("4"));
        push({bkey(i, "bkg"), QStringLiteral("scaffold"),
              QStringLiteral("rect"), {},
              {QStringLiteral("branchLabelBkg"),
               QStringLiteral("label%1").arg(digit)},
              bkgAttrs, inheritAll, {}});
        push({bkey(i, "blg"), QStringLiteral("scaffold"),
              QStringLiteral("g"), {}, {QStringLiteral("branchLabel")}, {},
              inheritAll, {}});
        push({bkey(i, "grp"), bkey(i, "blg"), QStringLiteral("g"), {},
              {QStringLiteral("label"),
               QStringLiteral("branch-label%1").arg(digit)},
              {}, inheritAll, {}});
        push({bkey(i, "txt"), bkey(i, "grp"), QStringLiteral("text"), {}, {},
              {}, inheritAll, {}});
      }
      // Arrow class digits depend on the layout (reroute lanes and
      // merge-parent flips pick the other branch's color); share the
      // builder's computation.
      const QVector<int> arrowDigits =
          gitgraph::gitGraphArrowClassDigits(data, config, style);
      push({QStringLiteral("arrows-g"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {QStringLiteral("commit-arrows")}, {},
            inheritAll, {}});
      for (qsizetype k = 0; k < arrowDigits.size(); ++k)
        push({QStringLiteral("ar%1").arg(k), QStringLiteral("arrows-g"),
              QStringLiteral("path"), {},
              {QStringLiteral("arrow"),
               QStringLiteral("arrow%1").arg(arrowDigits.at(k))},
              {}, inheritAll, {}});
      push({QStringLiteral("bullets-g"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {QStringLiteral("commit-bullets")}, {},
            inheritAll, {}});
      // Commit draw order matches the builder: seq-sorted, BT reversed.
      QVector<const gitgraph::GitCommit*> ordered;
      for (const gitgraph::GitCommit& commit : data.commits)
        ordered.push_back(&commit);
      std::sort(ordered.begin(), ordered.end(),
                [](const auto* a, const auto* b) { return a->seq < b->seq; });
      if (data.direction == gitgraph::Direction::BottomToTop)
        std::reverse(ordered.begin(), ordered.end());
      for (qsizetype c = 0; c < ordered.size(); ++c) {
        const gitgraph::GitCommit& commit = *ordered.at(c);
        const qsizetype branchIndex =
            data.orderedBranches.indexOf(commit.branch);
        const int digit = classDigit(static_cast<int>(branchIndex));
        const gitgraph::CommitType symbol =
            commit.customType.value_or(commit.type);
        qsizetype j = 0;
        const auto pushBullet = [&push, &c, &j, &inheritAll](
                                    const QStringList& classes,
                                    const QString& tag,
                                    const QString& presentation) {
          push({QStringLiteral("c%1-e%2").arg(c).arg(j++),
                QStringLiteral("bullets-g"), tag, {}, classes, {},
                inheritAll, {}, presentation});
        };
        const QString idClass = commit.id;
        const QString commitDigit =
            QStringLiteral("commit%1").arg(digit);
        if (symbol == gitgraph::CommitType::Highlight) {
          pushBullet({QStringLiteral("commit"), idClass,
                      QStringLiteral("commit-highlight%1").arg(digit),
                      QStringLiteral("commit-highlight-outer")},
                     QStringLiteral("rect"), {});
          pushBullet({QStringLiteral("commit"), idClass, commitDigit,
                      QStringLiteral("commit-highlight-inner")},
                     QStringLiteral("rect"), {});
        } else {
          pushBullet({QStringLiteral("commit"), idClass, commitDigit},
                     QStringLiteral("circle"), {});
          if (symbol == gitgraph::CommitType::Merge)
            pushBullet({QStringLiteral("commit"),
                        QStringLiteral("commit-merge"), idClass, commitDigit},
                       QStringLiteral("circle"), {});
          if (symbol == gitgraph::CommitType::Reverse)
            pushBullet({QStringLiteral("commit"),
                        QStringLiteral("commit-reverse"), idClass,
                        commitDigit},
                       QStringLiteral("path"), {});
          if (symbol == gitgraph::CommitType::CherryPick) {
            // The eye dots carry fill attributes; the stems carry stroke.
            pushBullet({QStringLiteral("commit"), idClass,
                        QStringLiteral("commit-cherry-pick")},
                       QStringLiteral("circle"),
                       QStringLiteral("fill:%1").arg(eye));
            pushBullet({QStringLiteral("commit"), idClass,
                        QStringLiteral("commit-cherry-pick")},
                       QStringLiteral("circle"),
                       QStringLiteral("fill:%1").arg(eye));
            pushBullet({QStringLiteral("commit"), idClass,
                        QStringLiteral("commit-cherry-pick")},
                       QStringLiteral("line"),
                       QStringLiteral("stroke:%1").arg(eye));
            pushBullet({QStringLiteral("commit"), idClass,
                        QStringLiteral("commit-cherry-pick")},
                       QStringLiteral("line"),
                       QStringLiteral("stroke:%1").arg(eye));
          }
        }
      }
      push({QStringLiteral("labels-g"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {QStringLiteral("commit-labels")}, {},
            inheritAll, {}});
      qsizetype labelIndex = 0;
      qsizetype tagIndex = 0;
      const auto hasLabel = [](const gitgraph::GitCommit& commit,
                               bool showCommitLabel) {
        return showCommitLabel &&
               commit.type != gitgraph::CommitType::CherryPick &&
               (commit.type != gitgraph::CommitType::Merge || commit.customId);
      };
      for (qsizetype c = 0; c < ordered.size(); ++c) {
        const gitgraph::GitCommit& commit = *ordered.at(c);
        if (hasLabel(commit, config.showCommitLabel)) {
          const QString wrap = QStringLiteral("l%1-w").arg(labelIndex);
          push({wrap, QStringLiteral("labels-g"), QStringLiteral("g"), {}, {},
                {}, inheritAll, {}});
          push({QStringLiteral("l%1-b").arg(labelIndex), wrap,
                QStringLiteral("rect"), {},
                {QStringLiteral("commit-label-bkg")}, {}, inheritAll, {}});
          push({QStringLiteral("l%1-t").arg(labelIndex), wrap,
                QStringLiteral("text"), {},
                {QStringLiteral("commit-label")}, {}, inheritAll, {}});
          ++labelIndex;
        }
        for (qsizetype ti = 0; ti < commit.tags.size(); ++ti) {
          push({QStringLiteral("tg%1-b").arg(tagIndex),
                QStringLiteral("labels-g"), QStringLiteral("polygon"), {},
                {QStringLiteral("tag-label-bkg")}, {}, inheritAll, {}});
          push({QStringLiteral("tg%1-h").arg(tagIndex),
                QStringLiteral("labels-g"), QStringLiteral("circle"), {},
                {QStringLiteral("tag-hole")}, {}, inheritAll, {}});
          push({QStringLiteral("tg%1-t").arg(tagIndex),
                QStringLiteral("labels-g"), QStringLiteral("text"), {},
                {QStringLiteral("tag-label")}, {}, inheritAll, {}});
          ++tagIndex;
        }
      }
      if (!data.title.isEmpty())
        push({QStringLiteral("title"), QStringLiteral("svg"),
              QStringLiteral("text"), {}, {QStringLiteral("gitTitleText")},
              {}, inheritAll, {}});

      const QHash<QString, ElementStyle> css = csscascade::resolveElements(
          themeCss, tree, baseCss);
      const auto at = [&css](const QString& key) -> const ElementStyle& {
        static const ElementStyle empty;
        const auto it = css.constFind(key);
        return it == css.cend() ? empty : it.value();
      };
      for (qsizetype i = 0; i < data.orderedBranches.size(); ++i) {
        gitgraph::GitGraphCssOverrides::Branch branch;
        branch.line = convert(at(bkey(i, "line")));
        branch.bkg = convert(at(bkey(i, "bkg")));
        branch.group = convert(at(bkey(i, "grp")));
        branch.text = convert(at(bkey(i, "txt")));
        overrides.branches.append(branch);
      }
      for (qsizetype k = 0; k < arrowDigits.size(); ++k)
        overrides.arrows.append(
            convert(at(QStringLiteral("ar%1").arg(k))));
      for (qsizetype c = 0; c < ordered.size(); ++c) {
        const gitgraph::GitCommit& commit = *ordered.at(c);
        const gitgraph::CommitType symbol =
            commit.customType.value_or(commit.type);
        qsizetype count = 1;
        if (symbol == gitgraph::CommitType::Highlight ||
            symbol == gitgraph::CommitType::Merge ||
            symbol == gitgraph::CommitType::Reverse)
          count = 2;
        else if (symbol == gitgraph::CommitType::CherryPick)
          count = 5;
        QVector<gitgraph::GitGraphElementCss> bullets;
        for (qsizetype j = 0; j < count; ++j)
          bullets.append(
              convert(at(QStringLiteral("c%1-e%2").arg(c).arg(j))));
        overrides.bullets.append(bullets);
      }
      for (qsizetype k = 0; k < labelIndex; ++k) {
        gitgraph::GitGraphCssOverrides::CommitLabel label;
        label.wrapper = convert(at(QStringLiteral("l%1-w").arg(k)));
        label.bkg = convert(at(QStringLiteral("l%1-b").arg(k)));
        label.text = convert(at(QStringLiteral("l%1-t").arg(k)));
        overrides.labels.append(label);
      }
      for (qsizetype k = 0; k < tagIndex; ++k) {
        gitgraph::GitGraphCssOverrides::Tag tag;
        tag.bkg = convert(at(QStringLiteral("tg%1-b").arg(k)));
        tag.hole = convert(at(QStringLiteral("tg%1-h").arg(k)));
        tag.text = convert(at(QStringLiteral("tg%1-t").arg(k)));
        overrides.tags.append(tag);
      }
      overrides.title = convert(at(QStringLiteral("title")));
    }

    gitgraph::GitGraphScene scene = gitgraph::buildGitGraphScene(
        data, std::move(config), std::move(style),
        themeCssActive ? &overrides : nullptr);
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, data.title, data.accTitle, data.accDescr,
        scene.style.textColor, scene.style.fontFamily, 16.0);
    // GitGraph draws the visible title in its own coordinate system.
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    // Chrome lays the svg out at LayoutUnit floor of the viewBox and the
    // CSS box rounds up, matching ceil of the exact bounds (the same
    // reconciliation the other families use).
    entry.naturalSize = QSize(qCeil(scene.bounds.width()),
                              qCeil(scene.bounds.height()));
    entry.scene =
        std::make_shared<const gitgraph::GitGraphScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& gitGraphDiagramAdapter() {
  static const GitGraphDiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor

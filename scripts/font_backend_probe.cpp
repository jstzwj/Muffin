#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRawFont>
#include <QTextLayout>
#include <cstdio>

#include "theme/FontRendering.h"

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_FONT_DPI", "96");
  QApplication app(argc, argv);
  if (argc != 3) {
    std::fprintf(stderr, "Usage: MuffinFontBackendProbe font.ttf output-directory\n");
    return 1;
  }
  const auto output = QDir(QString::fromLocal8Bit(argv[2]));
  if (!QDir().mkpath(output.absolutePath())) return 3;
  const int id = QFontDatabase::addApplicationFont(QString::fromLocal8Bit(argv[1]));
  const auto family = QFontDatabase::applicationFontFamilies(id).value(0);
  if (family.isEmpty()) return 2;
  const QString text = QStringLiteral("Muffin Markdown Example 中文混排 office fi");
  QJsonArray result;
  for (qreal size : {16., 18.4, 28.8, 29., 32.4})
    for (int precision : {1, 8, 64})
      for (bool bold : {false, true})
        for (bool italic : {false, true})
          for (bool optionalLigatures : {true, false}) {
            QImage device(1, 1, QImage::Format_ARGB32_Premultiplied);
            device.setDotsPerMeterX(qRound(96 * precision / .0254));
            device.setDotsPerMeterY(qRound(96 * precision / .0254));
            QFont font(family);
            font.setPointSizeF(size * .75);
            font.setWeight(bold ? QFont::Bold : QFont::Normal);
            font.setItalic(italic);
            muffin::font_rendering::configureCssFont(font, .7, .3);
            if (!optionalLigatures) {
              font.setFeature(QFont::Tag("liga"), 0);
              font.setFeature(QFont::Tag("clig"), 0);
            }
            QElapsedTimer timer;
            timer.start();
            QTextLayout layout(text, font, &device);
            layout.beginLayout();
            for (;;) {
              auto line = layout.createLine();
              if (!line.isValid()) break;
              line.setLineWidth(240 * precision);
            }
            layout.endLayout();
            qreal y = 0;
            QJsonArray lines, glyphs;
            for (int i = 0; i < layout.lineCount(); ++i) {
              auto line = layout.lineAt(i);
              line.setPosition({0, y});
              y += line.height();
              lines.append(QJsonObject{{"start", line.textStart()},
                                       {"end", line.textStart() + line.textLength()},
                                       {"width", line.horizontalAdvance() / precision},
                                       {"ascent", line.ascent() / precision}});
              for (int j = line.textStart(); j < line.textStart() + line.textLength(); ++j) {
                qreal x = line.cursorToX(j);
                // Ligature interiors can share an x, so record round trips for diagnosis.
                glyphs.append(QJsonObject{{"source", j}, {"x", x / precision}, {"hit", line.xToCursor(x)}});
              }
            }
            const auto layoutUs = timer.nsecsElapsed() / 1000.;
            QImage image(270, 200, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            {
              QPainter p(&image);
              p.scale(1. / precision, 1. / precision);
              layout.draw(&p, {});
            }
            image.save(
                output.filePath(QString("probe-%1-%2-%3-%4-%5.png").arg(size).arg(precision).arg(bold).arg(italic).arg(optionalLigatures)));
            QJsonArray runs;
            for (const auto& run : layout.glyphRuns())
              runs.append(QJsonObject{{"family", run.rawFont().familyName()},
                                      {"styleName", run.rawFont().styleName()},
                                      {"weight", run.rawFont().weight()},
                                      {"style", int(run.rawFont().style())},
                                      {"headSha256", QString::fromLatin1(QCryptographicHash::hash(run.rawFont().fontTable("head"), QCryptographicHash::Sha256).toHex())},
                                      {"size", run.rawFont().pixelSize() / precision},
                                      {"glyphs", run.glyphIndexes().size()}});
            result.append(QJsonObject{{"optionalLigatures", optionalLigatures},
                                      {"qt", QT_VERSION_STR},
                                      {"size", size},
                                      {"precision", precision},
                                      {"bold", bold},
                                      {"italic", italic},
                                      {"layoutUs", layoutUs},
                                      {"height", y / precision},
                                      {"lines", lines},
                                      {"carets", glyphs},
                                      {"runs", runs}});
          }
  QFile fontFile(QString::fromLocal8Bit(argv[1]));
  if (!fontFile.open(QIODevice::ReadOnly)) return 4;
  const auto sha = QString::fromLatin1(QCryptographicHash::hash(fontFile.readAll(), QCryptographicHash::Sha256).toHex());
  const auto json = QJsonDocument(QJsonObject{{"fontSha256", sha},
                                              {"family", family},
                                              {"qt", QT_VERSION_STR},
                                              {"platform", QApplication::platformName()},
                                              {"text", text},
                                              {"width", 240},
                                              {"letterSpacing", .7},
                                              {"wordSpacing", .3},
                                              {"results", result}})
                        .toJson();
  QFile file(output.filePath("native.json"));
  if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size()) return 5;
}

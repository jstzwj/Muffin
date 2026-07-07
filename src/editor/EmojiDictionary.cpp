#include "editor/EmojiDictionary.h"

#include <QFile>
#include <QTextStream>

namespace muffin {

const QHash<QString, QString>& emojiShortcodeMap() {
  static const QHash<QString, QString> map = [] {
    QHash<QString, QString> m;
    QFile file(QStringLiteral(":/emoji/emoji.txt"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&file);
      in.setGenerateByteOrderMark(false);
      QString line;
      while (in.readLineInto(&line)) {
        const qsizetype tab = line.indexOf(QLatin1Char('\t'));
        if (tab <= 0 || tab + 1 >= line.size()) {
          continue;
        }
        const QString code = line.left(tab).trimmed();
        const QString glyph = line.mid(tab + 1).trimmed();
        if (!code.isEmpty() && !glyph.isEmpty()) {
          m.insert(code, glyph);
        }
      }
    }
    return m;
  }();
  return map;
}

qsizetype emojiShortcodeLengthAt(QStringView source, qsizetype i) {
  if (i < 0 || i >= source.size() || source.at(i) != QLatin1Char(':')) {
    return 0;
  }
  // Scan the name: one or more [A-Za-z0-9_+-].
  qsizetype j = i + 1;
  for (; j < source.size(); ++j) {
    const ushort u = source.at(j).unicode();
    const bool nameChar = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
                          (u >= '0' && u <= '9') || u == '_' || u == '+' || u == '-';
    if (!nameChar) {
      break;
    }
  }
  if (j == i + 1) {
    return 0;  // empty name ("::" or ":...")
  }
  if (j >= source.size() || source.at(j) != QLatin1Char(':')) {
    return 0;  // no closing colon within the name run
  }
  const qsizetype total = j - i + 1;  // opening ':' + name + closing ':'
  if (total > 64) {
    return 0;  // bounded scan guard
  }
  const QStringView name = source.mid(i + 1, j - i - 1);
  return emojiShortcodeMap().contains(name.toString()) ? total : 0;
}

}  // namespace muffin

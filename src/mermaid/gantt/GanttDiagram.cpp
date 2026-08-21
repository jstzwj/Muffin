#include "mermaid/gantt/GanttDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <QDate>
#include <QRegularExpression>
#include <QTime>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <optional>

namespace muffin::mermaid::gantt {

GanttParseError::GanttParseError(const QString& message, int line, int column,
                                 GanttErrorKind kind)
    : std::runtime_error(message.toUtf8().constData()),
      line(line),
      column(column),
      kind(kind) {}

namespace {

bool jsWhitespace(QChar ch) {
  const ushort value = ch.unicode();
  return (value >= 0x0009 && value <= 0x000d) || value == 0x0020 ||
         value == 0x00a0 || value == 0x1680 ||
         (value >= 0x2000 && value <= 0x200a) || value == 0x2028 ||
         value == 0x2029 || value == 0x202f || value == 0x205f ||
         value == 0x3000 || value == 0xfeff;
}

QString jsTrimmed(QString value) {
  qsizetype begin = 0;
  while (begin < value.size() && jsWhitespace(value.at(begin))) ++begin;
  qsizetype end = value.size();
  while (end > begin && jsWhitespace(value.at(end - 1))) --end;
  return value.mid(begin, end - begin);
}

QString deindent(QString value) {
  QString result;
  result.reserve(value.size());
  for (qsizetype index = 0; index < value.size();) {
    const QChar ch = value.at(index++);
    result += ch;
    if (ch != QLatin1Char('\n')) continue;
    while (index < value.size() && jsWhitespace(value.at(index))) ++index;
  }
  return result;
}

QString sanitizedTitle(const QString& value) {
  return HtmlSanitizer().sanitizedMermaidText(value);
}

QString sanitizedAcc(QString value, bool multiline) {
  value = HtmlSanitizer().sanitizedMermaidText(jsTrimmed(std::move(value)));
  return multiline ? deindent(std::move(value)) : value;
}

bool startsKeyword(const QString& value, const QString& keyword,
                   bool requiresValue = false) {
  if (!value.startsWith(keyword, Qt::CaseInsensitive)) return false;
  if (value.size() == keyword.size()) return !requiresValue;
  return jsWhitespace(value.at(keyword.size()));
}

QString keywordValue(const QString& value, qsizetype actionOffset) {
  return value.mid(actionOffset);
}

QStringList mergeTokens(const QStringList& existing, const QString& text) {
  QStringList result = existing;
  const QStringList tokens = text.toLower().split(
      QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
  for (const QString& token : tokens)
    if (!result.contains(token)) result.append(token);
  return result;
}

// Gantt dates are wall-clock values. The UTC spec makes the stored instant
// timezone-independent: day arithmetic and every painted label go through the
// QDate wall clock (unchanged either way), while QDateTime::toUTC()/epoch
// consumers — the grammar oracle, and JS `new Date("YYYY-MM-DD")` which parses
// date-only ISO strings as UTC — now agree on any host. The previous
// LocalTime spec silently baked the runner's timezone into the instant and
// failed the oracle everywhere except the fixture-generating UTC+8 host.
QDateTime localDateTime(const QDate& date, const QTime& time = QTime(0, 0)) {
  return QDateTime(date, time, Qt::UTC);
}

QDateTime parseStrictDate(const QString& text, const QString& format) {
  const QString value = text.trimmed();
  const QString fmt = format.trimmed();
  if (fmt == QLatin1String("YYYY-MM-DD")) {
    static const QRegularExpression re(QStringLiteral(R"(^(\d{4})-(\d{2})-(\d{2})$)"));
    const auto match = re.match(value);
    if (match.hasMatch())
      return localDateTime(QDate(match.captured(1).toInt(),
                                 match.captured(2).toInt(),
                                 match.captured(3).toInt()));
  } else if (fmt == QLatin1String("DD/MM/YYYY")) {
    static const QRegularExpression re(QStringLiteral(R"(^(\d{2})/(\d{2})/(\d{4})$)"));
    const auto match = re.match(value);
    if (match.hasMatch())
      return localDateTime(QDate(match.captured(3).toInt(),
                                 match.captured(2).toInt(),
                                 match.captured(1).toInt()));
  } else if (fmt == QLatin1String("YYYY-MM-DD HH:mm")) {
    static const QRegularExpression re(QStringLiteral(
        R"(^(\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2})$)"));
    const auto match = re.match(value);
    if (match.hasMatch())
      return localDateTime(QDate(match.captured(1).toInt(),
                                 match.captured(2).toInt(),
                                 match.captured(3).toInt()),
                           QTime(match.captured(4).toInt(),
                                 match.captured(5).toInt()));
  }
  return {};
}

QDateTime parseJsDateFallback(const QString& text) {
  const QString value = text.trimmed();
  static const QRegularExpression isoDate(QStringLiteral(R"(^(\d{4})-(\d{2})-(\d{2})$)"));
  const auto match = isoDate.match(value);
  if (match.hasMatch())
    return QDateTime(QDate(match.captured(1).toInt(),
                           match.captured(2).toInt(),
                           match.captured(3).toInt()),
                     QTime(0, 0), QTimeZone::UTC);
  QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
  if (!parsed.isValid()) parsed = QDateTime::fromString(value, Qt::ISODate);
  // fromString produces a LocalTime spec; reinterpret the parsed wall clock as
  // UTC (no instant shift) so every gantt value shares one spec — mixing specs
  // skews daysTo/secsTo arithmetic by the local offset.
  if (parsed.isValid()) parsed.setTimeSpec(Qt::UTC);
  return parsed;
}

QDateTime addDuration(QDateTime value, const QString& source) {
  static const QRegularExpression duration(
      QStringLiteral(R"(^(\d+(?:\.\d+)?)(M|d|h|m|s|w|y|ms)$)"));
  const auto match = duration.match(source.trimmed());
  if (!match.hasMatch()) return value;
  const double amount = match.captured(1).toDouble();
  const QString unit = match.captured(2);
  if (unit == QLatin1String("ms")) return value.addMSecs(qint64(amount));
  if (unit == QLatin1String("s")) return value.addMSecs(qint64(amount * 1000.0));
  if (unit == QLatin1String("m")) return value.addMSecs(qint64(amount * 60000.0));
  if (unit == QLatin1String("h")) return value.addMSecs(qint64(amount * 3600000.0));
  if (unit == QLatin1String("d")) return value.addDays(qRound(amount));
  if (unit == QLatin1String("w")) return value.addDays(qRound(amount * 7.0));
  if (unit == QLatin1String("M")) return value.addMonths(int(amount));
  if (unit == QLatin1String("y")) return value.addYears(int(amount));
  return value;
}

bool exactManualEnd(const QString& value) {
  return parseStrictDate(value, QStringLiteral("YYYY-MM-DD")).isValid();
}

bool isInvalidDate(const QDateTime& date, const GanttData& data) {
  const QString dateOnly = date.date().toString(QStringLiteral("yyyy-MM-dd"));
  QString formatted;
  if (data.dateFormat.trimmed() == QLatin1String("YYYY-MM-DD"))
    formatted = dateOnly;
  else if (data.dateFormat.trimmed() == QLatin1String("DD/MM/YYYY"))
    formatted = date.date().toString(QStringLiteral("dd/MM/yyyy"));
  else
    formatted = dateOnly;
  if (data.includes.contains(formatted) || data.includes.contains(dateOnly))
    return false;
  const int day = date.date().dayOfWeek();
  const int weekendStart = data.weekend == QLatin1String("friday") ? 5 : 6;
  if (data.excludes.contains(QStringLiteral("weekends")) &&
      (day == weekendStart || day == weekendStart + 1))
    return true;
  static const QStringList names = {
      QStringLiteral("monday"), QStringLiteral("tuesday"),
      QStringLiteral("wednesday"), QStringLiteral("thursday"),
      QStringLiteral("friday"), QStringLiteral("saturday"),
      QStringLiteral("sunday")};
  if (day >= 1 && day <= names.size() && data.excludes.contains(names.at(day - 1)))
    return true;
  return data.excludes.contains(formatted) || data.excludes.contains(dateOnly);
}

QString safeUrl(const QString& raw) {
  const QString lower = raw.trimmed().toLower();
  if (lower.startsWith(QStringLiteral("javascript:")) ||
      lower.startsWith(QStringLiteral("data:")) ||
      lower.startsWith(QStringLiteral("vbscript:")))
    return QStringLiteral("about:blank");
  return raw;
}

struct Compiler {
  GanttData data;
  QHash<QString, qsizetype> byId;
  QString currentSection;
  QString lastTaskId;
  int taskCount = 0;
  int lastOrder = 0;

  QString parseId(const std::optional<QString>& source) {
    if (source) return *source;
    return QStringLiteral("task%1").arg(++taskCount);
  }

  void addTask(const QString& description, const QString& rawData) {
    GanttTask task;
    task.section = currentSection;
    task.type = currentSection;
    task.rawData = rawData;
    task.task = description;
    task.prevTaskId = lastTaskId;
    QStringList fields = rawData.mid(rawData.startsWith(QLatin1Char(':')) ? 1 : 0)
                             .split(QLatin1Char(','));
    const QStringList tags = {QStringLiteral("active"), QStringLiteral("done"),
                              QStringLiteral("crit"), QStringLiteral("milestone"),
                              QStringLiteral("vert")};
    bool found = true;
    while (found && !fields.isEmpty()) {
      found = false;
      for (const QString& tag : tags) {
        if (fields.first().trimmed() != tag) continue;
        if (tag == QLatin1String("active")) task.active = true;
        if (tag == QLatin1String("done")) task.done = true;
        if (tag == QLatin1String("crit")) task.crit = true;
        if (tag == QLatin1String("milestone")) task.milestone = true;
        if (tag == QLatin1String("vert")) task.vert = true;
        fields.removeFirst();
        found = true;
        break;
      }
    }
    for (QString& field : fields) field = field.trimmed();
    if (fields.size() == 1) {
      task.id = parseId(std::nullopt);
      task.rawStartType = QStringLiteral("prevTaskEnd");
      task.rawEndData = fields.at(0);
    } else if (fields.size() == 2) {
      task.id = parseId(std::nullopt);
      task.rawStartType = QStringLiteral("getStartDate");
      task.rawStartData = fields.at(0);
      task.rawEndData = fields.at(1);
    } else if (fields.size() == 3) {
      task.id = parseId(fields.at(0));
      task.rawStartType = QStringLiteral("getStartDate");
      task.rawStartData = fields.at(1);
      task.rawEndData = fields.at(2);
    }
    task.order = task.vert ? -1 : lastOrder++;
    const qsizetype index = data.tasks.size();
    data.tasks.append(std::move(task));
    lastTaskId = data.tasks.last().id;
    byId.insert(lastTaskId, index);
  }

  GanttTask* find(const QString& id) {
    const auto found = byId.constFind(id);
    return found == byId.constEnd() ? nullptr : &data.tasks[*found];
  }

  QDateTime startDate(const QString& source) {
    const QString value = source.trimmed();
    if ((data.dateFormat.trimmed() == QLatin1String("x") ||
         data.dateFormat.trimmed() == QLatin1String("X")) &&
        QRegularExpression(QStringLiteral(R"(^\d+$)")).match(value).hasMatch())
      // Upstream feeds BOTH x (ms) and X (s) values into `new Date(Number)`
      // as milliseconds — the seconds format therefore lands in 1970. That is
      // the recorded upstream behavior this parser mirrors.
      return QDateTime::fromMSecsSinceEpoch(value.toLongLong(), Qt::UTC);
    static const QRegularExpression after(
        QStringLiteral(R"(^after\s+([\d\w\- ]+))"));
    const auto afterMatch = after.match(value);
    if (afterMatch.hasMatch()) {
      GanttTask* latest = nullptr;
      for (const QString& id : afterMatch.captured(1).split(QLatin1Char(' '))) {
        GanttTask* candidate = find(id);
        if (candidate && (!latest || candidate->endTime > latest->endTime))
          latest = candidate;
      }
      if (latest) return latest->endTime;
      return localDateTime(QDate::currentDate());
    }
    QDateTime parsed = parseStrictDate(value, data.dateFormat);
    if (parsed.isValid()) return parsed;
    parsed = parseJsDateFallback(value);
    if (!parsed.isValid())
      throw GanttParseError(QStringLiteral("Invalid date:") + value, 0, 1,
                            GanttErrorKind::Runtime);
    return parsed;
  }

  QDateTime endDate(const QDateTime& start, const QString& source) {
    const QString value = source.trimmed();
    static const QRegularExpression until(
        QStringLiteral(R"(^until\s+([\d\w\- ]+))"));
    const auto untilMatch = until.match(value);
    if (untilMatch.hasMatch()) {
      GanttTask* earliest = nullptr;
      for (const QString& id : untilMatch.captured(1).split(QLatin1Char(' '))) {
        GanttTask* candidate = find(id);
        if (candidate && (!earliest || candidate->startTime < earliest->startTime))
          earliest = candidate;
      }
      if (earliest) return earliest->startTime;
      return localDateTime(QDate::currentDate());
    }
    QDateTime parsed = parseStrictDate(value, data.dateFormat);
    if (parsed.isValid())
      return data.inclusiveEndDates ? parsed.addDays(1) : parsed;
    return addDuration(start, value);
  }

  void adjustExcludedDates(GanttTask& task) {
    if (data.excludes.isEmpty() || task.manualEndTime) return;
    QDateTime cursor = task.startTime.addDays(1);
    QDateTime end = task.endTime;
    bool invalid = false;
    QDateTime render;
    int extensions = 0;
    while (cursor <= end) {
      if (!invalid) render = end;
      invalid = isInvalidDate(cursor, data);
      if (invalid) {
        end = end.addDays(1);
        if (++extensions > 10000)
          throw GanttParseError(
              QStringLiteral("Failed to find a valid date that was not excluded by `excludes` after 10,000 iterations."),
              0, 1, GanttErrorKind::Runtime);
      }
      cursor = cursor.addDays(1);
    }
    task.endTime = end;
    task.renderEndTime = render;
  }

  bool compilePass() {
    bool all = true;
    for (GanttTask& task : data.tasks) {
      if (task.rawStartType.isEmpty())
        throw GanttParseError(
            QStringLiteral("Cannot read properties of undefined (reading 'type')"),
            0, 1, GanttErrorKind::Runtime);
      if (task.rawStartType == QLatin1String("prevTaskEnd")) {
        GanttTask* previous = find(task.prevTaskId);
        if (!previous)
          throw GanttParseError(
              QStringLiteral("Cannot read properties of undefined (reading 'endTime')"),
              0, 1, GanttErrorKind::Runtime);
        task.startTime = previous->endTime;
      } else {
        task.startTime = startDate(task.rawStartData);
      }
      if (task.startTime.isValid()) {
        task.endTime = endDate(task.startTime, task.rawEndData);
        if (task.endTime.isValid()) {
          task.processed = true;
          task.manualEndTime = exactManualEnd(task.rawEndData);
          adjustExcludedDates(task);
        }
      }
      all = all && task.processed;
    }
    return all;
  }

  void compile() {
    bool all = compilePass();
    int iterations = 0;
    while (!all && iterations < 10) {
      all = compilePass();
      ++iterations;
    }
  }
};

[[noreturn]] void parserError(int line, int column, const QString& detail) {
  throw GanttParseError(QStringLiteral("Parse error on line %1: %2").arg(line).arg(detail),
                        line, column, GanttErrorKind::Parser);
}

void addClick(Compiler& compiler, const QString& line, int lineNumber,
              bool atEof) {
  static const QRegularExpression href(QStringLiteral(
      R"re(^click\s+([^\s]+)\s+href\s+"([^"]*)"\s*$)re"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression call(QStringLiteral(
      R"(^click\s+([^\s]+)\s+call\s+([^\s\(]+)(?:\(([^\)]*)\))?\s*$)"),
      QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatch match = href.match(line);
  QString ids;
  std::optional<QString> link;
  if (match.hasMatch()) {
    ids = match.captured(1);
    link = safeUrl(match.captured(2));
  } else {
    match = call.match(line);
    if (!match.hasMatch())
      parserError(atEof ? lineNumber + 1 : lineNumber, 1, line);
    ids = match.captured(1);
  }
  for (const QString& id : ids.split(QLatin1Char(','))) {
    GanttTask* task = compiler.find(id);
    if (!task) continue;
    task->classes.append(QStringLiteral("clickable"));
    if (link) compiler.data.links.insert(id, *link);
  }
}

}  // namespace

GanttData GanttDiagram::parse(const QString& rawSource) {
  QString source = rawSource;
  source.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  source.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  static const QRegularExpression header(
      QStringLiteral(R"(\A[\s\x{00a0}\x{feff}]*gantt\b)"),
      QRegularExpression::CaseInsensitiveOption);
  const auto headerMatch = header.match(source);
  if (!headerMatch.hasMatch()) parserError(1, 1, source.left(20));
  source.remove(0, headerMatch.capturedLength());

  Compiler compiler;
  int lineNumber = rawSource.left(headerMatch.capturedStart()).count(QLatin1Char('\n')) + 1;
  QStringList lines = source.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
  bool inAccBlock = false;
  QString accBlock;
  for (qsizetype index = 0; index < lines.size(); ++index) {
    ++lineNumber;
    QString line = lines.at(index);
    if (index == 0) --lineNumber;
    if (inAccBlock) {
      const qsizetype close = line.indexOf(QLatin1Char('}'));
      if (close < 0) {
        accBlock += line + QLatin1Char('\n');
        continue;
      }
      accBlock += line.left(close);
      compiler.data.accDescr = sanitizedAcc(accBlock, true);
      inAccBlock = false;
      line = line.mid(close + 1);
    }
    QString statement = line;
    while (!statement.isEmpty() && jsWhitespace(statement.front()))
      statement.remove(0, 1);
    const QString trimmed = statement.trimmed();
    if (trimmed.isEmpty() || statement.startsWith(QLatin1Char('%'))) continue;

    if (startsKeyword(statement, QStringLiteral("accDescr"))) {
      static const QRegularExpression block(QStringLiteral(R"(^accDescr\s*\{\s*)"),
                                             QRegularExpression::CaseInsensitiveOption);
      const auto blockMatch = block.match(statement);
      if (blockMatch.hasMatch()) {
        QString value = statement.mid(blockMatch.capturedLength());
        const qsizetype close = value.indexOf(QLatin1Char('}'));
        if (close >= 0) {
          compiler.data.accDescr = sanitizedAcc(value.left(close), true);
          if (!value.mid(close + 1).trimmed().isEmpty())
            parserError(lineNumber, close + 1, line);
        } else {
          inAccBlock = true;
          accBlock = value + QLatin1Char('\n');
        }
        continue;
      }
    }

    if (startsKeyword(statement, QStringLiteral("dateFormat"), true)) {
      compiler.data.dateFormat = keywordValue(statement, 11);
    } else if (trimmed.compare(QStringLiteral("inclusiveEndDates"), Qt::CaseInsensitive) == 0) {
      compiler.data.inclusiveEndDates = true;
    } else if (trimmed.compare(QStringLiteral("topAxis"), Qt::CaseInsensitive) == 0) {
      throw GanttParseError(QStringLiteral("yy.TopAxis is not a function"), 0, 1,
                            GanttErrorKind::Runtime);
    } else if (startsKeyword(statement, QStringLiteral("axisFormat"), true)) {
      compiler.data.axisFormat = keywordValue(statement, 11);
    } else if (startsKeyword(statement, QStringLiteral("tickInterval"), true)) {
      compiler.data.tickInterval = keywordValue(statement, 13);
    } else if (startsKeyword(statement, QStringLiteral("excludes"), true)) {
      compiler.data.excludes = mergeTokens(compiler.data.excludes,
                                           keywordValue(statement, 9));
    } else if (startsKeyword(statement, QStringLiteral("includes"), true)) {
      compiler.data.includes = mergeTokens(compiler.data.includes,
                                           keywordValue(statement, 9));
    } else if (startsKeyword(statement, QStringLiteral("todayMarker"), true)) {
      compiler.data.todayMarker = keywordValue(statement, 12);
    } else if (startsKeyword(statement, QStringLiteral("weekday"), true)) {
      compiler.data.weekday = keywordValue(statement, 8).trimmed().toLower();
    } else if (startsKeyword(statement, QStringLiteral("weekend"), true)) {
      compiler.data.weekend = keywordValue(statement, 8).trimmed().toLower();
    } else if (startsKeyword(statement, QStringLiteral("title"), true)) {
      compiler.data.title = sanitizedTitle(keywordValue(statement, 6));
    } else if (QRegularExpression(QStringLiteral(R"(^accTitle\s*:)") ,
                                  QRegularExpression::CaseInsensitiveOption)
                   .match(statement).hasMatch()) {
      const qsizetype colon = statement.indexOf(QLatin1Char(':'));
      if (colon < 0) parserError(lineNumber, 1, line);
      compiler.data.accTitle = sanitizedAcc(statement.mid(colon + 1), false);
    } else if (QRegularExpression(QStringLiteral(R"(^accDescr\s*:)") ,
                                  QRegularExpression::CaseInsensitiveOption)
                   .match(statement).hasMatch()) {
      const qsizetype colon = statement.indexOf(QLatin1Char(':'));
      if (colon < 0) parserError(lineNumber, 1, line);
      compiler.data.accDescr = sanitizedAcc(statement.mid(colon + 1), false);
    } else if (startsKeyword(statement, QStringLiteral("section"), true)) {
      compiler.currentSection = keywordValue(statement, 8);
      compiler.data.sections.append(compiler.currentSection);
    } else if (startsKeyword(statement, QStringLiteral("click"), true)) {
      addClick(compiler, statement, lineNumber, index + 1 == lines.size());
    } else {
      const qsizetype colon = statement.indexOf(QLatin1Char(':'));
      if (colon < 0 || colon == 0)
        parserError(colon < 0 && index + 1 == lines.size() ? lineNumber + 1
                                                           : lineNumber,
                    1, line);
      QString taskData = statement.mid(colon);
      const qsizetype comment = taskData.indexOf(QLatin1Char('#'));
      if (comment >= 0) taskData.truncate(comment);
      const qsizetype semicolon = taskData.indexOf(QLatin1Char(';'));
      if (semicolon >= 0) parserError(lineNumber, colon + semicolon + 1, line);
      if (taskData.mid(1).trimmed().isEmpty())
        parserError(lineNumber, colon + 1, line);
      compiler.addTask(statement.left(colon), taskData);
    }
  }
  if (inAccBlock) parserError(lineNumber + 1, 1, QStringLiteral("EOF"));
  compiler.compile();
  return std::move(compiler.data);
}

}  // namespace muffin::mermaid::gantt

#include "theme/CssGridStyle.h"
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace muffin {
namespace {
QStringList split(const QString& value, QChar separator = {}) {
  QStringList parts;
  int depth = 0, brackets = 0, start = 0;
  const auto append = [&](int end) {
    const auto part = value.mid(start, end - start).trimmed();
    if (!part.isEmpty()) parts.push_back(part);
  };
  for (int i = 0; i < value.size(); ++i) {
    if (value[i] == '(') ++depth;
    if (value[i] == ')' && --depth < 0) return {};
    if (value[i] == '[') {
      if (++brackets > 1) return {};
      if (separator.isNull() && depth == 0) {
        append(i);
        start = i;
      }
    }
    if (value[i] == ']') {
      if (--brackets < 0) return {};
      if (separator.isNull() && depth == 0) {
        append(i + 1);
        start = i + 1;
      }
    }
    if (depth == 0 && brackets == 0 && (separator.isNull() ? value[i].isSpace() : value[i] == separator)) {
      const auto part = value.mid(start, i - start).trimmed();
      if (!part.isEmpty())
        parts.push_back(part);
      else if (!separator.isNull())
        return {};
      start = i + 1;
    }
  }
  if (depth != 0 || brackets != 0) return {};
  const auto last = value.mid(start).trimmed();
  if (last.isEmpty() && !separator.isNull()) return {};
  if (!last.isEmpty()) parts.push_back(last);
  return parts;
}
bool identifier(const QString& value) {
  if (value.isEmpty()) return false;
  const auto first = [](QChar c) { return c.isLetter() || c == '_' || c.unicode() >= 128; };
  int i = value[0] == '-' ? 1 : 0;
  if (i >= value.size() || !(first(value[i]) || (i == 1 && value[i] == '-'))) return false;
  for (; i < value.size(); ++i)
    if (!first(value[i]) && !value[i].isDigit() && value[i] != '-') return false;
  const auto lower = value.toLower();
  return !QStringList{"auto", "span", "initial", "inherit", "unset", "revert", "revert-layer", "default"}.contains(lower);
}
void appendList(CssGridTrackList& target, const CssGridTrackList& source) {
  target.lineNames.back().append(source.lineNames.front());
  target.lineNames.back().removeDuplicates();
  for (size_t i = 0; i < source.size(); ++i) {
    target.tracks.push_back(source[i]);
    target.lineNames.push_back(source.lineNames[i + 1]);
  }
}
std::optional<CssGridBreadth> breadth(const QString& token, const CssLengthContext& context) {
  CssGridBreadth result;
  if (token == "auto") return result;
  if (token == "min-content" || token == "max-content") {
    result.kind = token == "min-content" ? CssGridBreadthKind::MinContent : CssGridBreadthKind::MaxContent;
    return result;
  }
  if (token.endsWith("fr")) {
    bool ok = false;
    result.fraction = token.chopped(2).toDouble(&ok);
    if (!ok || !std::isfinite(result.fraction) || result.fraction < 0) return {};
    result.kind = CssGridBreadthKind::Fraction;
    return result;
  }
  result.length = parseCssLengthPercentage(QStringView(token), context);
  if (result.length.status != CssLengthStatus::Valid ||
      (!token.startsWith("calc(") && (result.length.px < 0 || result.length.fraction < 0)))
    return {};
  result.kind = CssGridBreadthKind::Length;
  return result;
}
}  // namespace
std::optional<CssGridTrackList> parseCssGridTracks(const QString& value, const CssLengthContext& context, bool allowNone) {
  // Repetition bodies use this parser with template grammar, after rejecting
  // nested repeat. Implicit track lists use the restricted breadth grammar.
  const auto text = value.trimmed();
  if (text.compare("none", Qt::CaseInsensitive) == 0) return allowNone ? std::optional{CssGridTrackList{}} : std::nullopt;
  const auto tokens = split(text);
  if (tokens.empty()) return {};
  CssGridTrackList result;
  if (tokens.front().compare("subgrid", Qt::CaseInsensitive) == 0) {
    if (!allowNone) return {};
    result.subgrid = true;
    result.lineNames.clear();
    const auto parseNames = [](const QString& token) -> std::optional<QStringList> {
      if (!token.startsWith('[') || !token.endsWith(']')) return {};
      const auto names = split(token.mid(1, token.size() - 2));
      for (const auto& name : names)
        if (!identifier(name)) return {};
      return names;
    };
    for (int i = 1; i < tokens.size(); ++i) {
      const auto& token = tokens[i];
      if (token.startsWith("repeat(", Qt::CaseInsensitive) && token.endsWith(')')) {
        const auto parts = split(token.mid(7, token.size() - 8), ',');
        if (parts.size() != 2) return {};
        std::vector<QStringList> repeated;
        for (const auto& part : split(parts[1])) {
          const auto names = parseNames(part);
          if (!names) return {};
          repeated.push_back(*names);
        }
        if (repeated.empty()) return {};
        if (parts[0].compare("auto-fill", Qt::CaseInsensitive) == 0) {
          if (result.nameRepeat) return {};
          result.nameRepeat = CssGridNameRepeat{int(result.lineNames.size()), repeated};
        } else {
          bool ok = false;
          const int count = parts[0].toInt(&ok);
          if (!ok || count <= 0 || count > kMaxGridTracks || result.lineNames.size() + count * repeated.size() > kMaxGridTracks + 1)
            return {};
          for (int n = 0; n < count; ++n) result.lineNames.insert(result.lineNames.end(), repeated.begin(), repeated.end());
        }
      } else {
        const auto names = parseNames(token);
        if (!names) return {};
        result.lineNames.push_back(*names);
      }
      if (result.lineNames.size() > kMaxGridTracks + 1) return {};
    }
    return result;
  }
  bool hasTrack = false;
  for (const auto& token : tokens) {
    const auto lower = token.toLower();
    if (token.startsWith('[') && token.endsWith(']')) {
      if (!allowNone) return {};
      const auto names = split(token.mid(1, token.size() - 2));
      for (const auto& name : names)
        if (!identifier(name)) return {};
      result.lineNames.back().append(names);
      result.lineNames.back().removeDuplicates();
      continue;
    }
    if (lower.startsWith("repeat(") && token.endsWith(')')) {
      if (!allowNone) return {};
      const auto parts = split(token.mid(7, token.size() - 8), ',');
      if (parts.size() != 2 || parts[1].contains("repeat(", Qt::CaseInsensitive)) return {};
      const auto repeated = parseCssGridTracks(parts[1], context);
      if (!repeated || repeated->empty()) return {};
      const auto mode = parts[0].toLower();
      if (mode == "auto-fill" || mode == "auto-fit") {
        if (result.automatic) return {};
        result.automatic =
            CssGridAutoRepeat{int(result.size()), mode == "auto-fit", result.lineNames.back(), repeated->tracks, repeated->lineNames};
        result.lineNames.back().clear();
      } else {
        bool ok = false;
        const int count = parts[0].toInt(&ok);
        if (!ok || count <= 0 || count > kMaxGridTracks || repeated->size() * count + result.size() > kMaxGridTracks) return {};
        for (int i = 0; i < count; ++i) appendList(result, *repeated);
      }
    } else {
      CssGridTrack track;
      if (lower.startsWith("minmax(") && token.endsWith(')')) {
        const auto parts = split(lower.mid(7, lower.size() - 8), ',');
        if (parts.size() != 2) return {};
        const auto minimum = breadth(parts[0], context), maximum = breadth(parts[1], context);
        if (!minimum || !maximum || minimum->kind == CssGridBreadthKind::Fraction) return {};
        track = {*minimum, *maximum};
      } else if (lower.startsWith("fit-content(") && token.endsWith(')')) {
        const auto limit = breadth(lower.mid(12, lower.size() - 13), context);
        if (!limit || limit->kind != CssGridBreadthKind::Length) return {};
        track = {CssGridBreadth{}, *limit, true};
      } else {
        const auto size = breadth(lower, context);
        if (!size) return {};
        track = {size->kind == CssGridBreadthKind::Fraction ? CssGridBreadth{} : *size, *size};
      }
      result.tracks.push_back(track);
      result.lineNames.push_back({});
    }
    hasTrack = true;
    if (result.size() + (result.automatic ? result.automatic->tracks.size() : 0) > kMaxGridTracks) return {};
  }
  if (!hasTrack) return {};
  if (result.automatic) {
    const auto fixed = [](const CssGridTrack& track) {
      return !track.fitContent && (track.minimum.kind == CssGridBreadthKind::Length || track.maximum.kind == CssGridBreadthKind::Length);
    };
    if (!std::all_of(result.begin(), result.end(), fixed) ||
        !std::all_of(result.automatic->tracks.begin(), result.automatic->tracks.end(), fixed))
      return {};
  }
  return result;
}
std::optional<CssGridLine> parseCssGridLine(const QString& value) {
  const auto parts = split(value.trimmed());
  if (parts.size() == 1 && parts[0].compare("auto", Qt::CaseInsensitive) == 0) return CssGridLine{};
  if (parts.empty() || parts.size() > 3) return {};
  CssGridLine result;
  bool hasNumber = false;
  for (const auto& part : parts) {
    if (part.compare("span", Qt::CaseInsensitive) == 0 && !result.span)
      result.span = true;
    else {
      bool numeric = false;
      const int number = part.toInt(&numeric);
      if (numeric && !hasNumber) {
        result.number = number;
        hasNumber = true;
      } else if (result.name.isEmpty() && identifier(part))
        result.name = part;
      else
        return {};
    }
  }
  if (!hasNumber) result.number = result.span ? 1 : 0;
  if ((!hasNumber && result.name.isEmpty()) || (hasNumber && result.number == 0) || std::abs(qint64(result.number)) > kMaxGridTracks ||
      (result.span && result.number < 0))
    return {};
  return result;
}
std::optional<CssGridAreas> parseCssGridAreas(const QString& value) {
  const auto text = value.trimmed();
  if (text.compare("none", Qt::CaseInsensitive) == 0) return CssGridAreas{};
  CssGridAreas result;
  QHash<QString, int> counts;
  int cursor = 0;
  while (cursor < text.size()) {
    while (cursor < text.size() && text[cursor].isSpace()) ++cursor;
    if (cursor == text.size()) break;
    const auto quote = text[cursor++];
    if (quote != '\'' && quote != '"') return {};
    const int end = text.indexOf(quote, cursor);
    if (end < 0) return {};
    const auto row = text.mid(cursor, end - cursor);
    QStringList cells;
    for (int i = 0; i < row.size();) {
      if (row[i].isSpace()) {
        ++i;
        continue;
      }
      const int start = i;
      const bool dot = row[i] == '.';
      while (i < row.size() && !row[i].isSpace() && (row[i] == '.') == dot) ++i;
      const auto name = dot ? QStringLiteral(".") : row.mid(start, i - start);
      if (!dot)
        for (const auto c : name)
          if (!c.isLetterOrNumber() && c != '-' && c != '_' && c.unicode() < 128) return {};
      cells.push_back(name);
    }
    if (cells.empty() || cells.size() > kMaxGridTracks || (result.columns && cells.size() != result.columns) ||
        result.rows >= kMaxGridTracks)
      return {};
    result.columns = int(cells.size());
    for (int c = 0; c < cells.size(); ++c) {
      const auto& name = cells[c];
      if (name == ".") continue;
      const QRect cell(c, result.rows, 1, 1);
      result.rectangles[name] = result.rectangles.contains(name) ? result.rectangles[name].united(cell) : cell;
      ++counts[name];
    }
    ++result.rows;
    cursor = end + 1;
  }
  if (!result.rows) return {};
  for (auto it = counts.cbegin(); it != counts.cend(); ++it)
    if (result.rectangles[it.key()].width() * result.rectangles[it.key()].height() != it.value()) return {};
  return result;
}
}  // namespace muffin

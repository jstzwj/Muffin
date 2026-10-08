#include "render/CssFormattingContext.h"
#include "render/LayoutBox.h"
#include "render/CssSizing.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>

namespace muffin {
namespace {
using Kind = CssGridBreadthKind;
struct ExpandedTemplate {
  CssGridTrackList list;
  int automaticStart = 0, automaticCount = 0;
  bool fit = false;
};
ExpandedTemplate expandTemplate(const CssGridTrackList& source, qreal reference, qreal gap, qreal scale,
                                CssAxisConstraints constraints = {}) {
  if (!source.automatic) return {source};
  const auto& repeat = *source.automatic;
  const bool minimumOnly = reference < 0 && constraints.maximum == std::numeric_limits<qreal>::max() && constraints.minimum > 0;
  if (reference < 0 && constraints.maximum < std::numeric_limits<qreal>::max())
    reference = qMax(constraints.minimum, constraints.maximum);
  else if (minimumOnly)
    reference = constraints.minimum;
  const auto fixedSize = [&](const CssGridTrack& track) {
    const auto used = [&](const CssGridBreadth& breadth) {
      return breadth.length.px * scale + breadth.length.fraction * qMax<qreal>(0, reference);
    };
    qreal size = track.maximum.kind == Kind::Length ? used(track.maximum) : used(track.minimum);
    if (track.minimum.kind == Kind::Length) size = qMax(size, used(track.minimum));
    return qMax<qreal>(1, size);  // CSS floors zero-sized automatic tracks for counting
  };
  qreal outside = 0, pattern = 0;
  for (const auto& track : source) outside += fixedSize(track);
  for (const auto& track : repeat.tracks) pattern += fixedSize(track);
  int count = 1;
  if (reference >= 0) {
    const qreal fitting =
        (reference - outside - gap * (qreal(source.size()) - 1) + (minimumOnly ? -.0001 : .0001)) / (pattern + gap * repeat.tracks.size());
    count = int(qBound<qreal>(1, minimumOnly ? std::ceil(fitting) : std::floor(fitting), kMaxGridTracks));
  }
  count = qMin(count, int((kMaxGridTracks - source.size()) / repeat.tracks.size()));
  count = qMax(1, count);
  ExpandedTemplate result;
  result.automaticStart = repeat.index;
  result.automaticCount = count * int(repeat.tracks.size());
  result.fit = repeat.fit;
  const auto append = [&](const std::vector<CssGridTrack>& tracks, const std::vector<QStringList>& names) {
    result.list.lineNames.back().append(names.front());
    result.list.lineNames.back().removeDuplicates();
    for (size_t i = 0; i < tracks.size(); ++i) {
      result.list.tracks.push_back(tracks[i]);
      result.list.lineNames.push_back(names[i + 1]);
    }
  };
  std::vector<CssGridTrack> prefix(source.tracks.begin(), source.tracks.begin() + repeat.index);
  std::vector<QStringList> prefixNames(source.lineNames.begin(), source.lineNames.begin() + repeat.index + 1);
  prefixNames.back() = repeat.beforeNames;
  append(prefix, prefixNames);
  for (int i = 0; i < count; ++i) append(repeat.tracks, repeat.lineNames);
  append({source.tracks.begin() + repeat.index, source.tracks.end()}, {source.lineNames.begin() + repeat.index, source.lineNames.end()});
  return result;
}
void addAreaLines(CssGridTrackList& list, const CssGridTrackList& implicit, const CssGridAreas& areas, bool rows) {
  const int count = rows ? areas.rows : areas.columns;
  const int authored = int(list.size());
  while (!list.subgrid && int(list.size()) < count) {
    list.tracks.push_back(implicit.empty() ? CssGridTrack{} : implicit[(list.size() - authored) % implicit.size()]);
    list.lineNames.push_back({});
  }
  for (auto it = areas.rectangles.cbegin(); it != areas.rectangles.cend(); ++it) {
    const int start = rows ? it->top() : it->left(), span = rows ? it->height() : it->width();
    if (start < int(list.lineNames.size())) list.lineNames[start].push_back(it.key() + "-start");
    if (start + span < int(list.lineNames.size())) list.lineNames[start + span].push_back(it.key() + "-end");
  }
  for (auto& names : list.lineNames) names.removeDuplicates();
}
int namedLine(const CssGridLine& line, const CssGridTrackList& list, bool end) {
  const int explicitTracks = int(list.size());
  if (!line.number) {
    const auto edge = line.name + (end ? "-end" : "-start");
    for (int i = 0; i <= explicitTracks; ++i)
      if (list.lineNames[i].contains(edge)) return i;
  }
  const int direction = line.number < 0 ? -1 : 1;
  int remaining = qMax(1, std::abs(line.number));
  for (int i = direction > 0 ? 0 : explicitTracks; i >= 0 && i <= explicitTracks; i += direction)
    if (list.lineNames[i].contains(line.name) && --remaining == 0) return i;
  return direction > 0 ? explicitTracks + remaining : -remaining;
}
int namedSpan(CssGridLine line, const CssGridTrackList& list, int from, int direction) {
  if (line.name.isEmpty()) return line.number;
  int remaining = qMax(1, line.number), distance = 0;
  while (remaining > 0 && distance < kMaxGridTracks) {
    const int index = from + direction * ++distance;
    if (index < 0 || index > int(list.size()) || list.lineNames[index].contains(line.name)) --remaining;
  }
  return qMax(1, distance);
}
struct AxisPlacement {
  int start = 0, span = 1;
  bool definite = false;
};
struct Placement {
  AxisPlacement column, row;
};
AxisPlacement axisPlacement(CssGridLine start, CssGridLine end, const CssGridTrackList& list) {
  const int explicitTracks = int(list.size());
  const auto index = [explicitTracks](int line) { return line > 0 ? line - 1 : explicitTracks + 1 + line; };
  const auto definite = [](const CssGridLine& line) { return !line.span && (line.number || !line.name.isEmpty()); };
  const auto resolve = [&](const CssGridLine& line, bool endSide) {
    return line.name.isEmpty() ? index(line.number) : namedLine(line, list, endSide);
  };
  AxisPlacement result;
  if (definite(start)) {
    result.start = resolve(start, false);
    result.definite = true;
    if (end.span)
      result.span = namedSpan(end, list, result.start, 1);
    else if (definite(end)) {
      int finish = resolve(end, true);
      if (finish < result.start) std::swap(finish, result.start);
      result.span = qMax(1, finish - result.start);
    }
  } else if (definite(end)) {
    const int finish = resolve(end, true);
    result.span = start.span ? namedSpan(start, list, finish, -1) : 1;
    result.start = finish - result.span;
    result.definite = true;
  } else if (start.span || end.span)
    result.span = start.span ? start.number : end.number;
  result.span = qBound(1, result.span, kMaxGridTracks);
  result.start = qBound(1 - kMaxGridTracks, result.start, kMaxGridTracks - result.span);
  return result;
}
std::vector<Placement> placeItems(const CssLayoutStyle& style, const std::vector<CssFormattingItem>& items, int& columns, int& rows,
                                  int& columnOrigin, int& rowOrigin) {
  std::vector<Placement> positions(items.size());
  std::vector<size_t> order;
  for (size_t i = 0; i < items.size(); ++i) {
    if (items[i].style.layout.display == "none") continue;
    const auto& lines = items[i].style.layout.gridLines;
    positions[i] = {axisPlacement(lines[0], lines[1], style.gridColumns), axisPlacement(lines[2], lines[3], style.gridRows)};
    order.push_back(i);
  }
  std::stable_sort(order.begin(), order.end(),
                   [&](size_t a, size_t b) { return items[a].style.layout.order < items[b].style.layout.order; });
  const bool lockedColumns = style.gridFlowColumn ? style.gridRows.subgrid : style.gridColumns.subgrid;
  const bool lockedRows = style.gridFlowColumn ? style.gridColumns.subgrid : style.gridRows.subgrid;
  if (style.gridFlowColumn) {
    std::swap(columns, rows);
    for (auto& position : positions) std::swap(position.column, position.row);
  }
  const int sharedColumns = qMax(1, columns), sharedRows = qMax(1, rows);
  const auto clamp = [](AxisPlacement& p, int count) {
    const int end = qBound(1, p.start + p.span, count);
    p.start = qBound(0, p.start, end - 1);
    p.span = end - p.start;
  };
  for (auto& p : positions) {
    if (lockedColumns) {
      p.column.span = qMin(p.column.span, sharedColumns);
      if (p.column.definite) clamp(p.column, sharedColumns);
    }
    if (lockedRows) {
      p.row.span = qMin(p.row.span, sharedRows);
      if (p.row.definite) clamp(p.row, sharedRows);
    }
  }
  int minColumn = 0, minRow = 0;
  for (size_t i : order) {
    auto& p = positions[i];
    if (p.column.definite) {
      minColumn = qMin(minColumn, p.column.start);
      columns = qMax(columns, p.column.start + p.column.span);
    } else
      columns = qMax(columns, p.column.span);
    if (p.row.definite) {
      minRow = qMin(minRow, p.row.start);
      rows = qMax(rows, p.row.start + p.row.span);
    }
  }
  columns = qBound(1, columns - minColumn, kMaxGridTracks);
  rows = qBound(1, rows - minRow, kMaxGridTracks);
  if (lockedColumns) {
    columns = sharedColumns;
    minColumn = 0;
  }
  if (lockedRows) {
    rows = sharedRows;
    minRow = 0;
  }
  const int maxColumns = lockedColumns ? columns : kMaxGridTracks;
  const int maxRows = lockedRows ? rows : kMaxGridTracks;
  std::vector<bool> occupied(kMaxGridTracks * kMaxGridTracks);
  const auto free = [&](const Placement& p) {
    if (p.column.start < 0 || p.row.start < 0 || p.column.start + p.column.span > kMaxGridTracks ||
        p.row.start + p.row.span > kMaxGridTracks)
      return false;
    for (int r = p.row.start; r < p.row.start + p.row.span; ++r)
      for (int c = p.column.start; c < p.column.start + p.column.span; ++c)
        if (occupied[r * kMaxGridTracks + c]) return false;
    return true;
  };
  const auto occupy = [&](const Placement& p) {
    for (int r = p.row.start; r < p.row.start + p.row.span; ++r)
      for (int c = p.column.start; c < p.column.start + p.column.span; ++c) occupied[r * kMaxGridTracks + c] = true;
  };
  for (size_t i : order) {
    auto& p = positions[i];
    if (p.column.definite) p.column.start -= minColumn;
    if (p.row.definite) p.row.start -= minRow;
    p.column.start = qMin(p.column.start, kMaxGridTracks - p.column.span);
    p.row.start = qMin(p.row.start, kMaxGridTracks - p.row.span);
    if (p.column.definite && p.row.definite) occupy(p);
  }
  // Definite rows are placed before the remaining auto-placement cursor.
  std::vector<int> rowCursors(kMaxGridTracks);
  for (size_t i : order) {
    auto& p = positions[i];
    if (!p.row.definite || p.column.definite) continue;
    p.column.start = style.gridDense ? 0 : rowCursors[p.row.start];
    p.column.start = qMin(p.column.start, kMaxGridTracks - p.column.span);
    while (!free(p) && p.column.start + p.column.span < maxColumns) ++p.column.start;
    rowCursors[p.row.start] = p.column.start + p.column.span;
    if (lockedColumns) clamp(p.column, columns);
    columns = qMax(columns, p.column.start + p.column.span);
    occupy(p);
  }
  int cursorColumn = 0, cursorRow = 0;
  for (size_t i : order) {
    auto& p = positions[i];
    if (p.row.definite) continue;
    if (style.gridDense) cursorColumn = cursorRow = 0;
    if (p.column.definite) {
      if (p.column.start < cursorColumn) ++cursorRow;
      cursorColumn = p.column.start;
      p.row.start = cursorRow;
      while (!free(p) && p.row.start + p.row.span < maxRows) ++p.row.start;
      cursorRow = p.row.start;
    } else {
      p.column.start = cursorColumn;
      p.row.start = cursorRow;
      for (int attempt = 0; attempt < kMaxGridTracks * kMaxGridTracks; ++attempt) {
        if (p.column.start + p.column.span > columns) {
          p.column.start = 0;
          ++p.row.start;
        }
        if (p.row.start + p.row.span > maxRows) {
          p.row.start = lockedRows ? qMin(p.row.start, maxRows - 1) : maxRows - p.row.span;
          break;
        }
        if (free(p)) break;
        ++p.column.start;
      }
      cursorColumn = p.column.start;
      cursorRow = p.row.start;
    }
    if (lockedRows) clamp(p.row, rows);
    rows = qMax(rows, p.row.start + p.row.span);
    occupy(p);
  }
  columnOrigin = -minColumn;
  rowOrigin = -minRow;
  if (style.gridFlowColumn) {
    std::swap(columns, rows);
    std::swap(columnOrigin, rowOrigin);
    for (auto& position : positions) std::swap(position.column, position.row);
  }
  if (order.empty()) {
    columns = int(style.gridColumns.size());
    rows = int(style.gridRows.size());
  }
  return positions;
}
struct Track {
  CssGridTrack definition;
  qreal base = 0, limit = 0, fraction = 0;
  qreal fitLimit = std::numeric_limits<qreal>::max();
  bool growMinimum = false, autoMaximum = false, flexible = false, collapsed = false;
};
std::vector<Track> createTracks(const std::vector<CssGridTrack>& explicitTracks, const std::vector<CssGridTrack>& implicitTracks, int count,
                                int origin, qreal reference, qreal scale) {
  std::vector<Track> tracks(count);
  for (int i = 0; i < count; ++i) {
    auto& track = tracks[i];
    const int index = i - origin;
    if (index >= 0 && index < int(explicitTracks.size()))
      track.definition = explicitTracks[index];
    else if (!implicitTracks.empty()) {
      const int implicitIndex = index < 0 ? index : index - int(explicitTracks.size());
      const int size = int(implicitTracks.size());
      track.definition = implicitTracks[(implicitIndex % size + size) % size];
    }
    auto minimum = track.definition.minimum, maximum = track.definition.maximum;
    if (reference < 0 && minimum.length.hasPercentage) minimum.kind = Kind::Auto;
    if (reference < 0 && maximum.length.hasPercentage) maximum.kind = Kind::Auto;
    track.definition.minimum = minimum;
    track.definition.maximum = maximum;
    track.base = minimum.kind == Kind::Length ? qMax<qreal>(0, minimum.length.px * scale + minimum.length.fraction * reference) : 0;
    track.limit =
        maximum.kind == Kind::Length ? qMax(track.base, maximum.length.px * scale + maximum.length.fraction * reference) : track.base;
    if (track.definition.fitContent) {
      track.fitLimit = maximum.kind == Kind::Length ? qMax<qreal>(0, maximum.length.px * scale + maximum.length.fraction * reference)
                                                    : std::numeric_limits<qreal>::max();
      track.definition.maximum.kind = Kind::MaxContent;
      track.limit = track.base;
    }
    track.fraction = maximum.kind == Kind::Fraction ? maximum.fraction : 0;
    track.flexible = maximum.kind == Kind::Fraction;
    track.growMinimum = minimum.kind != Kind::Length;
    track.autoMaximum = maximum.kind == Kind::Auto;
  }
  return tracks;
}
int activeTrackCount(const std::vector<Track>& tracks) {
  return int(std::count_if(tracks.begin(), tracks.end(), [](const auto& track) { return !track.collapsed; }));
}
void collapseEmptyTracks(std::vector<Track>& tracks, const ExpandedTemplate& expanded, int origin, const std::vector<Placement>& positions,
                         const std::vector<CssFormattingItem>& items, bool rows) {
  if (!expanded.fit) return;
  std::vector<bool> occupied(tracks.size());
  for (size_t i = 0; i < positions.size(); ++i) {
    if (items[i].style.layout.display == "none") continue;
    const auto& p = rows ? positions[i].row : positions[i].column;
    for (int t = p.start; t < p.start + p.span; ++t) occupied[t] = true;
  }
  const int end = qMin(int(tracks.size()), origin + expanded.automaticStart + expanded.automaticCount);
  for (int i = origin + expanded.automaticStart; i < end; ++i)
    if (!occupied[i]) {
      tracks[i] = {};
      tracks[i].collapsed = true;
    }
}
void contribute(std::vector<Track>& tracks, AxisPlacement placement, qreal minimum, qreal maximum, qreal autoMinimum, qreal gap) {
  const auto distribute = [&](bool growthLimit) {
    qreal existing = gap * (placement.span - 1);
    std::vector<int> candidates;
    for (int i = placement.start; i < placement.start + placement.span; ++i) {
      const auto& track = tracks[i];
      existing += growthLimit ? track.limit : track.base;
      if (!track.collapsed && (growthLimit ? (!track.flexible && track.definition.maximum.kind != Kind::Length) : track.growMinimum))
        candidates.push_back(i);
    }
    bool minContentMaximum = true;
    for (int i = placement.start; i < placement.start + placement.span; ++i)
      minContentMaximum = minContentMaximum && tracks[i].definition.maximum.kind == Kind::MinContent;
    bool automaticMinimum = true, maxContentMinimum = false;
    for (int i = placement.start; i < placement.start + placement.span; ++i) {
      automaticMinimum = automaticMinimum && (tracks[i].definition.minimum.kind == Kind::Auto || !tracks[i].growMinimum);
      maxContentMinimum = maxContentMinimum || tracks[i].definition.minimum.kind == Kind::MaxContent;
    }
    const qreal wanted = growthLimit         ? (minContentMaximum ? minimum : maximum)
                         : maxContentMinimum ? maximum
                         : automaticMinimum  ? autoMinimum
                                             : minimum;
    qreal extra = qMax<qreal>(0, wanted - existing);
    while (extra > .0001 && !candidates.empty()) {
      const qreal share = extra / candidates.size();
      qreal spent = 0;
      for (int i : candidates) {
        auto& track = tracks[i];
        qreal& value = growthLimit ? track.limit : track.base;
        qreal increment = share;
        if (growthLimit) increment = qMin(increment, qMax<qreal>(0, qMax(track.base, track.fitLimit) - value));
        if (!growthLimit && track.definition.maximum.kind == Kind::Length && track.definition.minimum.kind == Kind::Auto)
          increment = qMin(share, qMax<qreal>(0, track.limit - value));
        value += increment;
        track.limit = qMax(track.limit, track.base);
        spent += increment;
      }
      extra -= spent;
      candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                      [&](int i) {
                                        return growthLimit ? tracks[i].limit >= qMax(tracks[i].base, tracks[i].fitLimit)
                                                           : tracks[i].definition.maximum.kind == Kind::Length &&
                                                                 tracks[i].definition.minimum.kind == Kind::Auto &&
                                                                 tracks[i].base >= tracks[i].limit;
                                      }),
                       candidates.end());
      if (spent <= .0001) break;
    }
  };
  distribute(false);
  distribute(true);
}
void sizeTracks(std::vector<Track>& tracks, qreal available, qreal gap, const QString& alignment) {
  const int active = activeTrackCount(tracks);
  qreal total = gap * qMax(0, active - 1);
  for (const auto& track : tracks) total += track.base;
  qreal free = available < 0 ? 0 : qMax<qreal>(0, available - total);
  std::vector<size_t> growing;
  for (size_t i = 0; i < tracks.size(); ++i)
    if (!tracks[i].flexible && tracks[i].limit > tracks[i].base) growing.push_back(i);
  if (available < 0) {
    for (auto& track : tracks)
      if (!track.flexible) track.base = track.limit;
  }
  while (free > .0001 && !growing.empty()) {
    const qreal share = free / growing.size();
    for (size_t i : growing) {
      const auto extra = qMin(share, tracks[i].limit - tracks[i].base);
      tracks[i].base += extra;
      free -= extra;
    }
    growing.erase(std::remove_if(growing.begin(), growing.end(), [&](size_t i) { return tracks[i].base + .0001 >= tracks[i].limit; }),
                  growing.end());
  }
  qreal unit = 0;
  if (available < 0) {
    for (const auto& track : tracks)
      if (track.fraction > 0) unit = qMax(unit, track.base / qMax<qreal>(1, track.fraction));
  } else {
    std::vector<bool> frozen(tracks.size());
    for (size_t pass = 0; pass <= tracks.size(); ++pass) {
      qreal space = available - gap * qMax(0, active - 1), sum = 0;
      for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].flexible && !frozen[i])
          sum += tracks[i].fraction;
        else
          space -= tracks[i].base;
      }
      unit = qMax<qreal>(0, space) / qMax<qreal>(1, sum);
      bool changed = false;
      for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].flexible && !frozen[i] && unit * tracks[i].fraction < tracks[i].base) {
          frozen[i] = true;
          changed = true;
        }
      if (!changed) break;
    }
  }
  for (auto& track : tracks)
    if (track.flexible) track.base = qMax(track.base, unit * track.fraction);
  if (available >= 0 && (alignment == "normal" || alignment == "stretch")) {
    qreal used = gap * qMax(0, active - 1);
    int autos = 0;
    for (const auto& track : tracks) {
      used += track.base;
      if (track.autoMaximum) ++autos;
    }
    if (autos)
      for (auto& track : tracks)
        if (track.autoMaximum) track.base += qMax<qreal>(0, available - used) / autos;
  }
}
std::vector<qreal> trackPositions(const std::vector<Track>& tracks, qreal gap, qreal available, const QString& alignment) {
  const int active = activeTrackCount(tracks);
  qreal used = gap * qMax(0, active - 1);
  for (const auto& track : tracks) used += track.base;
  const qreal free = available < 0 ? 0 : qMax<qreal>(0, available - used);
  qreal offset = 0, spacing = gap;
  if (alignment == "end" || alignment == "flex-end")
    offset = free;
  else if (alignment == "center")
    offset = free / 2;
  else if (alignment == "space-between" && active > 1)
    spacing += free / (active - 1);
  else if (alignment == "space-around" && active > 0) {
    spacing += free / active;
    offset = free / active / 2;
  } else if (alignment == "space-evenly") {
    spacing += free / (active + 1);
    offset = free / (active + 1);
  }
  std::vector<qreal> result;
  int remaining = active;
  for (const auto& track : tracks) {
    result.push_back(offset);
    if (!track.collapsed) {
      offset += track.base;
      if (--remaining > 0) offset += spacing;
    }
  }
  return result;
}
bool hasGridChildren(const CssFormattingItem& item) { return item.style.layout.isGrid() && item.children.has_value(); }
struct ItemBox {
  QMarginsF padding, margin;
  qreal horizontal = 0, vertical = 0;
};
ItemBox itemBox(const CssFormattingItem& item, qreal width, qreal scale) {
  const auto& box = item.style.box;
  ItemBox result;
  result.padding = box.paddingLengths.used(box.padding, width / scale, true) * scale;
  result.margin = box.marginLengths.used(box.margin, width / scale) * scale;
  result.horizontal = result.padding.left() + result.padding.right() + (box.borderLeftWidth + box.borderRightWidth) * scale;
  result.vertical = result.padding.top() + result.padding.bottom() + (box.borderTopWidth + box.borderBottomWidth) * scale;
  return result;
}
qreal usedSize(const CssFormattingItem& item, int axis, qreal reference, qreal intrinsic, qreal minimum, qreal maximum, qreal extra,
               bool stretch, qreal margins, qreal scale) {
  const auto& box = item.style.box;
  const CssLengthPercentage lengths[] = {box.widthLength,    box.heightLength,    box.minWidthLength,
                                         box.maxWidthLength, box.minHeightLength, box.maxHeightLength};
  const auto length = [&](int index) {
    return qMax<qreal>(0, lengths[index].px * scale + lengths[index].fraction * reference) + (box.borderBox ? 0 : extra);
  };
  qreal result = stretch ? qMax<qreal>(0, reference - margins) : qMax(minimum, qMin(maximum, reference - margins - extra)) + extra;
  const auto kind = item.style.layout.sizes[axis];
  if (kind == CssIntrinsicSize::Length)
    result = length(axis);
  else if (kind == CssIntrinsicSize::MinContent)
    result = minimum + extra;
  else if (kind == CssIntrinsicSize::MaxContent)
    result = maximum + extra;
  else if (axis == 1)
    result = stretch && kind == CssIntrinsicSize::Auto ? result : intrinsic + extra;
  const int minIndex = axis == 0 ? 2 : 4, maxIndex = axis == 0 ? 3 : 5;
  const auto constraint = [&](int index, qreal fallback) {
    switch (item.style.layout.sizes[index]) {
      case CssIntrinsicSize::Length:
        return length(index);
      case CssIntrinsicSize::MinContent:
        return minimum + extra;
      case CssIntrinsicSize::MaxContent:
        return maximum + extra;
      case CssIntrinsicSize::FitContent:
        return qMax(minimum, qMin(maximum, reference - margins - extra)) + extra;
      default:
        return fallback;
    }
  };
  result = qMin(result, constraint(maxIndex, std::numeric_limits<qreal>::max()));
  result = qMax(result, constraint(minIndex, 0));
  return qMax(extra, result);
}
qreal itemOffset(qreal area, qreal size, qreal before, qreal after, bool autoBefore, bool autoAfter, const QString& alignment) {
  const qreal free = area - size - before - after;
  if (autoBefore || autoAfter) return before + (autoBefore ? qMax<qreal>(0, free) / (autoAfter ? 2 : 1) : 0);
  return before + (alignment == "center" ? free / 2 : (alignment == "end" || alignment == "flex-end") ? free : 0);
}
// Used tracks are inherited only by a grid item on a subgridded axis. They
// never enter computed style or its cache: the same style can occupy any area.
CssGridTrackList sharedTemplate(const CssGridTrackList& authored, const CssGridAxisGeometry& axis) {
  CssGridTrackList result;
  result.subgrid = true;
  result.tracks = axis.definitions;
  result.tracks.resize(axis.sizes.size());
  result.lineNames = axis.lineNames;
  result.lineNames.resize(axis.sizes.size() + 1);
  auto names = authored.lineNames;
  if (authored.nameRepeat) {
    const auto& repeat = *authored.nameRepeat;
    const int count = qMax(0, int(result.lineNames.size()) - int(names.size())) / int(repeat.names.size());
    for (int i = 0; i < count; ++i)
      names.insert(names.begin() + repeat.index + i * repeat.names.size(), repeat.names.begin(), repeat.names.end());
  }
  for (size_t i = 0; i < names.size() && i < result.lineNames.size(); ++i) {
    result.lineNames[i].append(names[i]);
    result.lineNames[i].removeDuplicates();
  }
  return result;
}
CssGridAxisGeometry sliceAxis(const CssGridTrackList& list, const std::vector<Track>& tracks, const std::vector<qreal>& starts,
                              AxisPlacement p, int origin, qreal gap, qreal offset = 0, qreal after = 0) {
  CssGridAxisGeometry axis;
  axis.gap = gap;
  for (int i = p.start; i < p.start + p.span; ++i) {
    axis.starts.push_back((starts.empty() ? 0 : starts[i] - starts[p.start]) - offset);
    axis.sizes.push_back(tracks.empty() ? 0 : tracks[i].base);
    if (i == p.start) {
      axis.starts.back() += offset;
      axis.sizes.back() -= offset;
    }
    if (i + 1 == p.start + p.span) axis.sizes.back() -= after;
    axis.sizes.back() = qMax<qreal>(0, axis.sizes.back());
    const int index = i - origin;
    axis.definitions.push_back(index >= 0 && index < int(list.size()) ? list[index] : CssGridTrack{});
  }
  for (int i = p.start; i <= p.start + p.span; ++i) {
    const int index = i - origin;
    axis.lineNames.push_back(index >= 0 && index < int(list.lineNames.size()) ? list.lineNames[index] : QStringList{});
  }
  return axis;
}
CssGridAxisGeometry sharedGap(CssGridAxisGeometry axis, qreal gap) {
  const qreal half = (gap - axis.gap) / 2;
  for (size_t i = 0; i < axis.sizes.size(); ++i) {
    if (i > 0) {
      axis.starts[i] += half;
      axis.sizes[i] -= half;
    }
    if (i + 1 < axis.sizes.size()) axis.sizes[i] -= half;
    axis.sizes[i] = qMax<qreal>(0, axis.sizes[i]);
  }
  axis.gap = gap;
  return axis;
}
void useSharedTracks(std::vector<Track>& tracks, const CssGridAxisGeometry& axis) {
  for (size_t i = 0; i < tracks.size(); ++i) {
    tracks[i].base = tracks[i].limit = axis.sizes[i];
    tracks[i].growMinimum = tracks[i].autoMaximum = tracks[i].flexible = false;
  }
}
qreal tracksExtent(const std::vector<Track>& tracks, qreal gap) {
  qreal size = gap * qMax(0, activeTrackCount(tracks) - 1);
  for (const auto& track : tracks) size += track.base;
  return size;
}
void applyContributions(std::vector<Track>& tracks, std::vector<CssGridContribution>& contributions, qreal gap) {
  std::stable_sort(contributions.begin(), contributions.end(), [](const auto& a, const auto& b) { return a.span < b.span; });
  for (const auto& c : contributions) contribute(tracks, {c.start, c.span, true}, c.minimum, c.maximum, c.automaticMinimum, gap);
}
void addSubgridContributions(std::vector<CssGridContribution>& contributions, const std::vector<CssGridContribution>& descendants,
                             AxisPlacement p, qreal before, qreal after, qreal parentGap, qreal childGap) {
  const qreal half = (childGap - parentGap) / 2;
  // The subgrid's own edge decoration contributes even when an edge is empty.
  contributions.push_back({p.start, 1, before, before, before});
  contributions.push_back({p.start + p.span - 1, 1, after, after, after});
  for (auto c : descendants) {
    const qreal extra = (c.start == 0 ? before : half) + (c.start + c.span == p.span ? after : half);
    c.start += p.start;
    c.minimum = qMax<qreal>(0, c.minimum + extra);
    c.maximum = qMax<qreal>(0, c.maximum + extra);
    c.automaticMinimum = qMax<qreal>(0, c.automaticMinimum + extra);
    contributions.push_back(c);
  }
}
void indefiniteFractions(std::vector<Track>& tracks, const std::vector<CssGridContribution>& contributions, qreal gap) {
  qreal unit = 0;
  for (const auto& t : tracks)
    if (t.flexible) unit = qMax(unit, t.base / qMax<qreal>(1, t.fraction));
  for (const auto& c : contributions) {
    qreal wanted = c.maximum - gap * (c.span - 1), fractions = 0;
    for (int i = c.start; i < c.start + c.span; ++i) {
      if (tracks[i].flexible)
        fractions += tracks[i].fraction;
      else
        wanted -= tracks[i].base;
    }
    if (fractions > 0) unit = qMax(unit, wanted / qMax<qreal>(1, fractions));
  }
  for (auto& t : tracks)
    if (t.flexible) t.base = qMax(t.base, unit * t.fraction);
}
}  // namespace
CssGridInheritance contentGridInheritance(CssGridInheritance inherited, QMarginsF insets) {
  const auto inset = [](std::optional<CssGridAxisGeometry>& axis, qreal before, qreal after) {
    if (!axis || axis->sizes.empty()) return;
    axis->sizes.front() = qMax<qreal>(0, axis->sizes.front() - before);
    axis->sizes.back() = qMax<qreal>(0, axis->sizes.back() - after);
    for (auto& start : axis->starts) start -= before;
    axis->starts.front() += before;
  };
  inset(inherited.columns, insets.left(), insets.right());
  inset(inherited.rows, insets.top(), insets.bottom());
  return inherited;
}
namespace {
struct GridSizingState {
  CssLayoutPhase phase = CssLayoutPhase::InlineAllocation;
  std::vector<qreal> transferredHeights;
  // Directed edges from definite row allocation to each ratio-dependent
  // column contribution. Intrinsic cyclic references never create an edge.
  std::vector<size_t> blockToInlineDependencies;
};
CssFormattingResult solveGrid(const ThemeElementStyle& container, const std::vector<CssFormattingItem>& items, qreal contentWidth,
                              qreal contentHeight, qreal scale, const CssGridInheritance& inherited, bool intrinsicOnly);
CssFormattingResult layoutGridItemsImpl(const ThemeElementStyle& container, const std::vector<CssFormattingItem>& items, qreal contentWidth,
                                        qreal contentHeight, qreal scale, const CssGridInheritance& inherited, GridSizingState& state) {
  const bool intrinsicOnly = state.phase == CssLayoutPhase::Intrinsic;
  const auto& transferredHeights = state.transferredHeights;
  auto style = container.layout;
  const auto heightConstraints = cssContentConstraints(container.box, 1, -1, scale);
  if (contentHeight < 0 && container.box.heightLength.status == CssLengthStatus::Valid && !container.box.heightLength.hasPercentage) {
    const auto inset = LayoutBox::insets(container.box);
    contentHeight =
        qMax<qreal>(0, container.box.heightLength.px * scale - (container.box.borderBox ? (inset.top() + inset.bottom()) * scale : 0));
  }
  if (contentHeight >= 0) contentHeight = heightConstraints.clamp(contentHeight);
  CssFormattingResult result;
  auto sharedColumns = style.gridColumns.subgrid ? inherited.columns : std::nullopt;
  auto sharedRows = style.gridRows.subgrid ? inherited.rows : std::nullopt;
  if (style.gridColumns.subgrid && !sharedColumns) style.gridColumns = {};
  if (style.gridRows.subgrid && !sharedRows) style.gridRows = {};
  const auto gap = [scale](const CssLengthPercentage& length, qreal reference) {
    return cssGap(length, reference, scale);
  };
  const qreal columnGap =
      sharedColumns && style.columnGap.status != CssLengthStatus::Valid ? sharedColumns->gap : gap(style.columnGap, contentWidth);
  qreal rowGap = sharedRows && style.rowGap.status != CssLengthStatus::Valid ? sharedRows->gap : gap(style.rowGap, contentHeight);
  if (sharedColumns) {
    style.gridColumns = sharedTemplate(style.gridColumns, *sharedColumns);
    sharedColumns = sharedGap(*sharedColumns, columnGap);
  }
  if (sharedRows) {
    style.gridRows = sharedTemplate(style.gridRows, *sharedRows);
    sharedRows = sharedGap(*sharedRows, rowGap);
  }
  const auto widthConstraints = cssContentConstraints(container.box, 0, -1, scale);
  const bool intrinsicWidth = style.sizes[0] == CssIntrinsicSize::MinContent || style.sizes[0] == CssIntrinsicSize::MaxContent ||
                              style.sizes[0] == CssIntrinsicSize::FitContent;
  const auto expandedColumns = expandTemplate(style.gridColumns, intrinsicWidth ? -1 : contentWidth, columnGap, scale, widthConstraints);
  const auto expandedRows = expandTemplate(style.gridRows, contentHeight, rowGap, scale, heightConstraints);
  style.gridColumns = expandedColumns.list;
  style.gridRows = expandedRows.list;
  addAreaLines(style.gridColumns, style.gridAutoColumns, style.gridAreas, false);
  addAreaLines(style.gridRows, style.gridAutoRows, style.gridAreas, true);
  int columnCount = int(style.gridColumns.size()), rowCount = int(style.gridRows.size()), columnOrigin = 0, rowOrigin = 0;
  const auto positions = placeItems(style, items, columnCount, rowCount, columnOrigin, rowOrigin);
  auto initialRows = createTracks(style.gridRows.tracks, style.gridAutoRows.tracks, rowCount, rowOrigin, contentHeight, scale);
  if (sharedRows) useSharedTracks(initialRows, *sharedRows);
  const auto initialRowStarts = sharedRows ? sharedRows->starts : trackPositions(initialRows, rowGap, -1, style.alignContent);
  const auto definiteRowSize = [&](AxisPlacement p) -> qreal {
    if (!sharedRows)
      for (int r = p.start; r < p.start + p.span; ++r)
        if (initialRows[r].definition.minimum.kind != Kind::Length || initialRows[r].definition.maximum.kind != Kind::Length ||
            (contentHeight < 0 &&
             (initialRows[r].definition.minimum.length.hasPercentage || initialRows[r].definition.maximum.length.hasPercentage)))
          return -1;
    return initialRowStarts[p.start + p.span - 1] + initialRows[p.start + p.span - 1].base - initialRowStarts[p.start];
  };
  auto columns = createTracks(style.gridColumns.tracks, style.gridAutoColumns.tracks, columnCount, columnOrigin, contentWidth, scale);
  collapseEmptyTracks(columns, expandedColumns, columnOrigin, positions, items, false);
  std::vector<size_t> order(items.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return positions[a].column.span < positions[b].column.span; });
  for (size_t i : order) {
    if (items[i].style.layout.display == "none") continue;
    const auto& item = items[i];
    const auto& p = positions[i].column;
    const auto box = itemBox(item, 0, scale);  // cyclic percentages are zero during intrinsic contributions
    if (hasGridChildren(item) && item.style.layout.gridColumns.subgrid) {
      CssGridInheritance child;
      child.columns = sliceAxis(style.gridColumns, {}, {}, p, columnOrigin, columnGap);
      const auto nested = solveGrid(item.style, *item.children, -1, -1, scale, child, true);
      const qreal childGap =
          item.style.layout.columnGap.status == CssLengthStatus::Valid ? gap(item.style.layout.columnGap, -1) : columnGap;
      addSubgridContributions(result.columnContributions, nested.columnContributions, p,
                              box.padding.left() + item.style.box.borderLeftWidth * scale + box.margin.left(),
                              box.padding.right() + item.style.box.borderRightWidth * scale + box.margin.right(), columnGap, childGap);
      continue;
    }
    qreal minimum = item.intrinsic.minContent, maximum = item.intrinsic.maxContent;
    const qreal rowReference = definiteRowSize(positions[i].row);
    if (hasGridChildren(item) && rowReference >= 0) {
      CssGridInheritance child;
      if (item.style.layout.gridRows.subgrid)
        child.rows = sliceAxis(style.gridRows, initialRows, initialRowStarts, positions[i].row, rowOrigin, rowGap);
      const qreal nestedHeight = item.style.layout.gridRows.subgrid ? rowReference - box.vertical
                                 : item.style.box.heightLength.status == CssLengthStatus::Valid
                                     ? cssReplacedSize(item, -1, rowReference).height() - box.vertical
                                     : -1;
      const auto nested = solveGrid(item.style, *item.children, -1, nestedHeight, scale, contentGridInheritance(child, box.padding), true);
      minimum = nested.intrinsic.minContent;
      maximum = nested.intrinsic.maxContent;
    }
    const qreal transferredHeight = transferredHeights.empty() ? -1 : transferredHeights[i];
    if (item.naturalSize || transferredHeight >= 0 ||
        (cssPreferredRatio(item) > 0 && item.style.box.heightLength.status == CssLengthStatus::Valid &&
         (!item.style.box.heightLength.hasPercentage || rowReference >= 0))) {
      minimum = maximum = qMax<qreal>(0, cssReplacedSize(item, -1, rowReference, -1, transferredHeight).width() - box.horizontal);
    }
    if (item.style.layout.sizes[0] == CssIntrinsicSize::Length && !item.style.box.widthLength.hasPercentage)
      minimum = maximum = qMax<qreal>(0, item.style.box.widthLength.px * scale - (item.style.box.borderBox ? box.horizontal : 0));
    qreal autoMinimum = minimum;
    bool spansFlex = false;
    for (int c = p.start; c < p.start + p.span; ++c) spansFlex = spansFlex || columns[c].flexible;
    if ((p.span > 1 && spansFlex) || (item.style.layout.overflowX != "visible" && item.style.layout.overflowX != "clip") ||
        item.style.layout.sizes[2] == CssIntrinsicSize::Length)
      autoMinimum = 0;
    if (item.style.layout.sizes[2] == CssIntrinsicSize::Length && !item.style.box.minWidthLength.hasPercentage) {
      autoMinimum = qMax<qreal>(0, item.style.box.minWidthLength.px * scale - (item.style.box.borderBox ? box.horizontal : 0));
      minimum = qMax(minimum, autoMinimum);
    }
    if (item.style.layout.sizes[3] == CssIntrinsicSize::Length && !item.style.box.maxWidthLength.hasPercentage) {
      const auto cap = qMax<qreal>(0, item.style.box.maxWidthLength.px * scale - (item.style.box.borderBox ? box.horizontal : 0));
      minimum = qMin(minimum, cap);
      maximum = qMin(maximum, cap);
      autoMinimum = qMin(autoMinimum, cap);
    }
    const qreal extra = box.horizontal + box.margin.left() + box.margin.right();
    result.columnContributions.push_back({p.start, p.span, minimum + extra, maximum + extra, autoMinimum + extra});
  }
  applyContributions(columns, result.columnContributions, columnGap);
  result.intrinsic.minContent = widthConstraints.clamp(tracksExtent(columns, columnGap));
  sizeTracks(columns, contentWidth, columnGap, style.justifyContent);
  if (contentWidth < 0) indefiniteFractions(columns, result.columnContributions, columnGap);
  result.intrinsic.maxContent = widthConstraints.clamp(tracksExtent(columns, columnGap));
  if (intrinsicOnly) return result;
  if (sharedColumns) useSharedTracks(columns, *sharedColumns);
  const auto columnStarts = sharedColumns ? sharedColumns->starts : trackPositions(columns, columnGap, contentWidth, style.justifyContent);
  std::vector<qreal> widths(items.size()), heights(items.size()), firstBaselines(items.size()), lastBaselines(items.size());
  std::vector<int> rowContributions(items.size(), -1);
  std::vector<qreal> firstAscents(items.size()), lastDescentsByItem(items.size());
  std::vector<qreal> baselineAscents(rowCount), lastDescents(rowCount);
  std::vector<ItemBox> boxes(items.size());
  auto rows = createTracks(style.gridRows.tracks, style.gridAutoRows.tracks, rowCount, rowOrigin, contentHeight, scale);
  collapseEmptyTracks(rows, expandedRows, rowOrigin, positions, items, true);
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return positions[a].row.span < positions[b].row.span; });
  const auto areaSize = [](const std::vector<Track>& tracks, const std::vector<qreal>& starts, AxisPlacement p) {
    return starts[p.start + p.span - 1] + tracks[p.start + p.span - 1].base - starts[p.start];
  };
  for (size_t i : order) {
    const auto& item = items[i];
    if (item.style.layout.display == "none") continue;
    const auto& p = positions[i];
    const qreal rowReference = definiteRowSize(p.row);
    const qreal areaWidth = areaSize(columns, columnStarts, p.column);
    const auto box = boxes[i] = itemBox(item, areaWidth, scale);
    const auto justify = hasGridChildren(item) && item.style.layout.gridColumns.subgrid ? QStringLiteral("stretch")
                         : item.style.layout.justifySelf == "auto"                      ? style.justifyItems
                                                                                        : item.style.layout.justifySelf;
    const bool stretch = justify == "normal" || justify == "stretch";
    widths[i] = usedSize(item, 0, areaWidth, item.intrinsic.maxContent, item.intrinsic.minContent, item.intrinsic.maxContent,
                         box.horizontal, stretch && !item.style.layout.autoMargins[1] && !item.style.layout.autoMargins[3],
                         box.margin.left() + box.margin.right(), scale);
    if (hasGridChildren(item) && item.style.layout.gridColumns.subgrid)
      widths[i] = qMax(box.horizontal, areaWidth - box.margin.left() - box.margin.right());
    const qreal transferredHeight = transferredHeights.empty() ? -1 : transferredHeights[i];
    if (item.naturalSize || transferredHeight >= 0 ||
        (cssPreferredRatio(item) > 0 && item.style.box.heightLength.status == CssLengthStatus::Valid &&
         (!item.style.box.heightLength.hasPercentage || rowReference >= 0))) {
      widths[i] = cssReplacedSize(item, areaWidth, rowReference, -1, transferredHeight).width();
      if (justify == "stretch" && item.style.layout.sizes[0] == CssIntrinsicSize::Auto)
        widths[i] = usedSize(item, 0, areaWidth, item.intrinsic.maxContent, item.intrinsic.minContent, item.intrinsic.maxContent,
                             box.horizontal, true, box.margin.left() + box.margin.right(), scale);
    }
    CssGridInheritance child;
    if (hasGridChildren(item) && item.style.layout.gridColumns.subgrid)
      child.columns =
          sliceAxis(style.gridColumns, columns, columnStarts, p.column, columnOrigin, columnGap, box.margin.left(), box.margin.right());
    if (hasGridChildren(item)) {
      if (item.style.layout.gridRows.subgrid) child.rows = sliceAxis(style.gridRows, {}, {}, p.row, rowOrigin, rowGap);
      const auto insets = box.padding + QMarginsF(item.style.box.borderLeftWidth * scale, item.style.box.borderTopWidth * scale,
                                                  item.style.box.borderRightWidth * scale, item.style.box.borderBottomWidth * scale);
      const auto& heightLength = item.style.box.heightLength;
      const qreal nestedHeight = heightLength.status == CssLengthStatus::Valid && !heightLength.hasPercentage && !child.rows
                                     ? qMax<qreal>(0, heightLength.px * scale - (item.style.box.borderBox ? box.vertical : 0))
                                     : -1;
      const auto nested = solveGrid(item.style, *item.children, qMax<qreal>(0, widths[i] - box.horizontal), nestedHeight, scale,
                                    contentGridInheritance(child, insets), false);
      heights[i] = nested.size.height();
      firstBaselines[i] = nested.firstBaseline >= 0 ? nested.firstBaseline : heights[i];
      lastBaselines[i] = nested.lastBaseline >= 0 ? nested.lastBaseline : heights[i];
      if (item.style.layout.gridRows.subgrid) {
        const qreal childGap = item.style.layout.rowGap.status == CssLengthStatus::Valid ? gap(item.style.layout.rowGap, -1) : rowGap;
        addSubgridContributions(result.rowContributions, nested.rowContributions, p.row, insets.top() + box.margin.top(),
                                insets.bottom() + box.margin.bottom(), rowGap, childGap);
        continue;
      }
    } else {
      const auto measured =
          measureCssItem(item, {qMax<qreal>(0, widths[i] - box.horizontal), areaWidth, rowReference, -1, child, state.phase});
      heights[i] = measured.size.height();
      firstBaselines[i] = item.naturalSize ? heights[i] : measured.baseline;
      lastBaselines[i] = item.naturalSize ? heights[i] : measured.lastBaseline < 0 ? measured.baseline : measured.lastBaseline;
    }
    if (item.naturalSize || cssPreferredRatio(item) > 0) {
      const auto preferred = cssReplacedSize(item, areaWidth, contentHeight, widths[i]);
      heights[i] =
          item.naturalSize ? qMax<qreal>(0, preferred.height() - box.vertical) : qMax(heights[i], preferred.height() - box.vertical);
    }
    qreal height = heights[i] + box.vertical;
    if (item.style.box.heightLength.status == CssLengthStatus::Valid && !item.style.box.heightLength.hasPercentage)
      height = item.style.box.heightLength.px * scale + (item.style.box.borderBox ? 0 : box.vertical);
    if (item.style.box.minHeightLength.status == CssLengthStatus::Valid && !item.style.box.minHeightLength.hasPercentage)
      height = qMax(height, item.style.box.minHeightLength.px * scale + (item.style.box.borderBox ? 0 : box.vertical));
    if (item.style.box.maxHeightLength.status == CssLengthStatus::Valid && !item.style.box.maxHeightLength.hasPercentage)
      height = qMin(height, item.style.box.maxHeightLength.px * scale + (item.style.box.borderBox ? 0 : box.vertical));
    if (firstBaselines[i] < 0) firstBaselines[i] = qMax<qreal>(0, height - box.vertical);
    if (lastBaselines[i] < 0) lastBaselines[i] = firstBaselines[i];
    qreal autoMinimum = height;
    bool spansFlex = false;
    for (int r = p.row.start; r < p.row.start + p.row.span; ++r) spansFlex = spansFlex || rows[r].flexible;
    if ((p.row.span > 1 && spansFlex) || (item.style.layout.overflowY != "visible" && item.style.layout.overflowY != "clip"))
      autoMinimum = box.vertical;
    if (item.style.box.minHeightLength.status == CssLengthStatus::Valid && !item.style.box.minHeightLength.hasPercentage)
      autoMinimum = qMax(box.vertical, item.style.box.minHeightLength.px * scale + (item.style.box.borderBox ? 0 : box.vertical));
    height += box.margin.top() + box.margin.bottom();
    autoMinimum += box.margin.top() + box.margin.bottom();
    rowContributions[i] = int(result.rowContributions.size());
    result.rowContributions.push_back({p.row.start, p.row.span, height, height, autoMinimum});
    const auto align = item.style.layout.alignSelf == "auto" ? style.alignItems : item.style.layout.alignSelf;
    if (!item.style.layout.autoMargins[0] && !item.style.layout.autoMargins[2]) {
      const qreal top = box.padding.top() + item.style.box.borderTopWidth * scale;
      if (align == "baseline" || align == "first baseline") {
        const auto ascent = top + firstBaselines[i] + box.margin.top();
        firstAscents[i] = ascent;
        baselineAscents[p.row.start] = qMax(baselineAscents[p.row.start], ascent);
      } else if (align == "last baseline") {
        const auto ascent = top + lastBaselines[i] + box.margin.top();
        lastDescentsByItem[i] = height - ascent;
        const int end = p.row.start + p.row.span - 1;
        lastDescents[end] = qMax(lastDescents[end], height - ascent);
      }
    }
  }
  for (size_t i = 0; i < items.size(); ++i) {
    if (rowContributions[i] < 0 || items[i].style.layout.autoMargins[0] || items[i].style.layout.autoMargins[2]) continue;
    const auto align = items[i].style.layout.alignSelf == "auto" ? style.alignItems : items[i].style.layout.alignSelf;
    const auto p = positions[i].row;
    const qreal shim = align == "baseline" || align == "first baseline" ? baselineAscents[p.start] - firstAscents[i]
                       : align == "last baseline"                       ? lastDescents[p.start + p.span - 1] - lastDescentsByItem[i]
                                                                        : 0;
    // A baseline shim belongs to the item's entire span. Adding a spanning
    // item's full height to a single row incorrectly grows the grid twice.
    auto& contribution = result.rowContributions[size_t(rowContributions[i])];
    contribution.minimum += shim;
    contribution.maximum += shim;
    contribution.automaticMinimum += shim;
  }
  applyContributions(rows, result.rowContributions, rowGap);
  sizeTracks(rows, contentHeight, rowGap, style.alignContent);
  if (contentHeight < 0) indefiniteFractions(rows, result.rowContributions, rowGap);
  if (sharedRows) useSharedTracks(rows, *sharedRows);
  qreal naturalHeight = rowGap * qMax(0, activeTrackCount(rows) - 1);
  for (const auto& row : rows) naturalHeight += row.base;
  const qreal finalHeight = contentHeight < 0 ? heightConstraints.clamp(naturalHeight) : contentHeight;
  if (contentHeight < 0 && !sharedRows) {
    rowGap = gap(style.rowGap, finalHeight);
    const auto percentageTracks = [](const CssGridTrackList& list) {
      return std::any_of(list.begin(), list.end(),
                         [](const auto& track) { return track.minimum.length.hasPercentage || track.maximum.length.hasPercentage; });
    };
    if (finalHeight != naturalHeight || percentageTracks(style.gridRows) || percentageTracks(style.gridAutoRows)) {
      // The intrinsic height is frozen. Resolve cyclic tracks against it exactly
      // once; any resulting overflow must not feed back into container height.
      rows = createTracks(style.gridRows.tracks, style.gridAutoRows.tracks, rowCount, rowOrigin, finalHeight, scale);
      collapseEmptyTracks(rows, expandedRows, rowOrigin, positions, items, true);
      applyContributions(rows, result.rowContributions, rowGap);
      sizeTracks(rows, finalHeight, rowGap, style.alignContent);
    }
  }
  const auto rowStarts =
      sharedRows ? sharedRows->starts
                 : trackPositions(rows, rowGap, contentHeight < 0 && finalHeight == naturalHeight ? -1 : finalHeight, style.alignContent);
  if (state.phase == CssLayoutPhase::InlineAllocation && !intrinsicOnly) {
    // CSS Grid sizes columns before rows, then revisits columns whose aspect
    // ratio depends on a definite cross-axis allocation. Record those edges
    // for the scheduler; frozen cyclic heights cannot feed this phase.
    std::vector<qreal> cross(items.size(), -1);
    for (size_t i = 0; i < items.size(); ++i) {
      const auto& item = items[i];
      if (item.style.layout.display == "none" || cssPreferredRatio(item) <= 0 || item.style.layout.sizes[1] != CssIntrinsicSize::Auto)
        continue;
      const auto align = item.style.layout.alignSelf == "auto" ? style.alignItems : item.style.layout.alignSelf;
      if (align != "stretch") continue;
      const auto p = positions[i].row;
      bool definite = contentHeight >= 0 || sharedRows.has_value();
      if (!definite) {
        definite = true;
        for (int r = p.start; r < p.start + p.span; ++r)
          definite = definite && rows[r].definition.minimum.kind == Kind::Length && rows[r].definition.maximum.kind == Kind::Length;
      }
      if (!definite) continue;
      cross[i] = qMax<qreal>(boxes[i].vertical, areaSize(rows, rowStarts, p) - boxes[i].margin.top() - boxes[i].margin.bottom());
      state.blockToInlineDependencies.push_back(i);
    }
    if (!state.blockToInlineDependencies.empty()) {
      state.transferredHeights = std::move(cross);
    }
  }
  result.size = {contentWidth < 0 ? result.intrinsic.maxContent : contentWidth, finalHeight};
  result.items.resize(items.size());
  result.containingWidths.resize(items.size());
  result.inheritedGrids.resize(items.size());
  std::vector<size_t> gridOrder;
  std::vector<qreal> exportedFirst(items.size()), exportedLast(items.size());
  int firstOccupiedRow = rowCount, lastOccupiedRow = -1;
  for (size_t i = 0; i < items.size(); ++i) {
    const auto& item = items[i];
    if (item.style.layout.display == "none") continue;
    const auto& p = positions[i];
    const auto& box = boxes[i];
    const qreal areaWidth = areaSize(columns, columnStarts, p.column), areaHeight = areaSize(rows, rowStarts, p.row);
    result.containingWidths[i] = areaWidth;
    const auto align = hasGridChildren(item) && item.style.layout.gridRows.subgrid ? QStringLiteral("stretch")
                       : item.style.layout.alignSelf == "auto"                     ? style.alignItems
                                                                                   : item.style.layout.alignSelf;
    const auto justify = hasGridChildren(item) && item.style.layout.gridColumns.subgrid ? QStringLiteral("stretch")
                         : item.style.layout.justifySelf == "auto"                      ? style.justifyItems
                                                                                        : item.style.layout.justifySelf;
    const auto& automatic = item.style.layout.autoMargins;
    const qreal height = hasGridChildren(item) && item.style.layout.gridRows.subgrid
                             ? qMax(box.vertical, areaHeight - box.margin.top() - box.margin.bottom())
                             : usedSize(item, 1, areaHeight, heights[i], heights[i], heights[i], box.vertical,
                                        (align == "stretch" || (align == "normal" && !item.naturalSize && cssPreferredRatio(item) <= 0)) &&
                                            !automatic[0] && !automatic[2],
                                        box.margin.top() + box.margin.bottom(), scale);
    const qreal x = columnStarts[p.column.start] +
                    itemOffset(areaWidth, widths[i], box.margin.left(), box.margin.right(), automatic[3], automatic[1], justify);
    qreal y =
        rowStarts[p.row.start] + itemOffset(areaHeight, height, box.margin.top(), box.margin.bottom(), automatic[0], automatic[2], align);
    const qreal baselineInset = box.padding.top() + item.style.box.borderTopWidth * scale;
    if (!automatic[0] && !automatic[2]) {
      if (align == "baseline" || align == "first baseline")
        y = rowStarts[p.row.start] + baselineAscents[p.row.start] - baselineInset - firstBaselines[i];
      else if (align == "last baseline")
        y = rowStarts[p.row.start] + areaHeight - lastDescents[p.row.start + p.row.span - 1] - baselineInset - lastBaselines[i];
    }
    exportedFirst[i] = y + baselineInset + firstBaselines[i];
    exportedLast[i] = y + baselineInset + lastBaselines[i];
    firstOccupiedRow = qMin(firstOccupiedRow, p.row.start);
    lastOccupiedRow = qMax(lastOccupiedRow, p.row.start + p.row.span - 1);
    gridOrder.push_back(i);
    result.items[i] = {x, y, widths[i], height};
    if (hasGridChildren(item) && item.style.layout.gridColumns.subgrid)
      result.inheritedGrids[i].columns =
          sliceAxis(style.gridColumns, columns, columnStarts, p.column, columnOrigin, columnGap, x - columnStarts[p.column.start],
                    areaWidth - widths[i] - (x - columnStarts[p.column.start]));
    if (hasGridChildren(item) && item.style.layout.gridRows.subgrid)
      result.inheritedGrids[i].rows = sliceAxis(style.gridRows, rows, rowStarts, p.row, rowOrigin, rowGap, y - rowStarts[p.row.start],
                                                areaHeight - height - (y - rowStarts[p.row.start]));
  }
  // CSS Grid 2 section 11.6: use the first/last occupied row's shared
  // baseline, falling back to the first/last item in row-major grid order.
  // The lowest painted text baseline and the last explicit track need not
  // belong to that item (e.g. a tall first item or an empty trailing row).
  std::stable_sort(gridOrder.begin(), gridOrder.end(), [&](size_t a, size_t b) {
    return std::tie(positions[a].row.start, positions[a].column.start, items[a].style.layout.order) <
           std::tie(positions[b].row.start, positions[b].column.start, items[b].style.layout.order);
  });
  if (!gridOrder.empty()) {
    result.firstBaseline = exportedFirst[gridOrder.front()];
    result.lastBaseline = exportedLast[gridOrder.back()];
    for (const auto i : gridOrder) {
      const auto& item = items[i];
      if (item.style.layout.autoMargins[0] || item.style.layout.autoMargins[2]) continue;
      const auto align = item.style.layout.alignSelf == "auto" ? style.alignItems : item.style.layout.alignSelf;
      const auto p = positions[i].row;
      if (p.start == firstOccupiedRow && (align == "baseline" || align == "first baseline")) result.firstBaseline = exportedFirst[i];
      if (p.start + p.span - 1 == lastOccupiedRow && align == "last baseline") result.lastBaseline = exportedLast[i];
    }
  }
  return result;
}
CssFormattingResult solveGrid(const ThemeElementStyle& container, const std::vector<CssFormattingItem>& items, qreal width, qreal height,
                              qreal scale, const CssGridInheritance& inherited, bool intrinsicOnly) {
  GridSizingState state;
  state.phase = intrinsicOnly ? CssLayoutPhase::Intrinsic : CssLayoutPhase::InlineAllocation;
  auto result = layoutGridItemsImpl(container, items, width, height, scale, inherited, state);
  if (!state.blockToInlineDependencies.empty()) {
    // Grid's specified dependency order: allocated rows invalidate column
    // contributions once. Cyclic intrinsic block sizes are frozen inside each
    // pass and never become definite percentage references by iteration.
    state.phase = CssLayoutPhase::DependentAllocation;
    result = layoutGridItemsImpl(container, items, width, height, scale, inherited, state);
  }
  return result;
}
}  // namespace
CssIntrinsicMetrics intrinsicGridWidths(const ThemeElementStyle& style, const std::vector<CssFormattingItem>& items) {
  return solveGrid(style, items, -1, -1, 1, {}, true).intrinsic;
}
CssFormattingResult layoutGridItems(const ThemeElementStyle& container, const std::vector<CssFormattingItem>& items, qreal contentWidth,
                                    qreal contentHeight, qreal scale, const CssGridInheritance& inherited) {
  return solveGrid(container, items, contentWidth, contentHeight, scale, inherited, false);
}
}  // namespace muffin

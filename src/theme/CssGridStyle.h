#pragma once

#include "theme/CssCalc.h"
#include <QHash>
#include <QRect>
#include <QStringList>
#include <optional>
#include <vector>

namespace muffin {
// Bounded like browser grid implementations; declarations exceeding this limit
// are rejected before placement can allocate or iterate unbounded grids.
inline constexpr int kMaxGridTracks = 1000;
enum class CssGridBreadthKind { Auto, Length, MinContent, MaxContent, Fraction };
struct CssGridBreadth {
  bool operator==(const CssGridBreadth&) const = default;
  CssGridBreadthKind kind = CssGridBreadthKind::Auto;
  CssLengthPercentage length;
  qreal fraction = 0;
};
struct CssGridTrack {
  bool operator==(const CssGridTrack&) const = default;
  CssGridBreadth minimum, maximum;
  bool fitContent = false;
};
struct CssGridAutoRepeat {
  bool operator==(const CssGridAutoRepeat&) const = default;
  int index = 0;
  bool fit = false;
  QStringList beforeNames;
  std::vector<CssGridTrack> tracks;
  std::vector<QStringList> lineNames;
};
struct CssGridNameRepeat {
  bool operator==(const CssGridNameRepeat&) const = default;
  int index = 0;
  std::vector<QStringList> names;
};
// Computed tracks retain automatic repetition until the containing axis has a
// definite used size. Line names are case-sensitive and belong to boundaries.
struct CssGridTrackList {
  bool operator==(const CssGridTrackList&) const = default;
  std::vector<CssGridTrack> tracks;
  std::vector<QStringList> lineNames{1};
  std::optional<CssGridAutoRepeat> automatic;
  bool subgrid = false;
  std::optional<CssGridNameRepeat> nameRepeat;
  auto begin() const { return tracks.begin(); }
  auto end() const { return tracks.end(); }
  auto begin() { return tracks.begin(); }
  auto end() { return tracks.end(); }
  size_t size() const { return tracks.size(); }
  bool empty() const { return tracks.empty(); }
  const CssGridTrack& operator[](size_t index) const { return tracks[index]; }
};
struct CssGridAreas {
  bool operator==(const CssGridAreas&) const = default;
  int columns = 0, rows = 0;
  QHash<QString, QRect> rectangles;  // zero-based cells, width/height are spans
};
struct CssGridLine {
  int number = 0;  // zero is the internal auto sentinel, never an authored line
  bool span = false;
  QString name;
};
std::optional<CssGridTrackList> parseCssGridTracks(const QString& value, const CssLengthContext& context = {}, bool allowNone = true);
std::optional<CssGridLine> parseCssGridLine(const QString& value);
std::optional<CssGridAreas> parseCssGridAreas(const QString& value);
}  // namespace muffin

#include "document/PieceTable.h"

#include <algorithm>

namespace muffin {

const PieceTable& PieceTable::empty() {
  static const PieceTable instance;
  return instance;
}

PieceTable::PieceTable(QString initial) : original_(std::move(initial)) {
  if (!original_.isEmpty()) {
    pieces_.push_back(Piece{false, 0, original_.size()});
  }
  rebuildPrefix();
}

std::pair<qsizetype, qsizetype> PieceTable::locate(qsizetype offset) const {
  // prefix_ is sorted ascending; find the first index whose prefix_ > offset, then the piece is
  // index-1 and the in-piece offset is offset - prefix_[index-1].
  const auto it = std::upper_bound(prefix_.begin(), prefix_.end(), static_cast<qint64>(offset));
  const qsizetype idx = static_cast<qsizetype>(it - prefix_.begin()) - 1;
  return {idx, offset - static_cast<qsizetype>(prefix_[idx])};
}

QChar PieceTable::at(qsizetype offset) const {
  const auto [idx, off] = locate(offset);
  const Piece& p = pieces_[static_cast<size_t>(idx)];
  return buffer(p.fromChanges).at(p.start + off);
}

QString PieceTable::mid(qsizetype start, qsizetype len) const {
  if (start < 0) { start = 0; }
  if (start >= totalLength_ || len <= 0) { return QString(); }
  if (len > totalLength_ - start) { len = totalLength_ - start; }
  QString out;
  out.reserve(len);
  auto [idx, off] = locate(start);
  qsizetype remaining = len;
  while (remaining > 0 && idx < static_cast<qsizetype>(pieces_.size())) {
    const Piece& p = pieces_[static_cast<size_t>(idx)];
    const qsizetype take = std::min(remaining, p.length - off);
    out.append(QStringView(buffer(p.fromChanges)).mid(p.start + off, take));
    remaining -= take;
    ++idx;
    off = 0;
  }
  return out;
}

QString PieceTable::toString() const {
  return mid(0, static_cast<qsizetype>(totalLength_));
}

QByteArray PieceTable::toUtf8() const {
  return toString().toUtf8();
}

void PieceTable::replace(qsizetype start, qsizetype end, QStringView text) {
  if (start < 0) { start = 0; }
  if (end < start) { end = start; }
  if (start > totalLength_) { start = static_cast<qsizetype>(totalLength_); }
  if (end > totalLength_) { end = static_cast<qsizetype>(totalLength_); }

  // Append the new text to the append-only changes_ buffer; existing pieces that reference changes_
  // stay valid (append never moves earlier chars).
  const qsizetype changesStart = changes_.size();
  changes_.append(text);
  const qsizetype insertedLen = text.length();

  // Locate the pieces spanning the [start, end) boundaries. start/end == totalLength_ means "at the
  // very end" — the boundary is just past the last piece (pieces_.size()).
  qsizetype ps = 0, os = 0, pe = 0, oe = 0;
  if (start < totalLength_) {
    std::tie(ps, os) = locate(start);
  } else {
    ps = static_cast<qsizetype>(pieces_.size());
  }
  if (end < totalLength_) {
    std::tie(pe, oe) = locate(end);
  } else {
    pe = static_cast<qsizetype>(pieces_.size());
  }

  // Rebuild the piece list: [pieces before ps] + [left part of pieces[ps] if start is mid-piece]
  // + [new piece if non-empty insert] + [right part of pieces[pe] if end is mid-piece]
  // + [pieces after pe]. The pieces fully inside [start, end) are dropped (replaced).
  std::vector<Piece> next;
  next.reserve(pieces_.size() + 2);
  for (qsizetype i = 0; i < ps; ++i) { next.push_back(pieces_[static_cast<size_t>(i)]); }
  if (ps < static_cast<qsizetype>(pieces_.size()) && os > 0) {
    const Piece& p = pieces_[static_cast<size_t>(ps)];
    next.push_back(Piece{p.fromChanges, p.start, os});
  }
  if (insertedLen > 0) {
    // Coalesce with a trailing changes_ piece whose bytes immediately precede this insert in the
    // append-only changes_ buffer (they are contiguous there). This is the common case for
    // consecutive single-char typing at one caret position: without it every keystroke would push a
    // fresh piece and the piece count — hence every later replace/locate — would grow linearly with
    // keystroke count, turning a long typing session into an O(pieces)-per-keystroke drag.
    if (!next.empty()) {
      Piece& tail = next.back();
      if (tail.fromChanges && tail.start + tail.length == changesStart) {
        tail.length += insertedLen;
      } else {
        next.push_back(Piece{true, changesStart, insertedLen});
      }
    } else {
      next.push_back(Piece{true, changesStart, insertedLen});
    }
  }
  if (pe < static_cast<qsizetype>(pieces_.size()) && oe < pieces_[static_cast<size_t>(pe)].length) {
    const Piece& p = pieces_[static_cast<size_t>(pe)];
    next.push_back(Piece{p.fromChanges, p.start + oe, p.length - oe});
  }
  for (qsizetype i = pe + 1; i < static_cast<qsizetype>(pieces_.size()); ++i) {
    next.push_back(pieces_[static_cast<size_t>(i)]);
  }

  pieces_ = std::move(next);
  rebuildPrefix();
}

void PieceTable::rebuildPrefix() {
  prefix_.assign(pieces_.size() + 1, 0);
  qint64 acc = 0;
  for (size_t i = 0; i < pieces_.size(); ++i) {
    prefix_[i] = acc;
    acc += pieces_[i].length;
  }
  prefix_[pieces_.size()] = acc;
  totalLength_ = acc;
}

qsizetype PieceTable::indexOf(QChar ch, qsizetype from) const {
  for (qint64 i = qMax<qint64>(0, static_cast<qint64>(from)); i < totalLength_; ++i) {
    if (at(static_cast<qsizetype>(i)) == ch) {
      return static_cast<qsizetype>(i);
    }
  }
  return -1;
}

}  // namespace muffin

#pragma once

#include "document/PieceTable.h"

#include <QMetaType>

namespace muffin {

// Committed UTF-16 edits, ordered in the coordinates of the previous document.
// Reading anchors do not depend on the editor or on the lifetime of AST nodes.
struct SourceTextChange {
  struct Edit {
    qsizetype start = 0, removed = 0, inserted = 0;
  };
  QVector<Edit> edits;

  qsizetype mapOffset(qsizetype offset) const {
    if (offset < 0)
      return offset;
    qsizetype shift = 0;
    for (const auto& edit : edits) {
      if (offset < edit.start)
        break;
      if (offset < edit.start + edit.removed)
        return edit.start + shift + qMin(offset - edit.start, edit.inserted);
      shift += edit.inserted - edit.removed;
    }
    return offset + shift;
  }

  SourceTextChange reversed() const {
    SourceTextChange result;
    result.edits.reserve(edits.size());
    qsizetype shift = 0;
    for (const auto& edit : edits) {
      result.edits.append({edit.start + shift, edit.inserted, edit.removed});
      shift += edit.inserted - edit.removed;
    }
    return result;
  }

  // Full-text fallbacks retain unchanged prefixes/suffixes without flattening
  // the piece table. Known edits (including replace-all) supply exact ranges.
  static SourceTextChange between(const PieceTable& before, QStringView after) {
    qsizetype prefix = 0;
    bool matching = true;
    QVector<QStringView> chunks;
    before.forEachChunk([&](QStringView chunk) {
      chunks.append(chunk);
      if (!matching)
        return;
      const auto available = qMin(chunk.size(), after.size() - prefix);
      qsizetype common = 0;
      while (common < available && chunk[common] == after[prefix + common])
        ++common;
      prefix += common;
      matching = common == chunk.size();
    });
    if (prefix == before.size() && prefix == after.size())
      return {};
    qsizetype suffix = 0;
    for (auto chunk = chunks.crbegin(); chunk != chunks.crend(); ++chunk) {
      qsizetype i = chunk->size();
      while (i > 0 && suffix < before.size() - prefix && suffix < after.size() - prefix &&
             (*chunk)[i - 1] == after[after.size() - 1 - suffix]) {
        --i;
        ++suffix;
      }
      if (i > 0)
        break;
    }
    return {{{prefix, before.size() - prefix - suffix, after.size() - prefix - suffix}}};
  }
};
} // namespace muffin

Q_DECLARE_METATYPE(muffin::SourceTextChange)

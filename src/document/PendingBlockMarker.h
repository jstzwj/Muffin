#pragma once

#include "document/MarkdownTypes.h"

#include <QString>
#include <QStringView>
#include <QVector>

namespace muffin {

class MarkdownNode;

enum class PendingBlockMarkerKind {
  None,
  Heading,
  List,
  CodeFence,
  MathDollar,
  MathBracket
};

struct PendingBlockMarker {
  PendingBlockMarkerKind kind = PendingBlockMarkerKind::None;
  BlockType targetType = BlockType::Unknown;
  QString opener;
  QString closer;
  qsizetype prefixLength = 0;
  bool highlightPrefix = false;

  bool isValid() const;
  bool commitsOnEnter() const;
};

PendingBlockMarker detectPendingBlockMarker(QStringView singleLine);
// These read the document text only, so they take a QStringView. A QString binds implicitly, so
// existing callers passing session.markdownText() / a slice QString are unchanged — but the parse
// path no longer has to copy a 100MB doc to QString just to feed these read-only walks.
PendingBlockMarker detectPendingBlockMarkerForNode(QStringView markdown, const MarkdownNode& node);
bool shouldDemotePendingMarker(QStringView markdown, const MarkdownNode& node);

// Fold a pending-marker block (one whose source is a still-incomplete opener such as `###`, `*`,
// ``` ``` ```, `$$`) back into a Paragraph holding the opener as plain text. Clears any block
// children so a lone-marker List (cmark emits List -> ListItem for `*` alone, since a trailing
// newline satisfies its bullet check) collapses into a single-text paragraph.
void demotePendingMarkerToParagraph(QStringView markdown, MarkdownNode& node);

// Demote every lone-marker List (bullet/ordered marker with no trailing space, e.g. `*` or `1.`
// alone on a line) back to a Paragraph, recursively (so it also applies inside block quotes).
// Real lists (`* foo`) are left intact. Applied at parse time so load and edit paths agree and
// cmark never hands the editor a non-editable "zombie" list whose marker lacks a space.
void demotePendingListMarkers(MarkdownNode& root, QStringView markdown);

// Demote every childless BlockQuote (a `>`/`>` + spaces line with no following content, e.g. a
// blockquote being typed as `>` or `> ` before any text follows) back to a Paragraph holding the
// marker as plain text, recursively. A blockquote with real content (`> text`) parses to a
// BlockQuote with a child Paragraph and is left intact; only the empty container — which cmark
// emits with NO child at all, so the editor has no editable block to land the caret in — is folded.
// Applied at parse time so load and edit paths agree, mirroring demotePendingListMarkers.
void demoteEmptyBlockQuotes(MarkdownNode& root, QStringView markdown);

// Source offsets of every Paragraph whose source text is a still-incomplete block opener (`###`,
// `*`, ``` ``` ```, `$$`, `\[`), including Paragraph nodes nested in containers. Loaded structural
// blocks (e.g. an empty ATX heading, which is a Heading node) are excluded; only markers that are
// currently being typed and therefore demoted to paragraphs are collected.
QVector<qsizetype> collectPendingMarkerOffsets(QStringView markdown, const MarkdownNode& root);

qsizetype pendingBlockMarkerOffset(QStringView markdown, qsizetype offset);

// Shift pre-edit pending marker offsets to their post-edit positions.
// - Offsets before sourceStart: unchanged.
// - Offsets in [sourceStart, sourceStart + removedLength): discarded
//   (the edited line's new offset, if any, comes from pendingMarkerOffsetForSingleLineEdit).
// - Offsets >= sourceStart + removedLength: shifted by (insertedLength - removedLength).
QVector<qsizetype> shiftPendingMarkerOffsets(
    const QVector<qsizetype>& preEditOffsets,
    qsizetype sourceStart,
    qsizetype removedLength,
    qsizetype insertedLength);

}  // namespace muffin

#include "document/MarkdownNode.h"

#include "document/SourcePositionIndex.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace muffin {

// The lazy payload owns DefinitionBlock, so both metadata layers need explicit deep-copy support.
// Keep their field lists in sync with MarkdownNode.h when adding domain state.
MarkdownNode::ExtendedMetadata::ExtendedMetadata(const ExtendedMetadata& o)
    : codeLanguage(o.codeLanguage), tableAlignments(o.tableAlignments),
      definition(o.definition ? std::make_unique<DefinitionBlock>(*o.definition) : nullptr) {}

MarkdownNode::ExtendedMetadata& MarkdownNode::ExtendedMetadata::operator=(
    const ExtendedMetadata& o) {
  if (this != &o) {
    auto copiedDefinition = o.definition
        ? std::make_unique<DefinitionBlock>(*o.definition)
        : nullptr;
    codeLanguage = o.codeLanguage;
    tableAlignments = o.tableAlignments;
    definition = std::move(copiedDefinition);
  }
  return *this;
}

bool MarkdownNode::ExtendedMetadata::isEmpty() const {
  return codeLanguage.isEmpty() && tableAlignments.isEmpty() && !definition;
}

MarkdownNode::ExtendedMetadata& MarkdownNode::ensureExtendedMetadata() {
  if (!metadata_.extended) {
    metadata_.extended = std::make_unique<ExtendedMetadata>();
  }
  return *metadata_.extended;
}

void MarkdownNode::pruneExtendedMetadata() {
  if (metadata_.extended && metadata_.extended->isEmpty()) {
    metadata_.extended.reset();
  }
}

MarkdownNode::BlockMetadata::BlockMetadata(const BlockMetadata& o)
    : inlines(o.inlines), literal(o.literal), sourceRange(o.sourceRange),
      extended(o.extended ? std::make_unique<ExtendedMetadata>(*o.extended) : nullptr),
      headingLevel(o.headingLevel), listStart(o.listStart), flags(o.flags) {}

MarkdownNode::BlockMetadata& MarkdownNode::BlockMetadata::operator=(const BlockMetadata& o) {
  if (this != &o) {
    auto extendedMetadata = o.extended
        ? std::make_unique<ExtendedMetadata>(*o.extended)
        : nullptr;
    inlines = o.inlines;
    literal = o.literal;
    sourceRange = o.sourceRange;
    extended = std::move(extendedMetadata);
    headingLevel = o.headingLevel;
    listStart = o.listStart;
    flags = o.flags;
  }
  return *this;
}

MarkdownNode::MarkdownNode(BlockType type, NodeId id)
    : id_(std::move(id)), type_(type), sourcePositionIndex_(nullptr) {}

NodeId MarkdownNode::id() const {
  return id_;
}

void MarkdownNode::setId(NodeId id) {
  id_ = std::move(id);
}

BlockType MarkdownNode::type() const {
  return type_;
}

void MarkdownNode::setType(BlockType type) {
  type_ = type;
}

MarkdownNode* MarkdownNode::parent() const {
  return parent_;
}

MarkdownNode* MarkdownNode::previousSibling() const {
  return previous_;
}

MarkdownNode* MarkdownNode::nextSibling() const {
  return next_;
}

quint8 MarkdownNode::siblingKind() const {
  switch (type_) {
    case BlockType::Paragraph: return 1;
    case BlockType::Heading: return static_cast<quint8>(1 + std::clamp(metadata_.headingLevel, 1, 6));
    case BlockType::BlockQuote: return 8;
    case BlockType::List: return listKind() == ListKind::Ordered ? 9 : 10;
    case BlockType::ListItem: return 11;
    case BlockType::CodeFence:
    case BlockType::FrontMatter: return 12;
    case BlockType::Table: return 13;
    case BlockType::TableRow: return 14;
    case BlockType::TableCell: return 15;
    case BlockType::ThematicBreak: return 16;
    default: return 0;
  }
}

int MarkdownNode::siblingIndex() const {
  if (!parent_) {
    return -1;
  }
  if (!parent_->parent_ && parent_->sourcePositionIndex_) {
    return static_cast<int>(parent_->sourcePositionIndex_->rank(sourcePositionToken_));
  }
  int index = 0;
  for (const MarkdownNode* previous = previous_; previous; previous = previous->previous_) {
    ++index;
  }
  return index;
}

int MarkdownNode::siblingTypeIndex() const {
  if (!parent_) {
    return -1;
  }
  if (!parent_->parent_ && parent_->sourcePositionIndex_) {
    return static_cast<int>(parent_->sourcePositionIndex_->typeRank(sourcePositionToken_));
  }
  const quint8 kind = siblingKind();
  int index = 0;
  for (const MarkdownNode* previous = previous_; previous; previous = previous->previous_) {
    if (previous->siblingKind() == kind) {
      ++index;
    }
  }
  return index;
}

std::vector<std::unique_ptr<MarkdownNode>>& MarkdownNode::children() {
  return children_;
}

const std::vector<std::unique_ptr<MarkdownNode>>& MarkdownNode::children() const {
  return children_;
}

MarkdownNode& MarkdownNode::appendChild(std::unique_ptr<MarkdownNode> child) {
  return insertChild(children_.size(), std::move(child));
}

MarkdownNode& MarkdownNode::insertChild(qsizetype index, std::unique_ptr<MarkdownNode> child) {
  child->parent_ = this;
  const qsizetype boundedIndex = std::clamp<qsizetype>(index, 0, static_cast<qsizetype>(children_.size()));
  const qsizetype countBefore = static_cast<qsizetype>(children_.size());
  // Fix only the two sibling pointers at the insertion point. Rebuilding every child's links on
  // each insert made building a node with N children O(N^2) — the dominant cost of convertBlock
  // on large documents (a 27k-block root alone was ~1.3s of pointer writes).
  MarkdownNode* previous = boundedIndex > 0 ? children_[boundedIndex - 1].get() : nullptr;
  MarkdownNode* following = boundedIndex < countBefore ? children_[boundedIndex].get() : nullptr;
  children_.insert(children_.begin() + boundedIndex, std::move(child));
  MarkdownNode* inserted = children_[boundedIndex].get();
  inserted->previous_ = previous;
  inserted->next_ = following;
  if (previous) {
    previous->next_ = inserted;
  }
  if (following) {
    following->previous_ = inserted;
  }
  return *inserted;
}

std::unique_ptr<MarkdownNode> MarkdownNode::detachChild(qsizetype index) {
  if (index < 0 || index >= static_cast<qsizetype>(children_.size())) {
    return nullptr;
  }

  const qsizetype count = static_cast<qsizetype>(children_.size());
  MarkdownNode* previous = index > 0 ? children_[index - 1].get() : nullptr;
  MarkdownNode* following = index + 1 < count ? children_[index + 1].get() : nullptr;
  auto child = std::move(children_[index]);
  children_.erase(children_.begin() + index);
  child->parent_ = nullptr;
  child->previous_ = nullptr;
  child->next_ = nullptr;
  if (previous) {
    previous->next_ = following;
  }
  if (following) {
    following->previous_ = previous;
  }
  return child;
}

void MarkdownNode::clearChildren() {
  for (auto& child : children_) {
    child->parent_ = nullptr;
    child->previous_ = nullptr;
    child->next_ = nullptr;
  }
  children_.clear();
}

QVector<InlineNode>& MarkdownNode::inlines() {
  return metadata_.inlines;
}

const QVector<InlineNode>& MarkdownNode::inlines() const {
  return metadata_.inlines;
}

QString MarkdownNode::literal() const {
  return metadata_.literal;
}

void MarkdownNode::setLiteral(QString text) {
  metadata_.literal = std::move(text);
}

int MarkdownNode::headingLevel() const {
  return metadata_.headingLevel;
}

void MarkdownNode::setHeadingLevel(int level) {
  metadata_.headingLevel = level;
}

bool MarkdownNode::setext() const {
  return metadata_.flags.setext;
}

void MarkdownNode::setSetext(bool setext) {
  metadata_.flags.setext = setext;
}

ListKind MarkdownNode::listKind() const {
  return static_cast<ListKind>(metadata_.flags.listKind);
}

void MarkdownNode::setListKind(ListKind kind) {
  metadata_.flags.listKind = static_cast<quint32>(kind);
}

int MarkdownNode::listStart() const {
  return metadata_.listStart;
}

void MarkdownNode::setListStart(int start) {
  metadata_.listStart = start;
}

bool MarkdownNode::listTight() const {
  return metadata_.flags.listTight;
}

void MarkdownNode::setListTight(bool tight) {
  metadata_.flags.listTight = tight;
}

bool MarkdownNode::taskChecked() const {
  return metadata_.flags.taskChecked;
}

void MarkdownNode::setTaskChecked(bool checked) {
  metadata_.flags.taskChecked = checked;
}

bool MarkdownNode::isTaskItem() const {
  return metadata_.flags.taskItem;
}

void MarkdownNode::setTaskItem(bool taskItem) {
  metadata_.flags.taskItem = taskItem;
}

QString MarkdownNode::codeLanguage() const {
  return metadata_.extended ? metadata_.extended->codeLanguage : QString();
}

void MarkdownNode::setCodeLanguage(QString language) {
  if (language.isEmpty() && !metadata_.extended) {
    return;
  }
  ensureExtendedMetadata().codeLanguage = std::move(language);
  pruneExtendedMetadata();
}

bool MarkdownNode::isIndentedCode() const {
  return metadata_.flags.indentedCode;
}

void MarkdownNode::setIndentedCode(bool indented) {
  metadata_.flags.indentedCode = indented;
}

MathDelimiter MarkdownNode::mathDelimiter() const {
  return static_cast<MathDelimiter>(metadata_.flags.mathDelimiter);
}

void MarkdownNode::setMathDelimiter(MathDelimiter delimiter) {
  metadata_.flags.mathDelimiter = static_cast<quint32>(delimiter);
}

AlertKind MarkdownNode::alertKind() const {
  return static_cast<AlertKind>(metadata_.flags.alertKind);
}

void MarkdownNode::setAlertKind(AlertKind kind) {
  metadata_.flags.alertKind = static_cast<quint32>(kind);
}

FrontMatterFormat MarkdownNode::frontMatterFormat() const {
  return static_cast<FrontMatterFormat>(metadata_.flags.frontMatterFormat);
}

void MarkdownNode::setFrontMatterFormat(FrontMatterFormat format) {
  metadata_.flags.frontMatterFormat = static_cast<quint32>(format);
}

DefinitionBlock MarkdownNode::definition() const {
  DefinitionBlock def = metadata_.extended && metadata_.extended->definition
      ? *metadata_.extended->definition
      : DefinitionBlock{};
  if (!def.isValid()) {
    return def;
  }
  const MarkdownNode* top = topLevelBlock();
  // Pre-relativize (parser/annotation passes on the absolute tree): fields are absolute — raw.
  if (!top->metadata_.flags.offsetsRelative) {
    return def;
  }
  // Post-relativize: field ranges are stored relative to the owning top-level block's byteStart;
  // resolve to absolute. For a top-level definition block topLevelBlock() == this, so the base is
  // the block's own byteStart (its fields are relative to itself) — still correct.
  const qsizetype byteBase = top->sourceRange().byteStart;
  const auto shift = [byteBase](DefinitionFieldRange& field) {
    if (field.isValid()) {
      field.start += byteBase;
      field.end += byteBase;
    }
  };
  shift(def.labelRange);
  shift(def.destinationRange);
  shift(def.titleRange);
  shift(def.noteRange);
  shift(def.markerRange);
  shift(def.sourceRange);
  return def;
}

void MarkdownNode::setDefinition(DefinitionBlock definition) {
  ensureExtendedMetadata().definition =
      std::make_unique<DefinitionBlock>(std::move(definition));
}

QVector<TableAlignment> MarkdownNode::tableAlignments() const {
  return metadata_.extended ? metadata_.extended->tableAlignments : QVector<TableAlignment>();
}

void MarkdownNode::setTableAlignments(QVector<TableAlignment> alignments) {
  if (alignments.isEmpty() && !metadata_.extended) {
    return;
  }
  ensureExtendedMetadata().tableAlignments = std::move(alignments);
  pruneExtendedMetadata();
}

bool MarkdownNode::tableRowIsHeader() const {
  return metadata_.flags.tableRowIsHeader;
}

void MarkdownNode::setTableRowIsHeader(bool header) {
  metadata_.flags.tableRowIsHeader = header;
}

SourceRange MarkdownNode::sourceRange() const {
  SourceRange range = metadata_.sourceRange;
  // Top-level block (parent is the document root) or the root itself (parent_ == null): stored
  // ABSOLUTE — return as-is.
  if (!parent_) {
    return range;
  }
  if (!parent_->parent_) {
    if (parent_->sourcePositionIndex_) {
      const SourcePositionDelta delta = parent_->sourcePositionIndex_->adjustmentFor(sourcePositionToken_);
      if (range.byteStart >= 0 && range.byteEnd >= range.byteStart) {
        range.byteStart += delta.bytes;
        range.byteEnd += delta.bytes;
      }
      if (range.lineStart > 0) {
        range.lineStart += delta.lines;
      }
      if (range.lineEnd > 0) {
        range.lineEnd += delta.lines;
      }
    }
    return range;
  }
  const MarkdownNode* top = topLevelBlock();
  // Descendant: storage is block-relative only after the owning top-level block has been
  // relativized. Before that (parser/annotation/demote passes on the absolute tree) return raw.
  if (!top->metadata_.flags.offsetsRelative) {
    return range;
  }
  // Post-relativize: stored RELATIVE to the owning top-level block's byteStart/lineStart — resolve.
  // Lines resolve inside the byte-valid branch (mirror relativizeNodeAndDescendants) so a valid line
  // that relativized to 0 still round-trips.
  const SourceRange topRange = top->sourceRange();
  if (range.byteStart >= 0 && range.byteEnd >= range.byteStart) {
    range.byteStart += topRange.byteStart;
    range.byteEnd += topRange.byteStart;
    range.lineStart += topRange.lineStart;
    range.lineEnd += topRange.lineStart;
  }
  return range;
}

void MarkdownNode::setSourceRange(SourceRange range) {
  metadata_.sourceRange = std::move(range);
}

void MarkdownNode::shiftOwnSourceRange(qsizetype byteDelta, int lineDelta) {
  SourceRange& range = metadata_.sourceRange;
  if (range.byteStart >= 0 && range.byteEnd >= range.byteStart) {
    range.byteStart += byteDelta;
    range.byteEnd += byteDelta;
  }
  if (range.lineStart > 0) {
    range.lineStart += lineDelta;
  }
  if (range.lineEnd > 0) {
    range.lineEnd += lineDelta;
  }
}

const MarkdownNode* MarkdownNode::topLevelBlock() const {
  if (topLevelCache_) {
    return topLevelCache_;
  }
  // Walk up to the node whose parent is the document root (parent_ exists, parent_->parent_ does
  // not). For a direct root child the loop body never runs. For the document root (parent_ == null)
  // this returns the root itself. Memoized: a live node's top-level block never changes (within-
  // block surgery such as table cell moves preserves it; cross-block edits destroy + re-parse rather
  // than reparent), so the first computed value is valid for the node's lifetime.
  const MarkdownNode* n = this;
  while (n->parent_ && n->parent_->parent_) {
    n = n->parent_;
  }
  topLevelCache_ = n;
  return n;
}

void MarkdownNode::subtractDefinitionFields(DefinitionBlock& def, qsizetype byteBase) {
  if (!def.isValid()) {
    return;
  }
  const auto shiftField = [byteBase](DefinitionFieldRange& field) {
    if (field.isValid()) {
      field.start -= byteBase;
      field.end -= byteBase;
    }
  };
  shiftField(def.labelRange);
  shiftField(def.destinationRange);
  shiftField(def.titleRange);
  shiftField(def.noteRange);
  shiftField(def.markerRange);
  shiftField(def.sourceRange);
}

void MarkdownNode::relativizeNodeAndDescendants(const MarkdownNode* topLevel, qsizetype byteBase, int lineBase) {
  topLevelCache_ = topLevel;  // seed the cache while we are already walking the subtree
  SourceRange range = metadata_.sourceRange;
  // Shift bytes AND lines together whenever the byte range is resolved. Lines must move inside the
  // same guard as bytes (not a separate `lineStart > 0` check): a valid line can relativize to 0
  // (e.g. an item on the block's first line), and a `> 0` guard would then skip re-adding it on
  // resolve, breaking the round-trip.
  if (range.byteStart >= 0 && range.byteEnd >= range.byteStart) {
    range.byteStart -= byteBase;
    range.byteEnd -= byteBase;
    range.lineStart -= lineBase;
    range.lineEnd -= lineBase;
  }
  metadata_.sourceRange = range;
  if (metadata_.extended && metadata_.extended->definition) {
    subtractDefinitionFields(*metadata_.extended->definition, byteBase);
  }
  shiftInlineSourcePositions(metadata_.inlines, -byteBase);
  for (const auto& child : children_) {
    if (child) {
      child->relativizeNodeAndDescendants(topLevel, byteBase, lineBase);
    }
  }
}

void MarkdownNode::relativizeDescendants() {
  // 'this' is a top-level block. Its own sourceRange is the anchor and stays ABSOLUTE; read the
  // base directly from metadata_ (for a slice-assembled block this is read BEFORE its own range is
  // absolutized, so the base is slice-relative and descendants become relative-to-block).
  const qsizetype byteBase = metadata_.sourceRange.byteStart;
  const int lineBase = metadata_.sourceRange.lineStart;
  metadata_.flags.offsetsRelative = true;
  topLevelCache_ = this;  // this block is its own top-level
  if (metadata_.extended && metadata_.extended->definition) {
    subtractDefinitionFields(*metadata_.extended->definition, byteBase);
  }
  shiftInlineSourcePositions(metadata_.inlines, -byteBase);
  for (const auto& child : children_) {
    if (child) {
      child->relativizeNodeAndDescendants(this, byteBase, lineBase);
    }
  }
}

qsizetype MarkdownNode::bindSharedInlineText(const std::shared_ptr<const QString>& source) {
  return source ? bindSharedInlineTextRecursive(source) : 0;
}

qsizetype MarkdownNode::bindSharedInlineTextRecursive(const std::shared_ptr<const QString>& source) {
  qsizetype bound = 0;
  const auto bindInline = [&](const auto& self, InlineNode& inlineNode) -> void {
    if (inlineNode.bindSharedText(source)) {
      ++bound;
    }
    for (InlineNode& child : inlineNode.children()) {
      self(self, child);
    }
  };
  for (InlineNode& inlineNode : metadata_.inlines) {
    bindInline(bindInline, inlineNode);
  }
  for (const auto& child : children_) {
    if (child) {
      bound += child->bindSharedInlineTextRecursive(source);
    }
  }
  return bound;
}

std::unique_ptr<MarkdownNode> MarkdownNode::clone(CloneMode mode) const {
  auto copy = std::make_unique<MarkdownNode>(
      type_, mode == CloneMode::PreserveIds ? id_ : NodeId::create());
  // A single aggregate copy carries every domain field, so adding a field can no longer be
  // silently dropped here (the flat layout previously let clone() miss taskItem_).
  copy->metadata_ = metadata_;
  if (parent_ && !parent_->parent_) {
    // A detached snapshot has no document position index. Store the resolved top-level range so
    // it remains self-contained even when this block currently carries lazy suffix deltas.
    copy->metadata_.sourceRange = sourceRange();
  }
  // A snapshot must be self-contained. Keeping shared slices here would retain an entire source
  // buffer for a tiny table/node undo entry and historically made table clones read text in the
  // wrong coordinate frame after block-relative offsets shifted.
  for (InlineNode& inlineNode : copy->metadata_.inlines) {
    inlineNode.detachSharedText();
  }
  for (const auto& child : children_) {
    copy->appendChild(child->clone(mode));
  }
  return copy;
}

}  // namespace muffin

#pragma once

#include <QtTypes>

#include <cstdint>
#include <memory>
#include <utility>

namespace muffin {

// Compressed implicit tree of logical source lines. Consecutive lines with the
// same height share one run, so a newly-opened million-line document starts as
// one node. Visible lines split out lazily as their wrapped height is measured.
class SourceLineHeightIndex final {
public:
  SourceLineHeightIndex() = default;
  SourceLineHeightIndex(SourceLineHeightIndex&&) noexcept = default;
  SourceLineHeightIndex& operator=(SourceLineHeightIndex&&) noexcept = default;
  SourceLineHeightIndex(const SourceLineHeightIndex&) = delete;
  SourceLineHeightIndex& operator=(const SourceLineHeightIndex&) = delete;

  void reset(qsizetype lineCount, int estimatedHeight);
  qsizetype lineCount() const;
  qint64 totalHeight() const;
  int estimatedHeight() const;

  qint64 yForLine(qsizetype line) const;
  qsizetype lineAtY(qint64 y) const;
  int heightForLine(qsizetype line) const;
  void setHeight(qsizetype line, int height);

private:
  struct Node {
    qsizetype count = 0;
    int height = 1;
    std::uint32_t priority = 0;
    qsizetype subtreeCount = 0;
    qint64 subtreeHeight = 0;
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
  };

  static qsizetype countOf(const std::unique_ptr<Node>& node);
  static qint64 heightOf(const std::unique_ptr<Node>& node);
  static void refresh(Node& node);
  static std::unique_ptr<Node> makeNode(qsizetype count, int height);
  static std::pair<std::unique_ptr<Node>, std::unique_ptr<Node>> split(
      std::unique_ptr<Node> root, qsizetype leftCount);
  static std::unique_ptr<Node> merge(
      std::unique_ptr<Node> left, std::unique_ptr<Node> right);

  std::unique_ptr<Node> root_;
  int estimatedHeight_ = 1;
};

}  // namespace muffin

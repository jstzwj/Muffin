#pragma once

#include <QVector>
#include <QWidget>

class QResizeEvent;
class QScrollArea;
class QVBoxLayout;
class QWidget;

namespace muffin {

class InlineNode;
class MarkdownDocument;
class MarkdownNode;

class MarkdownRenderWidget final : public QWidget {
public:
  explicit MarkdownRenderWidget(QWidget* parent = nullptr);

  void setDocument(const MarkdownDocument& document);
  void setZoomPercent(int percent);

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  void rebuild(const MarkdownNode& root);
  void clearBody();
  void updateBodyWidth();
  void addBlock(const MarkdownNode& node, int depth = 0);
  void addRichBlock(const QString& html, const QString& css = {});
  void addCodeBlock(const MarkdownNode& node);
  void addMathBlock(const MarkdownNode& node);
  void addHorizontalRule();

  QString renderBlockHtml(const MarkdownNode& node, int depth = 0) const;
  QString renderChildrenHtml(const MarkdownNode& node, int depth = 0) const;
  QString renderListItemHtml(const MarkdownNode& node, int depth) const;
  QString renderInlines(const QVector<InlineNode>& inlines) const;
  QString renderInline(const InlineNode& node) const;

  QScrollArea* scrollArea_ = nullptr;
  QWidget* canvas_ = nullptr;
  QWidget* body_ = nullptr;
  QVBoxLayout* bodyLayout_ = nullptr;
  int zoomPercent_ = 100;
};

}  // namespace muffin

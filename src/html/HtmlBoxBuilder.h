#pragma once

#include "html/HtmlBox.h"

#include <memory>

namespace muffin::html {

class HtmlDocument;

class HtmlBoxBuilder {
public:
  HtmlBoxBuilder();
  ~HtmlBoxBuilder();

  std::unique_ptr<HtmlBox> build(const HtmlDocument& document);

private:
  HtmlTag mapTag(void* node) const;
  void buildChildren(HtmlBox& parent, void* parentNode);
  std::unique_ptr<HtmlBox> buildNode(void* node);
  std::unique_ptr<HtmlBox> buildTextNode(void* textNode);
  bool shouldSkipTag(HtmlTag tag) const;
  void extractInlineStyle(HtmlBox& box, const char* styleAttr, size_t length);
  void assignListMarkers(HtmlBox& box);
};

}  // namespace muffin::html

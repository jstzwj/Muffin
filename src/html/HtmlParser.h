#pragma once

#include <QString>

namespace muffin::html {

class HtmlDocument {
public:
  HtmlDocument();
  ~HtmlDocument();

  HtmlDocument(const HtmlDocument&) = delete;
  HtmlDocument& operator=(const HtmlDocument&) = delete;
  HtmlDocument(HtmlDocument&&) noexcept;
  HtmlDocument& operator=(HtmlDocument&&) noexcept;

  bool parse(const QString& html);
  bool parse(const QByteArray& utf8Html);

  // Returns the underlying lxb_html_document_t pointer.
  // Only usable when valid() returns true.
  void* document() const;
  bool valid() const;
  QString errorString() const;

private:
  void* doc_ = nullptr;
  bool valid_ = false;
  QString error_;
};

}  // namespace muffin::html

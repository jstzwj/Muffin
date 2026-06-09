#include "html/HtmlParser.h"

#include <lexbor/html/html.h>

namespace muffin::html {

HtmlDocument::HtmlDocument() {
  doc_ = lxb_html_document_create();
}

HtmlDocument::~HtmlDocument() {
  if (doc_) {
    lxb_html_document_destroy(static_cast<lxb_html_document_t*>(doc_));
  }
}

HtmlDocument::HtmlDocument(HtmlDocument&& other) noexcept
    : doc_(other.doc_), valid_(other.valid_), error_(std::move(other.error_)) {
  other.doc_ = nullptr;
  other.valid_ = false;
}

HtmlDocument& HtmlDocument::operator=(HtmlDocument&& other) noexcept {
  if (this != &other) {
    if (doc_) {
      lxb_html_document_destroy(static_cast<lxb_html_document_t*>(doc_));
    }
    doc_ = other.doc_;
    valid_ = other.valid_;
    error_ = std::move(other.error_);
    other.doc_ = nullptr;
    other.valid_ = false;
  }
  return *this;
}

bool HtmlDocument::parse(const QString& html) {
  return parse(html.toUtf8());
}

bool HtmlDocument::parse(const QByteArray& utf8Html) {
  if (!doc_) {
    error_ = QStringLiteral("Document not created");
    valid_ = false;
    return false;
  }

  auto* doc = static_cast<lxb_html_document_t*>(doc_);
  const lxb_char_t* data = reinterpret_cast<const lxb_char_t*>(utf8Html.constData());
  const size_t length = static_cast<size_t>(utf8Html.size());

  lxb_status_t status = lxb_html_document_parse(doc, data, length);
  if (status != LXB_STATUS_OK) {
    error_ = QStringLiteral("HTML parse error (status %1)").arg(static_cast<int>(status));
    valid_ = false;
    return false;
  }

  valid_ = true;
  return true;
}

void* HtmlDocument::document() const { return doc_; }
bool HtmlDocument::valid() const { return valid_; }
QString HtmlDocument::errorString() const { return error_; }

}  // namespace muffin::html

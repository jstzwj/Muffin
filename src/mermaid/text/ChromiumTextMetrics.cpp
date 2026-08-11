#include "mermaid/text/ChromiumTextMetrics.h"

#include "mermaid/editor/MermaidRenderSupport.h"

#include <QByteArray>
#include <QRawFont>

#include <hb-ot.h>
#include <hb.h>

#include <cmath>

namespace muffin::mermaid::textmetrics {
namespace {
struct RawSource {
  QRawFont raw;
};
void destroyBytes(void *data) { delete static_cast<QByteArray *>(data); }
void destroySource(void *data) { delete static_cast<RawSource *>(data); }
hb_blob_t *table(hb_face_t *, hb_tag_t tag, void *data) {
  const auto *source = static_cast<const RawSource *>(data);
  const char name[] = {char(tag >> 24), char(tag >> 16), char(tag >> 8),
                       char(tag)};
  auto *bytes = new QByteArray(source->raw.fontTable(QByteArray(name, 4)));
  if (bytes->isEmpty()) {
    delete bytes;
    return hb_blob_reference(hb_blob_get_empty());
  }
  return hb_blob_create(bytes->constData(), unsigned(bytes->size()),
                        HB_MEMORY_MODE_READONLY, bytes, destroyBytes);
}
} // namespace

std::optional<qreal> harfBuzzAdvance(const QString &text,
                                     const QString &cssFontFamilies,
                                     qreal fontSize, QFont::Weight weight) {
  if (text.isEmpty())
    return 0.0;
  const auto css = editor::makeUnhintedCssPixelFont(cssFontFamilies, 16.0);
  QFont resolvedFont = css.font;
  resolvedFont.setWeight(weight);
  QRawFont raw = QRawFont::fromFont(resolvedFont);
  if (!raw.isValid())
    return std::nullopt;
  for (uint codepoint : text.toUcs4())
    if (!raw.supportsCharacter(codepoint))
      return std::nullopt;
  auto *source = new RawSource{raw};
  hb_face_t *face = hb_face_create_for_tables(table, source, destroySource);
  const unsigned upem = hb_face_get_upem(face);
  if (!upem) {
    hb_face_destroy(face);
    return std::nullopt;
  }
  hb_font_t *font = hb_font_create(face);
  hb_ot_font_set_funcs(font);
  hb_font_set_scale(font, int(upem), int(upem));
  hb_buffer_t *buffer = hb_buffer_create();
  hb_buffer_add_utf16(buffer, reinterpret_cast<const uint16_t *>(text.utf16()),
                      text.size(), 0, text.size());
  hb_buffer_guess_segment_properties(buffer);
  hb_shape(font, buffer, nullptr, 0);
  unsigned count = 0;
  const hb_glyph_position_t *positions =
      hb_buffer_get_glyph_positions(buffer, &count);
  qint64 advance = 0;
  for (unsigned i = 0; i < count; ++i)
    advance += positions[i].x_advance;
  hb_buffer_destroy(buffer);
  hb_font_destroy(font);
  hb_face_destroy(face);
  return std::abs(qreal(advance)) * fontSize / qreal(upem);
}

std::optional<QRectF> harfBuzzInkBounds(const QString &text,
                                        const QString &cssFontFamilies,
                                        qreal fontSize, QFont::Weight weight) {
  if (text.isEmpty())
    return QRectF();
  const auto css = editor::makeUnhintedCssPixelFont(cssFontFamilies, 16.0);
  QFont resolvedFont = css.font;
  resolvedFont.setWeight(weight);
  QRawFont raw = QRawFont::fromFont(resolvedFont);
  if (!raw.isValid())
    return std::nullopt;
  for (uint codepoint : text.toUcs4())
    if (!raw.supportsCharacter(codepoint))
      return std::nullopt;
  auto *source = new RawSource{raw};
  hb_face_t *face = hb_face_create_for_tables(table, source, destroySource);
  const unsigned upem = hb_face_get_upem(face);
  if (!upem) {
    hb_face_destroy(face);
    return std::nullopt;
  }
  hb_font_t *font = hb_font_create(face);
  hb_ot_font_set_funcs(font);
  hb_font_set_scale(font, int(upem), int(upem));
  hb_buffer_t *buffer = hb_buffer_create();
  hb_buffer_add_utf16(buffer, reinterpret_cast<const uint16_t *>(text.utf16()),
                      text.size(), 0, text.size());
  hb_buffer_guess_segment_properties(buffer);
  hb_shape(font, buffer, nullptr, 0);
  unsigned count = 0;
  const hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &count);
  const hb_glyph_position_t *positions =
      hb_buffer_get_glyph_positions(buffer, &count);
  qint64 penX = 0;
  qint64 minimum = 0;
  qint64 maximum = 0;
  bool hasInk = false;
  for (unsigned i = 0; i < count; ++i) {
    hb_glyph_extents_t extents{};
    if (hb_font_get_glyph_extents(font, infos[i].codepoint, &extents)) {
      const qint64 first = penX + positions[i].x_offset + extents.x_bearing;
      const qint64 second = first + extents.width;
      const qint64 left = std::min(first, second);
      const qint64 right = std::max(first, second);
      if (!hasInk) {
        minimum = left;
        maximum = right;
        hasInk = true;
      } else {
        minimum = std::min(minimum, left);
        maximum = std::max(maximum, right);
      }
    }
    penX += positions[i].x_advance;
  }
  hb_buffer_destroy(buffer);
  hb_font_destroy(font);
  hb_face_destroy(face);
  if (!hasInk)
    return QRectF();
  const qreal scale = fontSize / qreal(upem);
  return QRectF(qreal(minimum) * scale, 0.0,
                qreal(maximum - minimum) * scale, 0.0);
}

} // namespace muffin::mermaid::textmetrics

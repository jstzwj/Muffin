#include "theme/ThemeFontDecoder.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <QtEndian>
#include <algorithm>
#include <vector>

namespace muffin {
namespace {
constexpr qsizetype maxFontBytes = 64 * 1024 * 1024;
void put16(QByteArray& bytes, qsizetype offset, quint16 value) { qToBigEndian(value, bytes.data() + offset); }
void put32(QByteArray& bytes, qsizetype offset, quint32 value) { qToBigEndian(value, bytes.data() + offset); }
quint32 checksum(const QByteArray& bytes) {
  quint32 sum = 0;
  for (qsizetype i = 0; i < bytes.size(); i += 4) {
    quint32 word = 0;
    for (int j = 0; j < 4; ++j) word = (word << 8) | (i + j < bytes.size() ? static_cast<unsigned char>(bytes[i + j]) : 0);
    sum += word;
  }
  return sum;
}
}  // namespace
QByteArray decodeThemeWebFont(const QByteArray& source) {
  if (source.size() < 4 || source.size() > maxFontBytes) return {};
  if (!source.startsWith("wOFF") && !source.startsWith("wOF2")) return source;
  FT_Library library = nullptr;
  if (FT_Init_FreeType(&library)) return {};
  FT_Face face = nullptr;
  if (FT_New_Memory_Face(library, reinterpret_cast<const FT_Byte*>(source.constData()), static_cast<FT_Long>(source.size()), 0, &face)) {
    FT_Done_FreeType(library);
    return {};
  }
  struct Table {
    quint32 tag;
    QByteArray bytes;
  };
  std::vector<Table> tables;
  FT_ULong count = 0;
  const bool valid = FT_Sfnt_Table_Info(face, 0, nullptr, &count) == 0 && count > 0 && count <= 256;
  qsizetype total = 12 + 16 * count;
  if (valid)
    for (FT_ULong i = 0; i < count; ++i) {
      FT_ULong tag = 0, length = 0;
      if (FT_Sfnt_Table_Info(face, i, &tag, &length) || length > maxFontBytes || total + length + 3 > maxFontBytes) {
        tables.clear();
        break;
      }
      QByteArray bytes(static_cast<qsizetype>(length), Qt::Uninitialized);
      if (FT_Load_Sfnt_Table(face, tag, 0, reinterpret_cast<FT_Byte*>(bytes.data()), &length)) {
        tables.clear();
        break;
      }
      if (tag == FT_MAKE_TAG('h', 'e', 'a', 'd') && bytes.size() >= 12) put32(bytes, 8, 0);
      total += (length + 3) & ~FT_ULong(3);
      tables.push_back({static_cast<quint32>(tag), std::move(bytes)});
    }
  FT_Done_Face(face);
  FT_Done_FreeType(library);
  if (tables.size() != count || tables.empty()) return {};
  std::sort(tables.begin(), tables.end(), [](const auto& a, const auto& b) { return a.tag < b.tag; });
  QByteArray result(total, '\0');
  const bool cff = std::any_of(tables.begin(), tables.end(), [](const auto& t) {
    return t.tag == FT_MAKE_TAG('C', 'F', 'F', ' ') || t.tag == FT_MAKE_TAG('C', 'F', 'F', '2');
  });
  put32(result, 0, cff ? FT_MAKE_TAG('O', 'T', 'T', 'O') : 0x00010000);
  put16(result, 4, static_cast<quint16>(count));
  quint16 power = 1, selector = 0;
  while (power * 2 <= count) {
    power *= 2;
    ++selector;
  }
  put16(result, 6, power * 16);
  put16(result, 8, selector);
  put16(result, 10, count * 16 - power * 16);
  qsizetype offset = 12 + 16 * count, headOffset = -1;
  for (std::size_t i = 0; i < tables.size(); ++i) {
    const auto& table = tables[i];
    const qsizetype entry = 12 + 16 * i;
    put32(result, entry, table.tag);
    put32(result, entry + 4, checksum(table.bytes));
    put32(result, entry + 8, static_cast<quint32>(offset));
    put32(result, entry + 12, static_cast<quint32>(table.bytes.size()));
    std::copy(table.bytes.begin(), table.bytes.end(), result.begin() + offset);
    if (table.tag == FT_MAKE_TAG('h', 'e', 'a', 'd')) headOffset = offset;
    offset += (table.bytes.size() + 3) & ~qsizetype(3);
  }
  if (headOffset < 0 || headOffset + 12 > result.size()) return {};
  put32(result, headOffset + 8, 0xB1B0AFBA - checksum(result));
  return result;
}
}  // namespace muffin

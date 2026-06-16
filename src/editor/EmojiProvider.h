#pragma once

#include <QString>
#include <QVector>

namespace muffin {

// A single emoji mapping: a GitHub-style shortcode (without colons, e.g. "smile") and its glyph.
struct EmojiEntry {
  QString shortcode;
  QString glyph;
};

// Abstract source of emoji completions. The default implementation loads a bundled dataset lazily;
// tests inject a fake implementation so the trigger/popup logic is exercised without the real data.
class EmojiProvider {
public:
  virtual ~EmojiProvider() = default;
  // Entries whose shortcode contains `prefix` (case-insensitive), capped at `max`, ordered by
  // shortest-shortcode-first so the most common emojis surface first.
  virtual QVector<EmojiEntry> matches(const QString& prefix, int max = 50) const = 0;
};

// Default provider backed by the bundled ":/emoji/emoji.txt" resource (one "shortcode<TAB>glyph"
// per line). Loaded lazily on the first query and cached for the process lifetime.
class BundledEmojiProvider : public EmojiProvider {
public:
  QVector<EmojiEntry> matches(const QString& prefix, int max = 50) const override;

private:
  void load() const;
  mutable QVector<EmojiEntry> entries_;
  mutable bool loaded_ = false;
};

}  // namespace muffin

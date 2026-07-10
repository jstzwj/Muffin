#pragma once

#include <QStringView>

namespace muffin {

class PieceTable;

namespace text_stats {

int countWords(QStringView text);
int countWords(const PieceTable& text);

}  // namespace text_stats
}  // namespace muffin

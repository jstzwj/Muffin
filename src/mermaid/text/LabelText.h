#pragma once

#include <QString>
#include <QStringList>

namespace muffin::mermaid::text {

// CSS whitespace collapse for SVG text measurement: any run of [\x09-\x0D\x20] (tab, LF, VT,
// FF, CR, space) becomes a single space — what Chromium's white-space:normal processing leaves
// for the text an SVG <text>/<title> renders. `trimEdges` additionally strips leading/trailing
// whitespace; the interior-only variant serves measurement that must preserve edge spaces.
QString collapsedSvgText(QString value, bool trimEdges = true);

// Split a CSS font-family list: comma-separated, each entry trimmed, surrounding single/double
// quotes stripped, empties dropped.
QStringList cssFontFamilies(const QString& expression);

}  // namespace muffin::mermaid::text

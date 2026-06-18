#pragma once

#include "parser/MarkdownParser.h"

namespace muffin {

// Reads the markdown/* preferences that gate PARSING and maps them to ParseOptions. This is the
// single funnel used at MainWindow startup and after the PreferencesDialog closes. Parse-side
// settings must flow through here (DocumentSession::setParseOptions re-parses on change); purely
// cosmetic / insertion-side markdown/* settings are instead read inline at their use sites.
ParseOptions markdownParseOptions();

}  // namespace muffin

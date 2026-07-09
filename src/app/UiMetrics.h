#pragma once

namespace muffin::ui_metrics {

// Self-painted chrome metrics. The status bar is the first widget converged onto named constants
// (extend as more chrome is tidied). Values are raw logical pixels — Qt's global scale transform
// handles HiDPI for widget geometry, matching the rest of the chrome.
constexpr int kStatusBarHeight = 28;
constexpr int kStatusIconSize = 16;
constexpr int kStatusGap = 18;
constexpr int kStatusPadX = 6;

}  // namespace muffin::ui_metrics

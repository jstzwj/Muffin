#pragma once

#import <AppKit/AppKit.h>

namespace muffin::quicklook {

// Finder previews run in a separate, sandboxed process. Keep input bounded and
// use only cmark and AppKit; do not load the editor, its Qt runtime or settings.
constexpr NSUInteger kMaximumPreviewBytes = 8 * 1024 * 1024;
NSAttributedString* renderMarkdown(NSData* data, NSError** error);
NSData* readPreviewFile(NSURL* url, NSError** error);

} // namespace muffin::quicklook

#import "quicklook/MarkdownPreview.h"
#import "quicklook/PreviewController.h"

#include <cstdio>
#include <cstdlib>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}

id attribute(NSAttributedString* text, NSString* token, NSAttributedStringKey key) {
  const NSRange range = [text.string rangeOfString:token];
  require(range.location != NSNotFound, "rendered text retains expected content");
  return [text attribute:key atIndex:range.location effectiveRange:nullptr];
}

} // namespace

int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    NSString* markdown = @"# 中文标题\n\n正文 **bold** *italic* ~~removed~~ `code` [site](https://example.com) "
                          "[unsafe](javascript:alert) ![remote alt](https://example.com/image.png)\n\n"
                          "- [x] Done\n- [ ] Todo\n  - Nested\n\n"
                          "| Name | Value |\n| --- | ---: |\n| 中文 | 42 |\n\n"
                          "```cpp\nint main() {}\n```\n\n<script>do_not_execute()</script>\n";
    NSError* error = nil;
    NSAttributedString* text =
        muffin::quicklook::renderMarkdown([markdown dataUsingEncoding:NSUTF8StringEncoding], &error);
    require(text && !error, "native Markdown preview renders a mixed document");
    NSFont* heading = attribute(text, @"中文标题", NSFontAttributeName);
    NSFont* body = attribute(text, @"正文", NSFontAttributeName);
    require(heading.pointSize > body.pointSize, "headings retain visual hierarchy");
    NSFont* bold = attribute(text, @"bold", NSFontAttributeName);
    NSFont* italic = attribute(text, @"italic", NSFontAttributeName);
    require((bold.fontDescriptor.symbolicTraits & NSFontDescriptorTraitBold) != 0, "bold is styled");
    require((italic.fontDescriptor.symbolicTraits & NSFontDescriptorTraitItalic) != 0, "italic is styled");
    require(attribute(text, @"removed", NSStrikethroughStyleAttributeName) != nil, "strikethrough is styled");
    require(attribute(text, @"site", NSLinkAttributeName) != nil, "web links remain selectable links");
    require(attribute(text, @"unsafe", NSLinkAttributeName) == nil, "script links cannot launch");
    require(attribute(text, @"remote alt", NSAttachmentAttributeName) == nil, "preview does not fetch remote images");
    require([text.string containsString:@"☑  Done"] && [text.string containsString:@"☐  Todo"],
            "task lists retain checked and unchecked states");
    NSParagraphStyle* cell = attribute(text, @"42", NSParagraphStyleAttributeName);
    require(cell.textBlocks.count == 1 && cell.alignment == NSTextAlignmentRight,
            "GFM tables use native cells and preserve column alignment");
    require([text.string containsString:@"<script>do_not_execute()</script>"], "HTML stays inert source");
    require([text.string containsString:@"int main() {}"], "fenced code remains visible");

    const unsigned char invalid[] = {0xff, 0xff, 0xff};
    error = nil;
    require(!muffin::quicklook::renderMarkdown([NSData dataWithBytes:invalid length:sizeof(invalid)], &error) && error,
            "invalid text produces a recoverable preview error");
    NSMutableData* large = [NSMutableData dataWithLength:muffin::quicklook::kMaximumPreviewBytes + 1];
    error = nil;
    require(!muffin::quicklook::renderMarkdown(large, &error) && error.code == NSFileReadTooLargeError,
            "large input is bounded before parsing");
    NSAttributedString* utf16 =
        muffin::quicklook::renderMarkdown([@"# 中文" dataUsingEncoding:NSUTF16StringEncoding], nullptr);
    require([utf16.string containsString:@"中文"], "UTF-16 BOM text is supported");
    require(muffin::quicklook::renderMarkdown([NSData data], nullptr) != nil, "empty files are valid previews");

    NSURL* file = [[NSURL fileURLWithPath:NSTemporaryDirectory()
                              isDirectory:YES] URLByAppendingPathComponent:NSUUID.UUID.UUIDString];
    require([large writeToURL:file atomically:YES], "create oversized preview fixture");
    error = nil;
    require(!muffin::quicklook::readPreviewFile(file, &error) && error.code == NSFileReadTooLargeError,
            "file loading is bounded as well as parsing");
    auto* longSource = [NSMutableString string];
    for (NSUInteger line = 0; line < 10000; ++line)
      [longSource appendString:@"中文 paragraph **bold**\n"];
    NSData* sourceBytes = [longSource dataUsingEncoding:NSUTF8StringEncoding];
    require([sourceBytes writeToURL:file atomically:YES], "create multi-chunk UTF-8 preview fixture");
    error = nil;
    require([muffin::quicklook::readPreviewFile(file, &error) isEqualToData:sourceBytes] && !error,
            "file loading preserves all bytes through EOF");
    [[NSFileManager defaultManager] removeItemAtURL:file error:nullptr];

    auto* controller = [[MuffinPreviewController alloc] init];
    require([controller conformsToProtocol:@protocol(QLPreviewingController)],
            "extension principal implements Quick Look");
    NSScrollView* scroll = (NSScrollView*)controller.view;
    NSTextView* view = (NSTextView*)scroll.documentView;
    require(!view.editable && view.selectable && scroll.hasVerticalScroller, "preview is read-only and scrollable");

    NSURL* first = [[NSURL fileURLWithPath:NSTemporaryDirectory()
                               isDirectory:YES] URLByAppendingPathComponent:NSUUID.UUID.UUIDString];
    NSURL* second = [[NSURL fileURLWithPath:NSTemporaryDirectory()
                                isDirectory:YES] URLByAppendingPathComponent:NSUUID.UUID.UUIDString];
    require([@"# First" writeToURL:first atomically:YES encoding:NSUTF8StringEncoding error:nullptr] &&
                [@"# Latest" writeToURL:second atomically:YES encoding:NSUTF8StringEncoding error:nullptr],
            "create consecutive preview requests");
    __block NSUInteger completions = 0;
    [controller preparePreviewOfFileAtURL:first
                        completionHandler:^(NSError* error) {
                          require(!error || error.code == NSUserCancelledError,
                                  "superseded request completes or cancels cleanly");
                          ++completions;
                        }];
    [controller preparePreviewOfFileAtURL:second
                        completionHandler:^(NSError* error) {
                          require(!error && NSThread.isMainThread, "latest preview completes on the main thread");
                          ++completions;
                        }];
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:10];
    while (completions < 2 && deadline.timeIntervalSinceNow > 0)
      [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    require(completions == 2 && [view.string containsString:@"Latest"] && ![view.string containsString:@"First"],
            "late callbacks cannot replace the latest document preview");
    [[NSFileManager defaultManager] removeItemAtURL:first error:nullptr];
    [[NSFileManager defaultManager] removeItemAtURL:second error:nullptr];
  }
  return 0;
}

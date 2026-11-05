#import "PreviewController.h"
#import "MarkdownPreview.h"

@interface MuffinPreviewController ()
@property(nonatomic, strong) NSTextView* textView;
@property(nonatomic, strong) dispatch_queue_t renderQueue;
@property(nonatomic, strong) NSProgress* currentProgress;
@property(nonatomic) NSUInteger requestGeneration;
@end

@implementation MuffinPreviewController

- (void)loadView {
  auto* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 800, 640)];
  scroll.hasVerticalScroller = YES;
  scroll.borderType = NSNoBorder;
  scroll.backgroundColor = NSColor.textBackgroundColor;
  auto* text = [[NSTextView alloc] initWithFrame:scroll.contentView.bounds];
  text.editable = NO;
  text.selectable = YES;
  text.allowsUndo = NO;
  text.usesFindPanel = YES;
  text.backgroundColor = NSColor.textBackgroundColor;
  text.textContainerInset = NSMakeSize(28, 24);
  text.verticallyResizable = YES;
  text.horizontallyResizable = NO;
  text.autoresizingMask = NSViewWidthSizable;
  text.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
  text.textContainer.containerSize = NSMakeSize(scroll.contentSize.width, CGFLOAT_MAX);
  text.textContainer.widthTracksTextView = YES;
  scroll.documentView = text;
  self.textView = text;
  self.view = scroll;
  self.preferredContentSize = NSMakeSize(800, 640);
}

- (void)preparePreviewOfFileAtURL:(NSURL*)url completionHandler:(void (^)(NSError* _Nullable))handler {
  // Quick Look owns the calling thread. Keep all view/controller state on the
  // main queue and serialize bounded parsing separately from AppKit layout.
  dispatch_async(dispatch_get_main_queue(), ^{
    if (!self.renderQueue)
      self.renderQueue = dispatch_queue_create("com.muffin-editor.Muffin.quicklook", DISPATCH_QUEUE_SERIAL);
    const NSUInteger generation = ++self.requestGeneration;
    [self.currentProgress cancel];
    NSProgress* progress = [NSProgress progressWithTotalUnitCount:1];
    self.currentProgress = progress;
    [self.textView.textStorage setAttributedString:[[NSAttributedString alloc] initWithString:@""]];
    __weak MuffinPreviewController* weakSelf = self;
    dispatch_async(self.renderQueue, ^{
      @autoreleasepool {
        NSError* error = nil;
        NSData* data = !progress.cancelled ? muffin::quicklook::readPreviewFile(url, &error) : nil;
        NSAttributedString* result =
            data && !progress.cancelled ? muffin::quicklook::renderMarkdown(data, &error) : nil;
        dispatch_async(dispatch_get_main_queue(), ^{
          MuffinPreviewController* controller = weakSelf;
          if (!controller || controller.requestGeneration != generation) {
            handler([NSError errorWithDomain:NSCocoaErrorDomain code:NSUserCancelledError userInfo:nil]);
            return;
          }
          if (result) {
            (void)controller.view;
            [controller.textView.textStorage setAttributedString:result];
            [controller.textView scrollRangeToVisible:NSMakeRange(0, 0)];
          }
          handler(error);
        });
      }
    });
  });
}

@end

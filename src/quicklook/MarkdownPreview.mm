#import "MarkdownPreview.h"

#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>

#include <cstring>
#include <initializer_list>
#include <memory>
#include <mutex>

namespace muffin::quicklook {
namespace {

NSString* string(const char* utf8) {
  return utf8 ? [NSString stringWithUTF8String:utf8] ?: @"" : @"";
}

NSError* readError(NSInteger code) {
  return [NSError errorWithDomain:NSCocoaErrorDomain code:code userInfo:nil];
}

using Attributes = NSDictionary<NSAttributedStringKey, id>*;

class Renderer {
public:
  NSMutableAttributedString* text = [[NSMutableAttributedString alloc] init];
  bool exceededLimit = false;

  void block(cmark_node* node, Attributes inherited, NSUInteger depth = 0, CGFloat indent = 0, NSString* prefix = @"") {
    if (!visit(depth))
      return;
    auto* style = [[NSMutableParagraphStyle alloc] init];
    style.lineSpacing = 4;
    style.paragraphSpacing = 10;
    style.headIndent = indent;
    style.firstLineHeadIndent = indent;
    NSMutableDictionary* attributes = [inherited mutableCopy];
    attributes[NSParagraphStyleAttributeName] = style;
    switch (cmark_node_get_type(node)) {
    case CMARK_NODE_DOCUMENT:
    case CMARK_NODE_ITEM:
      for (auto* child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
        block(child, attributes, depth + 1, indent, prefix);
        prefix = @"";
      }
      break;
    case CMARK_NODE_BLOCK_QUOTE:
      attributes[NSForegroundColorAttributeName] = NSColor.secondaryLabelColor;
      for (auto* child = cmark_node_first_child(node); child; child = cmark_node_next(child))
        block(child, attributes, depth + 1, indent + 20);
      break;
    case CMARK_NODE_LIST: {
      NSInteger number = cmark_node_get_list_start(node);
      for (auto* item = cmark_node_first_child(node); item; item = cmark_node_next(item), ++number) {
        NSString* marker = cmark_node_get_list_type(node) == CMARK_ORDERED_LIST
                               ? [NSString stringWithFormat:@"%ld.  ", (long)number]
                               : @"•  ";
        if (std::strcmp(cmark_node_get_type_string(item), "tasklist") == 0)
          marker = cmark_gfm_extensions_get_tasklist_item_checked(item) ? @"☑  " : @"☐  ";
        block(item, attributes, depth + 1, indent + 24, marker);
      }
      break;
    }
    case CMARK_NODE_HEADING: {
      constexpr CGFloat sizes[] = {28, 24, 21, 18, 16, 15};
      const int level = cmark_node_get_heading_level(node);
      attributes[NSFontAttributeName] = [NSFont systemFontOfSize:sizes[level - 1] weight:NSFontWeightSemibold];
      style.paragraphSpacingBefore = 12;
      [[fallthrough]];
    }
    case CMARK_NODE_PARAGRAPH:
      if (prefix.length) {
        style.firstLineHeadIndent = MAX(0, indent - 24);
        append(prefix, attributes);
      }
      inlines(node, attributes, depth + 1);
      append(@"\n", attributes);
      break;
    case CMARK_NODE_CODE_BLOCK:
    case CMARK_NODE_HTML_BLOCK: {
      attributes[NSFontAttributeName] = [NSFont monospacedSystemFontOfSize:13 weight:NSFontWeightRegular];
      attributes[NSBackgroundColorAttributeName] = NSColor.controlBackgroundColor;
      NSString* literal = string(cmark_node_get_literal(node));
      append(prefix, attributes);
      append(literal, attributes);
      if (![literal hasSuffix:@"\n"])
        append(@"\n", attributes);
      break;
    }
    case CMARK_NODE_THEMATIC_BREAK:
      attributes[NSForegroundColorAttributeName] = NSColor.separatorColor;
      append(@"────────────────────────\n", attributes);
      break;
    default:
      if (std::strcmp(cmark_node_get_type_string(node), "table") == 0)
        table(node, attributes, depth + 1);
      else
        for (auto* child = cmark_node_first_child(node); child; child = cmark_node_next(child))
          block(child, attributes, depth + 1, indent);
      break;
    }
  }

private:
  NSUInteger nodes_ = 0;

  bool visit(NSUInteger depth) {
    if (depth > 64 || ++nodes_ > 50000)
      exceededLimit = true;
    return !exceededLimit;
  }

  void append(NSString* value, Attributes attributes) {
    [text appendAttributedString:[[NSAttributedString alloc] initWithString:value attributes:attributes]];
  }

  void inlines(cmark_node* parent, Attributes inherited, NSUInteger depth) {
    for (auto* node = cmark_node_first_child(parent); node; node = cmark_node_next(node)) {
      if (!visit(depth))
        return;
      NSMutableDictionary* attributes = [inherited mutableCopy];
      const auto type = cmark_node_get_type(node);
      switch (type) {
      case CMARK_NODE_TEXT:
      case CMARK_NODE_HTML_INLINE:
        // HTML is displayed as source, never imported through a web engine.
        append(string(cmark_node_get_literal(node)), attributes);
        break;
      case CMARK_NODE_SOFTBREAK:
        append(@" ", attributes);
        break;
      case CMARK_NODE_LINEBREAK:
        append(@"\u2028", attributes);
        break;
      case CMARK_NODE_CODE:
        attributes[NSFontAttributeName] = [NSFont monospacedSystemFontOfSize:13 weight:NSFontWeightRegular];
        attributes[NSBackgroundColorAttributeName] = NSColor.controlBackgroundColor;
        append(string(cmark_node_get_literal(node)), attributes);
        break;
      case CMARK_NODE_EMPH:
      case CMARK_NODE_STRONG: {
        NSFont* font = attributes[NSFontAttributeName];
        NSFontDescriptorSymbolicTraits traits = font.fontDescriptor.symbolicTraits;
        traits |= type == CMARK_NODE_STRONG ? NSFontDescriptorTraitBold : NSFontDescriptorTraitItalic;
        attributes[NSFontAttributeName] =
            [NSFont fontWithDescriptor:[font.fontDescriptor fontDescriptorWithSymbolicTraits:traits]
                                  size:font.pointSize]
                ?: font;
        inlines(node, attributes, depth + 1);
        break;
      }
      case CMARK_NODE_LINK: {
        NSURL* url = [NSURL URLWithString:string(cmark_node_get_url(node))];
        NSString* scheme = url.scheme.lowercaseString;
        // Only explicit web/mail links can be opened by a user click. Relative
        // paths and arbitrary app/file/script schemes remain ordinary text.
        if ([scheme isEqualToString:@"https"] || [scheme isEqualToString:@"http"] || [scheme isEqualToString:@"mailto"])
          attributes[NSLinkAttributeName] = url;
        inlines(node, attributes, depth + 1);
        break;
      }
      case CMARK_NODE_IMAGE:
        // A Quick Look sandbox is granted the document, not arbitrary siblings.
        // Preserve the alt text without fetching remote or local image URLs.
        attributes[NSForegroundColorAttributeName] = NSColor.secondaryLabelColor;
        append(@"[", attributes);
        inlines(node, attributes, depth + 1);
        append(@"]", attributes);
        break;
      default:
        if (std::strcmp(cmark_node_get_type_string(node), "strikethrough") == 0)
          attributes[NSStrikethroughStyleAttributeName] = @(NSUnderlineStyleSingle);
        if (cmark_node_first_child(node))
          inlines(node, attributes, depth + 1);
        else
          append(string(cmark_node_get_literal(node)), attributes);
        break;
      }
    }
  }

  void table(cmark_node* node, Attributes inherited, NSUInteger depth) {
    const NSUInteger columns = cmark_gfm_extensions_get_table_columns(node);
    if (!columns)
      return;
    auto* table = [[NSTextTable alloc] init];
    table.numberOfColumns = columns;
    table.collapsesBorders = YES;
    [table setContentWidth:100 type:NSTextBlockPercentageValueType];
    const uint8_t* alignments = cmark_gfm_extensions_get_table_alignments(node);
    NSInteger rowIndex = 0;
    for (auto* row = cmark_node_first_child(node); row; row = cmark_node_next(row), ++rowIndex) {
      if (!visit(depth))
        return;
      NSInteger column = 0;
      for (auto* cell = cmark_node_first_child(row); cell; cell = cmark_node_next(cell), ++column) {
        if (!visit(depth + 1))
          return;
        auto* cellBlock = [[NSTextTableBlock alloc] initWithTable:table
                                                      startingRow:rowIndex
                                                          rowSpan:1
                                                   startingColumn:column
                                                       columnSpan:1];
        [cellBlock setWidth:1 type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockBorder];
        [cellBlock setBorderColor:NSColor.separatorColor];
        [cellBlock setWidth:7 type:NSTextBlockAbsoluteValueType forLayer:NSTextBlockPadding];
        NSMutableDictionary* attributes = [inherited mutableCopy];
        NSMutableParagraphStyle* style = [attributes[NSParagraphStyleAttributeName] mutableCopy];
        style.textBlocks = @[ cellBlock ];
        if (alignments && column < (NSInteger)columns) {
          if (alignments[column] == 'c')
            style.alignment = NSTextAlignmentCenter;
          else if (alignments[column] == 'r')
            style.alignment = NSTextAlignmentRight;
        }
        attributes[NSParagraphStyleAttributeName] = style;
        if (cmark_gfm_extensions_get_table_row_is_header(row))
          attributes[NSFontAttributeName] = [NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];
        inlines(cell, attributes, depth + 2);
        append(@"\n", attributes);
      }
    }
    append(@"\n", inherited);
  }
};

} // namespace

NSData* readPreviewFile(NSURL* url, NSError** error) {
  if (!url.isFileURL) {
    if (error)
      *error = readError(NSFileReadUnsupportedSchemeError);
    return nil;
  }
  NSFileHandle* file = [NSFileHandle fileHandleForReadingFromURL:url error:error];
  if (!file)
    return nil;
  auto* data = [NSMutableData data];
  // A successful read may be short. Accumulate through EOF without ever
  // reading an unbounded file into the Quick Look process.
  while (data.length <= kMaximumPreviewBytes) {
    NSData* chunk = [file readDataUpToLength:MIN(64 * 1024, kMaximumPreviewBytes + 1 - data.length) error:error];
    if (!chunk) {
      [file closeAndReturnError:nullptr];
      return nil;
    }
    if (!chunk.length)
      break;
    [data appendData:chunk];
  }
  [file closeAndReturnError:nullptr];
  if (data.length > kMaximumPreviewBytes) {
    if (error)
      *error = readError(NSFileReadTooLargeError);
    return nil;
  }
  return data;
}

NSAttributedString* renderMarkdown(NSData* data, NSError** error) {
  if (!data || data.length > kMaximumPreviewBytes) {
    if (error)
      *error = readError(NSFileReadTooLargeError);
    return nil;
  }
  NSString* source = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
  const auto* bytes = static_cast<const unsigned char*>(data.bytes);
  if (!source && data.length >= 2 && ((bytes[0] == 0xff && bytes[1] == 0xfe) || (bytes[0] == 0xfe && bytes[1] == 0xff)))
    source = [[NSString alloc] initWithData:data encoding:NSUTF16StringEncoding];
  if (!source) {
    if (error)
      *error = readError(NSFileReadInapplicableStringEncodingError);
    return nil;
  }
  if ([source hasPrefix:@"\ufeff"])
    source = [source substringFromIndex:1];
  NSData* utf8 = [source dataUsingEncoding:NSUTF8StringEncoding];
  static std::once_flag registerExtensions;
  std::call_once(registerExtensions, cmark_gfm_core_extensions_ensure_registered);
  std::unique_ptr<cmark_parser, decltype(&cmark_parser_free)> parser(cmark_parser_new(CMARK_OPT_VALIDATE_UTF8),
                                                                     cmark_parser_free);
  for (const char* name : {"table", "strikethrough", "autolink", "tasklist"})
    cmark_parser_attach_syntax_extension(parser.get(), cmark_find_syntax_extension(name));
  cmark_parser_feed(parser.get(), static_cast<const char*>(utf8.bytes), utf8.length);
  std::unique_ptr<cmark_node, decltype(&cmark_node_free)> root(cmark_parser_finish(parser.get()), cmark_node_free);
  Renderer renderer;
  renderer.block(
      root.get(),
      @{NSFontAttributeName : [NSFont systemFontOfSize:15], NSForegroundColorAttributeName : NSColor.labelColor});
  if (renderer.exceededLimit) {
    if (error)
      *error = readError(NSFileReadTooLargeError);
    return nil;
  }
  return [renderer.text copy];
}

} // namespace muffin::quicklook

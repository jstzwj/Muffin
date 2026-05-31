#include "math_extension.h"

#include <chunk.h>
#include <html.h>
#include <houdini.h>
#include <parser.h>
#include <render.h>
#include <string.h>

cmark_node_type CMARK_NODE_INLINE_MATH, CMARK_NODE_MATH_BLOCK;

static int is_line_end(unsigned char c) {
  return c == '\n' || c == '\r';
}

static int is_space(unsigned char c) {
  return c == ' ' || c == '\t' || is_line_end(c);
}

static int is_escaped(cmark_chunk *chunk, int pos) {
  int count = 0;
  int i = pos - 1;
  while (i >= 0 && chunk->data[i] == '\\') {
    count++;
    i--;
  }
  return (count % 2) == 1;
}

static int line_has_only_closing_dollars(unsigned char *input, int len, int offset) {
  int i = offset;
  if (i + 1 >= len || input[i] != '$' || input[i + 1] != '$') {
    return 0;
  }
  i += 2;
  while (i < len && (input[i] == ' ' || input[i] == '\t')) {
    i++;
  }
  return i >= len || is_line_end(input[i]);
}

int cmark_gfm_extensions_math_is_closing(cmark_node *node, unsigned char *input, int len, int first_nonspace) {
  return node && node->type == CMARK_NODE_MATH_BLOCK &&
         line_has_only_closing_dollars(input, len, first_nonspace);
}

static cmark_node *match_inline_math(cmark_syntax_extension *self, cmark_parser *parser,
                                     cmark_node *parent, unsigned char character,
                                     cmark_inline_parser *inline_parser) {
  if (character != '$') {
    return NULL;
  }

  const int start = cmark_inline_parser_get_offset(inline_parser);
  cmark_chunk *chunk = cmark_inline_parser_get_chunk(inline_parser);
  if (start > 0 && chunk->data[start - 1] == '$') {
    return NULL;
  }
  if (start + 2 <= chunk->len && chunk->data[start + 1] == '$') {
    return NULL;
  }
  if (start + 1 >= chunk->len || is_space(chunk->data[start + 1])) {
    return NULL;
  }
  if (is_escaped(chunk, start)) {
    return NULL;
  }

  for (int i = start + 1; i < chunk->len; ++i) {
    if (is_line_end(chunk->data[i])) {
      return NULL;
    }
    if (chunk->data[i] != '$' || is_escaped(chunk, i)) {
      continue;
    }
    if (i + 1 < chunk->len && chunk->data[i + 1] == '$') {
      continue;
    }
    if (i == start + 1 || is_space(chunk->data[i - 1])) {
      continue;
    }

    cmark_chunk tex = cmark_chunk_dup(chunk, start + 1, i - start - 1);
    const char *literal = cmark_chunk_to_cstr(parser->mem, &tex);
    cmark_node *node = cmark_node_new_with_mem(CMARK_NODE_INLINE_MATH, parser->mem);
    cmark_node_set_syntax_extension(node, self);
    cmark_node_set_string_content(node, literal);
    cmark_chunk_free(parser->mem, &tex);
    node->start_line = node->end_line = cmark_inline_parser_get_line(inline_parser);
    node->start_column = cmark_inline_parser_get_column(inline_parser);
    node->end_column = node->start_column + (i - start);
    cmark_inline_parser_set_offset(inline_parser, i + 1);
    return node;
  }

  return NULL;
}

static cmark_node *try_opening_math_block(cmark_syntax_extension *self, int indented,
                                          cmark_parser *parser,
                                          cmark_node *parent_container,
                                          unsigned char *input, int len) {
  if (indented || !line_has_only_closing_dollars(input, len, cmark_parser_get_first_nonspace(parser))) {
    return NULL;
  }

  cmark_node *node = cmark_parser_add_child(
      parser, parent_container, CMARK_NODE_MATH_BLOCK,
      cmark_parser_get_first_nonspace_column(parser) + 1);
  cmark_node_set_syntax_extension(node, self);
  cmark_parser_advance_offset(parser, (char *)input,
                              len - 1 - cmark_parser_get_offset(parser), false);
  return node;
}

static int match_math_block(cmark_syntax_extension *self, cmark_parser *parser,
                            unsigned char *input, int len,
                            cmark_node *container) {
  if (line_has_only_closing_dollars(input, len, cmark_parser_get_first_nonspace(parser))) {
    cmark_parser_advance_offset(parser, (char *)input,
                                len - 1 - cmark_parser_get_offset(parser), false);
    return 0;
  }
  return 1;
}

static const char *get_type_string(cmark_syntax_extension *extension, cmark_node *node) {
  if (node->type == CMARK_NODE_INLINE_MATH) {
    return "inline_math";
  }
  if (node->type == CMARK_NODE_MATH_BLOCK) {
    return "math_block";
  }
  return "<unknown>";
}

static int can_contain(cmark_syntax_extension *extension, cmark_node *node,
                       cmark_node_type child_type) {
  return 0;
}

static int contains_inlines(cmark_syntax_extension *extension, cmark_node *node) {
  return 0;
}

static void commonmark_render(cmark_syntax_extension *extension,
                              cmark_renderer *renderer, cmark_node *node,
                              cmark_event_type ev_type, int options) {
  if (node->type == CMARK_NODE_INLINE_MATH) {
    renderer->out(renderer, node, "$", false, LITERAL);
    renderer->out(renderer, node, cmark_node_get_string_content(node), false, LITERAL);
    renderer->out(renderer, node, "$", false, LITERAL);
  } else if (node->type == CMARK_NODE_MATH_BLOCK && ev_type == CMARK_EVENT_ENTER) {
    renderer->cr(renderer);
    renderer->out(renderer, node, "$$", false, LITERAL);
    renderer->cr(renderer);
    renderer->out(renderer, node, cmark_node_get_string_content(node), false, LITERAL);
    renderer->cr(renderer);
    renderer->out(renderer, node, "$$", false, LITERAL);
    renderer->cr(renderer);
  }
}

static void plaintext_render(cmark_syntax_extension *extension,
                             cmark_renderer *renderer, cmark_node *node,
                             cmark_event_type ev_type, int options) {
  if (node->type == CMARK_NODE_INLINE_MATH) {
    renderer->out(renderer, node, cmark_node_get_string_content(node), false, LITERAL);
  } else if (node->type == CMARK_NODE_MATH_BLOCK && ev_type == CMARK_EVENT_ENTER) {
    renderer->out(renderer, node, cmark_node_get_string_content(node), false, LITERAL);
    renderer->cr(renderer);
  }
}

static void latex_render(cmark_syntax_extension *extension,
                         cmark_renderer *renderer, cmark_node *node,
                         cmark_event_type ev_type, int options) {
  commonmark_render(extension, renderer, node, ev_type, options);
}

static void html_render(cmark_syntax_extension *extension,
                        cmark_html_renderer *renderer, cmark_node *node,
                        cmark_event_type ev_type, int options) {
  if (node->type == CMARK_NODE_INLINE_MATH) {
    if (ev_type == CMARK_EVENT_ENTER) {
      const char *content = cmark_node_get_string_content(node);
      cmark_strbuf_puts(renderer->html, "<span class=\"mfn-inline-math\" data-tex=\"");
      houdini_escape_html0(renderer->html, (const unsigned char *)content,
                           (bufsize_t)strlen(content), 0);
      cmark_strbuf_puts(renderer->html, "\">");
      houdini_escape_html0(renderer->html, (const unsigned char *)content,
                           (bufsize_t)strlen(content), 0);
      cmark_strbuf_puts(renderer->html, "</span>");
    }
  } else if (node->type == CMARK_NODE_MATH_BLOCK && ev_type == CMARK_EVENT_ENTER) {
    const char *content = cmark_node_get_string_content(node);
    cmark_html_render_cr(renderer->html);
    cmark_strbuf_puts(renderer->html, "<div class=\"mfn-math-block\" data-tex=\"");
    houdini_escape_html0(renderer->html, (const unsigned char *)content, (bufsize_t)strlen(content), 0);
    cmark_strbuf_puts(renderer->html, "\">");
    houdini_escape_html0(renderer->html, (const unsigned char *)content, (bufsize_t)strlen(content), 0);
    cmark_strbuf_puts(renderer->html, "</div>\n");
  }
}

cmark_syntax_extension *create_math_extension(void) {
  cmark_syntax_extension *ext = cmark_syntax_extension_new("math");
  cmark_llist *special_chars = NULL;
  cmark_mem *mem = cmark_get_default_mem_allocator();

  CMARK_NODE_INLINE_MATH = cmark_syntax_extension_add_node(1);
  CMARK_NODE_MATH_BLOCK = cmark_syntax_extension_add_node(0);

  cmark_syntax_extension_set_get_type_string_func(ext, get_type_string);
  cmark_syntax_extension_set_open_block_func(ext, try_opening_math_block);
  cmark_syntax_extension_set_match_block_func(ext, match_math_block);
  cmark_syntax_extension_set_match_inline_func(ext, match_inline_math);
  cmark_syntax_extension_set_can_contain_func(ext, can_contain);
  cmark_syntax_extension_set_contains_inlines_func(ext, contains_inlines);
  cmark_syntax_extension_set_commonmark_render_func(ext, commonmark_render);
  cmark_syntax_extension_set_plaintext_render_func(ext, plaintext_render);
  cmark_syntax_extension_set_latex_render_func(ext, latex_render);
  cmark_syntax_extension_set_html_render_func(ext, html_render);

  special_chars = cmark_llist_append(mem, special_chars, (void *)'$');
  cmark_syntax_extension_set_special_inline_chars(ext, special_chars);

  return ext;
}

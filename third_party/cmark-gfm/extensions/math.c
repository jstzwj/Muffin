#include <cmark-gfm-extension_api.h>
#include <parser.h>
#include <render.h>
#include <html.h>
#include <inlines.h>
#include <chunk.h>
#include <string.h>
#include <stdlib.h>

#include "cmark-gfm-math.h"

cmark_node_type CMARK_NODE_MATH_INLINE;
cmark_node_type CMARK_NODE_MATH_DISPLAY;
cmark_node_type CMARK_NODE_MATH_FENCED;

/* ── Opaque data for math display blocks ── */

typedef struct {
  int fence_offset;
  bool single_line;
  cmark_strbuf content;
} node_math_display;

static void opaque_alloc(cmark_syntax_extension *self, cmark_mem *mem,
                          cmark_node *node) {
  if (node->type == CMARK_NODE_MATH_DISPLAY) {
    node_math_display *nd = (node_math_display *)mem->calloc(1, sizeof(node_math_display));
    cmark_strbuf_init(mem, &nd->content, 64);
    node->as.opaque = nd;
  }
}

static void opaque_free(cmark_syntax_extension *self, cmark_mem *mem,
                         cmark_node *node) {
  if (node->type == CMARK_NODE_MATH_DISPLAY && node->as.opaque) {
    node_math_display *nd = (node_math_display *)node->as.opaque;
    cmark_strbuf_free(&nd->content);
    mem->free(nd);
    node->as.opaque = NULL;
  }
}

/* ── Inline $...$ matching (delimiter-based, like strikethrough) ── */

static cmark_node *match_inline_math(cmark_syntax_extension *self,
                                      cmark_parser *parser, cmark_node *parent,
                                      unsigned char character,
                                      cmark_inline_parser *inline_parser) {
  if (character != '$')
    return NULL;

  int left_flanking, right_flanking, punct_before, punct_after;
  int num_dollars = cmark_inline_parser_scan_delimiters(
      inline_parser, 2, '$', &left_flanking, &right_flanking,
      &punct_before, &punct_after);

  if (num_dollars == 0)
    return NULL;

  /* Peek ahead: check for backtick-dollar syntax $`...`$ */
  int pos = cmark_inline_parser_get_offset(inline_parser);
  cmark_chunk *chunk = cmark_inline_parser_get_chunk(inline_parser);
  const unsigned char *data = chunk->data;
  int len = (int)chunk->len;

  if (num_dollars == 1 && pos < len && data[pos] == '`') {
    /* $` syntax: scan forward for closing `$ */
    int start = pos + 1;
    for (int i = start; i < len - 1; i++) {
      if (data[i] == '`' && data[i + 1] == '$') {
        int tex_len = i - start;
        char *tex = (char *)parser->mem->calloc(tex_len + 1, 1);
        memcpy(tex, data + start, tex_len);

        cmark_node *math_node = cmark_node_new_with_mem(CMARK_NODE_MATH_INLINE, parser->mem);
        cmark_node_set_syntax_extension(math_node, self);
        cmark_node_set_literal(math_node, tex);
        parser->mem->free(tex);

        math_node->start_line = math_node->end_line =
            cmark_inline_parser_get_line(inline_parser);
        math_node->start_column =
            cmark_inline_parser_get_column(inline_parser) - 1;
        math_node->end_column = math_node->start_column + 2 + (i - start) + 2;

        cmark_inline_parser_set_offset(inline_parser, i + 2);
        return math_node;
      }
    }
    /* No closing `$ found: fall through to normal $ handling */
  }

  /* Currency heuristic: $ followed by digit with no close $ on same line */
  if (num_dollars == 1 && right_flanking && pos < len &&
      data[pos] >= '0' && data[pos] <= '9') {
    bool found_close = false;
    for (int i = pos + 1; i < len; i++) {
      if (data[i] == '$') {
        found_close = true;
        break;
      }
      if (data[i] == '\n')
        break;
    }
    if (!found_close) {
      /* Treat as plain text (currency) */
      cmark_node *text = cmark_node_new_with_mem(CMARK_NODE_TEXT, parser->mem);
      cmark_node_set_literal(text, "$");
      text->start_line = text->end_line = cmark_inline_parser_get_line(inline_parser);
      text->start_column = cmark_inline_parser_get_column(inline_parser) - 1;
      text->end_column = text->start_column;
      return text;
    }
  }

  /* Create text node for delimiter */
  cmark_node *node = cmark_node_new_with_mem(CMARK_NODE_TEXT, parser->mem);
  char *lit = (char *)parser->mem->calloc(num_dollars + 1, 1);
  memset(lit, '$', num_dollars);
  cmark_node_set_literal(node, lit);
  parser->mem->free(lit);

  node->start_line = node->end_line = cmark_inline_parser_get_line(inline_parser);
  node->start_column = cmark_inline_parser_get_column(inline_parser) - num_dollars;
  node->end_column = node->start_column + num_dollars - 1;

  if (left_flanking || right_flanking) {
    cmark_inline_parser_push_delimiter(inline_parser, '$', left_flanking,
                                       right_flanking, node);
  }

  return node;
}

/* ── Inline $...$ delimiter insertion (opener-closer matching) ── */

static delimiter *insert_inline_math(cmark_syntax_extension *self,
                                     cmark_parser *parser,
                                     cmark_inline_parser *inline_parser,
                                     delimiter *opener, delimiter *closer) {
  delimiter *res = closer->next;
  cmark_node *math_node = opener->inl_text;

  /* Only match single $ to single $ */
  if (opener->inl_text->as.literal.len != closer->inl_text->as.literal.len)
    goto done;

  if (!cmark_node_set_type(math_node, CMARK_NODE_MATH_INLINE))
    goto done;

  cmark_node_set_syntax_extension(math_node, self);

  /* Move content between opener and closer as children */
  cmark_node *tmp = cmark_node_next(opener->inl_text);
  while (tmp) {
    cmark_node *next = cmark_node_next(tmp);
    if (tmp == closer->inl_text)
      break;
    cmark_node_append_child(math_node, tmp);
    tmp = next;
  }

  math_node->end_column =
      closer->inl_text->start_column + closer->inl_text->as.literal.len - 1;
  cmark_node_free(closer->inl_text);

done : {
  delimiter *delim = closer;
  while (delim != NULL && delim != opener) {
    delimiter *tmp_delim = delim->previous;
    cmark_inline_parser_remove_delimiter(inline_parser, delim);
    delim = tmp_delim;
  }
  cmark_inline_parser_remove_delimiter(inline_parser, opener);
}

  return res;
}

/* ── Block $$...$$ opening ── */

static cmark_node *try_opening_math_block(cmark_syntax_extension *self,
                                          int indented, cmark_parser *parser,
                                          cmark_node *parent_container,
                                          unsigned char *input, int len) {
  if (indented)
    return NULL;

  int first_nonspace = cmark_parser_get_first_nonspace(parser);
  int offset = cmark_parser_get_offset(parser);

  /* Must start with $$ at the beginning of the line */
  if (first_nonspace + 1 >= len)
    return NULL;
  if (input[first_nonspace] != '$' || input[first_nonspace + 1] != '$')
    return NULL;

  cmark_mem *mem = parser->mem;
  cmark_node *math_block =
      cmark_node_new_with_mem(CMARK_NODE_MATH_DISPLAY, mem);
  cmark_node_set_syntax_extension(math_block, self);

  /* Initialize opaque data */
  node_math_display *nd =
      (node_math_display *)mem->calloc(1, sizeof(node_math_display));
  cmark_strbuf_init(mem, &nd->content, 64);
  nd->fence_offset = first_nonspace - offset;
  nd->single_line = false;
  math_block->as.opaque = nd;

  math_block->start_line = cmark_parser_get_line_number(parser);
  math_block->start_column = first_nonspace;

  /* Check for closing $$ on the same line */
  int rest = first_nonspace + 2;
  while (rest < len && input[rest] == '$')
    rest++; /* skip extra $ signs */

  /* Trim trailing whitespace */
  int content_end = len;
  while (content_end > rest &&
         (input[content_end - 1] == ' ' || input[content_end - 1] == '\t' ||
          input[content_end - 1] == '\r' || input[content_end - 1] == '\n'))
    content_end--;

  /* Check if there's a closing $$ on this same line */
  int close_pos = -1;
  for (int i = content_end - 1; i >= rest + 2; i--) {
    if (input[i] == '$' && input[i - 1] == '$') {
      close_pos = i - 1;
      break;
    }
  }

  if (close_pos >= 0) {
    /* Single-line: $$ content $$ */
    int content_start = rest;
    int cend = close_pos;
    while (cend > content_start &&
           (input[cend - 1] == ' ' || input[cend - 1] == '\t'))
      cend--;

    cmark_strbuf_put(&nd->content, input + content_start, cend - content_start);
    nd->single_line = true;
    math_block->end_line = cmark_parser_get_line_number(parser);
  } else if (rest < content_end) {
    /* Multi-line: content starts on this line after $$ */
    cmark_strbuf_put(&nd->content, input + rest, content_end - rest);
    cmark_strbuf_putc(&nd->content, '\n');
  }

  cmark_parser_advance_offset(parser, (char *)input, len - offset, false);

  /* Attach to the tree: add as child of parent_container */
  cmark_node_append_child(parent_container, math_block);

  return math_block;
}

/* ── Block $$...$$ continuation ── */

static int math_block_matches(cmark_syntax_extension *self,
                              cmark_parser *parser, unsigned char *input,
                              int len, cmark_node *container) {
  node_math_display *nd = (node_math_display *)container->as.opaque;

  if (nd->single_line)
    return 0;

  int first_nonspace = cmark_parser_get_first_nonspace(parser);
  int offset = cmark_parser_get_offset(parser);

  /* Check for closing $$ */
  if (first_nonspace < len && input[first_nonspace] == '$' &&
      first_nonspace + 1 < len && input[first_nonspace + 1] == '$') {
    /* Verify rest of line is just $ signs and whitespace */
    int pos = first_nonspace + 2;
    while (pos < len && input[pos] == '$')
      pos++;
    while (pos < len && (input[pos] == ' ' || input[pos] == '\t'))
      pos++;
    if (pos >= len || input[pos] == '\r' || input[pos] == '\n') {
      /* Closing fence found */
      container->end_line = cmark_parser_get_line_number(parser);
      cmark_parser_advance_offset(parser, (char *)input, len - offset,
                                  false);
      return 0;
    }
  }

  /* Accumulate content */
  int start = offset;
  int i = nd->fence_offset;
  while (i > 0 && start < len &&
         (input[start] == ' ' || input[start] == '\t')) {
    start++;
    i--;
  }

  int line_len = len;
  while (line_len > start &&
         (input[line_len - 1] == '\r' || input[line_len - 1] == '\n'))
    line_len--;

  if (line_len > start) {
    cmark_strbuf_put(&nd->content, input + start, line_len - start);
    cmark_strbuf_putc(&nd->content, '\n');
  } else {
    cmark_strbuf_putc(&nd->content, '\n');
  }

  cmark_parser_advance_offset(parser, (char *)input,
                              len - cmark_parser_get_offset(parser), false);
  return 1;
}

/* ── Finalize math display: convert accumulated content to text child ── */

static void finalize_math_display(cmark_syntax_extension *self,
                                   cmark_parser *parser, cmark_node *node) {
  if (node->type != CMARK_NODE_MATH_DISPLAY || !node->as.opaque)
    return;

  node_math_display *nd = (node_math_display *)node->as.opaque;

  if (nd->content.size > 0) {
    /* Remove trailing newline */
    if (nd->content.size > 0 && nd->content.ptr[nd->content.size - 1] == '\n')
      nd->content.size--;

    if (nd->content.size > 0) {
      cmark_node *text = cmark_node_new_with_mem(CMARK_NODE_TEXT, parser->mem);
      char *content_str = cmark_strbuf_detach(&nd->content);
      cmark_node_set_literal(text, content_str);
      cmark_node_append_child(node, text);
    }
  }
}

/* ── Post-process: convert ```math code blocks to MATH_FENCED ── */

static cmark_node *postprocess_math(cmark_syntax_extension *self,
                                    cmark_parser *parser, cmark_node *root) {
  cmark_iter *iter = cmark_iter_new(root);
  cmark_event_type ev_type;

  while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    cmark_node *node = cmark_iter_get_node(iter);
    if (ev_type == CMARK_EVENT_ENTER) {
      if (node->type == CMARK_NODE_CODE_BLOCK) {
        /* Check if info string is "math" */
        if (node->as.code.info.len >= 4 &&
            strncmp(node->as.code.info.data, "math", 4) == 0 &&
            (node->as.code.info.len == 4 ||
             node->as.code.info.data[4] == '\0')) {
          cmark_node_set_type(node, CMARK_NODE_MATH_FENCED);
          cmark_node_set_syntax_extension(node, self);
        }
      } else if (node->type == CMARK_NODE_MATH_DISPLAY && node->as.opaque) {
        /* Finalize: convert accumulated content to text child */
        finalize_math_display(self, parser, node);
      }
    }
  }

  cmark_iter_free(iter);
  return root;
}

/* ── Type string ── */

static const char *get_type_string(cmark_syntax_extension *extension,
                                   cmark_node *node) {
  if (node->type == CMARK_NODE_MATH_INLINE)
    return "math_inline";
  if (node->type == CMARK_NODE_MATH_DISPLAY)
    return "math_display";
  if (node->type == CMARK_NODE_MATH_FENCED)
    return "math_fenced";
  return "<unknown>";
}

/* ── Containment rules ── */

static int can_contain(cmark_syntax_extension *extension, cmark_node *node,
                       cmark_node_type child_type) {
  if (node->type == CMARK_NODE_MATH_INLINE)
    return CMARK_NODE_TYPE_INLINE_P(child_type);
  if (node->type == CMARK_NODE_MATH_DISPLAY ||
      node->type == CMARK_NODE_MATH_FENCED)
    return child_type == CMARK_NODE_TEXT;
  return false;
}

static int contains_inlines(cmark_syntax_extension *extension,
                            cmark_node *node) {
  return node->type == CMARK_NODE_MATH_INLINE;
}

/* ── HTML renderer ── */

static void html_render(cmark_syntax_extension *extension,
                        cmark_html_renderer *renderer, cmark_node *node,
                        cmark_event_type ev_type, int options) {
  bool entering = (ev_type == CMARK_EVENT_ENTER);

  if (node->type == CMARK_NODE_MATH_INLINE) {
    if (entering) {
      cmark_strbuf_puts(renderer->html, "<span class=\"math math-inline\">");
      /* Collect all text content */
      cmark_iter *iter = cmark_iter_new(node);
      cmark_event_type ev;
      while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *child = cmark_iter_get_node(iter);
        if (ev == CMARK_EVENT_ENTER && child->type == CMARK_NODE_TEXT) {
          cmark_strbuf_put(renderer->html, child->as.literal.data,
                           child->as.literal.len);
        }
      }
      cmark_iter_free(iter);
      cmark_strbuf_puts(renderer->html, "</span>");
    }
  } else if (node->type == CMARK_NODE_MATH_DISPLAY ||
             node->type == CMARK_NODE_MATH_FENCED) {
    if (entering) {
      cmark_strbuf_puts(renderer->html, "<div class=\"math math-display\">");
      cmark_iter *iter = cmark_iter_new(node);
      cmark_event_type ev;
      while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *child = cmark_iter_get_node(iter);
        if (ev == CMARK_EVENT_ENTER && child->type == CMARK_NODE_TEXT) {
          cmark_strbuf_put(renderer->html, child->as.literal.data,
                           child->as.literal.len);
        }
      }
      cmark_iter_free(iter);
      cmark_strbuf_puts(renderer->html, "</div>\n");
    }
  }
}

/* ── CommonMark renderer (round-trip) ── */

static void commonmark_render(cmark_syntax_extension *extension,
                              cmark_renderer *renderer, cmark_node *node,
                              cmark_event_type ev_type, int options) {
  bool entering = (ev_type == CMARK_EVENT_ENTER);

  if (node->type == CMARK_NODE_MATH_INLINE) {
    if (entering) {
      renderer->out(renderer, node, "$", false, LITERAL);
    } else {
      renderer->out(renderer, node, "$", false, LITERAL);
    }
  } else if (node->type == CMARK_NODE_MATH_DISPLAY) {
    if (entering) {
      renderer->cr(renderer);
      renderer->out(renderer, node, "$$", false, LITERAL);
      renderer->cr(renderer);
    } else {
      renderer->cr(renderer);
      renderer->out(renderer, node, "$$", false, LITERAL);
      renderer->cr(renderer);
    }
  } else if (node->type == CMARK_NODE_MATH_FENCED) {
    if (entering) {
      renderer->cr(renderer);
      renderer->out(renderer, node, "```math", false, LITERAL);
      renderer->cr(renderer);
    } else {
      renderer->out(renderer, node, "```", false, LITERAL);
      renderer->cr(renderer);
    }
  }
}

/* ── LaTeX renderer ── */

static void latex_render(cmark_syntax_extension *extension,
                         cmark_renderer *renderer, cmark_node *node,
                         cmark_event_type ev_type, int options) {
  bool entering = (ev_type == CMARK_EVENT_ENTER);
  if (node->type == CMARK_NODE_MATH_INLINE) {
    if (entering)
      renderer->out(renderer, node, "$", false, LITERAL);
    else
      renderer->out(renderer, node, "$", false, LITERAL);
  } else {
    if (entering) {
      renderer->cr(renderer);
      renderer->out(renderer, node, "$$", false, LITERAL);
      renderer->cr(renderer);
    } else {
      renderer->cr(renderer);
      renderer->out(renderer, node, "$$", false, LITERAL);
      renderer->cr(renderer);
    }
  }
}

/* ── Plaintext renderer ── */

static void plaintext_render(cmark_syntax_extension *extension,
                             cmark_renderer *renderer, cmark_node *node,
                             cmark_event_type ev_type, int options) {
  /* Passthrough: default text rendering handles child text nodes */
}

/* ── Extension registration ── */

cmark_syntax_extension *create_math_extension(void) {
  cmark_syntax_extension *ext = cmark_syntax_extension_new("math");
  cmark_mem *mem = cmark_get_default_mem_allocator();
  cmark_llist *special_chars = NULL;

  /* Register node types */
  CMARK_NODE_MATH_INLINE = cmark_syntax_extension_add_node(1);  /* inline */
  CMARK_NODE_MATH_DISPLAY = cmark_syntax_extension_add_node(0); /* block */
  CMARK_NODE_MATH_FENCED = cmark_syntax_extension_add_node(0);  /* block */

  /* Inline $ parsing */
  cmark_syntax_extension_set_match_inline_func(ext, match_inline_math);
  cmark_syntax_extension_set_inline_from_delim_func(ext, insert_inline_math);

  /* Block $$ parsing */
  cmark_syntax_extension_set_open_block_func(ext, try_opening_math_block);
  cmark_syntax_extension_set_match_block_func(ext, math_block_matches);

  /* ```math conversion */
  cmark_syntax_extension_set_postprocess_func(ext, postprocess_math);

  /* Block finalization */
  /* cmark doesn't have a set_finalize_func in the extension API,
     so we handle finalization in postprocess_math instead */

  /* Special character: $ */
  special_chars = cmark_llist_append(mem, special_chars, (void *)(size_t)'$');
  cmark_syntax_extension_set_special_inline_chars(ext, special_chars);

  /* Mark as emphasis so $ is added to SKIP_CHARS */
  cmark_syntax_extension_set_emphasis(ext, 1);

  /* Callbacks */
  cmark_syntax_extension_set_get_type_string_func(ext, get_type_string);
  cmark_syntax_extension_set_can_contain_func(ext, can_contain);
  cmark_syntax_extension_set_contains_inlines_func(ext, contains_inlines);

  /* Opaque data for MATH_DISPLAY */
  cmark_syntax_extension_set_opaque_alloc_func(ext, opaque_alloc);
  cmark_syntax_extension_set_opaque_free_func(ext, opaque_free);

  /* Renderers */
  cmark_syntax_extension_set_html_render_func(ext, html_render);
  cmark_syntax_extension_set_commonmark_render_func(ext, commonmark_render);
  cmark_syntax_extension_set_latex_render_func(ext, latex_render);
  cmark_syntax_extension_set_plaintext_render_func(ext, plaintext_render);

  return ext;
}

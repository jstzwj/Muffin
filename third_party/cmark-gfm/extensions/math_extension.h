#ifndef CMARK_GFM_MATH_EXTENSION_H
#define CMARK_GFM_MATH_EXTENSION_H

#include "cmark-gfm-core-extensions.h"

extern cmark_node_type CMARK_NODE_INLINE_MATH, CMARK_NODE_MATH_BLOCK;

cmark_syntax_extension *create_math_extension(void);

#endif

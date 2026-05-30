#ifndef CMARK_GFM_MATH_EXT_H
#define CMARK_GFM_MATH_EXT_H

#include "cmark-gfm-extension_api.h"

#ifdef __cplusplus
extern "C" {
#endif

extern cmark_node_type CMARK_NODE_MATH_INLINE;
extern cmark_node_type CMARK_NODE_MATH_DISPLAY;
extern cmark_node_type CMARK_NODE_MATH_FENCED;

cmark_syntax_extension *create_math_extension(void);

#ifdef __cplusplus
}
#endif

#endif

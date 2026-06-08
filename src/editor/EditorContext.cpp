#include "editor/EditorContext.h"

#include "editor/BlockEditContext.h"

namespace muffin {

BlockEditContextResolver EditorContext::contextResolver() const {
  return BlockEditContextResolver(session, selection);
}

}  // namespace muffin

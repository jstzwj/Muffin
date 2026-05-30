#pragma once
#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>

extern "C" {
#include <strikethrough.h>
#include <table.h>
#include <tasklist.h>
}

#include <QString>
#include <QRect>

namespace Muffin {

struct SourceRange {
    int startLine = 0;
    int startColumn = 0;
    int endLine = 0;
    int endColumn = 0;
};

class AstNode {
public:
    AstNode() : m_node(nullptr) {}
    explicit AstNode(cmark_node* node) : m_node(node) {}

    bool isNull() const { return !m_node; }
    cmark_node* cmarkNode() const { return m_node; }

    cmark_node_type type() const { return cmark_node_get_type(m_node); }
    const char* typeString() const { return cmark_node_get_type_string(m_node); }

    QString literal() const {
        const char* lit = cmark_node_get_literal(m_node);
        return lit ? QString::fromUtf8(lit) : QString();
    }

    int headingLevel() const { return cmark_node_get_heading_level(m_node); }

    QString url() const {
        const char* u = cmark_node_get_url(m_node);
        return u ? QString::fromUtf8(u) : QString();
    }

    QString title() const {
        const char* t = cmark_node_get_title(m_node);
        return t ? QString::fromUtf8(t) : QString();
    }

    QString fenceInfo() const {
        const char* f = cmark_node_get_fence_info(m_node);
        return f ? QString::fromUtf8(f) : QString();
    }

    cmark_list_type listType() const { return cmark_node_get_list_type(m_node); }
    int listStart() const { return cmark_node_get_list_start(m_node); }
    cmark_delim_type listDelim() const { return cmark_node_get_list_delim(m_node); }

    SourceRange sourceRange() const {
        return {
            cmark_node_get_start_line(m_node),
            cmark_node_get_start_column(m_node),
            cmark_node_get_end_line(m_node),
            cmark_node_get_end_column(m_node)
        };
    }

    bool isBlock() const { return (type() & CMARK_NODE_TYPE_MASK) == CMARK_NODE_TYPE_BLOCK; }
    bool isInline() const { return (type() & CMARK_NODE_TYPE_MASK) == CMARK_NODE_TYPE_INLINE; }

    AstNode firstChild() const { return AstNode(cmark_node_first_child(m_node)); }
    AstNode lastChild() const { return AstNode(cmark_node_last_child(m_node)); }
    AstNode next() const { return AstNode(cmark_node_next(m_node)); }
    AstNode previous() const { return AstNode(cmark_node_previous(m_node)); }
    AstNode parent() const { return AstNode(cmark_node_parent(m_node)); }

    // GFM extensions
    int tableColumns() const { return cmark_gfm_extensions_get_table_columns(m_node); }
    bool isTableRowHeader() const { return cmark_gfm_extensions_get_table_row_is_header(m_node); }
    bool isTasklistItem() const { return strcmp(typeString(), "tasklist") == 0; }
    bool isTasklistChecked() const { return cmark_gfm_extensions_get_tasklist_item_checked(m_node); }

private:
    cmark_node* m_node;
};

} // namespace Muffin

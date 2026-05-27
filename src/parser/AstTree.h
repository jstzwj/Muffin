#pragma once
#include "AstNode.h"

namespace Muffin {

class AstTree {
public:
    AstTree() : m_root(nullptr) {}
    explicit AstTree(cmark_node* root) : m_root(root) {}

    ~AstTree() { if (m_root) cmark_node_free(m_root); }

    AstTree(AstTree&& other) noexcept : m_root(other.m_root) { other.m_root = nullptr; }
    AstTree& operator=(AstTree&& other) noexcept {
        if (this != &other) {
            if (m_root) cmark_node_free(m_root);
            m_root = other.m_root;
            other.m_root = nullptr;
        }
        return *this;
    }

    AstTree(const AstTree&) = delete;
    AstTree& operator=(const AstTree&) = delete;

    bool isNull() const { return !m_root; }
    AstNode root() const { return AstNode(m_root); }

private:
    cmark_node* m_root;
};

} // namespace Muffin

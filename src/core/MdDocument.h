#pragma once

#include "MdNode.h"
#include <QHash>
#include <QObject>
#include <memory>

struct cmark_node;

namespace Md {

struct CmarkNodeDeleter {
    void operator()(cmark_node *n) const;
};

using CmarkNodePtr = std::unique_ptr<cmark_node, CmarkNodeDeleter>;

class MdDocument : public QObject {
    Q_OBJECT
public:
    explicit MdDocument(QObject *parent = nullptr);
    ~MdDocument();

    bool loadFromMarkdown(const QString &markdown);
    QString toMarkdown() const;

    MdNode *root() const { return m_root.get(); }
    cmark_node *cmarkRoot() const { return m_cmarkRoot.get(); }
    MdNode *nodeById(NodeId id) const;

signals:
    void documentReset();

private:
    std::unique_ptr<MdNode> m_root;
    CmarkNodePtr m_cmarkRoot;
    QHash<NodeId, MdNode *> m_nodeIndex;
};

} // namespace Md

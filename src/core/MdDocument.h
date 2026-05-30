#pragma once

#include "MdNode.h"
#include <QHash>
#include <QObject>
#include <memory>

namespace Md {

class MdDocument : public QObject {
    Q_OBJECT
public:
    explicit MdDocument(QObject *parent = nullptr);

    bool loadFromMarkdown(const QString &markdown);
    QString toMarkdown() const;

    MdNode *root() const { return m_root.get(); }
    MdNode *nodeById(NodeId id) const;

signals:
    void documentReset();

private:
    std::unique_ptr<MdNode> m_root;
    QHash<NodeId, MdNode *> m_nodeIndex;
    NodeId m_nextId = 1;
};

} // namespace Md

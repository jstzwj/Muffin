#pragma once

#include "parser/SourceSpan.h"
#include "renderer/RenderSourceMap.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace Muffin {

struct RenderUpdateRequest {
    QVector<MarkdownNodeId> nodeIds;
    SourceSpan editedSource;
    RenderSpan::Kind preferredKind = RenderSpan::Kind::Unsupported;
    QString reason;
};

struct RenderUpdateBatch {
    QVector<MarkdownNodeId> nodeIds;
    SourceSpan combinedEditedSource;
    RenderSpan::Kind preferredKind = RenderSpan::Kind::Unsupported;
    QStringList reasons;
};

class RenderUpdateQueue {
public:
    void enqueue(RenderUpdateRequest request);
    bool isEmpty() const { return m_requests.isEmpty(); }
    int size() const { return m_requests.size(); }
    void clear() { m_requests.clear(); }
    RenderUpdateBatch drain();

private:
    QVector<RenderUpdateRequest> m_requests;
};

} // namespace Muffin

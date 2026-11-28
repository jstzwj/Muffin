#pragma once

#include "render/CssFormattingContext.h"
#include "render/LayoutResources.h"
#include "document/MarkdownNode.h"
#include "theme/RenderTheme.h"

#include <optional>

namespace muffin {

// Per-document formatting reuse. Signatures, resource generations, measurements
// and observed solver inputs share one pass lifecycle and one invalidation policy.
class BlockFormattingCache {
public:
  struct Measurement {
    QByteArray signature;
    CssIntrinsicMetrics intrinsic;
    bool intrinsicValid = false;
    qreal containingWidth = -1;
    std::shared_ptr<CssMeasurementCache> measurements = std::make_shared<CssMeasurementCache>();
    quint64 pass = 0;
    LayoutResourceDependencies resources;
  };

  struct SolveResult {
    CssFormattingResult result;
    bool reused = false;
  };

  void beginPass(bool incremental, QSet<NodeId> dirty);
  // What every signature depends on besides the text of the block itself. Signatures kept from an
  // earlier pass hold for this one while that is unchanged and the theme kept its styles.
  void beginKept(const QByteArray& epoch, bool stylesKept);
  std::optional<QByteArray> keptSignature(NodeId id, const QString& source, const QString& literal, quint64 counters,
                                          const QPair<QString, QString>& headingText);
  void keepSignature(NodeId id, const QString& source, const QString& literal, quint64 counters,
                     const QPair<QString, QString>& headingText, const QByteArray& signature);
  QPair<quint64, quint64> finishPass();
  std::optional<QByteArray> signature(NodeId id) const;
  void rememberSignature(NodeId id, QByteArray value);
  const QByteArray& themeSignature(const RenderTheme& theme);
  bool isDirty(NodeId id) const { return dirty_.contains(id); }
  quint64 pass() const { return pass_; }
  std::shared_ptr<Measurement> measurement(NodeId id, const QByteArray& signature);
  SolveResult solve(NodeId id, const ThemeElementStyle& style, const std::vector<CssFormattingItem>& items, qreal width,
                    qreal height, const CssGridInheritance& inherited = {});

private:
  QHash<NodeId, std::shared_ptr<Measurement>> measurements_;
  struct Solution {
    QByteArray inputs;
    CssFormattingResult result;
    std::vector<CssMeasurementObservation> observations;
    quint64 pass = 0;
  };
  QHash<NodeId, std::vector<std::shared_ptr<Solution>>> solutions_;

  QHash<NodeId, QByteArray> signatures_;
  struct Kept {
    QByteArray signature;
    QString source, literal;
    quint64 counters = 0;
    QPair<QString, QString> headingText;
  };
  QHash<NodeId, Kept> kept_;
  QByteArray keptEpoch_;
  QSet<NodeId> dirty_;
  quint64 pass_ = 0;
  QByteArray themeSignature_;
  std::shared_ptr<const CssThemeSheet> signatureSheet_;
  quint64 signatureRevision_ = 0;
};

} // namespace muffin

#include "render/BlockFormattingCache.h"
#include "diagnostics/ScopedPerfProbe.h"
#include "theme/CssThemeParser.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <algorithm>
#include <utility>

namespace muffin {
namespace {
Q_LOGGING_CATEGORY(formattingDetailPerf, "muffin.perf.detail", QtWarningMsg)
}

void BlockFormattingCache::beginPass(bool incremental, QSet<NodeId> dirty) {
  ++pass_;
  signatures_.clear();
  dirty_ = std::move(dirty);
  if (!incremental) {
    measurements_.clear();
    solutions_.clear();
  }
  for (auto& entry : measurements_)
    entry->measurements->hits = entry->measurements->misses = 0;
}

std::optional<QByteArray> BlockFormattingCache::signature(NodeId id) const {
  const auto it = signatures_.constFind(id);
  return it == signatures_.cend() ? std::nullopt : std::optional<QByteArray>(*it);
}

void BlockFormattingCache::rememberSignature(NodeId id, QByteArray value) {
  signatures_.insert(id, std::move(value));
}

const QByteArray& BlockFormattingCache::themeSignature(const RenderTheme& theme) {
  diag::ScopedPerfProbe perf("formatting.themeSignature", formattingDetailPerf());
  const auto sheet = theme.documentStyleSheet();
  const quint64 revision = sheet ? sheet->revision() : 0;
  if (signatureSheet_ != sheet || signatureRevision_ != revision) {
    signatureSheet_ = sheet;
    signatureRevision_ = revision;
    themeSignature_.clear();
  }
  if (themeSignature_.isEmpty()) {
    diag::ScopedPerfProbe hashPerf("formatting.hashTheme", formattingDetailPerf());
    QByteArray sheetData;
    QDataStream sheetStream(&sheetData, QIODevice::WriteOnly);
    if (sheet) {
      sheetStream << quint64(sheet->rules().size());
      for (const auto& rule : sheet->rules()) {
        sheetStream << rule.selectors << rule.mediaQueries << rule.supportQueries << rule.darkScope << rule.userOverride
                    << quint64(rule.declarations.size());
        for (const auto& declaration : rule.declarations)
          sheetStream << declaration.property << declaration.value << declaration.important;
      }
      auto variables = sheet->variables().keys();
      variables.sort();
      sheetStream << quint64(variables.size());
      for (const auto& variable : variables)
        sheetStream << variable << sheet->variables().value(variable);
      sheetStream << quint64(sheet->fontFaces().size());
      for (const auto& face : sheet->fontFaces())
        sheetStream << face.family << face.srcPath << face.weight << face.style;
    }
    themeSignature_ = QCryptographicHash::hash(sheetData, QCryptographicHash::Sha256);
  }
  return themeSignature_;
}

std::shared_ptr<BlockFormattingCache::Measurement> BlockFormattingCache::measurement(NodeId id,
                                                                                     const QByteArray& signature) {
  auto& entry = measurements_[id];
  if (!entry || signature.isEmpty() || entry->signature != signature ||
      !LayoutResources::instance().matches(entry->resources, false)) {
    entry = std::make_shared<Measurement>();
    entry->signature = signature;
  }
  entry->pass = pass_;
  return entry;
}

QPair<quint64, quint64> BlockFormattingCache::finishPass() {
  QPair<quint64, quint64> stats;
  for (auto it = measurements_.begin(); it != measurements_.end();) {
    if ((*it)->pass != pass_)
      it = measurements_.erase(it);
    else {
      stats.first += (*it)->measurements->hits;
      stats.second += (*it)->measurements->misses;
      ++it;
    }
  }
  for (auto it = solutions_.begin(); it != solutions_.end();) {
    if (it.key().isValid() && !signatures_.contains(it.key()))
      it = solutions_.erase(it);
    else
      ++it;
  }
  return stats;
}

BlockFormattingCache::SolveResult BlockFormattingCache::solve(NodeId id, const ThemeElementStyle& style,
                                                              const std::vector<CssFormattingItem>& items, qreal width,
                                                              qreal height, const CssGridInheritance& inherited) {
  QByteArray inputs;
  QDataStream stream(&inputs, QIODevice::WriteOnly);
  stream << style.fingerprint << themeSignature_ << cssMeasureKey({width, width, height, height, inherited});
  QHash<QString, const CssFormattingItem*> byIdentity;
  const auto collect = [&](const auto& self, const std::vector<CssFormattingItem>& children) -> void {
    stream << quint64(children.size());
    for (const auto& child : children) {
      byIdentity.insert(child.measurementIdentity, &child);
      stream << child.measurementIdentity << child.style.fingerprint << child.intrinsic.minContent
             << child.intrinsic.maxContent << bool(child.naturalSize);
      if (child.naturalSize)
        stream << *child.naturalSize;
      if (child.children)
        self(self, *child.children);
      else
        stream << quint64(0);
    }
  };
  collect(collect, items);
  const auto candidates = solutions_.value(id);
  for (const auto& cached : candidates)
    if (cached->inputs == inputs) {
      bool unchanged = true;
      // Replay the exact constraints used by the previous solution. Content or
      // resource changes only propagate when their measured contribution changes.
      for (const auto& observation : cached->observations) {
        const auto* item = byIdentity.value(observation.identity, nullptr);
        if (!item || measureCssItem(*item, observation.request) != observation.value) {
          unchanged = false;
          break;
        }
      }
      if (unchanged) {
        cached->pass = pass_;
        return {cached->result, true};
      }
    }
  CssMeasurementTrace trace;
  auto result = layoutFormattingItems(style, items, width, height, 1, inherited);
  auto entry = std::make_shared<Solution>();
  entry->inputs = inputs;
  entry->result = result;
  entry->observations = std::move(trace.observations);
  entry->pass = pass_;
  auto& solutions = solutions_[id];
  std::erase_if(solutions, [&](const auto& old) { return old->inputs == inputs; });
  if (solutions.size() >= 16)
    solutions.erase(solutions.begin());
  solutions.push_back(std::move(entry));
  return {std::move(result), false};
}

} // namespace muffin

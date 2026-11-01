#include "theme/DocumentCounterCache.h"

#include <algorithm>
#include <limits>

#include "diagnostics/ThemePerformance.h"

namespace muffin {
void DocumentCounterCache::reuseSemanticPool(const DocumentCounterCache& seed) {
  semanticReferences_ = seed.semanticReferences_;
}
QByteArray DocumentCounterCache::retainSemantic(QByteArray value) {
  auto found = semanticReferences_.find(value);
  if (found == semanticReferences_.end())
    found = semanticReferences_.insert(value, 0);
  ++found.value();
  return found.key();
}
void DocumentCounterCache::releaseSemantic(const QByteArray& value) {
  const auto found = semanticReferences_.find(value);
  if (found != semanticReferences_.end() && --found.value() == 0)
    semanticReferences_.erase(found);
}
DocumentCounterStats DocumentCounterCache::stats() const {
  DocumentCounterStats result;
  result.blocks = blocks.size();
  result.semanticBuilds = semanticBuilds;
  result.candidateScans = candidateScans;
  result.semanticShapes = semanticReferences_.size();
  for (auto it = semanticReferences_.cbegin(); it != semanticReferences_.cend(); ++it) {
    result.semanticBytes += it.key().size();
    result.unsharedSemanticBytes += it.key().size() * it.value();
  }
  result.memoizedBlocks = memo_.size();
  for (const auto& bucket : statePool_)
    for (const auto& state : bucket)
      if (!state.expired())
        ++result.sharedStates;
  return result;
}
void DocumentCounterCache::evaluate() {
  auto task = evaluateSteps();
  task.resumeFor(-1);
}

CooperativeTask DocumentCounterCache::evaluateSteps() {
  struct Instance {
    int depth, value;
  };
  using State = QHash<QString, QVector<std::shared_ptr<Instance>>>;
  for (auto it = statePool_.begin(); it != statePool_.end();) {
    std::erase_if(it.value(), [](const auto& value) { return value.expired(); });
    if (it->empty())
      it = statePool_.erase(it);
    else
      ++it;
  }
  const auto snapshot = [&](const State& state) -> std::shared_ptr<const Snapshot> {
    Snapshot result;
    quint64 hash = 0;
    for (auto it = state.cbegin(); it != state.cend(); ++it) {
      quint64 chainHash = qHash(it.key());
      for (const auto& instance : it.value()) {
        result[it.key()].append({instance->depth, instance->value});
        chainHash = qHashMulti(size_t(chainHash), instance->depth, instance->value);
      }
      hash += chainHash;
    }
    auto& bucket = statePool_[hash];
    for (const auto& weak : bucket)
      if (const auto value = weak.lock(); value && *value == result)
        return value;
    auto value = std::make_shared<const Snapshot>(std::move(result));
    bucket.push_back(value);
    return value;
  };
  const auto restore = [](State& state, const Snapshot& output) {
    State restored;
    for (auto it = output.cbegin(); it != output.cend(); ++it) {
      const auto previous = state.value(it.key());
      auto& chain = restored[it.key()];
      for (qsizetype i = 0; i < it->size(); ++i) {
        const auto value = it->at(i);
        auto instance = i < previous.size() && previous[i]->depth == value.depth
                            ? previous[i]
                            : std::make_shared<Instance>(Instance{value.depth, value.value});
        instance->value = value.value;
        chain.append(std::move(instance));
      }
    }
    state = std::move(restored);
  };
  const auto apply = [](const CounterProgram& program, State& state, int depth) {
    for (int op = 0; op < 3; ++op)
      for (const auto& [name, value] : program.operations[op]) {
        auto& chain = state[name];
        if (op == 0) {
          while (!chain.isEmpty() && chain.back()->depth >= depth)
            chain.pop_back();
          chain.append(std::make_shared<Instance>(Instance{depth, value}));
        } else {
          if (chain.isEmpty())
            chain.append(std::make_shared<Instance>(Instance{depth, 0}));
          chain.back()->value =
              op == 2 ? value
                      : int(std::clamp<qint64>(qint64(chain.back()->value) + value, std::numeric_limits<int>::min(),
                                               std::numeric_limits<int>::max()));
        }
      }
  };
  chains.clear();
  fingerprints.clear();
  text.clear();
  Memo* current = nullptr;
  const auto record = [&](const CounterProgram& program, const State& state) {
    if (!std::any_of(program.content.begin(), program.content.end(), [](const auto& token) {
          return token.kind == ContentToken::Kind::Counter || token.kind == ContentToken::Kind::Counters;
        }))
      return;
    QHash<QString, QVector<int>> values;
    for (auto it = state.cbegin(); it != state.cend(); ++it)
      for (const auto& instance : it.value())
        values[it.key()].append(instance->value);
    const auto content = resolveContentTokens(
        program.content, [&](const auto& name) { return values.value(name, {0}).back(); },
        [&](const auto& name) { return values.value(name, {0}); });
    chains.insert(program.key, values);
    const auto side = program.key.endsWith("/before") ? QStringLiteral("before") : QStringLiteral("after");
    if (program.owner.isValid()) {
      fingerprints[program.owner] = qHashMulti(size_t(fingerprints.value(program.owner)), side, content);
      // Only block-host pseudos belong in the legacy heading text map. Inline
      // generated content is retrieved by its semantic key instead.
      if (program.key == program.owner.toString() + '/' + side)
        (side == "before" ? text[program.owner].first : text[program.owner].second) = content;
    }
    if (current) {
      current->chains.insert(program.key, values);
      if (program.owner.isValid())
        current->fingerprints.insert(program.owner, fingerprints.value(program.owner));
      if (text.contains(program.owner))
        current->text.insert(program.owner, text.value(program.owner));
    }
  };
  QHash<NodeId, Memo> next;
  std::function<CooperativeTask(const CounterProgram&, State&, int)> visit;
  const auto block = [&](const Block& entry, State& state, int depth) -> CooperativeTask {
    if (!entry.program)
      co_return;
    const auto input = snapshot(state);
    const auto old = memo_.constFind(entry.id);
    if (old != memo_.cend() && old->program == entry.program && old->input == input) {
      restore(state, *old->output);
      chains.insert(old->chains);
      fingerprints.insert(old->fingerprints);
      text.insert(old->text);
      next.insert(entry.id, old.value());
      co_return;
    }
    Memo memo;
    memo.program = entry.program;
    memo.input = input;
    current = &memo;
    auto task = visit(*entry.program, state, depth);
    while (task.resumeFor(0))
      co_yield nullptr;
    current = nullptr;
    memo.output = snapshot(state);
    next.insert(entry.id, std::move(memo));
  };
  visit = [&](const CounterProgram& program, State& state, int depth) -> CooperativeTask {
    if (!program.key.endsWith("/before") && !program.key.endsWith("/after"))
      countThemePerformanceWork(ThemePerfStage::Counters);
    apply(program, state, depth);
    record(program, state);
    State children = state;
    const auto inherit = [&] {
      for (auto it = state.cbegin(); it != state.cend(); ++it)
        children[it.key()] = it.value();
    };
    if (program.before) {
      auto task = visit(*program.before, children, depth + 1);
      while (task.resumeFor(0))
        co_yield nullptr;
    }
    if (program.documentRoot) {
      for (const auto& entry : blocks) {
        inherit();
        auto task = block(entry, children, depth + 1);
        while (task.resumeFor(0))
          co_yield nullptr;
        co_yield nullptr;
      }
    } else {
      for (const auto& child : program.children) {
        inherit();
        auto task = visit(*child, children, depth + 1);
        while (task.resumeFor(0))
          co_yield nullptr;
        co_yield nullptr;
      }
    }
    inherit();
    if (program.after) {
      auto task = visit(*program.after, children, depth + 1);
      while (task.resumeFor(0))
        co_yield nullptr;
    }
  };
  State state;
  if (host) {
    auto task = visit(*host, state, 0);
    while (task.resumeFor(0))
      co_yield nullptr;
  }
  memo_ = std::move(next);
}
} // namespace muffin

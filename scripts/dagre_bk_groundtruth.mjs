// Ground-truth harness: runs dagre-d3-es's own `layout` on a flowchart graph
// matching the C++ pipeline's input, with layout.js patched to dump post-rank
// node ranks + edge minlens (and bk.js patched to dump per-alignment xs when
// investigating BK). Diff against the C++ MERMAID_DAGRE_DEBUG dump.
//
//   node scripts/dagre_bk_groundtruth.mjs            -> nested-cluster
//   node scripts/dagre_bk_groundtruth.mjs crossing   -> crossing
//
// Temporarily edit ../../mermaid-cli/node_modules/dagre-d3-es/src/dagre/{layout,position/bk}.js
// to add console.error dumps, then RESTORE from the .bak files after.

import path from 'node:path';
import { pathToFileURL } from 'node:url';

const dagreRoot = path.resolve('../mermaid-cli/node_modules/dagre-d3-es/src');
const { Graph } = await import(pathToFileURL(path.join(dagreRoot, 'graphlib/index.js')).href);
const { layout } = await import(pathToFileURL(path.join(dagreRoot, 'dagre/layout.js')).href);

const which = process.argv[2] || 'nested-cluster';

const g = new Graph({ multigraph: true, compound: true });
g.setGraph({ rankdir: 'TB', nodesep: 50, edgesep: 20, ranksep: 50, marginx: 0, marginy: 0 });

if (which === 'crossing') {
  for (const [id, w, h] of [
    ['A', 111.594, 54], ['D', 146.266, 54], ['B', 121.375, 54], ['C', 136.484, 54],
  ]) g.setNode(id, { width: w, height: h });
  for (const [v, w] of [['A', 'D'], ['B', 'C'], ['A', 'C'], ['B', 'D']]) g.setEdge(v, w, { minlen: 1, weight: 1 });
} else if (which === 'self-edge') {
  // flowchart TB / A[Loop] --> A
  g.setNode('A', { width: 95.594, height: 54 });
  g.setEdge('A', 'A', { minlen: 1, weight: 1 });
} else if (which === 'compound-crossing') {
  // Left { A -> B }, Right { C -> D }, cross A->D, C->B
  for (const [id, w, h] of [
    ['A', 113.375, 54], ['B', 140.047, 54], ['C', 124.047, 54], ['D', 150.719, 54],
  ]) g.setNode(id, { width: w, height: h });
  g.setNode('Left', {});
  g.setNode('Right', {});
  g.setParent('A', 'Left');
  g.setParent('B', 'Left');
  g.setParent('C', 'Right');
  g.setParent('D', 'Right');
  for (const [v, w] of [['A', 'B'], ['C', 'D'], ['A', 'D'], ['C', 'B']]) g.setEdge(v, w, { minlen: 1, weight: 1 });
} else {
  // nested-cluster: Outer { Inner { A, B }, C }; C -> D
  for (const [id, w, h] of [
    ['A', 100.922, 54], ['B', 92.922, 54], ['C', 116.906, 54], ['D', 97.359, 54],
  ]) g.setNode(id, { width: w, height: h });
  g.setNode('Inner', {});
  g.setNode('Outer', {});
  g.setParent('A', 'Inner');
  g.setParent('B', 'Inner');
  g.setParent('Inner', 'Outer');
  g.setParent('C', 'Outer');
  for (const [v, w] of [['A', 'B'], ['B', 'C'], ['C', 'D']]) g.setEdge(v, w, { minlen: 1, weight: 1 });
}

layout(g);

console.error('--- FINAL node coords ---');
for (const id of g.nodes()) {
  const n = g.node(id);
  if (n && (n.x !== undefined || n.rank !== undefined))
    console.error(`${id}: x=${n.x} y=${n.y} w=${n.width} h=${n.height}`);
}

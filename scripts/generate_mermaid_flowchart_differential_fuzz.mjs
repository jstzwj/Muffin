import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ??
    path.join("tests", "fixtures", "mermaid", "flowchart-differential-fuzz.json"),
);
const chrome =
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const seed = Number(process.argv[5] ?? "1597463007") >>> 0;
const caseCount = Number(process.argv[6] ?? "256");
const candidateMultiplier = Number(process.argv[7] ?? "4");
const negativeCaseLimit = Number(process.argv[9] ?? "192");
const productionFixturePath = path.resolve(
  process.argv[8] ?? path.join("tests", "fixtures", "mermaid", "flowchart-db.json"),
);

const packageJson = JSON.parse(
  fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"),
);
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
}
if (!Number.isInteger(caseCount) || caseCount < 1 || caseCount > 2000) {
  throw new Error(`Invalid case count: ${caseCount}`);
}
if (!Number.isInteger(candidateMultiplier) || candidateMultiplier < 1 || candidateMultiplier > 16) {
  throw new Error(`Invalid candidate multiplier: ${candidateMultiplier}`);
}
if (!Number.isInteger(negativeCaseLimit) || negativeCaseLimit < 16 || negativeCaseLimit > 512) {
  throw new Error(`Invalid negative case limit: ${negativeCaseLimit}`);
}
const candidateCount = caseCount * candidateMultiplier;
const productionFixture = JSON.parse(fs.readFileSync(productionFixturePath, "utf8"));
if (productionFixture.upstream?.version !== packageJson.version) {
  throw new Error(
    `Production fixture version ${productionFixture.upstream?.version} does not match ${packageJson.version}`,
  );
}
const targetProductions = productionFixture.productions
  .filter((production) => production.status === "covered")
  .map((production) => production.id)
  .sort((left, right) => left - right);

function xorshift32(initial) {
  let state = initial || 0x6d2b79f5;
  return () => {
    state ^= state << 13;
    state ^= state >>> 17;
    state ^= state << 5;
    return state >>> 0;
  };
}

const random = xorshift32(seed);
const pick = (values) => values[random() % values.length];

function label(caseIndex, nodeIndex) {
  const stem = `node ${caseIndex}-${nodeIndex}`;
  switch (random() % 5) {
    case 0:
      return stem;
    case 1:
      return `"quoted ${stem}"`;
    case 2:
      return `"\`**bold ${caseIndex}** ${nodeIndex}\`"`;
    case 3:
      return `中文 ${caseIndex}-${nodeIndex}`;
    default:
      return `value:${caseIndex},${nodeIndex}`;
  }
}

function node(id, caseIndex, nodeIndex) {
  const text = label(caseIndex, nodeIndex);
  const shapes = [
    () => id,
    () => `${id}[${text}]`,
    () => `${id}(${text})`,
    () => `${id}((${text}))`,
    () => `${id}{${text}}`,
    () => `${id}{{${text}}}`,
    () => `${id}[[${text}]]`,
    () => `${id}([${text}])`,
    () => `${id}[(${text})]`,
    () => `${id}(((${text})))`,
    () => `${id}(-${text}-)`,
    () => `${id}>${text}]`,
    () => `${id}[/${text}\\]`,
    () => `${id}[\\${text}/]`,
  ];
  return pick(shapes)();
}

function plainEdge(left, right) {
  return `${left} ${pick(["-->", "---", "-.->", "==>", "---->", "-..->", "~~~"])} ${right}`;
}

function makeCase(index) {
  const template = index % 16;
  const direction = pick(["TB", "BT", "RL", "LR", "TD"]);
  const ids = Array.from({ length: 4 + (random() % 3) }, (_, i) => `N${index}_${i}`);
  const specs = ids.map((id, i) => node(id, index, i));
  const features = [`template:${template}`, `direction:${direction}`];
  const lines = [`flowchart ${direction}`];

  switch (template) {
    case 0: {
      features.push("vertices", "edge-chain");
      lines.push(...specs);
      for (let i = 0; i + 1 < ids.length; ++i) lines.push(plainEdge(ids[i], ids[i + 1]));
      break;
    }
    case 1: {
      features.push("edge-labels", "long-links");
      lines.push(...specs);
      lines.push(`${ids[0]} -->|go ${index}| ${ids[1]}`);
      lines.push(`${ids[1]} -- "quoted edge ${index}" --> ${ids[2]}`);
      lines.push(`${ids[2]} -. dotted ${index} .-> ${ids[3]}`);
      break;
    }
    case 2: {
      features.push("node-groups", "shape-data");
      lines.push(`${specs[0]} & ${specs[1]} --> ${specs[2]} & ${specs[3]}`);
      lines.push(`${ids[0]}@{ shape: rounded, label: "meta ${index}" }`);
      break;
    }
    case 3: {
      features.push("nested-subgraphs", "subgraph-direction");
      lines.push(`subgraph outer${index}[Outer ${index}]`);
      lines.push(`direction ${pick(["TB", "BT", "RL", "LR", "TD"])}`);
      lines.push(specs[0]);
      lines.push(`subgraph "inner ${index}"`);
      lines.push(plainEdge(specs[1], specs[2]));
      lines.push("end");
      lines.push("end");
      lines.push(plainEdge(ids[0], ids[3]));
      break;
    }
    case 4: {
      features.push("classes", "styles", "link-style");
      lines.push(...specs.slice(0, 4));
      lines.push(`${ids[0]} --> ${ids[1]} --> ${ids[2]} --> ${ids[3]}`);
      lines.push(`classDef hot${index} fill:#f00,color:#fff,stroke-width:${1 + (random() % 4)}px`);
      lines.push(`class ${ids[0]},${ids[2]} hot${index}`);
      lines.push(`style ${ids[1]} fill:#0f0,stroke:#333,opacity:${20 + (random() % 80)}%`);
      lines.push(`linkStyle 0,1 interpolate ${pick(["linear", "basis", "step"])} stroke:#00f`);
      break;
    }
    case 5: {
      features.push("click-productions", "tooltips");
      lines.push(...specs.slice(0, 4));
      lines.push(`${ids[0]} --> ${ids[1]} --> ${ids[2]} --> ${ids[3]}`);
      lines.push(`click  ${ids[0]} callback${index} "callback tip ${index}"`);
      lines.push(`click ${ids[1]} call handler${index}(1, two) "call tip ${index}"`);
      lines.push(`click ${ids[2]} href "https://example.com/${index}" "link tip ${index}" _blank`);
      lines.push(`click ${ids[3]} "https://example.org/${index}" _self`);
      break;
    }
    case 6: {
      features.push("edge-id", "edge-metadata", "node-metadata");
      lines.push(`${ids[0]}@{ shape: ${pick(["rounded", "bang", "notch-rect"])}, label: "source ${index}" }`);
      lines.push(specs[1], specs[2]);
      lines.push(`${ids[0]} edge${index}@-- "edge ${index}" --> ${ids[1]}`);
      lines.push(`edge${index}@{ animate: true, animation: ${pick(["fast", "slow"])}, curve: ${pick(["basis", "linear", "step"])} }`);
      lines.push(plainEdge(ids[1], ids[2]));
      break;
    }
    case 7: {
      features.push("semicolons", "accessibility", "markdown");
      lines[0] += `; accTitle: Fuzz ${index}; accDescr: Generated ${index};`;
      lines.push(`${ids[0]}["one; ${index}"] --> ${ids[1]}["\`**two; ${index}**\`"]`);
      lines.push(`style ${ids[0]} fill:#${(random() & 0xffffff).toString(16).padStart(6, "0")};`);
      break;
    }
    case 8: {
      features.push("lexer-delimiters", "quoted", "markdown", "callback-args");
      lines.push(`${ids[0]}["quoted [](){}|;# ${index}"] --> ${ids[1]}["\`markdown []{}|; ${index}\`"]`);
      lines.push(`${ids[1]} -- "edge []{}| ${index}" --> ${ids[2]}`);
      lines.push(`click ${ids[0]} call handler${index}(one, two:three, [four]) "tip [] ${index}"`);
      break;
    }
    case 9: {
      features.push("shape-data-string", "metadata-delimiters", "comments");
      lines.push(`${ids[0]}@{ shape: rounded, label: "meta }, comma, : ${index}" } --> ${ids[1]}`);
      lines.push(`%% comment ${index} with --> and []`);
      lines.push(`${ids[1]}@{ shape: bang, label: "second ${index}" } --> ${ids[2]}`);
      break;
    }
    case 10: {
      features.push("crlf", "blank-lines", "trailing-space", "click-space");
      lines.push("", `${specs[0]} --> ${specs[1]}   `, "");
      lines.push(`click    ${ids[0]} callback${index} "space tip ${index}"`);
      lines.push(`style ${ids[1]} fill:#0f0   `);
      break;
    }
    case 11: {
      features.push("unicode-boundaries", "unicode-id", "unicode-edge-text");
      const boundary = String.fromCodePoint(
        pick([0x00aa, 0x00c0, 0x02c1, 0x0370, 0x0531, 0x3041, 0x3400, 0x4e00, 0xac00, 0xff21]),
      );
      const unicodeA = `${boundary}${ids[0]}`;
      const unicodeB = `${ids[1]}${boundary}`;
      lines.push(`${unicodeA}[${boundary} ${index}] -- ${boundary} edge ${index} --> ${unicodeB}[${boundary}]`);
      lines.push(`${unicodeB} --> ${ids[2]}["quoted ${boundary} ${index}"]`);
      break;
    }
    case 12: {
      features.push("double-ended-edges", "edge-label-types");
      lines.push(...specs.slice(0, 4));
      lines.push(`${ids[0]} o--o ${ids[1]}`);
      lines.push(`${ids[1]} x==x ${ids[2]}`);
      lines.push(`${ids[2]} <--> ${ids[3]}`);
      lines.push(`${ids[3]} -- "\`**edge markdown ${index}**\`" --> ${ids[0]}`);
      break;
    }
    case 13: {
      features.push("action-order", "redefinitions", "duplicate-click", "duplicate-style");
      lines.push(`${ids[0]}[First ${index}]`, `${ids[0]}[Second ${index}]`, specs[1]);
      lines.push(`${ids[0]} --> ${ids[1]}`, `${ids[0]} --> ${ids[1]}`);
      lines.push(`classDef repeated${index} fill:#f00`, `classDef repeated${index} stroke:#00f`);
      lines.push(`class ${ids[0]} repeated${index}`, `class ${ids[0]} repeated${index}`);
      lines.push(`style ${ids[0]} fill:#0f0`, `style ${ids[0]} stroke:#333`);
      lines.push(`click ${ids[0]} callback${index}`, `click ${ids[0]} callbackAgain${index}`);
      break;
    }
    case 14: {
      features.push("action-order", "edge-metadata-updates", "subgraph-node-reuse");
      lines.push(`subgraph ordered${index}[Ordered ${index}]`);
      lines.push(`${specs[0]} edge${index}@--> ${specs[1]}`);
      lines.push("end");
      lines.push(`${ids[0]} --> ${ids[2]}`);
      lines.push(`edge${index}@{ animate: false, animation: slow, curve: basis }`);
      lines.push(`edge${index}@{ animate: true, animation: fast, curve: linear }`);
      break;
    }
    case 15: {
      features.push("link-style-six-productions", "style-components");
      lines.push(...specs.slice(0, 4));
      lines.push(`${ids[0]} --> ${ids[1]} --> ${ids[2]} --> ${ids[3]}`);
      lines.push("linkStyle default stroke:#999");
      lines.push("linkStyle 0,1 stroke:#0f0,stroke-width:2px");
      lines.push("linkStyle default interpolate basis stroke:#999");
      lines.push("linkStyle 0,1 interpolate linear stroke:#f00");
      lines.push("linkStyle default interpolate step");
      lines.push("linkStyle 2 interpolate stepAfter");
      break;
    }
  }

  let source = lines.join("\n");
  if (template === 10) source = source.replace(/\n/g, "\r\n");

  return {
    id: `seed-${seed.toString(16)}-case-${index.toString().padStart(4, "0")}`,
    ordinal: index,
    features,
    source,
  };
}

const randomCandidates = Array.from({ length: candidateCount }, (_, index) => makeCase(index));
const productionCandidates = productionFixture.cases.map((fixture, index) => ({
  id: `production-seed-${fixture.id}`,
  ordinal: candidateCount + index,
  features: ["production-seed", `production-fixture:${fixture.id}`],
  source: fixture.source,
  fixtureProductions: fixture.productions,
}));
const regressionCandidates = [
  {
    id: "regression-direction-outside-subgraph",
    ordinal: candidateCount + productionCandidates.length,
    features: ["required-regression", "top-level-direction"],
    source: "flowchart LR\ndirection TB\nA-->B",
    required: true,
  },
];
const candidates = [...randomCandidates, ...productionCandidates, ...regressionCandidates];

const curatedNegativeCandidates = [
  { id: "missing-header", source: "plain text without a diagram header" },
  { id: "unexpected-end", source: "flowchart TB\nend" },
  { id: "unclosed-subgraph", source: "flowchart TB\nsubgraph S[Group]\nA-->B" },
  { id: "unclosed-square", source: "flowchart LR\nA[broken" },
  { id: "unclosed-round", source: "flowchart LR\nA(broken" },
  { id: "unclosed-diamond", source: "flowchart LR\nA{broken" },
  { id: "style-extra-space", source: "flowchart LR\nstyle  A fill:#fff" },
  { id: "style-missing-space", source: "flowchart LR\nstyle Afill:#fff" },
  { id: "classdef-extra-space", source: "flowchart LR\nclassDef  hot fill:#fff" },
  { id: "classdef-missing-style", source: "flowchart LR\nclassDef hot" },
  { id: "class-extra-space", source: "flowchart LR\nclass  A hot" },
  { id: "linkstyle-extra-space", source: "flowchart LR\nA-->B\nlinkStyle  0 stroke:red" },
  { id: "linkstyle-missing-style", source: "flowchart LR\nA-->B\nlinkStyle 0" },
  { id: "linkstyle-out-of-bounds", source: "flowchart LR\nA-->B\nlinkStyle 9 stroke:red" },
  { id: "click-missing-target", source: "flowchart LR\nA\nclick A href" },
  { id: "click-unclosed-callback", source: "flowchart LR\nA\nclick A call handler(one, two" },
  { id: "shape-data-unclosed", source: 'flowchart LR\nA@{ shape: rect, label: "broken"' },
  { id: "invalid-vertex-metadata", source: "flowchart LR\nA[|role service|Label]" },
  { id: "edge-missing-target", source: "flowchart LR\nA -- broken -->" },
].map((fixture) => ({
  ...fixture,
  required: true,
  operator: "curated",
  originIds: [],
  originProductions: [],
}));

const productionById = new Map(productionFixture.productions.map((production) =>
  [production.id, production]));

function lineStarts(source) {
  const starts = [0];
  for (let index = 0; index < source.length; ++index)
    if (source[index] === "\n") starts.push(index + 1);
  return starts;
}

function locationOffset(source, starts, line, column) {
  if (line < 1 || line > starts.length) return source.length;
  return Math.min(source.length, starts[line - 1] + Math.max(0, column));
}

function spanOffsets(source, starts, span) {
  return {
    start: locationOffset(source, starts, span.first_line, span.first_column),
    end: locationOffset(source, starts, span.last_line, span.last_column),
  };
}

function mutateProductionSeed(fixture, reductions) {
  const result = [];
  const source = fixture.source.replace(/\r\n?/g, "\n");
  const starts = lineStarts(source);
  const add = (operator, production, rhsIndex, mutated, mutationOffset,
               diagnosticOffset = mutationOffset, detail = {}) => {
    if (mutated === source || mutated.length === 0) return;
    result.push({
      operator,
      source: mutated,
      mutationOffset,
      diagnosticOffset,
      targetProduction: production.id,
      targetLhs: production.lhs,
      targetAlternative: production.alternative,
      rhsIndex,
      ...detail,
    });
  };
  for (const reduction of reductions) {
    const production = productionById.get(reduction.production);
    if (!production || production.status !== "covered") continue;
    const rhsSpans = reduction.rhsSpans ?? [];
    if (reduction.span) {
      const whole = spanOffsets(source, starts, reduction.span);
      if (whole.end > whole.start) {
        add("delete-production-span", production, -1,
            source.slice(0, whole.start) + source.slice(whole.end), whole.start, whole.start);
        for (const replacement of ["]", "@", "\""])
          add("replace-production-token-class", production, -1,
              source.slice(0, whole.start) + replacement + source.slice(whole.end),
              whole.start, whole.start, { replacement });
      } else if (production.nullable) {
        add("insert-nullable-boundary-token", production, 0,
            source.slice(0, whole.start) + "]" + source.slice(whole.start), whole.start, whole.start,
            { replacement: "]" });
      }
    }
    for (let rhsIndex = 0; rhsIndex < production.rhs.length; ++rhsIndex) {
      const symbol = production.rhs[rhsIndex];
      if (!production.terminals.includes(symbol)) continue;
      if (symbol === "EOF") {
        for (const replacement of ["]", "@"])
          add("replace-terminal-class", production, rhsIndex,
              source + replacement, source.length, source.length,
              { targetSymbol: symbol, replacement });
        continue;
      }
      const span = rhsSpans[rhsIndex];
      if (!span) continue;
      const offsets = spanOffsets(source, starts, span);
      if (offsets.end <= offsets.start) {
        for (const replacement of ["]", "@"])
          add("replace-terminal-class", production, rhsIndex,
              source.slice(0, offsets.start) + replacement + source.slice(offsets.start),
              offsets.start, offsets.start, { targetSymbol: symbol, replacement });
        continue;
      }
      const lexeme = source.slice(offsets.start, offsets.end);
      add("delete-required-terminal", production, rhsIndex,
          source.slice(0, offsets.start) + source.slice(offsets.end), offsets.start,
          offsets.start, { targetSymbol: symbol });
      const replacements = production.separatorTerminals.includes(symbol)
        ? ["]", "@"] : [";", "@", "\""];
      for (const replacement of replacements)
        add("replace-terminal-class", production, rhsIndex,
            source.slice(0, offsets.start) + replacement + source.slice(offsets.end), offsets.start,
            offsets.start, { targetSymbol: symbol, replacement });
      if (production.separatorTerminals.includes(symbol)) {
        const separator = lexeme;
        add("duplicate-separator", production, rhsIndex,
            source.slice(0, offsets.end) + separator + source.slice(offsets.end), offsets.end,
            offsets.end, { targetSymbol: symbol });
      }
      const closing = lexeme.at(-1);
      const openingByClosing = { "]": "[", ")": "(", "}": "{", "\"": "\"" };
      if (openingByClosing[closing]) {
        const openingAt = lexeme.indexOf(openingByClosing[closing]);
        if (openingAt >= 0 && (closing !== "\"" || openingAt < lexeme.length - 1)) {
          add("break-terminal-delimiter", production, rhsIndex,
              source.slice(0, offsets.end - 1) + source.slice(offsets.end), offsets.end - 1,
              offsets.start + openingAt, { targetSymbol: symbol, closing });
        }
      }
      if (symbol === "acc_descr_multiline_value") {
        const close = source.indexOf("}", offsets.end);
        const open = source.lastIndexOf("{", offsets.start);
        if (open >= 0)
          add("break-terminal-delimiter", production, rhsIndex,
              source.slice(0, open) + source.slice(open + 1), open, open,
              { targetSymbol: symbol, opening: "{" });
        if (close >= 0)
          add("break-terminal-delimiter", production, rhsIndex,
              source.slice(0, close) + source.slice(close + 1), close,
              open >= 0 ? open : offsets.start, { targetSymbol: symbol, closing: "}" });
      }
    }
    if (production.recursive && rhsSpans.length > 0) {
      const recursiveIndex = production.rhs.findIndex((symbol) => symbol === production.lhs);
      const boundaryIndex = recursiveIndex >= 0 && recursiveIndex + 1 < rhsSpans.length
        ? recursiveIndex + 1 : Math.max(0, rhsSpans.length - 1);
      const boundary = rhsSpans[boundaryIndex];
      if (boundary) {
        const offset = spanOffsets(source, starts, boundary).start;
        if (offset > 0)
          add("truncate-recursive-production", production, boundaryIndex,
              source.slice(0, offset), offset, offset);
      }
    }
    if (production.delimiter) {
      const openIndex = production.rhs.indexOf(production.delimiter.open);
      const closeIndex = production.rhs.lastIndexOf(production.delimiter.close);
      const openSpan = rhsSpans[openIndex];
      const closeSpan = rhsSpans[closeIndex];
      if (openSpan && closeSpan && (openIndex !== closeIndex || production.delimiter.open !== production.delimiter.close)) {
        const open = spanOffsets(source, starts, openSpan);
        const close = spanOffsets(source, starts, closeSpan);
        if (close.end > close.start)
          add("break-paired-delimiter", production, closeIndex,
              source.slice(0, close.start) + source.slice(close.end), close.start, open.start,
              { targetSymbol: production.delimiter.close, openingSymbol: production.delimiter.open });
      }
    }
    const reductionSpan = reduction.span;
    for (const boundaryIndex of production.nullableBoundaries) {
      const boundarySpan = rhsSpans[boundaryIndex] ?? reductionSpan;
      if (!boundarySpan) continue;
      const offset = spanOffsets(source, starts, boundarySpan).start;
      add("insert-nullable-boundary-token", production, boundaryIndex,
          source.slice(0, offset) + "]" + source.slice(offset), offset, offset,
          { replacement: "]" });
    }
  }
  return result;
}

function sourcePosition(source, offset) {
  const prefix = source.slice(0, Math.max(0, Math.min(offset, source.length)));
  const lines = prefix.split("\n");
  return { offset, line: lines.length, column: lines.at(-1).length + 1 };
}

function coverageKeys(fixture) {
  const productions = [...new Set(fixture.productions)].sort((a, b) => a - b);
  const keys = fixture.features.map((feature) => `feature:${feature}`);
  for (const production of productions) keys.push(`production:${production}`);
  for (let i = 0; i < productions.length; ++i) {
    for (let j = i + 1; j < productions.length; ++j) {
      keys.push(`pair:${productions[i]}:${productions[j]}`);
    }
  }
  return keys;
}

function selectCoverageCorpus(fixtures, limit, requiredProductions) {
  const remaining = fixtures.map((fixture) => ({ fixture, keys: coverageKeys(fixture) }));
  const selected = [];
  const covered = new Set();
  const required = new Set(requiredProductions);
  const selectAt = (index) => {
    const [{ fixture, keys }] = remaining.splice(index, 1);
    selected.push(fixture);
    for (const key of keys) covered.add(key);
    for (const production of fixture.productions) required.delete(production);
  };
  for (let index = remaining.length - 1; index >= 0; --index)
    if (remaining[index].fixture.required) selectAt(index);
  while (selected.length < limit && remaining.length > 0 && required.size > 0) {
    let bestIndex = -1;
    let bestScore = 0;
    for (let i = 0; i < remaining.length; ++i) {
      const score = new Set(remaining[i].fixture.productions.filter((id) => required.has(id))).size;
      if (score > bestScore) {
        bestScore = score;
        bestIndex = i;
      }
    }
    if (bestIndex < 0) break;
    selectAt(bestIndex);
  }
  if (required.size > 0) {
    throw new Error(`Candidate pool did not reach productions: ${[...required].join(", ")}`);
  }
  while (selected.length < limit && remaining.length > 0) {
    let bestIndex = 0;
    let bestScore = -1;
    for (let i = 0; i < remaining.length; ++i) {
      let score = 0;
      for (const key of remaining[i].keys) if (!covered.has(key)) ++score;
      if (score > bestScore) {
        bestScore = score;
        bestIndex = i;
      }
    }
    selectAt(bestIndex);
  }
  selected.sort((left, right) => left.ordinal - right.ordinal);
  const productions = new Set();
  const pairs = new Set();
  for (const fixture of selected) {
    const ids = [...new Set(fixture.productions)].sort((a, b) => a - b);
    for (const id of ids) productions.add(id);
    for (let i = 0; i < ids.length; ++i)
      for (let j = i + 1; j < ids.length; ++j) pairs.add(`${ids[i]}:${ids[j]}`);
  }
  return { selected, productionCount: productions.size, productionPairCount: pairs.size };
}

function negativeCoverageKeys(fixture) {
  return [
    `class:${fixture.upstreamError.class}`,
    `stage:${fixture.upstreamError.stage}`,
    `operator:${fixture.operator}`,
    ...fixture.originProductions.map((production) => `origin-production:${production}`),
  ];
}

function selectNegativeCorpus(fixtures, limit, requiredProductions) {
  const remaining = fixtures.map((fixture) => ({ fixture, keys: negativeCoverageKeys(fixture) }));
  const selected = [];
  const covered = new Set();
  const missingProductions = new Set(requiredProductions);
  const selectAt = (index) => {
    const [{ fixture, keys }] = remaining.splice(index, 1);
    selected.push(fixture);
    for (const key of keys) covered.add(key);
    for (const production of fixture.originProductions) missingProductions.delete(production);
  };
  for (let index = remaining.length - 1; index >= 0; --index)
    if (remaining[index].fixture.required) selectAt(index);
  while (selected.length < limit && missingProductions.size > 0) {
    let bestIndex = -1;
    let bestScore = 0;
    for (let index = 0; index < remaining.length; ++index) {
      const score = new Set(
        remaining[index].fixture.originProductions.filter((id) => missingProductions.has(id)),
      ).size;
      if (score > bestScore) {
        bestIndex = index;
        bestScore = score;
      }
    }
    if (bestIndex < 0) break;
    selectAt(bestIndex);
  }
  if (missingProductions.size > 0) {
    throw new Error(
      `Rejected mutation pool did not reach origin productions: ${[...missingProductions].join(", ")}`,
    );
  }
  while (selected.length < limit && remaining.length > 0) {
    let bestIndex = 0;
    let bestScore = -1;
    for (let index = 0; index < remaining.length; ++index) {
      const score = remaining[index].keys.filter((key) => !covered.has(key)).length;
      if (score > bestScore) {
        bestIndex = index;
        bestScore = score;
      }
    }
    selectAt(bestIndex);
  }
  selected.sort((left, right) => left.id.localeCompare(right.id));
  const originProductions = new Set();
  for (const fixture of selected)
    for (const production of fixture.originProductions) originProductions.add(production);
  return { selected, originProductionCount: originProductions.size };
}
const { default: puppeteer } = await import(
  pathToFileURL(
    path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"),
  ),
);

const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const positiveGenerated = await page.evaluate(
    async ({ candidates, mermaidModule }) => {
      const { default: mermaid } = await import(mermaidModule);
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        flowchart: { defaultRenderer: "dagre-wrapper" },
      });
      const clean = (value) => JSON.parse(JSON.stringify(value));
      const result = [];
      const bootstrap = await mermaid.mermaidAPI.getDiagramFromText(
        "flowchart LR\ncoverageA-->coverageB",
      );
      const parser = bootstrap.parser.parser;
      const originalPerformAction = parser.performAction;
      let reductions;
      let reductionEvents;
      parser.performAction = function (...args) {
        reductions?.add(args[4]);
        if (reductionEvents) {
          const length = parser.productions_[args[4]][1];
          const locations = args[6];
          const copyLocation = (location) => location ? ({
            first_line: location.first_line,
            first_column: location.first_column,
            last_line: location.last_line,
            last_column: location.last_column,
          }) : null;
          reductionEvents.push({
            production: args[4],
            span: copyLocation(this._$),
            rhsSpans: length === 0 ? []
              : locations.slice(locations.length - length).map(copyLocation),
          });
        }
        return originalPerformAction.apply(this, args);
      };
      const traces = [];
      for (const fixture of candidates) {
        reductions = new Set();
        reductionEvents = fixture.fixtureProductions ? [] : null;
        let diagram;
        try {
          diagram = await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
        } catch (error) {
          throw new Error(`${fixture.id} was not valid by construction: ${error}\n${fixture.source}`);
        }
        const db = diagram.db;
        if (reductionEvents) traces.push({ id: fixture.id, reductions: reductionEvents });
        result.push({
          ...fixture,
          productions: [...reductions].sort((a, b) => a - b),
          expected: {
            direction: db.getDirection() ?? null,
            title: db.getDiagramTitle?.() ?? "",
            accTitle: db.getAccTitle?.() ?? "",
            accDescription: db.getAccDescription?.() ?? "",
            vertices: clean([...db.getVertices().values()]),
            edges: clean(db.getEdges()),
            classes: clean([...db.getClasses().values()]),
            subgraphs: clean(db.getSubGraphs()),
            tooltips: Object.fromEntries(
              [...db.getVertices().keys()]
                .map((id) => [id, db.getTooltip(id)])
                .filter(([, tooltip]) => tooltip !== undefined),
            ),
          },
        });
      }
      reductions = undefined;
      reductionEvents = undefined;
      parser.performAction = originalPerformAction;
      return { positive: result, traces };
    },
    { candidates, mermaidModule },
  );

  const tracesById = new Map(positiveGenerated.traces.map((trace) => [trace.id, trace.reductions]));
  const generatedMutationCandidates = [];
  const mutationsByKey = new Map();
  for (const fixture of productionCandidates) {
    for (const mutation of mutateProductionSeed(fixture, tracesById.get(fixture.id) ?? [])) {
      const key = `${mutation.operator}\0${mutation.source}`;
      const existing = mutationsByKey.get(key);
      if (existing) {
        if (!existing.originIds.includes(fixture.id)) existing.originIds.push(fixture.id);
        if (!existing.originProductions.includes(mutation.targetProduction))
          existing.originProductions.push(mutation.targetProduction);
        continue;
      }
      const candidate = {
        id: `mutation-${generatedMutationCandidates.length.toString().padStart(5, "0")}`,
        ...mutation,
        mutationPosition: sourcePosition(mutation.source, mutation.mutationOffset),
        diagnosticPosition: sourcePosition(mutation.source, mutation.diagnosticOffset),
        required: false,
        originIds: [fixture.id],
        originProductions: [mutation.targetProduction],
      };
      mutationsByKey.set(key, candidate);
      generatedMutationCandidates.push(candidate);
    }
  }
  const negativeCandidates = [...curatedNegativeCandidates, ...generatedMutationCandidates];
  const negativeGenerated = await page.evaluate(
    async ({ negativeCandidates, mermaidModule }) => {
      const { default: mermaid } = await import(mermaidModule);
      const errorClass = (error) => {
        const message = String(error);
        if (message.includes("No diagram type detected")) return "detection";
        if (message.includes("Parse error") || message.includes("Lexical error")) return "syntax";
        return "semantic";
      };
      const errorStage = (error) => {
        const message = String(error);
        if (message.includes("No diagram type detected")) return "detector";
        if (message.includes("Lexical error")) return "lexer";
        if (message.includes("Parse error")) return "parser";
        return "semantic";
      };
      const errorCode = (error) => {
        const message = String(error);
        if (message.includes("No diagram type detected")) return "missing-header";
        if (message.includes("Lexical error")) return "lexical-error";
        if (message.includes("Parse error")) return "parse-error";
        return "semantic-error";
      };
      const negative = [];
      for (const fixture of negativeCandidates) {
        try {
          await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
          if (fixture.required) throw new Error(`${fixture.id} unexpectedly succeeded upstream`);
          continue;
        } catch (error) {
          if (String(error).includes("unexpectedly succeeded upstream")) throw error;
          const message = String(error);
          const hashLine = error?.hash?.loc?.first_line;
          const hashColumn = error?.hash?.loc?.first_column;
          const hashToken = error?.hash?.token;
          const messageLine = message.match(/(?:on line|line:)\s*(\d+)/i)?.[1];
          const rawLine = Number(hashLine ?? messageLine ?? (errorClass(error) === "detection" ? 1 : 0));
          const rawColumn = Number(hashColumn === undefined ? (rawLine > 0 ? 1 : 0) : hashColumn + 1);
          const lineStarts = [0];
          for (let index = 0; index < fixture.source.length; ++index)
            if (fixture.source[index] === "\n") lineStarts.push(index + 1);
          const rawOffset = rawLine > 0 && rawLine <= lineStarts.length
            ? Math.min(fixture.source.length, lineStarts[rawLine - 1] + Math.max(0, rawColumn - 1))
            : -1;
          const stage = errorStage(error);
          const eofPosition = (() => {
            const lines = fixture.source.split("\n");
            return { offset: fixture.source.length, line: lines.length, column: lines.at(-1).length + 1 };
          })();
          let normalized = { offset: rawOffset, line: rawLine, column: rawColumn, basis: "jison-hash-loc" };
          if (stage === "detector") normalized = { offset: 0, line: 1, column: 1, basis: "diagram-detector" };
          else if (stage === "parser" && (hashToken === "EOF" || rawLine > lineStarts.length))
            normalized = { ...eofPosition, basis: "canonical-eof" };
          else if (stage === "parser" && hashToken === "NEWLINE") {
            const newline = fixture.source.indexOf("\n", Math.max(0, rawOffset));
            const offset = newline < 0 ? fixture.source.length : newline;
            const prefix = fixture.source.slice(0, offset).split("\n");
            normalized = {
              offset,
              line: prefix.length,
              column: prefix.at(-1).length + 1,
              basis: "canonical-statement-end",
            };
          }
          else if (stage === "parser" && rawOffset >= 0 &&
                   /[ \t]/.test(fixture.source[rawOffset] ?? "") &&
                   /[ \t]/.test(fixture.source[rawOffset + 1] ?? ""))
            normalized = {
              offset: rawOffset + 1,
              line: rawLine,
              column: rawColumn + 1,
              basis: "first-extra-separator",
            };
          else if (stage === "parser" && fixture.diagnosticPosition &&
                   rawLine !== fixture.diagnosticPosition.line)
            normalized = { ...fixture.diagnosticPosition, basis: "grammar-mutation-anchor" };
          else if (stage === "lexer" && fixture.diagnosticPosition)
            normalized = { ...fixture.diagnosticPosition, basis: "grammar-mutation-anchor" };
          let refinement = fixture.diagnosticPosition
            ? { ...fixture.diagnosticPosition, basis: "grammar-mutation-anchor" }
            : normalized;
          if (!fixture.diagnosticPosition && hashToken === "CALLBACKARGS") {
            const offset = fixture.source.lastIndexOf("(", Math.max(0, rawOffset));
            if (offset >= 0) {
              const prefix = fixture.source.slice(0, offset).split("\n");
              refinement = {
                offset,
                line: prefix.length,
                column: prefix.at(-1).length + 1,
                basis: "callback-opening-delimiter",
              };
            }
          }
          const comparable = stage !== "semantic" && normalized.line > 0 && normalized.column > 0;
          negative.push({
            ...fixture,
            upstreamError: {
              class: errorClass(error),
              stage,
              code: errorCode(error),
              token: hashToken ?? "",
              raw: { offset: rawOffset, line: rawLine, column: rawColumn },
              normalized,
              refinement,
              compareLine: comparable,
              compareColumn: comparable,
              summary: message.split("\n", 1)[0],
            },
          });
        }
      }
      return negative;
    },
    { negativeCandidates, mermaidModule },
  );
  const generated = { positive: positiveGenerated.positive, negative: negativeGenerated };
  const coverage = selectCoverageCorpus(generated.positive, caseCount, targetProductions);
  const negativeCoverage = selectNegativeCorpus(
    generated.negative, negativeCaseLimit, targetProductions,
  );
  const negativeClassCounts = Object.fromEntries(
    [...new Set(negativeCoverage.selected.map((fixture) => fixture.upstreamError.class))]
      .sort()
      .map((name) => [name, negativeCoverage.selected.filter(
        (fixture) => fixture.upstreamError.class === name,
      ).length]),
  );
  const fixture = {
    upstream: {
      package: "mermaid",
      version: packageJson.version,
      license: packageJson.license,
    },
    generator: {
      name: "grammar-driven-flowchart-differential-fuzz",
      algorithm: "xorshift32",
      seed,
      caseCount,
      candidateCount: candidates.length,
      randomCandidateCount: candidateCount,
      productionSeedCount: productionCandidates.length,
      candidateMultiplier,
      selection: "required-production-then-pair-feedback",
      targetProductionCount: targetProductions.length,
      productionCount: coverage.productionCount,
      productionPairCount: coverage.productionPairCount,
      negativeCandidateCount: negativeCandidates.length,
      rejectedNegativeCandidateCount: generated.negative.length,
      negativeCaseLimit,
      negativeSelection: "required-baseline-then-origin-production-feedback",
      negativeCaseCount: negativeCoverage.selected.length,
      negativeOriginProductionCount: negativeCoverage.originProductionCount,
      negativeClassCounts,
    },
    cases: coverage.selected,
    negativeCases: negativeCoverage.selected,
  };
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(fixture, null, 2)}\n`);
  console.log(
    `Wrote ${output} (${caseCount}/${candidates.length} positive, ` +
      `${negativeCoverage.selected.length}/${generated.negative.length}/${negativeCandidates.length} negative, ` +
      `seed=0x${seed.toString(16)}, ${coverage.productionCount} productions, ` +
      `${coverage.productionPairCount} pairs)`,
  );
} finally {
  await browser.close();
}

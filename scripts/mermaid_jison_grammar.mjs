import crypto from "node:crypto";
import fs from "node:fs";
import https from "node:https";

const grammarByVersion = {
  "11.16.0": {
    url: "https://raw.githubusercontent.com/mermaid-js/mermaid/mermaid%4011.16.0/packages/mermaid/src/diagrams/flowchart/parser/flow.jison",
    sha256: "b95d5a12271ece39b9be35fdc082801ed73c179e54f2903b472452394991e384",
  },
};

const sequenceGrammarByVersion = {
  "11.16.0": {
    url: "https://raw.githubusercontent.com/mermaid-js/mermaid/mermaid%4011.16.0/packages/mermaid/src/diagrams/sequence/parser/sequenceDiagram.jison",
    sha256: "8771148ef56c2b98b021597a53585e45acc088cb321a3d457d265eca352fb40f",
  },
};

function download(url, redirects = 4) {
  return new Promise((resolve, reject) => {
    https.get(url, { headers: { "user-agent": "Muffin grammar fixture generator" } }, (response) => {
      if (response.statusCode >= 300 && response.statusCode < 400 && response.headers.location && redirects > 0) {
        response.resume();
        resolve(download(new URL(response.headers.location, url), redirects - 1));
        return;
      }
      if (response.statusCode !== 200) {
        response.resume();
        reject(new Error(`Could not download Mermaid grammar: HTTP ${response.statusCode}`));
        return;
      }
      response.setEncoding("utf8");
      let text = "";
      response.on("data", (chunk) => { text += chunk; });
      response.on("end", () => resolve(text));
    }).on("error", reject);
  });
}

async function readLockedGrammar(descriptor, version, label, sourcePath) {
  if (!descriptor) throw new Error(`No ${label} source registered for Mermaid ${version}`);
  let source;
  if (sourcePath) {
    source = fs.readFileSync(sourcePath, "utf8");
  } else {
    let lastError;
    for (let attempt = 0; attempt < 3; ++attempt) {
      try {
        source = await download(descriptor.url);
        break;
      } catch (error) {
        lastError = error;
      }
    }
    if (source === undefined) throw lastError;
  }
  const sha256 = crypto.createHash("sha256").update(source).digest("hex");
  if (sha256 !== descriptor.sha256) {
    throw new Error(`${label} hash mismatch for Mermaid ${version}: ${sha256}`);
  }
  return { source, url: descriptor.url, sha256 };
}

function stripActionsAndComments(source) {
  let result = "";
  for (let index = 0; index < source.length;) {
    if (source.startsWith("//", index)) {
      const newline = source.indexOf("\n", index + 2);
      index = newline < 0 ? source.length : newline;
      continue;
    }
    if (source.startsWith("/*", index)) {
      const close = source.indexOf("*/", index + 2);
      index = close < 0 ? source.length : close + 2;
      continue;
    }
    if (source[index] === "{") {
      let depth = 1;
      let quote = null;
      index += 1;
      while (index < source.length && depth > 0) {
        const character = source[index];
        if (quote) {
          if (character === "\\") index += 2;
          else {
            if (character === quote) quote = null;
            index += 1;
          }
        } else if (character === "\"" || character === "'" || character === "`") {
          quote = character;
          index += 1;
        } else if (source.startsWith("//", index)) {
          const newline = source.indexOf("\n", index + 2);
          index = newline < 0 ? source.length : newline;
        } else if (source.startsWith("/*", index)) {
          const close = source.indexOf("*/", index + 2);
          index = close < 0 ? source.length : close + 2;
        } else {
          if (character === "{") ++depth;
          if (character === "}") --depth;
          ++index;
        }
      }
      result += " ";
      continue;
    }
    result += source[index++];
  }
  return result;
}

function splitOutsideQuotes(source, delimiter) {
  const result = [];
  let start = 0;
  let quote = null;
  for (let index = 0; index < source.length; ++index) {
    const character = source[index];
    if (quote) {
      if (character === "\\") ++index;
      else if (character === quote) quote = null;
    } else if (character === "'" || character === "\"") {
      quote = character;
    } else if (character === delimiter) {
      result.push(source.slice(start, index));
      start = index + 1;
    }
  }
  result.push(source.slice(start));
  return result;
}

function parseRules(source) {
  const marker = source.indexOf("%%", source.indexOf("%start"));
  const end = source.lastIndexOf("%%");
  if (marker < 0 || end <= marker) throw new Error("Could not locate flow.jison grammar section");
  const grammar = stripActionsAndComments(source.slice(marker + 2, end));
  const productions = [];
  for (const rule of splitOutsideQuotes(grammar, ";")) {
    const colon = rule.indexOf(":");
    if (colon < 0) continue;
    const lhs = rule.slice(0, colon).trim();
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(lhs)) continue;
    const alternatives = splitOutsideQuotes(rule.slice(colon + 1), "|");
    for (let alternative = 0; alternative < alternatives.length; ++alternative) {
      const rhs = alternatives[alternative].trim().length === 0 ? []
        : alternatives[alternative].trim().split(/\s+/).map((symbol) => {
            const alias = symbol.indexOf("\\[");
            const withoutAlias = alias < 0 ? symbol : symbol.slice(0, alias);
            if ((withoutAlias.startsWith("'") && withoutAlias.endsWith("'")) ||
                (withoutAlias.startsWith("\"") && withoutAlias.endsWith("\""))) {
              return withoutAlias.slice(1, -1);
            }
            return withoutAlias;
          });
      productions.push({ id: productions.length + 1, lhs, alternative: alternative + 1, rhs });
    }
  }
  return productions;
}

function enrichProductions(productions) {
  const nonterminals = new Set(productions.map((production) => production.lhs));
  const nullable = new Set();
  let changed = true;
  while (changed) {
    changed = false;
    for (const production of productions) {
      if (!nullable.has(production.lhs) &&
          production.rhs.every((symbol) => nonterminals.has(symbol) && nullable.has(symbol))) {
        nullable.add(production.lhs);
        changed = true;
      }
    }
  }
  const dependencies = new Map([...nonterminals].map((name) => [name, new Set()]));
  for (const production of productions)
    for (const symbol of production.rhs)
      if (nonterminals.has(symbol)) dependencies.get(production.lhs).add(symbol);
  const reaches = (start, target, seen = new Set()) => {
    if (seen.has(start)) return false;
    seen.add(start);
    for (const next of dependencies.get(start) ?? [])
      if (next === target || reaches(next, target, seen)) return true;
    return false;
  };
  const delimiterPairs = new Map([
    ["SQS", "SQE"], ["PS", "PE"], ["DOUBLECIRCLESTART", "DOUBLECIRCLEEND"],
    ["STADIUMSTART", "STADIUMEND"], ["SUBROUTINESTART", "SUBROUTINEEND"],
    ["CYLINDERSTART", "CYLINDEREND"], ["DIAMOND_START", "DIAMOND_STOP"],
    ["TRAPSTART", "TRAPEND"], ["INVTRAPSTART", "INVTRAPEND"], ["PIPE", "PIPE"],
    ["(-", "-)"],
  ]);
  return productions.map((production) => {
    const terminals = production.rhs.filter((symbol) => !nonterminals.has(symbol));
    const recursive = production.rhs.some((symbol) =>
      nonterminals.has(symbol) && (symbol === production.lhs || reaches(symbol, production.lhs)));
    const delimiter = [...delimiterPairs].find(([open, close]) =>
      production.rhs.includes(open) && production.rhs.includes(close));
    return {
      ...production,
      rhsLength: production.rhs.length,
      terminals,
      nullable: production.rhs.length === 0 || production.rhs.every((symbol) => nullable.has(symbol)),
      nullableBoundaries: production.rhs.length === 0 ? [0]
        : Array.from({ length: production.rhs.length + 1 }, (_, index) => index).filter((index) =>
            (index > 0 && nullable.has(production.rhs[index - 1])) ||
            (index < production.rhs.length && nullable.has(production.rhs[index]))),
      recursive,
      separatorTerminals: terminals.filter((symbol) =>
        ["SPACE", "COMMA", "SEMI", "NEWLINE", "EOF", "AMP", "COLON", "PIPE"].includes(symbol)),
      delimiter: delimiter ? { open: delimiter[0], close: delimiter[1] } : null,
    };
  });
}

export async function loadFlowchartGrammar(version, sourcePath) {
  const { source, url, sha256 } = await readLockedGrammar(
    grammarByVersion[version], version, "flow.jison", sourcePath,
  );
  const productions = enrichProductions(parseRules(source));
  if (productions.length !== 189) {
    throw new Error(`Expected 189 flowchart productions, parsed ${productions.length}`);
  }
  return { source: { url, sha256 }, productions };
}

export async function loadSequenceGrammar(version, sourcePath) {
  const { source, url, sha256 } = await readLockedGrammar(
    sequenceGrammarByVersion[version], version, "sequenceDiagram.jison", sourcePath,
  );
  const productions = enrichProductions(parseRules(source));
  if (productions.length !== 105) {
    throw new Error(`Expected 105 sequence productions, parsed ${productions.length}`);
  }
  return { source: { url, sha256 }, productions };
}

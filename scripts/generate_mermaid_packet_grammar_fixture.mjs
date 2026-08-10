import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Freezes Mermaid 11.16.0 PacketGrammar plus PacketDB population behavior.
// Usage:
//   node scripts/generate_mermaid_packet_grammar_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]

const EXPECTED_MERMAID_VERSION = "11.16.0";
const EXPECTED_MERMAID_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_PACKET_MODULE_SHA256 =
  "cc7ea4edbd16fc76bd5ce0ad461a8ed6829ab64acef15690d313c8d5ca69181e";
const EXPECTED_CHROME_PRODUCT = "Chrome/151.0.7922.76";
const EXPECTED_CHROME_SHA256 =
  "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "packet-grammar.json"),
);
const chrome = path.resolve(
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe",
);
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const packetModuleFile = path.join(
  mermaidRoot,
  "dist",
  "chunks",
  "mermaid.esm",
  "diagram-MPNEUAEF.mjs",
);

const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const assertEqual = (actual, expected, label) => {
  if (actual !== expected)
    throw new Error(`${label}: expected ${expected}, found ${actual}`);
};
const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
assertEqual(pkg.version, EXPECTED_MERMAID_VERSION, "Mermaid version");
assertEqual(sha256(fs.readFileSync(moduleFile)), EXPECTED_MERMAID_MODULE_SHA256, "Mermaid module sha256");
assertEqual(sha256(fs.readFileSync(packetModuleFile)), EXPECTED_PACKET_MODULE_SHA256, "Packet module sha256");
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256, "Chrome sha256");

const init = (config, body) => `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const cases = [
  { id: "empty-stable", source: "packet" },
  { id: "empty-beta", source: "packet-beta" },
  { id: "header-same-line", source: 'packet 0: "x"' },
  { id: "leading-whitespace-comments", source: ' \t\n%% comment\n packet-beta\n0: "x" %% tail' },
  { id: "one-bit", source: 'packet\n0: "zero"' },
  { id: "zero-range-one-bit", source: 'packet\n0-0: "zero"' },
  { id: "range-spaces", source: 'packet\n0 - 7 : "eight"' },
  { id: "implicit-plus", source: 'packet\n+8: "a"\n+8: "b"' },
  { id: "explicit-contiguous", source: 'packet\n0-3: "a"\n4: "b"\n5-7: "c"' },
  { id: "cross-row-default", source: 'packet\n0-7: "a"\n+40: "wide"' },
  { id: "single-quoted", source: "packet\n0: 'A B'" },
  { id: "multiline-double-quoted", source: 'packet\n0: "line one\nline two"' },
  { id: "multiline-single-quoted", source: "packet\n0: 'line one\nline two'" },
  { id: "empty-label", source: 'packet\n0: ""' },
  { id: "unicode-label", source: 'packet\n0-2: "日本 中文"' },
  { id: "escaped-label", source: 'packet\n0: "A\\nB\\tC\\\"D\\u0041"' },
  { id: "percent-in-string", source: 'packet\n0: "A%%B"' },
  { id: "metadata", source: 'packet\ntitle Hello  world\naccTitle:  Packet title\naccDescr:  Packet  description\n0: "x"' },
  { id: "metadata-last-wins", source: 'packet\ntitle One\ntitle Two\naccTitle: A\naccTitle: B\n0: "x"' },
  { id: "metadata-empty-last", source: 'packet\ntitle One\ntitle\n0: "x"' },
  { id: "accdescr-block", source: 'packet\naccDescr {\n first  line\n\n second line\n}\n0: "x"' },
  { id: "accdescr-block-next-line", source: 'packet\naccDescr\n  {\n first  line\n second line\n}\n0: "x"' },
  { id: "metadata-html-sanitizer", source: 'packet\ntitle <script>x</script><b>ok</b><img src=x onerror=evil()>\naccTitle: <style>x</style><i>AT</i><a href="javascript:evil()">bad</a>\naccDescr: <svg onload=evil()>S</svg><u>AD</u>\n0: "x"' },
  { id: "label-html-is-text", source: 'packet\n0: "<script>x</script><b>literal</b>"' },
  { id: "frontmatter-title", source: '---\ntitle: Front Packet\n---\npacket\n0: "x"', render: true },
  { id: "frontmatter-title-html-sanitizer", source: '---\ntitle: <script>x</script><b>ok</b><img src=x onerror=evil()>\n---\npacket\n0: "x"', render: true },
  { id: "frontmatter-inline-wins", source: '---\ntitle: Front\n---\npacket\ntitle Inline\n0: "x"', render: true },
  { id: "bits-8", source: init({ packet: { bitsPerRow: 8 } }, 'packet\n+20: "x"') },
  { id: "bits-string-8", source: init({ packet: { bitsPerRow: "8" } }, 'packet\n+20: "x"') },
  { id: "bits-string-hex", source: init({ packet: { bitsPerRow: "0x8" } }, 'packet\n+20: "x"') },
  { id: "bits-string-binary", source: init({ packet: { bitsPerRow: "0b10" } }, 'packet\n+20: "x"') },
  { id: "bits-string-octal", source: init({ packet: { bitsPerRow: "0o10" } }, 'packet\n+20: "x"') },
  { id: "bits-string-js-whitespace", source: init({ packet: { bitsPerRow: "\ufeff0x8" } }, 'packet\n+20: "x"') },
  { id: "bits-true", source: init({ packet: { bitsPerRow: true } }, 'packet\n+3: "x"') },
  { id: "bits-fractional", source: init({ packet: { bitsPerRow: 2.5 } }, 'packet\n+6: "x"') },
  { id: "bits-garbage-string", source: init({ packet: { bitsPerRow: "abc" } }, 'packet\n0: "x"') },
  { id: "bits-null-default", source: init({ packet: { bitsPerRow: null } }, 'packet\n+33: "x"') },
  { id: "bits-object-default", source: init({ packet: { bitsPerRow: {} } }, 'packet\n+33: "x"') },
  { id: "bits-array-default", source: init({ packet: { bitsPerRow: [8] } }, 'packet\n+33: "x"') },
  { id: "bits-false-max", source: init({ packet: { bitsPerRow: false } }, 'packet\n0: "x"'), noRender: true },
  { id: "showbits-false", source: init({ packet: { showBits: false } }, 'packet\n0: "x"') },
  { id: "showbits-string-false", source: init({ packet: { showBits: "false" } }, 'packet\n0: "x"') },
  { id: "reject-no-header", source: '0: "x"' },
  { id: "reject-header-case", source: 'Packet\n0: "x"' },
  { id: "reject-header-prefix", source: 'packetXYZ\n0: "x"' },
  { id: "reject-beta-prefix", source: 'packet-betax\n0: "x"' },
  { id: "reject-header-colon", source: 'packet:\n0: "x"' },
  { id: "reject-bare-label", source: "packet\n0: x" },
  { id: "reject-leading-zero", source: 'packet\n00: "x"' },
  { id: "reject-float", source: 'packet\n0.0: "x"' },
  { id: "reject-negative", source: 'packet\n-1: "x"' },
  { id: "reject-exponent", source: 'packet\n1e2: "x"' },
  { id: "reject-two-same-line", source: 'packet\n0: "a" 1: "b"' },
  { id: "reject-semicolon", source: 'packet\n0: "a";' },
  { id: "reject-trailing-dollar", source: 'packet\n0: "a"$' },
  { id: "reject-colon-top-level", source: 'packet\n:' },
  { id: "reject-title-suffix", source: 'packet\ntitleX\n0: "x"' },
  { id: "reject-title-colon", source: 'packet\ntitle: Bad\n0: "x"' },
  { id: "reject-missing-colon", source: 'packet\n0 "x"' },
  { id: "reject-missing-label", source: "packet\n0:" },
  { id: "reject-unterminated-label", source: 'packet\n0: "x' },
  { id: "reject-missing-range-end", source: 'packet\n0-: "x"' },
  { id: "reject-missing-plus-bits", source: 'packet\n+: "x"' },
  { id: "runtime-plus-zero", source: 'packet\n+0: "x"' },
  { id: "runtime-reverse", source: 'packet\n3-2: "x"' },
  { id: "runtime-first-nonzero", source: 'packet\n1: "x"' },
  { id: "runtime-gap", source: 'packet\n0-1: "a"\n3: "b"' },
  { id: "runtime-overlap", source: 'packet\n0-2: "a"\n2: "b"' },
  { id: "runtime-large-first-bit", source: 'packet\n9007199254740993: "x"' },
];

const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")),
);
const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});

try {
  assertEqual(await browser.version(), EXPECTED_CHROME_PRODUCT, "Chrome product");
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const results = await page.evaluate(
    async ({ cases, mermaidModule, packetModule }) => {
      const { default: mermaid } = await import(mermaidModule);
      const { diagram } = await import(packetModule);
      const number = (value) => {
        if (Number.isNaN(value)) return "NaN";
        if (value === Infinity) return "Infinity";
        if (value === -Infinity) return "-Infinity";
        return value;
      };
      const block = ({ start, end, bits, label }) => ({
        start: number(start),
        end: number(end),
        bits: number(bits),
        label,
      });
      const classify = (message) => {
        if (message.startsWith("No diagram type detected")) return "no-diagram";
        if (message.includes("Packet block ") || message.includes("Block start ")) return "runtime";
        if (message.includes("Lexer error")) return "lexer";
        if (message.includes("Parse error") || message.includes("Parsing failed")) return "parser";
        return "unknown";
      };
      const location = (message) => {
        const match = message.match(/(?:Lexer|Parse) error on line (\d+|\?), column (\d+|\?)/);
        return {
          line: match && match[1] !== "?" ? Number(match[1]) : 0,
          column: match && match[2] !== "?" ? Number(match[2]) : 0,
        };
      };
      const out = [];
      for (let index = 0; index < cases.length; ++index) {
        const fixture = cases[index];
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          look: "classic",
        });
        try {
          await mermaid.parse(fixture.source);
          if (fixture.render)
            await mermaid.render(`packet-grammar-${index}`, fixture.source);
          const db = diagram.parser.parser.yy;
          const words = db.getPacket();
          const config = db.getConfig();
          const expectedDb = {
            title: db.getDiagramTitle(),
            accTitle: db.getAccTitle(),
            accDescription: db.getAccDescription(),
            wordCount: words.length,
            words: words.length <= 64 ? words.map((word) => word.map(block)) : null,
            firstWord: words.length ? words[0].map(block) : null,
            lastWord: words.length ? words.at(-1).map(block) : null,
            config: {
              bitsPerRow: config.bitsPerRow,
              showBits: config.showBits,
              paddingY: config.paddingY,
            },
          };
          out.push({ id: fixture.id, source: fixture.source, accept: true, expectedDb });
        } catch (error) {
          const message = String(error?.message ?? error).replace(/\s+/g, " ").trim();
          out.push({
            id: fixture.id,
            source: fixture.source,
            accept: false,
            reject: { class: classify(message), message, ...location(message) },
          });
        }
      }
      return out;
    },
    {
      cases,
      mermaidModule: pathToFileURL(moduleFile).href,
      packetModule: pathToFileURL(packetModuleFile).href,
    },
  );

  const byId = new Map(results.map((item) => [item.id, item]));
  for (const id of ["cross-row-default", "bits-8", "bits-string-hex", "bits-string-binary", "bits-string-octal", "bits-string-js-whitespace", "bits-fractional", "frontmatter-title", "multiline-double-quoted", "accdescr-block-next-line"])
    assertEqual(byId.get(id)?.accept, true, `${id} accept`);
  for (const id of ["reject-bare-label", "reject-leading-zero", "runtime-plus-zero", "runtime-gap"])
    assertEqual(byId.get(id)?.accept, false, `${id} reject`);
  assertEqual(byId.get("bits-false-max").expectedDb.wordCount, 10000, "bits=false max rows");
  assertEqual(
    byId.get("runtime-plus-zero").reject.message,
    "Packet block 0 is invalid. Cannot have a zero bit field.",
    "zero-bit runtime message",
  );

  const payload = {
    upstream: {
      package: "mermaid",
      version: EXPECTED_MERMAID_VERSION,
      moduleSha256: EXPECTED_MERMAID_MODULE_SHA256,
      packetModuleSha256: EXPECTED_PACKET_MODULE_SHA256,
      chromeProduct: EXPECTED_CHROME_PRODUCT,
      chromeSha256: EXPECTED_CHROME_SHA256,
      parser: "Langium PacketGrammar + PacketDB",
      sourceEntry: true,
    },
    oracle: "source-entry detection/accept/reject plus PacketDB words, metadata, and parse-time config",
    cases: results,
  };
  payload.fixtureSha256 = sha256(JSON.stringify(payload));
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
  const accepted = results.filter((item) => item.accept).length;
  console.log(`Wrote ${output} (${results.length} cases: ${accepted} accept, ${results.length - accepted} reject, sha=${payload.fixtureSha256})`);
} finally {
  await browser.close();
}

import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Freezes Mermaid 11.16.0's erDiagram detector, parser boundary and compiled
// database: which sources parse and render, where the others fail, and the
// entities (with aliases and attributes), relationships and class definitions
// the parser hands to the renderer. Several boundaries recorded here are
// quirks of upstream's lexer and grammar (a relationship must carry a label, a
// quoted word is an entity name of its own, `a[One] a[Two]` fails because
// `one` lexes as a cardinality); they are upstream's behaviour all the same.
//
//   node scripts/generate_mermaid_er_grammar_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/er-grammar.json,
//           C:/Program Files/Google/Chrome/Application/chrome.exe

const EXPECTED_VERSION = "11.16.0";
const mermaidRoot = path.resolve(
  process.env.MERMAID_REFERENCE_ROOT ??
    process.argv[2] ??
    path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "er-grammar.json"));
const chrome = path.resolve(process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe");

const sha256 = (value) => createHash("sha256").update(value).digest("hex");
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== EXPECTED_VERSION) {
  throw new Error(`Expected Mermaid ${EXPECTED_VERSION}, found ${packageJson.version}`);
}
if (!fs.existsSync(chrome)) throw new Error(`Chrome not found: ${chrome}`);

const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "esm", "puppeteer", "puppeteer.js")).href,
);

// Verdict is what upstream is expected to answer: "ok" (parses), "reject"
// (the erDiagram parser fails), "nodetect" (no diagram detected). The
// generator stops if upstream disagrees, so a mislabelled case cannot slip in.
const er = (lines) => `erDiagram\n${lines.join("\n")}`;
const block = (name, lines) => `${name} {\n${lines.join("\n")}\n}`;
const rel = (left, right, label = "x") => `${left} ||--o{ ${right} : ${label}`;

const cases = [
  // ---- relationships: shape
  ["rel-basic", er([rel("A", "B")]), "ok"],
  ["rel-unlabeled", er(["A ||--o{ B"]), "reject"],
  ["rel-label-multiword-unquoted", er(["A ||--o{ B : has many"]), "reject"],
  ["rel-label-three-words", er(["A ||--o{ B : one two three"]), "reject"],
  ["rel-label-word-then-quoted", er(['A ||--o{ B : has "many"']), "ok"],
  ["rel-label-empty", er(["A ||--o{ B :"]), "reject"],
  ["rel-label-quoted-empty", er(['A ||--o{ B : ""']), "ok"],
  ["rel-label-quoted-spaces", er(['A ||--o{ B : "has many"']), "ok"],
  ["rel-label-quoted-hash", er(['A ||--o{ B : "a#b"']), "ok"],
  ["rel-label-quoted-escaped-quote", er(['A ||--o{ B : "a\\"b"']), "reject"],
  ["rel-label-unicode", er(["A ||--o{ B : 拥有"]), "ok"],
  ["rel-label-with-dash", er(["A ||--o{ B : belongs-to"]), "ok"],
  ["rel-label-with-dots", er(["A ||--o{ B : a.b"]), "ok"],
  ["rel-label-with-hash-unquoted", er(["A ||--o{ B : a#b"]), "reject"],
  ["rel-label-backtick", er(["A ||--o{ B : `has many`"]), "reject"],
  ["rel-label-decimal", er(["A ||--o{ B : 1.5"]), "reject"],
  ["rel-label-one", er(["A ||--o{ B : 1"]), "reject"],
  ["rel-label-is-entity-like", er(["A ||--o{ B : C"]), "ok"],
  ["rel-label-then-class", er(["A ||--o{ B : x:::hot", "classDef hot fill:#f96"]), "reject"],
  ["rel-label-then-brace", er(["A ||--o{ B : x {", "string id PK", "}"]), "reject"],
  ["rel-trailing-word-is-entity", er(["A ||--o{ B : x extra"]), "ok"],
  ["rel-two-on-one-line", er(["A ||--o{ B : x A ||--o{ C : y"]), "ok"],
  ["rel-duplicate", er([rel("A", "B"), rel("A", "B")]), "ok"],
  ["rel-self", er([rel("A", "A", "self")]), "ok"],
  ["rel-undefined-entities", er([rel("A", "B")]), "ok"],
  ["rel-entity-defined-later", er([rel("A", "B"), block("B", ["string id PK"])]), "ok"],
  ["rel-entities-then-blocks", er([block("A", ["string id PK"]), block("B", ["int age"]), rel("A", "B")]), "ok"],
  ["rel-comment-line-between", er(["A ||--o{ B : x", "%% note", "A ||--o{ C : y"]), "ok"],
  // ---- relationships: class lists
  ["rel-class-both-sides", er(["A:::hot ||--o{ B:::cold : x", "classDef hot fill:#f96", "classDef cold fill:#6cf"]), "ok"],
  ["rel-class-left", er(["A:::hot ||--o{ B : x", "classDef hot fill:#f96"]), "ok"],
  ["rel-class-right", er(["A ||--o{ B:::cold : x", "classDef cold fill:#6cf"]), "ok"],
  ["rel-class-undefined", er(["A:::nowhere ||--o{ B : x"]), "ok"],
  // ---- relationships: cardinality
  ["card-glyphs-all", er(["A |o--o| B : r1", "C ||--|| D : r2", "E }|--|{ F : r3", "G }o--o{ H : r4", "I |o--|| J : r5", "K ||--o| L : r6"]), "ok"],
  ["card-words-one-or-zero", er(["A one or zero--one or zero B : x"]), "ok"],
  ["card-words-zero-or-one", er(["A zero or one--zero or one B : x"]), "ok"],
  ["card-words-one-or-more", er(["A one or more--one or more B : x"]), "ok"],
  ["card-words-one-or-many", er(["A one or many--one or many B : x"]), "ok"],
  ["card-words-zero-or-more", er(["A zero or more--zero or more B : x"]), "ok"],
  ["card-words-zero-or-many", er(["A zero or many--zero or many B : x"]), "ok"],
  ["card-words-many", er(["A many--many B : x"]), "ok"],
  ["card-words-many-paren-zero", er(["A many(0)--many(0) B : x"]), "ok"],
  ["card-words-many-paren-one", er(["A many(1)--many(1) B : x"]), "ok"],
  ["card-words-only-one", er(["A only one--only one B : x"]), "ok"],
  ["card-words-one-plus", er(["A 1+--1+ B : x"]), "ok"],
  ["card-words-zero-plus", er(["A 0+--0+ B : x"]), "ok"],
  ["card-word-one", er(["A one--one B : x"]), "ok"],
  ["card-word-uppercase", er(["A ZERO OR MORE--o| B : x"]), "ok"],
  ["card-mixed-glyph-word", er(["A |o--zero or more B : x"]), "ok"],
  ["card-mixed-word-glyph", er(["A zero or more--o| B : x"]), "ok"],
  ["card-entity-one-on-left", er(["1 ||--o{ B : x"]), "ok"],
  ["card-entity-one-on-right", er(["A ||--o{ 1 : x"]), "ok"],
  ["card-decimal-entity-on-left", er(["1.5 ||--o{ B : x"]), "ok"],
  ["card-word-glued-to-dashes", er(["A onlyone--B : x"]), "reject"],
  ["card-bad-single-tick", er(["A |---o| B : x"]), "reject"],
  ["card-bad-three-ticks", er(["A |||--o| B : x"]), "reject"],
  ["card-bad-lone-circle", er(["A o--o| B : x"]), "reject"],
  ["card-bad-reversed-circle", er(["A {o--o| B : x"]), "reject"],
  ["card-bad-two-letters", er(["A po--o| B : x"]), "reject"],
  ["rel-missing-right-card", er(["A ||-- B : x"]), "reject"],
  ["rel-missing-left-card", er(["A --o{ B : x"]), "reject"],
  ["rel-missing-both-cards", er(["A -- B : x"]), "reject"],
  ["rel-missing-target", er(["A ||--o{"]), "reject"],
  ["rel-missing-target-with-label", er(["A ||--o{ : x"]), "reject"],
  // ---- relationships: type
  ["rel-type-identifying", er(["A ||--o{ B : x"]), "ok"],
  ["rel-type-non-identifying", er(["A ||..o{ B : x"]), "ok"],
  ["rel-type-dot-dash", er(["A ||.-o{ B : x"]), "ok"],
  ["rel-type-dash-dot", er(["A ||-.o{ B : x"]), "ok"],
  ["rel-type-to", er(["A ||to o{ B : x"]), "ok"],
  ["rel-type-optionally-to", er(["A ||optionally to o{ B : x"]), "ok"],
  ["rel-type-single-dash", er(["A ||-o{ B : x"]), "reject"],
  ["rel-type-three-dashes", er(["A ||---o| B : x"]), "reject"],
  ["rel-type-mixed", er(["A ||.-.o{ B : x"]), "reject"],
  ["rel-type-to-without-spaces", er(["C ||to{ D : y"]), "reject"],
  // ---- attributes
  ["attr-basic", er([block("A", ["string id"])]), "ok"],
  ["attr-keys-and-comment", er([block("CUSTOMER", ['string name PK "primary name key"', "int age", "bigint account_id FK", 'string email UK "unique email"'])]), "ok"],
  ["attr-comment-only", er([block("A", ['string id "the identifier"'])]), "ok"],
  ["attr-key-list", er([block("A", ["string id PK, FK"])]), "ok"],
  ["attr-key-list-spaces", er([block("A", ["string id PK ,  FK"])]), "ok"],
  ["attr-key-list-three", er([block("A", ["string id PK, FK, UK"])]), "ok"],
  ["attr-key-list-trailing-comma", er([block("A", ["string id PK,"])]), "reject"],
  ["attr-key-order-reversed", er([block("A", ["string id FK, PK"])]), "ok"],
  ["attr-key-duplicate", er([block("A", ["string id PK, PK"])]), "ok"],
  ["attr-key-lowercase", er([block("A", ["string id pk"])]), "ok"],
  ["attr-key-mixed-case", er([block("A", ["string id Pk"])]), "ok"],
  ["attr-key-prefix-of-word", er([block("A", ["string id PKX"])]), "reject"],
  ["attr-key-as-type", er([block("A", ["PK id"])]), "reject"],
  ["attr-key-alone", er([block("A", ["PK"])]), "reject"],
  ["attr-optional-type", er([block("A", ["string? id"])]), "ok"],
  ["attr-two-word-type", er([block("A", ["big int id"])]), "reject"],
  ["attr-four-word-type", er([block("A", ["very big int id"])]), "ok"],
  ["attr-two-word-type-optional", er([block("A", ["big int? id"])]), "reject"],
  ["attr-backtick-type", er([block("A", ["`string list` id"])]), "ok"],
  ["attr-backtick-type-and-name", er([block("A", ["`string list` `favorite tags`", "`int count` `open, orders`"])]), "ok"],
  ["attr-backtick-comment", er([block("A", ["string id `the key`"])]), "reject"],
  ["attr-comment-unicode", er([block("A", ['string id "主键"'])]), "ok"],
  ["attr-comment-with-percent", er([block("A", ['string id "50%"'])]), "ok"],
  ["attr-unquoted-words-after-key", er([block("A", ["string id PK the identifier"])]), "ok"],
  ["attr-missing-name", er([block("A", ["string"])]), "reject"],
  ["attr-numeric-name", er([block("A", ["int 1"])]), "reject"],
  ["attr-numeric-type", er([block("A", ["1 id"])]), "reject"],
  ["attr-name-with-dots", er([block("A", ["string a.b"])]), "ok"],
  ["attr-name-with-comma", er([block("A", ["string a,b"])]), "ok"],
  ["attr-name-unicode", er([block("A", ["string 客户"])]), "ok"],
  ["attr-hash-inside", er([block("A", ["string id #x"])]), "reject"],
  ["attr-percent-comment-inside", er([block("A", ["string id PK %% note"])]), "reject"],
  ["attr-comment-line-inside", er([block("A", ["%% note", "string id PK"])]), "ok"],
  // ---- entity blocks
  ["block-empty-lines", er([block("A", [])]), "ok"],
  ["block-empty-one-line", er(["A {}"]), "ok"],
  ["attr-on-brace-line", er(["A { string id PK }"]), "ok"],
  ["attr-on-brace-line-then-more", er(["A { string id PK", "int age }"]), "ok"],
  ["block-close-then-relationship", er(["A {", "string id PK } B ||--o{ C : x"]), "ok"],
  ["block-unclosed", er(["A {", "string id PK"]), "reject"],
  ["block-extra-close", er([block("A", ["string id PK"]), "}"]), "reject"],
  ["block-nested-open", er(["A {", "string id PK", "{", "}"]), "reject"],
  ["block-blank-lines-inside", er(["A {", "", "string id", "", "}"]), "ok"],
  ["block-twice-appends", er([block("A", ["string id PK"]), block("A", ["int age"])]), "ok"],
  ["block-indented", er(["    A {", "        string id PK", "    }"]), "ok"],
  // ---- entities
  ["entity-bare", er(["CUSTOMER"]), "ok"],
  ["entity-bare-then-block", er(["CUSTOMER", block("CUSTOMER", ["string id PK"])]), "ok"],
  ["entity-block-then-bare", er([block("CUSTOMER", ["string id PK"]), "CUSTOMER"]), "ok"],
  ["entity-two-on-one-line", er(["A B"]), "ok"],
  ["entity-then-relationship-on-one-line", er(["C A ||--o{ B : x"]), "ok"],
  ["entity-name-hyphen", er(["ORDER-LINE-ITEM"]), "ok"],
  ["entity-name-dots", er(["a.b.c"]), "ok"],
  ["entity-name-underscore-digit", er(["Order_Line_2"]), "ok"],
  ["entity-name-unicode", er(["客户"]), "ok"],
  ["entity-name-accented", er(["Café"]), "ok"],
  ["entity-name-digits", er(["123"]), "ok"],
  ["entity-name-decimal", er(["1.5"]), "ok"],
  ["entity-name-one", er(["1"]), "ok"],
  ["entity-name-star", er(["*"]), "ok"],
  ["entity-name-star-prefix", er(["*ABC"]), "ok"],
  ["entity-name-leading-digit-word", er(["1abc"]), "ok"],
  ["entity-name-parentheses", er(["a(b)"]), "reject"],
  ["entity-name-comma", er(["a,b"]), "reject"],
  ["entity-name-hyphen-leading", er(["-a"]), "ok"],
  ["entity-name-single-quotes", er(["'Single Quoted'"]), "reject"],
  ["entity-name-backticks", er(["`Backtick Entity`"]), "reject"],
  ["entity-name-quoted", er(['"Quoted Entity"']), "ok"],
  ["entity-name-quoted-with-block", er([block('"Quoted Entity"', ["string id PK"])]), "ok"],
  ["entity-name-quoted-empty", er(['""']), "reject"],
  ["entity-name-quoted-percent", er(['"A%B"']), "reject"],
  ["entity-name-quoted-backslash", er(['"A\\"B"']), "reject"],
  ["entity-name-quoted-unterminated", er(['"Unterminated']), "reject"],
  ["entity-quoted-after-name", er(['ENTITY "Display Name"']), "ok"],
  ["entity-quoted-after-name-with-block", er(['USER "Application User" {', "string id PK", "}"]), "ok"],
  // ---- aliases
  ["alias-plain", er(["a[Person]"]), "ok"],
  ["alias-space-before-bracket", er(["a [Person]"]), "ok"],
  ["alias-quoted-spaces", er(['a["Person Name"]']), "ok"],
  ["alias-quoted-brackets", er(['a["Per[son]"]']), "ok"],
  ["alias-with-block", er([block("a[Person]", ["string id PK"])]), "ok"],
  ["alias-with-block-no-space", er(["a[Person]{", "string id PK", "}"]), "ok"],
  ["alias-with-class-and-block", er(["a[Person] ::: hot {", "string id PK", "}", "classDef hot fill:#f96"]), "ok"],
  ["alias-with-class-no-block", er(["a[Person]:::hot", "classDef hot fill:#f96"]), "ok"],
  ["alias-with-two-classes", er(["a[Person]:::hot,cold {", "string id PK", "}", "classDef hot fill:#f96", "classDef cold stroke:#00f"]), "ok"],
  ["alias-after-plain-declaration", er([block("a", ["string id PK"]), "a[Person]"]), "ok"],
  ["alias-before-plain-declaration", er(["a[Person]", block("a", ["string id PK"])]), "ok"],
  ["alias-after-relationship", er([rel("a", "b"), "a[Person]"]), "ok"],
  ["alias-used-in-relationship", er(["a[Person]", "b[Company]", rel("a", "b", "works")]), "ok"],
  ["alias-in-relationship-left", er(["a[Person] ||--o{ b : works"]), "reject"],
  ["alias-in-relationship-right", er(["a ||--o{ b[Company] : works"]), "reject"],
  ["alias-empty", er(["a[]"]), "reject"],
  ["alias-empty-with-block", er(["a[] {", "string id PK", "}"]), "reject"],
  ["alias-two-words-unquoted", er(["a[Person With Spaces]"]), "reject"],
  ["alias-quoted-empty", er(['a[""]']), "reject"],
  ["alias-quoted-escaped-quote", er(['a["Per\\"son"]']), "reject"],
  ["alias-double-open", er(["a[[Person]"]), "reject"],
  ["alias-double-close", er(["a[Person]]"]), "reject"],
  ["alias-then-space-then-alias", er(["a[Person] [Other]"]), "reject"],
  ["alias-then-word-then-alias", er(["a[Person]x[Other]"]), "ok"],
  ["alias-then-word-no-space", er(["a[Person]x"]), "ok"],
  ["alias-then-quoted", er(['a[Person] "Display"']), "ok"],
  ["alias-then-colon", er(["a[Person] : label"]), "reject"],
  ["alias-then-relationship", er(["a[Person] ||--o{ b : works"]), "reject"],
  ["alias-cardinality-word-one", er(["a[one]"]), "reject"],
  ["alias-cardinality-word-many", er(["a[many]"]), "reject"],
  ["alias-cardinality-word-two", er(["a[two]"]), "ok"],
  ["alias-cardinality-glyph", er(["a[1+]"]), "reject"],
  ["alias-pipe-glyph", er(["a[|o]"]), "reject"],
  ["alias-circle-brace-glyph", er(["a[o{]"]), "reject"],
  ["alias-one", er(["a[1]"]), "ok"],
  ["alias-one-then-word", er(["a[1 x]"]), "reject"],
  ["alias-parenthesised", er(["a[Person(1)]"]), "reject"],
  ["alias-digits", er(["a[123]"]), "ok"],
  ["alias-decimal", er(["a[1.5]"]), "ok"],
  ["alias-star", er(["a[*]"]), "ok"],
  ["alias-star-suffix", er(["a[Person*]"]), "ok"],
  ["alias-underscore", er(["a[person_info]"]), "ok"],
  ["alias-hyphen", er(["a[Person-Info]"]), "ok"],
  ["alias-dots", er(["a[per.son-name]"]), "ok"],
  ["alias-unicode", er(["a[客户信息]"]), "ok"],
  ["alias-accented", er(["a[Café]"]), "ok"],
  ["alias-redefined", er(["a[One]", "a[Two]"]), "reject"],
  ["alias-on-digit-id", er(["1[One]"]), "reject"],
  ["alias-on-hyphen-id", er(["a-b[Person]"]), "ok"],
  ["alias-on-dotted-id", er(["a.b[Person]"]), "ok"],
  ["alias-on-quoted-id", er(['"a b"[Alias]']), "ok"],
  ["alias-on-quoted-id-with-space", er(['"a b" [Alias]']), "ok"],
  // ---- classes and styles
  ["class-inline", er(["a:::hot", "classDef hot fill:#f96"]), "ok"],
  ["class-inline-with-block", er(["a:::hot {", "string id PK", "}", "classDef hot fill:#f96"]), "ok"],
  ["class-inline-two", er(["a:::hot,cold {", "string id PK", "}", "classDef hot fill:#f96", "classDef cold stroke:#00f"]), "ok"],
  ["class-inline-quoted", er(['a:::"hot" {', "string id PK", "}", "classDef hot fill:#f96"]), "reject"],
  ["class-inline-brackets", er(["a:::[hot] {", "string id PK", "}"]), "reject"],
  ["class-inline-empty", er(["a::: {", "string id PK", "}"]), "reject"],
  ["class-inline-trailing-comma", er(["a:::hot, {", "string id PK", "}"]), "reject"],
  ["class-inline-on-undefined-entity", er(["a:::nowhere"]), "ok"],
  ["class-statement", er([block("a", ["string id PK"]), "class a hot", "classDef hot fill:#f96"]), "ok"],
  ["class-statement-undefined-entity", er(["class ghost hot", "classDef hot fill:#f96"]), "ok"],
  ["class-statement-before-entity", er(["class a hot", block("a", ["string id PK"]), "classDef hot fill:#f96"]), "ok"],
  ["class-statement-two-entities", er(["a", "b", "class a,b hot", "classDef hot fill:#f96"]), "ok"],
  ["class-statement-two-classes", er(["a", "class a hot,cold", "classDef hot fill:#f96", "classDef cold stroke:#00f"]), "ok"],
  ["class-statement-duplicate", er(["a", "class a hot", "class a hot", "classDef hot fill:#f96"]), "ok"],
  ["class-statement-no-class", er(["a", "class a"]), "reject"],
  ["class-statement-no-entity", er([block("a", ["string id PK"]), "class"]), "reject"],
  ["class-def", er(["a:::hot", "classDef hot fill:#f96,stroke:#333,stroke-width:2px"]), "ok"],
  ["class-def-semicolon", er(["a:::hot", "classDef hot fill:#f96;"]), "ok"],
  ["class-def-space-after-colon", er(["a:::hot", "classDef hot fill: #f96"]), "ok"],
  ["class-def-empty-styles", er(["a:::hot", "classDef hot"]), "reject"],
  ["class-def-redefined-appends", er(["a:::hot", "classDef hot fill:#f96", "classDef hot stroke:#333"]), "ok"],
  ["class-def-color-text-style", er(["a:::hot", "classDef hot fill:#f96,color:#00f"]), "ok"],
  ["class-def-two-targets", er(["a:::x,y", "classDef x fill:#f96", "classDef y stroke:#333"]), "ok"],
  ["class-def-quoted-name", er(["a:::hot", 'classDef "hot" fill:#f96']), "reject"],
  ["class-def-bracket-name", er(["a:::hot", "classDef [hot] fill:#f96"]), "reject"],
  ["style-statement", er([block("a", ["string id PK"]), "style a fill:#f96,stroke:#333"]), "ok"],
  ["style-statement-two-entities", er(["a", "b", "style a,b fill:#f96"]), "ok"],
  ["style-statement-no-entity", er([block("a", ["string id PK"]), "style"]), "reject"],
  ["style-statement-no-styles", er(["a", "style a"]), "reject"],
  ["style-statement-undefined-entity", er(["style ghost fill:#f96"]), "ok"],
  ["style-statement-before-entity", er(["style a fill:#f96", "a"]), "ok"],
  ["style-statement-semicolon", er(["a", "style a fill:#f96;"]), "ok"],
  ["style-and-class", er(["a:::hot", "style a stroke:#333", "classDef hot fill:#f96"]), "ok"],
  // ---- metadata
  ["acc-title", er(["accTitle: ER Model", "A"]), "ok"],
  ["acc-title-no-space", er(["accTitle:ER Model", "A"]), "ok"],
  ["acc-title-empty-swallows-next-line", er(["accTitle:", "A"]), "ok"],
  ["acc-title-after-entities", er(["A", "accTitle: Late"]), "ok"],
  ["acc-title-twice", er(["accTitle: One", "accTitle: Two", "A"]), "ok"],
  ["acc-title-uppercase-keyword", er(["ACCTITLE: Shout", "A"]), "ok"],
  ["acc-title-unicode", er(["accTitle: 标题", "A"]), "ok"],
  ["acc-descr-line", er(["accDescr: One line", "A"]), "ok"],
  ["acc-descr-block", er(["accDescr {", "first line", "second line", "}", "A"]), "ok"],
  ["acc-descr-block-one-line", er(["accDescr { one line }", "A"]), "ok"],
  ["acc-descr-block-unterminated", er(["accDescr {", "first line", "A {", "string id PK", "}"]), "ok"],
  ["acc-descr-line-then-block", er(["accDescr: One", "accDescr {", "Two", "}", "A"]), "ok"],
  ["direction-tb", er(["direction TB", rel("A", "B")]), "ok"],
  ["direction-bt", er(["direction BT", rel("A", "B")]), "ok"],
  ["direction-rl", er(["direction RL", rel("A", "B")]), "ok"],
  ["direction-lr", er(["direction LR", rel("A", "B")]), "ok"],
  ["direction-lowercase-ignored", er(["direction tb", rel("A", "B")]), "ok"],
  ["direction-embedded-in-text", er(["some direction TB text", "A"]), "ok"],
  ["direction-unknown-is-entities", er(["direction XX", "A"]), "ok"],
  ["direction-lr-on-line-with-entity", er(["A direction LR", "B"]), "ok"],
  // ---- comments
  ["comment-line", er(["%% a comment", "A"]), "ok"],
  ["comment-only", er(["%% nothing"]), "ok"],
  ["comment-no-space", er(["%%comment", "A"]), "ok"],
  ["comment-trailing-after-entity", er(["A %% trailing"]), "reject"],
  ["comment-trailing-after-relationship", er(["A ||--o{ B : x %% note"]), "reject"],
  ["hash-comment", er(["# hash comment", "A"]), "reject"],
  ["slash-comment", er(["// slash comment", "A"]), "reject"],
  // ---- header
  ["header-only", "erDiagram", "ok"],
  ["header-trailing-space", "erDiagram \nA", "ok"],
  ["header-with-colon", "erDiagram:\nA", "reject"],
  ["header-twice", "erDiagram\nA\nerDiagram\nB", "reject"],
  ["header-prefixed", "erDiagrams\nA", "reject"],
  ["header-uppercase", "ERDIAGRAM\nA", "nodetect"],
  ["header-mixed-case", "ErDiagram\nA", "nodetect"],
  ["no-header", "A ||--o{ B : x", "nodetect"],
  ["header-late", "A\n\nerDiagram\nB", "nodetect"],
  ["init-directive", '%%{init: {"theme":"forest"}}%%\nerDiagram\nA ||--o{ B : x', "ok"],
  ["init-alias-config", '%%{init: {"er": {"layoutDirection":"TB"}}}%%\nerDiagram\nA ||--o{ B : x', "ok"],
  // ---- whitespace
  ["blank-lines", "erDiagram\n\nA\n\n\nB ||--o{ A : x\n", "ok"],
  ["leading-blank-line", "\n\nerDiagram\nA", "ok"],
  ["crlf", "erDiagram\r\nA {\r\nstring id PK\r\n}\r\nB ||--o{ A : x", "ok"],
  ["cr-only", "erDiagram\rA ||--o{ B : x", "ok"],
  ["no-trailing-newline", "erDiagram\nA ||--o{ B : x", "ok"],
  ["tabs", "erDiagram\n\tA ||--o{ B :\tx", "ok"],
  ["multiple-spaces", "erDiagram\nA   ||--o{   B   :   x", "ok"],
  ["no-spaces-at-all", "erDiagram\nA||--o{B:x", "ok"],
  // ---- stray tokens
  ["stray-close-brace", er(["}"]), "reject"],
  ["stray-open-brace", er(["{"]), "reject"],
  ["stray-close-bracket", er(["]"]), "reject"],
  ["stray-colon", er([":"]), "reject"],
  ["stray-separator", er([":::"]), "reject"],
  ["stray-cardinality", er(["||"]), "reject"],
  ["stray-comma", er([","]), "reject"],
  ["stray-semicolon", er([";"]), "reject"],
  ["stray-hash", er(["#"]), "reject"],
  ["stray-at", er(["@"]), "reject"],
  ["stray-slash", er(["/"]), "reject"],
  ["stray-dollar", er(["$"]), "reject"],
  ["stray-equals", er(["="]), "reject"],
  ["stray-question", er(["?"]), "reject"],
  ["stray-tilde", er(["~"]), "reject"],
  ["stray-backslash", er(["\\"]), "reject"],
  ["stray-caret", er(["^"]), "reject"],
  ["stray-ampersand", er(["&"]), "reject"],
  ["stray-exclamation", er(["!"]), "reject"],
  ["stray-plus", er(["+"]), "reject"],
  ["stray-less-than", er(["<"]), "reject"],
  ["stray-greater-than", er([">"]), "reject"],
  ["stray-quoted-unterminated", er(['"x']), "reject"],
  ["stray-open-parenthesis", er(["("]), "reject"],
  ["unknown-words-are-entities", er(["something else here"]), "ok"],
  ["title-line-is-entities", er(["title My ER", "A"]), "ok"],
  ["title-with-colon", er(["title: My ER", "A"]), "reject"],
];

const browser = await puppeteer.launch({
  headless: "new",
  executablePath: chrome,
  args: ["--allow-file-access-from-files", "--disable-gpu"],
});
const hostPage = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;

const generated = [];
for (const [id, source, verdict] of cases) {
  const page = await browser.newPage();
  await page.goto(hostPage);
  const result = await page.evaluate(
    async ({ id, source, moduleUrl }) => {
      const { default: mermaid } = await import(moduleUrl);
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict", deterministicIds: true, deterministicIDSeed: "er-grammar" });

      const classify = (error) => {
        const message = String(error?.message ?? error);
        if (message.startsWith("No diagram type detected")) return "no-diagram";
        if (message.includes("Lexical error")) return "lexer";
        if (message.includes("Parse error")) return "parser";
        return "runtime";
      };
      const describe = (error) => {
        const message = String(error?.message ?? error);
        // The line upstream shows the user. When the parser trips over the
        // newline that ends a statement, error.hash.line points at the next
        // line while the message names the statement's own.
        const shown = /error on line (\d+)/.exec(message);
        return {
          class: classify(error),
          message,
          ...(shown ? { messageLine: Number(shown[1]) } : {}),
          ...(Number.isInteger(error?.hash?.line) ? { line: error.hash.line + 1 } : {}),
          ...(Number.isInteger(error?.hash?.loc?.first_column) ? { column: error.hash.loc.first_column + 1 } : {}),
        };
      };

      let diagram;
      try {
        diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
      } catch (error) {
        return { id, source, accept: false, reject: describe(error) };
      }
      if (diagram.type !== "er") return { id, source, accept: false, reject: { class: `other-diagram:${diagram.type}` } };

      const db = diagram.db;
      const names = new Map([...db.getEntities()].map(([name, node]) => [node.id, name]));
      const data = {
        accTitle: db.getAccTitle(),
        accDescription: db.getAccDescription(),
        direction: db.getDirection(),
        // Map order is declaration order; the renderer positions nodes in it.
        entities: [...db.getEntities()].map(([name, node]) => ({
          name,
          alias: node.alias,
          attributes: (node.attributes ?? []).map((a) => ({ type: a.type, name: a.name, keys: a.keys ?? [], comment: a.comment ?? "" })),
          cssClasses: node.cssClasses,
          cssStyles: node.cssStyles ?? [],
        })),
        relationships: db.getRelationships().map((r) => ({
          entityA: names.get(r.entityA),
          roleA: r.roleA,
          entityB: names.get(r.entityB),
          // Upstream names the cardinalities from the relationship's point of
          // view: relSpec.cardA is the one next to entityB (arrowTypeEnd).
          startCard: r.relSpec.cardB,
          relType: r.relSpec.relType,
          endCard: r.relSpec.cardA,
        })),
        // classDef table: id -> accumulated declarations and text styles.
        classes: [...db.getClasses()].map(([id, definition]) => ({ id, styles: definition.styles, textStyles: definition.textStyles })),
      };

      let renderAccept = true;
      let renderReject = null;
      try {
        await mermaid.render(`er-grammar-${id}`, source);
      } catch (error) {
        renderAccept = false;
        renderReject = describe(error);
      }
      return { id, source, accept: true, expectedDb: data, renderAccept, ...(renderReject ? { renderReject } : {}) };
    },
    { id, source, moduleUrl: pathToFileURL(moduleFile).href },
  );
  generated.push(result);
  await page.close();
}
await browser.close();

const ids = new Set(generated.map((entry) => entry.id));
if (ids.size !== generated.length) throw new Error("duplicate case id");
const mislabelled = [];
for (const [index, entry] of generated.entries()) {
  const expected = cases[index][2];
  const actual = entry.accept ? "ok" : entry.reject.class === "no-diagram" ? "nodetect" : "reject";
  if (expected !== actual) {
    mislabelled.push(
      `case ${entry.id} is ${actual} upstream but declared ${expected}` +
        (entry.reject?.message ? `: ${entry.reject.message.replace(/\n/g, " ").slice(0, 160)}` : ""),
    );
  }
}
if (mislabelled.length) throw new Error(mislabelled.join("\n"));

const fixture = {
  upstream: {
    package: "mermaid",
    version: packageJson.version,
    license: packageJson.license,
    moduleSha256: sha256(fs.readFileSync(moduleFile)),
  },
  cases: generated,
};
fixture.fixtureSha256 = sha256(JSON.stringify(fixture));
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, `${JSON.stringify(fixture, null, 2)}\n`);
const accepted = generated.filter((entry) => entry.accept);
const rejected = generated.filter((entry) => !entry.accept);
console.log(
  `Wrote ${output}: ${generated.length} cases, ${accepted.length} DB accepts, ` +
    `${accepted.filter((entry) => entry.renderAccept).length} render accepts, ${rejected.length} rejects ` +
    `(${[...new Set(rejected.map((entry) => entry.reject.class))].join(", ")})`,
);

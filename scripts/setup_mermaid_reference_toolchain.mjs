#!/usr/bin/env node
// Restores the sibling mermaid reference toolchain that the fixture generators
// in scripts/generate_mermaid_*.mjs depend on. The generators import
//   ../mermaid-cli/node_modules/{mermaid,dagre-d3-es,puppeteer}
// and drive the system Chrome via puppeteer to capture real mermaid 11.16.0
// output (geometry / pixel / dagre-snapshot references).
//
// This script is idempotent: safe to re-run. It (re)creates the sibling dir's
// package.json, installs the pinned deps (skipping puppeteer's bundled Chromium
// since the generators launch the system Chrome), and writes the two small
// files the generators need that a bare npm install does NOT provide:
//   - index.html with a #container mount (generators render into it)
//   - a shim at node_modules/puppeteer/lib/puppeteer/puppeteer.js so the
//     generators' legacy deep import path resolves to the package main
//     (modern puppeteer moved the file under lib/cjs/).
//
// Usage:  node scripts/setup_mermaid_reference_toolchain.mjs [sibling-path]
// Default sibling path is <repo>/../mermaid-cli (matches the generators).
//
// After setup, verify reproducibility by regenerating a committed fixture and
// byte-comparing, e.g.:
//   node scripts/generate_mermaid_flowchart_geometry_fixture.mjs \
//     "<sibling>/node_modules/mermaid" /tmp/fg.json \
//     "C:/Program Files/Google/Chrome/Application/chrome.exe"
//   diff tests/fixtures/mermaid/flowchart-geometry.json /tmp/fg.json   # => identical

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { execSync } from "node:child_process";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const sibling = process.argv[2]
  ? path.resolve(process.argv[2])
  : path.join(repoRoot, "..", "mermaid-cli");

// mermaid 11.16.0 depends on dagre-d3-es@7.0.14 (its own internal dagre), so the
// snapshots harness must use the same version to reproduce flowchart geometry.
// puppeteer ^21 works with node 24; the shim below makes the version irrelevant
// to the generators' import path.
const packageJson = {
  name: "mermaid-cli-sibling",
  private: true,
  dependencies: {
    mermaid: "11.16.0",
    "dagre-d3-es": "7.0.14",
    puppeteer: "^21.11.0",
  },
};

const indexHtml = `<!DOCTYPE html>
<html><head><meta charset="utf-8"></head><body><div id="container"></div></body></html>
`;

// Modern puppeteer's main is lib/cjs/puppeteer/puppeteer.js; the generators
// import the pre-cjs-split path lib/puppeteer/puppeteer.js. Re-exporting the
// package main makes any puppeteer version work without editing generators.
const puppeteerShim = `// Shim: generators import node_modules/puppeteer/lib/puppeteer/puppeteer.js
// (a pre-cjs/esm-split path). Re-export the package main so any puppeteer
// version whose main exports the Puppeteer object works without editing generators.
module.exports = require('puppeteer');
`;

console.log(`Setting up mermaid reference toolchain at: ${sibling}`);
fs.mkdirSync(sibling, { recursive: true });
fs.writeFileSync(path.join(sibling, "package.json"), JSON.stringify(packageJson, null, 2) + "\n");
fs.writeFileSync(path.join(sibling, "index.html"), indexHtml);

console.log("Running npm install (skipping bundled Chromium; generators use system Chrome)...");
execSync("npm install --no-audit --no-fund", {
  cwd: sibling,
  stdio: "inherit",
  env: {
    ...process.env,
    PUPPETEER_SKIP_DOWNLOAD: "true",
    PUPPETEER_SKIP_CHROMIUM_DOWNLOAD: "true",
  },
});

const shimDir = path.join(sibling, "node_modules", "puppeteer", "lib", "puppeteer");
fs.mkdirSync(shimDir, { recursive: true });
fs.writeFileSync(path.join(shimDir, "puppeteer.js"), puppeteerShim);

const mermaidVersion = JSON.parse(
  fs.readFileSync(path.join(sibling, "node_modules", "mermaid", "package.json"), "utf8"),
).version;
const dagreVersion = JSON.parse(
  fs.readFileSync(path.join(sibling, "node_modules", "dagre-d3-es", "package.json"), "utf8"),
).version;

console.log("");
console.log(`Done. mermaid ${mermaidVersion}, dagre-d3-es ${dagreVersion}.`);
console.log("Verify reproducibility by regenerating a fixture and byte-comparing:");
console.log(`  node scripts/generate_mermaid_flowchart_geometry_fixture.mjs \\`);
console.log(`    "${path.join(sibling, "node_modules", "mermaid").replace(/\\/g, "/")}" /tmp/fg.json \\`);
console.log(`    "C:/Program Files/Google/Chrome/Application/chrome.exe"`);
console.log(`  diff tests/fixtures/mermaid/flowchart-geometry.json /tmp/fg.json`);

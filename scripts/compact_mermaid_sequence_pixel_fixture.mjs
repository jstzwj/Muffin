import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import {
  collectPngReferences,
  keepSequenceLabelCrop,
  sequenceScenePixelCaseIds,
} from "./mermaid_sequence_pixel_policy.mjs";

const outDir = path.resolve(process.argv[2] ??
  path.join("tests", "fixtures", "mermaid", "sequence-pixel"));
const manifestPath = path.join(outDir, "manifest.json");
const payload = JSON.parse(fs.readFileSync(manifestPath, "utf8"));

for (const fixture of payload.cases) {
  if (!sequenceScenePixelCaseIds.has(fixture.id)) {
    delete fixture.file;
    delete fixture.sha256;
  }
  if (!keepSequenceLabelCrop(fixture)) {
    delete fixture.cropFile;
    delete fixture.cropSha256;
  }
}

delete payload.fixtureSha256;
payload.fixtureSha256 = createHash("sha256")
  .update(JSON.stringify(payload)).digest("hex");
fs.writeFileSync(manifestPath, `${JSON.stringify(payload, null, 2)}\n`);

const references = collectPngReferences(payload);
let removed = 0;
let removedBytes = 0;
for (const name of fs.readdirSync(outDir).filter((name) => name.endsWith(".png"))) {
  if (references.has(name)) continue;
  const filePath = path.join(outDir, name);
  removedBytes += fs.statSync(filePath).size;
  fs.rmSync(filePath);
  ++removed;
}

console.log(`Kept ${references.size} sequence PNG oracles; removed ${removed} (${removedBytes} bytes)`);
console.log(`fixtureSha256=${payload.fixtureSha256}`);

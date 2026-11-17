import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import {fileURLToPath, pathToFileURL} from "node:url";
import {after, before, test} from "node:test";
import {chromium} from "playwright";
import {captureElementInk, scanInkBBox} from "./browser_ink_capture.mjs";

let browser;
before(async () => { browser = await chromium.launch({args: ["--font-render-hinting=none"]}); });
after(async () => { await browser?.close(); });

const withPage = async callback => {
  const page = await browser.newPage({viewport: {width: 600, height: 400}, deviceScaleFactor: 1});
  try { await callback(page); } finally { await page.close(); }
};

const setFixture = (page, css = "") => page.setContent(`
  <style>
    html, body { margin: 0; background: white; }
    #stage { position: absolute; left: 24px; top: 24px; }
    #target { position: relative; width: 40px; height: 20px; background: black; }
    ${css}
  </style><div id="stage"><div id="target"></div></div>`);
const capture = page => captureElementInk(page, {target: "#target", stage: "#stage"});

test("captures descendants outside the layout and the page's original origin", async () => withPage(async page => {
  await setFixture(page);
  await page.evaluate(() => {
    const child = document.createElement("span");
    child.style.cssText = "position:absolute;left:-80px;top:-40px;width:30px;height:20px;background:black";
    document.querySelector("#target").append(child);
  });
  const result = await capture(page);
  assert.equal(result.inkBBox.width, 120);
  assert.equal(result.inkBBox.height, 60);
  assert.ok(result.inkBBox.x >= 2 && result.inkBBox.y >= 2);
  assert.ok(result.clip.x >= 0 && result.clip.y >= 0);
}));

test("expands for paint overflow not reported by DOM rectangles", async () => withPage(async page => {
  await setFixture(page, "#target { box-shadow: 0 0 0 20px black; }");
  const result = await capture(page);
  assert.equal(result.inkBBox.width, 80);
  assert.equal(result.inkBBox.height, 60);
  assert.ok(result.inkBBox.x >= 2 && result.inkBBox.y >= 2);
}));

test("respects overflow clipping around oversized stretchy SVGs", async () => withPage(async page => {
  await setFixture(page, "#target { overflow: hidden; }");
  await page.locator("#target").evaluate(target => {
    target.innerHTML = '<svg width="8000" height="20" style="position:absolute;right:0"><rect width="8000" height="20"/></svg>';
  });
  const result = await capture(page);
  assert.equal(result.inkBBox.width, 40);
  assert.equal(result.inkBBox.height, 20);
  assert.ok(result.clip.width < 100, "clipped SVG geometry must not inflate the image");
  assert.equal((await page.locator("#target").boundingBox()).x, 24);
}));

test("fails instead of publishing a still-clipped image", async () => withPage(async page => {
  await setFixture(page, "#target { box-shadow: 0 0 0 20px black; }");
  await assert.rejects(captureElementInk(page, {target: "#target", stage: "#stage", maxAttempts: 1}), /refusing a clipped golden/);
}));

test("accepts invisible formula content", async () => withPage(async page => {
  await setFixture(page, "#target { visibility: hidden; }");
  assert.equal((await capture(page)).inkBBox, null);
}));

test("KaTeX lap ink is complete and layout metrics stay unchanged", async () => {
  const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
  const dist = path.join(repo, "tests/vendor/katex/dist");
  const temporary = await fs.mkdtemp(path.join(os.tmpdir(), "muffin-ink-capture-"));
  try {
    const html = path.join(temporary, "math.html");
    await fs.writeFile(html, `<!doctype html><link rel="stylesheet" href="${pathToFileURL(path.join(dist, "katex.min.css")).href}">
      <style>html,body{margin:0;background:white}body{font-size:16px;color:#111}.wrap{display:inline-block;padding:24px}#math{display:inline-block}</style>
      <div class="wrap"><span id="math"></span></div>`);
    await withPage(async page => {
      await page.goto(pathToFileURL(html).href);
      await page.addScriptTag({path: path.join(dist, "katex.min.js")});
      await page.evaluate(async () => {
        katex.render("\\mathllap{a}b+\\mathrlap{c}d+\\mathclap{e}f", document.querySelector("#math"), {output: "html"});
        await document.fonts.ready;
      });
      const before = await page.locator(".katex-html").boundingBox();
      const oldClip = {x: Math.floor(before.x - 4), y: Math.floor(before.y - 4), width: Math.ceil(before.width + 8), height: Math.ceil(before.height + 8)};
      const oldInk = await scanInkBBox(await page.screenshot({clip: oldClip}));
      assert.equal(oldInk.x, 0, "the previous generator cropped the leading glyph");
      const result = await captureElementInk(page, {target: ".katex-html", stage: ".wrap"});
      assert.ok(result.inkBBox.width > oldInk.width, "overflow must be measured, not discarded");
      const after = await page.locator(".katex-html").boundingBox();
      assert.equal(after.width, before.width);
      assert.equal(after.height, before.height);
      // Independently scan a generous screenshot: the helper must retain all ink.
      const fullInk = await scanInkBBox(await page.screenshot({fullPage: true}));
      assert.equal(result.inkBBox.width, fullInk.width);
      assert.equal(result.inkBBox.height, fullInk.height);
    });
  } finally { await fs.rm(temporary, {recursive: true, force: true}); }
});

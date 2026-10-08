// Refresh browser reference data for DocumentStyleConformanceTest.
// Requires Playwright; MUFFIN_PLAYWRIGHT_MODULE can point to an installed module.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const moduleName = process.env.MUFFIN_PLAYWRIGHT_MODULE;
const { chromium } = await import(moduleName ? pathToFileURL(path.resolve(moduleName)).href : "playwright");
const browser = await chromium.launch({
  headless: true,
  ...(process.env.CHROME_EXECUTABLE ? { executablePath: process.env.CHROME_EXECUTABLE } : {}),
});
const base = fs.readFileSync(path.join(root, "resources/themes/document-base.css"), "utf8");
const cases = [];
try {
  const page = await browser.newPage({ viewport: { width: 800, height: 700 } });
  for (const tag of ["h1", "p", "pre"]) {
    for (const sizing of ["content-box", "border-box"]) {
      const css = `#write{max-width:500px;padding:0;margin:0}body{color:black}
        ${tag}{width:240px;height:100px;box-sizing:${sizing};padding:7px 13px 11px 17px;
        border:3px solid red;font-size:30px;line-height:2;margin:0}`;
      await page.setContent(`<style>${base}${css}</style><div id="write"><${tag}>Text</${tag}></div>`);
      const expected = await page.evaluate((tag) => {
        const element = document.querySelector(`#write > ${tag}`);
        const style = getComputedStyle(element), rect = element.getBoundingClientRect();
        const left = parseFloat(style.paddingLeft) + parseFloat(style.borderLeftWidth);
        const top = parseFloat(style.paddingTop) + parseFloat(style.borderTopWidth);
        return { width: rect.width, height: rect.height, left, top,
          contentWidth: rect.width - left - parseFloat(style.paddingRight) - parseFloat(style.borderRightWidth),
          fontSize: parseFloat(style.fontSize) };
      }, tag);
      cases.push({ id: `${tag}-${sizing}`, tag, css, expected });
    }
  }
  const output = { browser: await browser.version(), viewport: { width: 800, height: 700 }, cases };
  fs.writeFileSync(path.join(root, "tests/fixtures/theme/document-box-browser.json"), JSON.stringify(output, null, 2) + "\n");
} finally {
  await browser.close();
}

// Pinned-font representative Abyss/LaTeX typography, not full-theme screenshots.
// node scripts/generate_theme_typography_reference.mjs
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {pathToFileURL} from 'node:url';
const root = path.resolve(import.meta.dirname, '..');
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const sources = {};
const read = file => {
  const text = fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
  sources[file] = hash(text);
  return text;
};
const fontReference = JSON.parse(read('tests/fixtures/theme/text-backend-browser.json'));
const faces = fontReference.fonts.map(font => {
  const bytes = fs.readFileSync(path.join(root, font.file));
  if (hash(bytes) !== font.sha256) throw new Error(`Changed font: ${font.file}`);
  return `@font-face{font-family:${font.family};font-weight:${font.weight};font-style:${font.style};src:url(data:font/woff2;base64,${bytes.toString('base64')})}`;
}).join('\n');
const base = read('resources/themes/document-base.css');
const markdown = '# Muffin Markdown Example 中文混排\n\nPlain **bold** *italic* `code` <kbd>Ctrl</kbd> + <kbd>S</kbd> office fi wrap.\n\n## Section 中文混排\n\nA longer paragraph with several words to exercise spacing and line wrapping.\n';
const html = '<h1 class="md-heading">Muffin Markdown Example 中文混排</h1><p>Plain <strong>bold</strong> <em>italic</em> <code>code</code> <kbd>Ctrl</kbd> + <kbd>S</kbd> office fi wrap.</p><h2 class="md-heading">Section 中文混排</h2><p>A longer paragraph with several words to exercise spacing and line wrapping.</p>';
const {chromium} = await import(process.env.MUFFIN_PLAYWRIGHT_MODULE ?
  pathToFileURL(path.resolve(process.env.MUFFIN_PLAYWRIGHT_MODULE)).href : 'playwright');
const browser = await chromium.launch({headless: true,
  ...(process.env.CHROME_EXECUTABLE ? {executablePath: process.env.CHROME_EXECUTABLE} : {})});
const cases = [];
try {
  for (const theme of ['abyss', 'latex']) {
    const cssFile = `tests/fixtures/theme/${theme}-typography.css`, css = read(cssFile);
    for (const width of [320, 720, 960]) {
      const page = await browser.newPage({viewport: {width, height: 1000}, deviceScaleFactor: 1});
      await page.setContent(`<!doctype html><style>${faces}${base}${css}</style><div id="write">${html}</div>`);
      await page.evaluate(async () => {
        for (const element of document.querySelectorAll('#write,#write *')) {
          const style = getComputedStyle(element);
          await document.fonts.load(`${style.fontStyle} ${style.fontWeight} ${style.fontSize} ${style.fontFamily}`);
        }
        await document.fonts.ready;
      });
      const client = await page.context().newCDPSession(page);
      await client.send('DOM.enable'); await client.send('CSS.enable');
      const {root: documentRoot} = await client.send('DOM.getDocument');
      for (const tag of ['h1', 'p', 'h2']) {
        const {nodeId} = await client.send('DOM.querySelector', {nodeId: documentRoot.nodeId, selector: tag});
        const {fonts} = await client.send('CSS.getPlatformFontsForNode', {nodeId});
        if (fonts.some(font => !font.isCustomFont)) throw new Error(`Unexpected OS fallback in ${theme}/${tag}`);
      }
      const expected = await page.evaluate(() => {
        const rect = r => [r.x, r.y, r.width, r.height];
        const host = document.querySelector('#write');
        return {page: rect(host.getBoundingClientRect()), blocks: [...host.children].map(element => {
          const baseline = first => {
            const marker = document.createElement('span');
            marker.style.cssText = 'display:inline-block;width:0;height:0;padding:0;margin:0;border:0;vertical-align:baseline';
            if (first) element.prepend(marker); else element.append(marker);
            const y = marker.getBoundingClientRect().y; marker.remove(); return y;
          };
          const firstBaseline = baseline(true), lastBaseline = baseline(false);
          const walker = document.createTreeWalker(element, NodeFilter.SHOW_TEXT), characters = [];
          while (walker.nextNode()) {
            const text = walker.currentNode;
            for (let i = 0; i < text.length; ++i) {
              const range = new Range(); range.setStart(text, i); range.setEnd(text, i + 1);
              const r = range.getBoundingClientRect(); characters.push({x: r.x, y: r.y, width: r.width});
            }
          }
          return {border: rect(element.getBoundingClientRect()), firstBaseline, lastBaseline, characters,
            components: [...element.querySelectorAll('code,kbd')].map(e => ({border: rect(e.getBoundingClientRect())}))};
        })};
      });
      cases.push({id: `${theme}-${width}`, cssFile, width, expected});
      await page.close();
    }
  }
  fs.writeFileSync(path.join(root, 'tests/fixtures/theme/theme-typography-browser.json'),
    JSON.stringify({browser: await browser.version(), sources, markdown, cases}, null, 2) + '\n');
} finally { await browser.close(); }

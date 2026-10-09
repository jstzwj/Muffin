// Exact font bytes, explicit Han fallback, and fractional CSS sizes.
// node scripts/generate_text_backend_reference.mjs
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {pathToFileURL} from 'node:url';

const root = path.resolve(import.meta.dirname, '..');
const sources = [
  ['MuffinFixtureAbyss', 'tests/fixtures/theme/fonts/abyss.woff2', 400, 'normal'],
  ['MuffinFixtureHan', 'tests/fixtures/theme/fonts/han.woff2', 400, 'normal'],
  ...['regular', 'bold', 'italic', 'bolditalic'].map(variant => [
    'MuffinFixtureLatex', `tests/fixtures/theme/fonts/latex-${variant}.woff2`,
    variant.startsWith('bold') ? 700 : 400, variant.includes('italic') ? 'italic' : 'normal',
  ]),
  ...['regular', 'italic', '700', '700italic'].map(variant => [
    'MuffinFixtureSans', `resources/themes/github/open-sans-v17-latin-ext_latin-${variant}.woff2`,
    variant.startsWith('700') ? 700 : 400, variant.includes('italic') ? 'italic' : 'normal',
  ]),
  ...['regular', 'italic', '700', '700italic'].map(variant => [
    'MuffinFixtureSerif', `resources/themes/newsprint/pt-serif-v11-latin-${variant}.woff2`,
    variant.startsWith('700') ? 700 : 400, variant.includes('italic') ? 'italic' : 'normal',
  ]),
];
const fonts = sources.map(([family, file, weight, style]) => {
  const bytes = fs.readFileSync(path.join(root, file));
  return {family, file, weight, style, sha256: createHash('sha256').update(bytes).digest('hex'), bytes};
});
const {chromium} = await import(process.env.MUFFIN_PLAYWRIGHT_MODULE
  ? pathToFileURL(path.resolve(process.env.MUFFIN_PLAYWRIGHT_MODULE)).href : 'playwright');
const browser = await chromium.launch({headless: true,
  ...(process.env.CHROME_EXECUTABLE ? {executablePath: process.env.CHROME_EXECUTABLE} : {})});
try {
  const page = await browser.newPage({viewport: {width: 800, height: 600}});
  const faces = fonts.map(f => `@font-face{font-family:${f.family};font-weight:${f.weight};font-style:${f.style};src:url(data:font/woff2;base64,${f.bytes.toString('base64')})}`).join('\n');
  await page.setContent(`<style>${faces}body{margin:0}#text{white-space:pre-wrap;overflow-wrap:anywhere}</style><div id=text></div>`);
  const client = await page.context().newCDPSession(page);
  await client.send('DOM.enable'); await client.send('CSS.enable');
  const {root: documentRoot} = await client.send('DOM.getDocument');
  const {nodeId} = await client.send('DOM.querySelector', {nodeId: documentRoot.nodeId, selector: '#text'});
  const cases = [];
  for (const [family, prefix] of [['MuffinFixtureAbyss', 'abyss'], ['MuffinFixtureSans', 'sans-han'],
                                ['MuffinFixtureSerif', 'serif'], ['MuffinFixtureLatex', 'latex']]) {
    // The imported LaTeX theme uses 9.5pt body text and 1.5/1.9em headings.
    const sizes = prefix === 'latex' ? [9.5 * 96 / 72, 19, 9.5 * 96 / 72 * 1.9, 28.8] : [16, 18.4, 28.8, 32.4];
    for (const size of sizes) for (const bold of [false, true]) for (const italic of [false, true])
      for (const width of italic ? [240, 1000] : [240]) {
      const entry = {id: `${prefix}-${size}-${bold}-${italic}-${width}`, family, size, bold, italic,
        text: 'Muffin Markdown Example 中文混排 office fi', width, letterSpacing: .7, wordSpacing: .3,
        diagnosticOnly: italic && width === 240};
      await page.locator('#text').evaluate(async (host, entry) => {
        host.style.cssText = `width:${entry.width}px;font-family:${entry.family},MuffinFixtureHan;font-size:${entry.size}px;font-weight:${entry.bold ? 700 : 400};font-style:${entry.italic ? 'italic' : 'normal'};letter-spacing:${entry.letterSpacing}px;word-spacing:${entry.wordSpacing}px;line-height:1.5`;
        host.textContent = entry.text;
        await document.fonts.load(`${entry.italic ? 'italic ' : ''}${entry.bold ? '700 ' : ''}${entry.size}px ${entry.family}`);
        await document.fonts.load(`${entry.size}px MuffinFixtureHan`);
        await document.fonts.ready;
      }, entry);
      const expected = await page.evaluate(() => {
        const host = document.querySelector('#text'), text = host.firstChild, lines = [], carets = [];
        for (let i = 0; i < text.length; ++i) {
          const range = new Range(); range.setStart(text, i); range.setEnd(text, i + 1);
          const box = range.getBoundingClientRect();
          // Mixed fonts have different ink heights; identify CSS lines by their
          // vertical centres rather than splitting on a fallback font's ink y.
          let line = lines.find(l => Math.abs(l.center - (box.y + box.height / 2)) < parseFloat(getComputedStyle(host).lineHeight) / 2);
          if (!line) { line = {start: i, end: i, y: box.y, center: box.y + box.height / 2}; lines.push(line); }
          line.end = i + 1;
          carets.push({x: box.x, line: lines.indexOf(line)});
        }
        return {lines: lines.map(({start, end}) => ({start, end})), carets, height: host.offsetHeight};
      });
      expected.platformFonts = (await client.send('CSS.getPlatformFontsForNode', {nodeId})).fonts;
      if (expected.platformFonts.some(font => !font.isCustomFont)) throw new Error(`Unexpected OS fallback: ${entry.id}`);
      cases.push({...entry, expected});
    }
  }
  const result = {browser: await browser.version(), fonts: fonts.map(({bytes, ...f}) => f), cases};
  fs.writeFileSync(path.join(root, 'tests/fixtures/theme/text-backend-browser.json'), JSON.stringify(result, null, 2) + '\n');
} finally { await browser.close(); }

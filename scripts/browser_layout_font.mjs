// Browser probes and native tests consume these authored font files.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';

export function browserLayoutFont(root) {
  const family = 'MuffinFixtureSans';
  const variants = [['regular', 400, 'normal'], ['italic', 400, 'italic'], ['700', 700, 'normal'], ['700italic', 700, 'italic']];
  const files = {};
  const css = variants.map(([suffix, weight, style]) => {
    const name = `github/open-sans-v17-latin-ext_latin-${suffix}.woff2`;
    const bytes = fs.readFileSync(path.join(root, 'resources/themes', name));
    files[name] = createHash('sha256').update(bytes).digest('hex');
    return `@font-face{font-family:${family};font-style:${style};font-weight:${weight};src:url(data:font/woff2;base64,${bytes.toString('base64')}) format('woff2')}`;
  }).join('\n');
  return {css, metadata: {family, files}, replace: html => html.replace(/\bArial\b/g, family)};
}

export async function loadBrowserLayoutFont(page, font) {
  await page.evaluate(async family => {
    for (const variant of ['16px', 'italic 16px', 'bold 16px', 'italic bold 16px']) {
      const faces = await document.fonts.load(`${variant} ${family}`);
      if (!faces.length) throw new Error(`Fixture font did not load: ${variant} ${family}`);
    }
    await document.fonts.ready;
  }, font.metadata.family);
}

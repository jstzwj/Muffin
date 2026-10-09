// Fixed-font browser geometry for the real bundled themes. References are
// committed; native CI compares geometry without downloading a browser.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {browserLayoutFont, loadBrowserLayoutFont} from './browser_layout_font.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const {chromium} = await import(process.env.MUFFIN_PLAYWRIGHT_MODULE ?
  pathToFileURL(path.resolve(process.env.MUFFIN_PLAYWRIGHT_MODULE)).href : 'playwright');
const browser = await chromium.launch({headless:true,
  ...(process.env.CHROME_EXECUTABLE ? {executablePath:process.env.CHROME_EXECUTABLE} : {})});
const font = browserLayoutFont(root), sources = {}, cases = [];
const read = relative => {
  const bytes = fs.readFileSync(path.join(root, relative));
  sources[relative] = createHash('sha256').update(bytes.toString().replace(/\r\n/g,'\n')).digest('hex');
  return bytes.toString();
};
const base = read('resources/themes/document-base.css');
const fixedFontCss = '#write,#write *{font-family:MuffinFixtureSans!important}';
const markdown = '# Title\n\nIntro text with `code` and <kbd>Ctrl</kbd>.\n\n## Section\n\nA second paragraph.\n';
try {
  for (const theme of ['newsprint', 'night']) {
    const css = read(`resources/themes/${theme}.css`) + '\n' + fixedFontCss;
    for (const width of [720, 960, 1440]) {
      const page = await browser.newPage({viewport:{width,height:1000},deviceScaleFactor:1});
      await page.setContent(`<!doctype html><style>${font.css}${base}${css}</style><div id="write"><h1>Title</h1><p>Intro text with <code>code</code> and <kbd>Ctrl</kbd>.</p><h2>Section</h2><p>A second paragraph.</p></div>`);
      await loadBrowserLayoutFont(page,font);
      const expected = await page.evaluate(() => {
        const rect = r => [r.x,r.y,r.width,r.height];
        const write = document.querySelector('#write');
        return {page:rect(write.getBoundingClientRect()),blocks:[...write.children].map(host => {
          const baseline = before => {
            const marker=document.createElement('span');
            marker.style.cssText='display:inline-block;width:0;height:0;padding:0;margin:0;border:0;vertical-align:baseline';
            if(before) host.prepend(marker); else host.append(marker);
            const y=marker.getBoundingClientRect().y; marker.remove(); return y;
          };
          const firstBaseline=baseline(true),lastBaseline=baseline(false);
          const walker=document.createTreeWalker(host,NodeFilter.SHOW_TEXT),characters=[];
          while(walker.nextNode()) {
            const text=walker.currentNode;
            for(let i=0;i<text.length;i++) {
              const range=new Range();range.setStart(text,i);range.setEnd(text,i+1);
              const r=range.getBoundingClientRect();characters.push({text:text.data[i],x:r.x,y:r.y,width:r.width});
            }
          }
          return {tag:host.tagName.toLowerCase(),border:rect(host.getBoundingClientRect()),firstBaseline,lastBaseline,characters,
            components:[...host.querySelectorAll('code,kbd')].map(e=>({tag:e.tagName.toLowerCase(),border:rect(e.getBoundingClientRect())}))};
        })};
      });
      cases.push({id:`${theme}-${width}`,theme,width,expected}); await page.close();
    }
  }
  fs.writeFileSync(path.join(root,'tests/fixtures/theme/real-theme-browser.json'),
    JSON.stringify({browser:await browser.version(),font:font.metadata,sources,fixedFontCss,markdown,cases},null,2)+'\n');
} finally {await browser.close();}

// Regenerate the generated-inline geometry reference from Chromium.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {browserLayoutFont, loadBrowserLayoutFont} from './browser_layout_font.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const moduleName = process.env.MUFFIN_PLAYWRIGHT_MODULE;
const {chromium} = await import(moduleName ? pathToFileURL(path.resolve(moduleName)).href : 'playwright');
const browser = await chromium.launch({headless:true,
  ...(process.env.CHROME_EXECUTABLE ? {executablePath:process.env.CHROME_EXECUTABLE} : {})});
const font = browserLayoutFont(root);
const cases = [
  ...[90,170].map(width => ({id:`marker-wrap-${width}`,width,text:'Long heading that wraps',
    pseudo:'h4::before{content:"";display:inline-block;width:12px;height:12px;margin-right:8px;background:red}'})),
  {id:'generated-text',width:170,text:'Long heading that wraps',
    pseudo:'h4::before{content:"1.2\u00a0";color:red}h4::after{content:"\u00a0end";color:blue}'},
  {id:'concatenated-strings',width:170,text:'Heading',
    pseudo:'h4::before{content:"pre " "fix ";color:red}'},
  {id:'suffix-wrap',width:90,text:'Heading text',
    pseudo:'h4::after{content:"";display:inline-block;width:40px;height:40px;vertical-align:top;background:blue}'},
  {id:'block-underline',width:170,text:'Heading',
    pseudo:'h4::after{content:"";display:block;width:100px;height:0;border-bottom:1px solid blue;margin:5px auto 0}'},
  {id:'invisible-box',width:90,text:'Long heading that wraps',
    pseudo:'h4::before{content:"";display:inline-block;width:20px;height:12px}'},
  {id:'zero-width-box',width:90,text:'Long heading that wraps',
    pseudo:'h4::before{content:"";display:inline-block;width:0;height:0;margin-right:8px}'},
  {id:'mixed-alignment',width:170,text:'Heading',
    pseudo:'h4::before{content:"";display:inline-block;width:12px;height:40px;vertical-align:top;background:red}' +
      'h4::after{content:"";display:inline-block;width:12px;height:60px;vertical-align:bottom;background:blue}'},
  {id:'mixed-alignment-reversed',width:170,text:'Heading',
    pseudo:'h4::before{content:"";display:inline-block;width:12px;height:60px;vertical-align:top;background:red}' +
      'h4::after{content:"";display:inline-block;width:12px;height:40px;vertical-align:bottom;background:blue}'},
];
try {
  const page = await browser.newPage({viewport:{width:400,height:600},deviceScaleFactor:1});
  for (const entry of cases) {
    entry.css = `html,body{margin:0}#write{margin:0;padding:0;max-width:none;width:${entry.width}px}
      h4{margin:0;padding:0;border:0;font-family:MuffinFixtureSans;font-size:16px;font-weight:400;line-height:24px}${entry.pseudo}`;
    await page.setContent(`<!doctype html><style>${font.css}${entry.css}</style><div id="write"><h4>${entry.text}</h4></div>`);
    await loadBrowserLayoutFont(page,font);
    entry.expected = await page.evaluate(() => {
      const host = document.querySelector('h4'), bounds = host.getBoundingClientRect(), text = host.firstChild;
      const boxes = [];
      for (let i=0;i<text.length;i++) {
        const range = new Range(); range.setStart(text,i);range.setEnd(text,i+1);
        const rect = range.getBoundingClientRect();
        boxes.push({x:rect.x-bounds.x,y:rect.y-bounds.y,width:rect.width});
      }
      host.style.width='min-content'; const minContent=host.getBoundingClientRect().width;
      host.style.width='max-content'; const maxContent=host.getBoundingClientRect().width;
      host.style.width='';
      return {height:bounds.height,minContent,maxContent,characters:boxes};
    });
    delete entry.pseudo;
  }
  fs.writeFileSync(path.join(root,'tests/fixtures/theme/inline-pseudos-browser.json'),
    JSON.stringify({browser:await browser.version(),font:font.metadata,cases},null,2)+'\n');
} finally {await browser.close();}

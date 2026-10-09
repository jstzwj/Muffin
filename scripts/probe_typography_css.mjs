// Regenerate native typography/gradient regressions from Chromium used geometry.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {browserLayoutFont, loadBrowserLayoutFont} from './browser_layout_font.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const moduleName = process.env.MUFFIN_PLAYWRIGHT_MODULE;
const {chromium} = await import(moduleName ? pathToFileURL(path.resolve(moduleName)).href : 'playwright');
const browser = await chromium.launch({headless:true,
  ...(process.env.CHROME_EXECUTABLE ? {executablePath:process.env.CHROME_EXECUTABLE} : {})});
const base = fs.readFileSync(path.join(root, 'resources/themes/document-base.css'), 'utf8');
const font = browserLayoutFont(root);
const auditDir = process.env.MUFFIN_THEME_AUDIT_DIR;
if (auditDir) fs.mkdirSync(auditDir,{recursive:true});
const pinFont = '#write,#write *{font-family:MuffinFixtureSans!important}';
const headings = Array.from({length:6}, (_,i)=>({tag:`h${i+1}`,text:`Heading ${i+1} sample`}));
const prose = [{tag:'h1',text:'Muffin Markdown Example'}, {tag:'p',text:'Chinese | English'},
  {tag:'p',text:"This document is a Markdown syntax sample for exercising Muffin's parser, renderer, and editing paths."},
  {tag:'h2',text:'Inline Features'}, {tag:'p',text:'Plain text and inline features.'}, {tag:'h2',text:'Headings'}, ...headings];
const headingCases = ['newsprint','night'].map(theme=>({id:theme,
  css:fs.readFileSync(path.join(root, `resources/themes/${theme}.css`),'utf8') + pinFont, nodes:prose}));
// Reduced authored declarations from the reported LaTeX theme. In particular,
// no heading margins are supplied: the document host must provide them.
headingCases.push({id:'latex-heading-defaults',css:`#write{font-size:9.5pt;max-width:21cm}h1{font-size:1.9em;text-align:center}
  h2{font-size:1.5em}h3{font-size:1.25em;line-height:1.25em}h4{font-size:1.15em;line-height:1.15em}
  h5{font-size:1.10em;line-height:1.10em}h6{font-size:1.05em;line-height:1.10em}${pinFont}`, nodes:headings});

const inlineCases = [
  {id:'negative-leading', width:500, css:'p{font-size:40px;line-height:.5}', html:'Alpha<br>Beta', markdown:'Alpha  \nBeta'},
  {id:'mixed-small-line-height', width:500, css:'p{font-size:16px;line-height:.5}',
    html:'A <span style="font-size:30px;line-height:1">B</span> C<br>next',
    markdown:'A <span style="font-size:30px;line-height:1">B</span> C  \nnext'},
  ...[88,108,140].map(width=>({id:`keyboard-wrap-${width}`,width,
    css:'p{font-size:16px;line-height:24px}kbd{font-size:1em;padding:0 8px;border:1px solid black}',
    html:'abcdefgh <kbd>S</kbd>, next.',markdown:'abcdefgh <kbd>S</kbd>, next.'})),
];
const gradientCases = [
  {id:'abyss-dot',css:'radial-gradient(#ffffff 1px, transparent 1px)',width:20,height:20,samples:[[9,9],[10,10],[8,9],[11,10],[0,0],[5,5]]},
  {id:'abyss-dot-zoom',css:'radial-gradient(#ffffff 1px, transparent 1px)',width:40,height:40,zoom:2,samples:[[19,19],[20,20],[18,19],[17,19],[23,20],[0,0]]},
  {id:'font-relative-stops',css:'linear-gradient(to right, red 1em, blue 1em)',fontSize:20,width:40,height:8,samples:[[1,3],[18,3],[21,3],[35,3]]},
  {id:'hard-linear',css:'linear-gradient(to right, red 10px, blue 10px)',width:40,height:8,samples:[[1,3],[8,3],[11,3],[35,3]]},
  {id:'mixed-calc-40',css:'linear-gradient(to right, red calc(25% + 2px), blue calc(25% + 2px))',width:40,height:8,samples:[[10,3],[13,3],[35,3]]},
  {id:'mixed-calc-80',css:'linear-gradient(to right, red calc(25% + 2px), blue calc(25% + 2px))',width:80,height:8,samples:[[20,3],[23,3],[70,3]]},
  {id:'omitted-between-lengths',css:'linear-gradient(to right, red 4px, lime, blue 36px)',width:40,height:8,samples:[[3,3],[11,3],[19,3],[27,3],[37,3]]},
  {id:'backwards-stop-fixup',css:'linear-gradient(to right, red 20px, blue 10px)',width:40,height:8,samples:[[1,3],[18,3],[21,3],[35,3]]},
  {id:'double-position',css:'linear-gradient(to right, red 0 10px, blue 10px 100%)',width:40,height:8,samples:[[1,3],[8,3],[11,3],[35,3]]},
  {id:'outside-axis',css:'linear-gradient(to right, red -20px, blue 60px)',width:40,height:8,samples:[[1,3],[10,3],[20,3],[38,3]]},
  {id:'abyss-ellipse',css:'radial-gradient(ellipse at center bottom, white, black 70%)',width:200,height:40,samples:[[99,38],[20,30],[50,20],[99,10],[180,30]]},
  {id:'explicit-ellipse',css:'radial-gradient(ellipse 60px 20px at center, white, black)',width:200,height:60,samples:[[99,29],[130,29],[99,39],[160,29],[99,52]]},
  {id:'clockwise-conic',css:'conic-gradient(from 45deg, red, lime 50%, blue)',width:80,height:80,samples:[[60,20],[60,60],[20,60],[20,20]]},
];
try {
  const page = await browser.newPage({viewport:{width:960,height:1600},deviceScaleFactor:1});
  for (const entry of headingCases) {
    await page.setContent(`<!doctype html><style>${font.css}${base}${entry.css}</style><div id="write">`+
      entry.nodes.map(n=>`<${n.tag}>${n.text}</${n.tag}>`).join('')+'</div>');
    await loadBrowserLayoutFont(page,font);
    if (auditDir) await page.screenshot({path:path.join(auditDir,`${entry.id}-browser.png`),fullPage:true});
    entry.expected = await page.evaluate(()=>{
      const nodes=[...document.querySelector('#write').children];
      const origin=nodes[0].getBoundingClientRect().top;
      return nodes.map(n=>{const r=n.getBoundingClientRect(),s=getComputedStyle(n);
        return {top:r.top-origin,height:r.height,width:r.width,fontSize:parseFloat(s.fontSize),marginTop:parseFloat(s.marginTop),marginBottom:parseFloat(s.marginBottom)};});
    });
  }
  for (const entry of inlineCases) {
    entry.css=`#write{padding:0;margin:0;max-width:none;width:${entry.width}px}p{margin:0}${entry.css}${pinFont}`;
    await page.setContent(`<!doctype html><style>${font.css}${base}${entry.css}</style><div id="write"><p>${entry.html}</p></div>`);
    await loadBrowserLayoutFont(page,font);
    if (auditDir) await page.screenshot({path:path.join(auditDir,`${entry.id}-browser.png`),fullPage:true});
    entry.expected = await page.evaluate(()=>{
      const p=document.querySelector('p'),r=p.getBoundingClientRect();
      return {height:r.height,boxes:[...p.querySelectorAll('kbd')].map(n=>{const b=n.getBoundingClientRect();return {left:b.left-r.left,width:b.width};})};
    });
  }
  for (const entry of gradientCases) {
    const zoom=entry.zoom||1;
    await page.setContent(`<!doctype html><style>body{margin:0}#case{width:${entry.width/zoom}px;height:${entry.height/zoom}px;zoom:${zoom};font-size:${entry.fontSize||16}px;background:${entry.css}}</style><div id="case"></div>`);
    entry.png=(await page.locator('#case').screenshot({omitBackground:true})).toString('base64');
  }
  const output={browser:await browser.version(),viewport:{width:960,height:1600},font:font.metadata,headings:headingCases,inlines:inlineCases,gradients:gradientCases};
  fs.writeFileSync(path.join(root,'tests/fixtures/theme/typography-browser.json'),JSON.stringify(output,null,2)+'\n');
} finally {await browser.close();}

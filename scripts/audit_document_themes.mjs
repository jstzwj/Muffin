// Run MuffinRealThemeRegressionTest with MUFFIN_THEME_AUDIT_DIR first. An optional
// JSON array [{id,css}] supplied to the executable includes locally imported themes.
// This script uses the exported AST HTML and original CSS/font files; no font pin.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const directory=path.resolve(process.argv[2]||'build/theme-audit/real');
const native=JSON.parse(fs.readFileSync(path.join(directory,'native.json'),'utf8'));
const documentHtml=fs.readFileSync(path.join(directory,'document.html'),'utf8');
const moduleName=process.env.MUFFIN_PLAYWRIGHT_MODULE;
const {chromium}=await import(moduleName?pathToFileURL(path.resolve(moduleName)).href:'playwright');
const browser=await chromium.launch({headless:true,...(process.env.CHROME_EXECUTABLE?{executablePath:process.env.CHROME_EXECUTABLE}:{})});
const rows=[];
try {
  for(const entry of native.cases){
    const page=await browser.newPage({viewport:{width:entry.width,height:entry.height},deviceScaleFactor:1});
    const html=`<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="${pathToFileURL(path.join(root,'resources/themes/document-base.css')).href}">
      <style>body{background-color:${entry.viewportColor};color:${entry.textColor}}</style>
      <link rel="stylesheet" href="${pathToFileURL(entry.css).href}">
      <link rel="stylesheet" href="${pathToFileURL(path.join(root,'tests/vendor/katex/dist/katex.min.css')).href}">
      <style>.mfn-inline-math{font-size:1.04166666666667em}.mfn-inline-math .katex{font-size:1em}</style>
      <div id="write">${documentHtml}</div>
      <script src="${pathToFileURL(path.join(root,'tests/vendor/katex/dist/katex.min.js')).href}"></script>
      <script>for(const n of document.querySelectorAll('.mfn-inline-math'))katex.render(n.dataset.tex,n,{throwOnError:false});</script>`;
    const htmlPath=path.join(directory,entry.id+'.html');
    fs.writeFileSync(htmlPath,html);
    await page.goto(pathToFileURL(htmlPath).href);
    // The editor style tree supplies this host class to Markdown headings.
    await page.evaluate(()=>{for(const n of document.querySelectorAll('h1,h2,h3,h4,h5,h6'))n.classList.add('md-heading');});
    await page.evaluate(()=>document.fonts.ready);
    // Native textScale scales fonts and line-height, and consequently em/rem
    // geometry, while leaving absolute box dimensions unchanged. Snapshot all
    // original computed font sizes first to avoid scaling inherited sizes twice.
    if(entry.fontSize!==16) await page.evaluate(scale=>{
      const nodes=[...document.querySelectorAll('*')];
      const styles=nodes.map(n=>{const s=getComputedStyle(n);return {size:s.fontSize,line:s.lineHeight,
        pseudos:['before','after'].map(p=>{const s=getComputedStyle(n,'::'+p);return {p,size:s.fontSize,line:s.lineHeight};})};});
      let pseudoCss='';
      nodes.forEach((n,i)=>{
        const s=styles[i];n.style.fontSize=parseFloat(s.size)*scale+'px';
        if(s.line!=='normal')n.style.lineHeight=parseFloat(s.line)*scale+'px';
        n.setAttribute('data-audit-node',i);
        for(const p of s.pseudos)pseudoCss+=`[data-audit-node="${i}"]::${p.p}{font-size:${parseFloat(p.size)*scale}px;${p.line!=='normal'?`line-height:${parseFloat(p.line)*scale}px;`:''}}`;
      });
      const style=document.createElement('style');style.textContent=pseudoCss;document.head.append(style);
    },entry.fontSize/16);
    const blocks=await page.evaluate(()=>[...document.querySelector('#write').children].map(n=>{
      const r=n.getBoundingClientRect(),s=getComputedStyle(n);
      const measure=document.createElement('span');
      measure.textContent=n.textContent;
      Object.assign(measure.style,{position:'absolute',whiteSpace:'pre',font:s.font,letterSpacing:s.letterSpacing,wordSpacing:s.wordSpacing});
      document.body.append(measure);
      const fontAdvance=measure.getBoundingClientRect().width;
      measure.remove();
      return {tag:n.tagName.toLowerCase(),rect:[r.x,r.y,r.width,r.height],fontFamily:s.fontFamily,fontSize:parseFloat(s.fontSize),fontAdvance,
        lineHeight:s.lineHeight,fontWeight:s.fontWeight,letterSpacing:s.letterSpacing,text:n.textContent,pseudos:['before','after'].map(p=>{const s=getComputedStyle(n,'::'+p);
          return {pseudo:p,content:s.content,position:s.position,width:s.width,height:s.height,top:s.top,left:s.left,bottom:s.bottom,right:s.right,color:s.color,transform:s.transform};})};
    }));
    await page.screenshot({path:path.join(directory,entry.id+'-browser.png'),fullPage:true});
    // Each heading is focused separately in the editor; display every heading's
    // focus state in this diagnostic image to make state-dependent rules visible.
    await page.evaluate(()=>{for(const n of document.querySelectorAll('h1,h2,h3,h4,h5,h6'))n.classList.add('md-focus');});
    await page.screenshot({path:path.join(directory,entry.id+'-focus-browser.png'),fullPage:true});
    const comparable=entry.blocks.length===blocks.length;
    const delta=comparable?entry.blocks.map((n,i)=>({index:i,tag:blocks[i].tag,editorEmpty:n.editorEmpty,
      rect:n.rect.map((v,k)=>+(v-blocks[i].rect[k]).toFixed(3)),fontSize:+(n.fontSize-blocks[i].fontSize).toFixed(3),
      fontAdvance:n.text===blocks[i].text ? +(n.fontMetrics.advance-blocks[i].fontAdvance).toFixed(3) : null})):[];
    rows.push({...entry,browserBlocks:blocks,delta});
    console.log(entry.id,comparable?'geometry recorded':'AST child count differs');
    await page.close();
  }
  fs.writeFileSync(path.join(directory,'comparison.json'),JSON.stringify({browser:await browser.version(),sourceSha256:native.sourceSha256,cases:rows},null,2)+'\n');
  const report=['# Real theme comparison','',`Chromium ${await browser.version()}. Same Markdown AST; original CSS and fonts.`,
    'The browser host receives the same application canvas/ink palette. Soft-break mode is disabled in both renderers.',
    'Native/browser PNG pairs and normal/focus variants are in this directory. The JSON records used geometry and pseudo computed styles.',
    '','Numbers below are native minus browser, in CSS pixels. Font rasterization is not a pixel-identical contract.',
    '','| Case | First heading box width delta | Font size delta | Heading text advance delta | Last block top delta |','|---|---:|---:|---:|---:|'];
  for(const row of rows)report.push(`| ${row.id} | ${row.delta[0]?.rect[2]??'n/a'} | ${row.delta[0]?.fontSize??'n/a'} | ${row.delta[0]?.fontAdvance??'n/a'} | ${row.delta.filter(d=>!d.editorEmpty).at(-1)?.rect[1]??'n/a'} |`);
  fs.writeFileSync(path.join(directory,'report.md'),report.join('\n')+'\n');
}finally{await browser.close();}

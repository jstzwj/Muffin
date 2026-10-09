// Compare a native probe with Chromium using the exact same local font bytes.
// Usage: node scripts/probe_font_backend.mjs font.ttf build/font-probe
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const [fontPath,outputDir]=process.argv.slice(2);
if(!fontPath||!outputDir) throw new Error('Usage: probe_font_backend.mjs font.ttf output-directory');
const native=JSON.parse(fs.readFileSync(path.join(outputDir,'native.json'),'utf8'));
const specimen={text:native.text,width:native.width,letterSpacing:native.letterSpacing,wordSpacing:native.wordSpacing};
const bytes=fs.readFileSync(fontPath),sha=createHash('sha256').update(bytes).digest('hex');
if(sha!==native.fontSha256) throw new Error('Native and browser font files differ');
const {chromium}=await import(process.env.MUFFIN_PLAYWRIGHT_MODULE?
  pathToFileURL(path.resolve(process.env.MUFFIN_PLAYWRIGHT_MODULE)).href:'playwright');
const browser=await chromium.launch({headless:true,
  ...(process.env.CHROME_EXECUTABLE?{executablePath:process.env.CHROME_EXECUTABLE}:{})});
try {
  const page=await browser.newPage({viewport:{width:400,height:400}}),results=[];
  await page.setContent(`<style>@font-face{font-family:Probe;src:url(data:font/ttf;base64,${bytes.toString('base64')})}body{margin:0}</style><div id=text></div>`);
  const client=await page.context().newCDPSession(page);
  await client.send('DOM.enable');await client.send('CSS.enable');
  const {root}=await client.send('DOM.getDocument');
  const {nodeId}=await client.send('DOM.querySelector',{nodeId:root.nodeId,selector:'#text'});
  for(const size of [16,18.4,28.8,29,32.4]) for(const bold of [false,true]) for(const italic of [false,true]) {
    await page.locator('#text').evaluate(async(host,{native,size,bold,italic})=>{
      host.style.cssText=`width:${native.width}px;font-family:Probe;font-size:${size}px;font-weight:${bold?700:400};font-style:${italic?'italic':'normal'};letter-spacing:${native.letterSpacing}px;word-spacing:${native.wordSpacing}px;line-height:normal`;
      host.textContent=native.text;
      const faces=await document.fonts.load(`${italic?'italic ':''}${bold?'700 ':''}${size}px Probe`);
      if(!faces.length) throw new Error('Probe font did not load');
      await document.fonts.ready;
    },{native:specimen,size,bold,italic});
    const geometry=await page.evaluate(()=>{
      const host=document.querySelector('#text'),text=host.firstChild,lines=[],carets=[];
      for(let i=0;i<text.length;i++) {
        const range=new Range();range.setStart(text,i);range.setEnd(text,i+1);const box=range.getBoundingClientRect();
        let line=lines.find(l=>l.y===box.y);
        if(!line){line={start:i,end:i+1,y:box.y};lines.push(line)}
        line.end=i+1;carets.push({source:i,x:box.x,y:box.y});
      }
      return {height:host.offsetHeight,lines,carets};
    });
    const {fonts}=await client.send('CSS.getPlatformFontsForNode',{nodeId});
    results.push({size,bold,italic,...geometry,fonts});
  }
  const comparison=native.results.map(n=>{
    const b=results.find(b=>b.size===n.size&&b.bold===n.bold&&b.italic===n.italic);
    const sameWrap=n.lines.length===b.lines.length&&n.lines.every((l,i)=>l.start===b.lines[i].start&&l.end===b.lines[i].end);
    return {size:n.size,precision:n.precision,bold:n.bold,italic:n.italic,optionalLigatures:n.optionalLigatures,
      sameWrap,maxCaretError:sameWrap?Math.max(...n.carets.map((c,i)=>Math.abs(c.x-b.carets[i].x))):null,
      hitMismatches:n.carets.filter(c=>c.source!==c.hit).length,runs:n.runs,browserFonts:b.fonts};
  });
  fs.writeFileSync(path.join(outputDir,'browser.json'),JSON.stringify({browser:await browser.version(),fontSha256:sha,results,comparison},null,2)+'\n');
  console.log(JSON.stringify(comparison.filter(c=>c.size===28.8&&!c.bold&&!c.italic),null,2));
} finally {await browser.close()}

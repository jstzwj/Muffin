import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
const {chromium} = await import(process.env.MUFFIN_PLAYWRIGHT_MODULE ? pathToFileURL(path.resolve(process.env.MUFFIN_PLAYWRIGHT_MODULE)).href : 'playwright');
import {browserLayoutFont,loadBrowserLayoutFont} from './browser_layout_font.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),font=browserLayoutFont(root);
const browser=await chromium.launch({headless:true,executablePath:process.env.CHROME_EXECUTABLE});
const cases=[
 {id:'adjacent-links',width:90,markdown:'[one](https://one.test)[two](https://two.test)',
  html:'<a href="https://one.test">one</a><a href="https://two.test">two</a>',
  rules:'a[href$="one.test"]::before{content:"1:"}a:not([href$="one.test"])::after{content:":2"}'},
 {id:'html-link',width:130,markdown:'prefix <a href="https://one.test">first</a> suffix',
  html:'prefix <a href="https://one.test">first</a> suffix',
  rules:'a:is([href$="one.test"])::before{content:"["}a::after{content:"]"}'},
 {id:'link-both',width:170,markdown:'prefix [first](https://one.test) [**second**](https://two.test) suffix',
  html:'prefix <a href="https://one.test">first</a> <a href="https://two.test"><strong>second</strong></a> suffix',
  rules:'a::before{content:"[";color:red}a::after{content:"]";color:blue}'},
 {id:'link-structural',width:130,markdown:'[first](https://one.test) [second](https://two.test)',
  html:'<a href="https://one.test">first</a> <a href="https://two.test">second</a>',
  rules:'a{font-size:20px}a:first-of-type::before{content:"";display:inline-block;width:12px;height:12px;margin-right:8px;background:red}a:nth-of-type(2)::after{content:" end";font-size:50%;color:blue}'},
 {id:'absolute-structural',width:170,markdown:'#### Heading\n\n#### Second',
  html:'<h4>Heading</h4><h4>Second</h4>',
  rules:'h4{position:relative;padding:4px 8px}h4:first-child::before{content:"";position:absolute;left:3px;top:4px;width:2px;height:50%;background:red}h4:nth-of-type(2)::after{content:"!";position:absolute;right:3px;bottom:2px;color:blue}'},
];
try {
 const page=await browser.newPage({viewport:{width:400,height:600}});
 for(const c of cases){
  c.css=`html,body{margin:0}#write{width:${c.width}px;padding:0;margin:0;max-width:none;font-family:MuffinFixtureSans;font-size:16px;line-height:24px}p,h4{margin:0;font-weight:400;font-size:16px;line-height:24px}strong{font-weight:700}${c.rules}`;
  await page.setContent(`<!doctype html><style>${font.css}${c.css}</style><div id="write">${c.html.includes('<h4>')?c.html:`<p>${c.html}</p>`}</div>`);
  await loadBrowserLayoutFont(page,font);
  c.expected=await page.evaluate(()=>{
   const write=document.querySelector('#write'),origin=write.getBoundingClientRect(),walker=document.createTreeWalker(write,NodeFilter.SHOW_TEXT),characters=[];
   while(walker.nextNode()){
    const text=walker.currentNode;
    for(let i=0;i<text.length;i++){
     const range=new Range();range.setStart(text,i);range.setEnd(text,i+1);const r=range.getBoundingClientRect();
     characters.push({text:text.data[i],x:r.x-origin.x,y:r.y-origin.y,width:r.width});
    }
   }
   return {height:origin.height,characters};
  });
  delete c.rules;delete c.html;
 }
 fs.writeFileSync(path.join(root,'tests/fixtures/theme/live-pseudos-browser.json'),JSON.stringify({browser:await browser.version(),font:font.metadata,cases},null,2)+'\n');
}finally{await browser.close();}

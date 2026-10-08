// Browser geometry reference for the shared Markdown/HTML Flex formatter.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const moduleName = process.env.MUFFIN_PLAYWRIGHT_MODULE;
const { chromium } = await import(moduleName ? pathToFileURL(path.resolve(moduleName)).href : "playwright");
const browser = await chromium.launch({headless:true, ...(process.env.CHROME_EXECUTABLE ? {executablePath:process.env.CHROME_EXECUTABLE} : {})});
const cases = [
  {id:"grow-gap", container:"width:320px;gap:10px", children:["flex:1;height:40px", "flex:2;height:40px"]},
  {id:"reverse-order", container:"width:320px;flex-direction:row-reverse;justify-content:space-between", children:["width:40px;height:20px;order:2", "width:50px;height:30px;order:-1", "width:60px;height:40px"]},
  {id:"wrap-gap", container:"width:220px;flex-wrap:wrap;gap:11px 10px;align-items:flex-start", children:["width:100px;height:30px", "width:100px;height:40px", "width:100px;height:50px"]},
  {id:"column-reverse", container:"width:200px;height:200px;flex-direction:column-reverse;gap:10px;align-items:center", children:["width:50px;height:30px", "width:80px;height:40px"]},
  {id:"percent-basis", container:"width:300px;gap:10px", children:["flex:0 0 25%;height:20px", "flex:1;height:30px"]},
  {id:"auto-margin", container:"width:300px;align-items:flex-end;height:80px", children:["width:60px;height:30px", "width:40px;height:40px;margin-left:auto"]},
  {id:"border-box", container:"width:300px;padding:10px;border:2px solid black;box-sizing:border-box;gap:10px", children:["flex:1;padding:5px;border:2px solid red;min-width:0;height:40px;box-sizing:border-box", "flex:1;height:50px"]},
  {id:"auto-minimum", container:"width:140px", children:["flex:1", "flex:1"], texts:["WWWWWWWWWWWWWWWW", "x"]},
  {id:"zero-minimum", container:"width:140px", children:["flex:1;min-width:0", "flex:1;min-width:0"], texts:["WWWWWWWWWWWWWWWW", "x"]},
  {id:"wrap-text", container:"width:200px;gap:10px;align-items:flex-start", children:["flex:1;min-width:0", "width:50px;flex-shrink:0"], texts:["alpha beta gamma delta epsilon zeta", "B"]},
  {id:"max-width-freeze", container:"width:300px;gap:10px", children:["flex:1;max-width:60px;height:30px", "flex:1;height:30px"]},
  {id:"explicit-minimum", container:"width:300px", children:["flex:1;min-width:100px;height:30px", "flex:1;height:30px"]},
  {id:"explicit-minimum-freeze", container:"width:150px", children:["flex:1;min-width:100px;height:30px", "flex:1;height:30px"]},
  {id:"baseline", container:"width:300px;align-items:baseline", children:["font-size:16px;line-height:20px", "font-size:32px;line-height:40px"], texts:["Hello", "Hello"]},
  {id:"intrinsic-width", container:"width:300px;align-items:flex-start", children:["width:min-content", "width:max-content"], texts:["alpha beta", "alpha beta"]},
  {id:"percent-padding", container:"width:240px", children:["flex:1;padding:10%;min-width:0", "flex:1;padding:10%;min-width:0"], texts:["A", "B"]},
  {id:"calc-basis", container:"width:300px;gap:10px", children:["flex:0 0 calc(50% - 20px);height:20px", "flex:1;height:30px"]},
  {id:"calc-gap-margin", container:"width:300px;column-gap:calc(10% + 5px)", children:["flex:1;height:20px;margin-left:calc(5% + 2px)", "flex:1;height:30px"]},
  {id:"percent-height", container:"width:200px;height:200px;flex-direction:column", children:["height:25%;flex-shrink:0", "flex:1"]},
  {id:"hidden-minimum", container:"width:140px", children:["flex:1;overflow:hidden", "flex:1"], texts:["WWWWWWWWWWWWWWWW", "x"]},
  {id:"wrap-reverse", container:"width:220px;height:200px;flex-wrap:wrap-reverse;align-content:space-between;gap:10px", children:["width:100px;height:30px", "width:100px;height:40px", "width:100px;height:50px"]},
  {id:"intrinsic-border-box", container:"width:300px;align-items:flex-start", children:["width:min-content;box-sizing:border-box;padding:5px;border:2px solid black", "width:max-content;box-sizing:border-box;padding:5px;border:2px solid black"], texts:["alpha beta", "alpha beta"]},
  {id:"baseline-padding", container:"width:300px;align-items:baseline", children:["font-size:16px;line-height:20px;padding:7px 3px;border:2px solid red", "font-size:32px;line-height:40px;padding:3px"], texts:["Hello", "Hello"]},
  {id:"display-none", container:"width:200px;gap:10px", children:["flex:1;height:20px", "display:none;width:100px;height:60px", "flex:1;height:30px"]},
  {id:"nested", html:'<div id="case" style="display:flex;width:300px;gap:10px;font:16px Arial;line-height:20px"><div id="a" style="display:flex;flex:1;min-width:0;gap:5px"><div id="c" style="flex:1;height:20px"></div><div id="d" style="flex:2;height:30px"></div></div><div id="b" style="width:50px;height:40px"></div></div>'},
];
const base = fs.readFileSync(path.join(root,"resources/themes/document-base.css"),"utf8");
try {
  const page = await browser.newPage({viewport:{width:800,height:700}});
  for (const c of cases) {
    c.html ??= `<div id="case" style="display:flex;font-family:Arial;font-size:16px;line-height:20px;${c.container}">${c.children.map((style,i)=>`<div id="${"abcd"[i]}" style="${style}">${c.texts?.[i] ?? ""}</div>`).join("")}</div>`;
    await page.setContent(`<style>${base}</style><div id="write" style="padding:0;width:800px">${c.html}</div>`);
    c.expected = await page.evaluate(()=>{
      const outer=document.querySelector("#case").getBoundingClientRect();
      return Object.fromEntries([...document.querySelectorAll("#case,#case [id]")].map(el=>{
        const r=el.getBoundingClientRect();return [el.id,{x:r.left-outer.left,y:r.top-outer.top,width:r.width,height:r.height}];
      }));
    });
    delete c.container; delete c.children; delete c.texts;
  }
  fs.writeFileSync(path.join(root,"tests/fixtures/theme/flex-layout-browser.json"),JSON.stringify({browser:await browser.version(),cases},null,2)+"\n");
} finally {await browser.close();}

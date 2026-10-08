// Browser geometry reference for the shared Markdown/HTML Flex formatter.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const moduleName = process.env.MUFFIN_PLAYWRIGHT_MODULE;
const { chromium } = await import(moduleName ? pathToFileURL(path.resolve(moduleName)).href : "playwright");
const browser = await chromium.launch({headless:true, ...(process.env.CHROME_EXECUTABLE ? {executablePath:process.env.CHROME_EXECUTABLE} : {})});
const cases = [
  {"id": "image-small-auto-min", "html": "<div id=\"case\" style=\"display:flex;width:140px;align-items:start;gap:10px;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-small-zero-min", "html": "<div id=\"case\" style=\"display:flex;width:140px;align-items:start;gap:10px;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"min-width:0\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-grow", "html": "<div id=\"case\" style=\"display:flex;width:300px;align-items:start;gap:10px;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"flex:1;min-width:0\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-explicit-stretch", "html": "<div id=\"case\" style=\"display:flex;width:300px;height:140px;align-items:stretch;justify-items:stretch;gap:10px;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-transferred-size", "html": "<div id=\"case\" style=\"display:flex;width:140px;align-items:start;gap:10px;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"height:40px\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-percent-padding-image", "html": "<div id=\"case\" style=\"display:flex;width:300px;align-items:start;gap:10px;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"width:80px;padding:5%\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-percent-height-image", "html": "<div id=\"case\" style=\"display:flex;width:300px;height:200px;align-items:start;gap:10px;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"height:50%\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-auto-ratio-border", "html": "<div id=\"case\" style=\"display:flex;width:300px;align-items:start;gap:10px;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"width:100px;padding:10px;border:2px solid black;box-sizing:border-box;aspect-ratio:auto 1\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},

  {"id": "image-natural", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-width", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"width:80px\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-height", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"height:40px\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-max-width", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"max-width:80px\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-min-width", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"min-width:230px\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-max-height", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"max-height:40px\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-min-height", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"min-height:140px\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-ratio", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"aspect-ratio:1\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-auto-ratio", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"aspect-ratio:auto 1\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-percent", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"width:50%\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-constraints", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"max-width:80px;min-height:70px\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-box-content", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"width:100px;padding:10px;border:2px solid black\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-box-border", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"width:100px;padding:10px;border:2px solid black;box-sizing:border-box\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "image-ratio-border", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><img id=\"a\" src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIyMDAiIGhlaWdodD0iMTAwIj48cmVjdCB3aWR0aD0iMjAwIiBoZWlnaHQ9IjEwMCIgZmlsbD0icmVkIi8+PC9zdmc+\" style=\"width:100px;aspect-ratio:1;padding:10px;border:2px solid black;box-sizing:border-box\"><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "ratio-box", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><div id=\"a\" style=\"width:100px;aspect-ratio:2\"></div><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "ratio-stretch", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><div id=\"a\" style=\"aspect-ratio:2\"></div><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "ratio-cross-size", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><div id=\"a\" style=\"height:40px;aspect-ratio:2\"></div><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},
  {"id": "ratio-min", "html": "<div id=\"case\" style=\"display:flex;width:300px;gap:10px;align-items:start;font:16px Arial;line-height:20px\"><div id=\"a\" style=\"width:80px;aspect-ratio:2;min-height:70px\"></div><div id=\"b\" style=\"width:50px;height:30px;flex-shrink:0\"></div></div>"},

  {"id": "cyclic-column-gap", "container": "width:300px;flex-direction:column;row-gap:20%", "children": ["height:50%", "height:calc(50% + 10px)"], "texts": ["A", "A<br>B<br>C"]},
  {"id": "cyclic-column-calc-gap", "container": "width:300px;flex-direction:column;row-gap:calc(20% + 10px)", "children": ["height:50%", "height:calc(50% + 10px)"], "texts": ["A", "A<br>B<br>C"]},
  {"id": "cyclic-flex-height", "container": "width:300px;flex-direction:column", "children": ["height:50%", "height:calc(50% + 10px)"], "texts": ["A", "A<br>B<br>C"]},
  {"id": "flex-min-height-percent", "container": "width:300px;flex-direction:column;min-height:150px", "children": ["height:50%", "height:calc(50% + 10px)"], "texts": ["A", "A<br>B<br>C"]},
  {"id": "flex-max-height-percent", "container": "width:300px;flex-direction:column;max-height:40px", "children": ["height:50%", "height:calc(50% + 10px)"], "texts": ["A", "A<br>B<br>C"]},
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
const imageSource = /src="([^"]+)"/.exec(cases[0].html)[1];
for (const [id, attributes] of [
  ['image-width-attribute', 'width="80"'],
  ['image-width-attribute-css-auto', 'width="80" style="width:auto"'],
]) cases.push({id, html:cases.find(c => c.id === 'image-natural').html.replace('style=""', attributes)});
for (const [id, image] of [
  ['image-ratio-degenerate', 'width:80px;aspect-ratio:0'],
  ['image-ratio-zero-denominator', 'width:80px;aspect-ratio:2/0'],
  ['image-max-height-padding', 'max-height:40px;padding:10px;border:2px solid black'],
  ['image-min-height-padding', 'min-height:120px;padding:10px;border:2px solid black'],
  ['image-max-height-border-box', 'max-height:64px;padding:10px;border:2px solid black;box-sizing:border-box'],
]) cases.push({id, html:cases.find(c => c.id === 'image-natural').html.replace('style=""', `style="${image}"`)});
for (const [id, container, image] of [
  ['image-column-natural', 'height:200px', ''],
  ['image-column-shrink', 'height:80px', 'min-height:0'],
  ['image-column-auto-min', 'height:80px', ''],
  ['image-column-cross-width', 'height:200px', 'width:120px'],
  ['image-column-ratio', 'height:200px', 'aspect-ratio:1;max-height:70px'],
]) cases.push({id, html:`<div id="case" style="display:flex;flex-direction:column;width:300px;align-items:start;gap:10px;${container}"><img id="a" src="${imageSource}" style="${image}"><div id="b" style="width:50px;height:30px;flex-shrink:0"></div></div>`});
const base = fs.readFileSync(path.join(root,"resources/themes/document-base.css"),"utf8");
try {
  const page = await browser.newPage({viewport:{width:800,height:700}});
  for (const c of cases) {
    c.html ??= `<div id="case" style="display:flex;font-family:Arial;font-size:16px;line-height:20px;${c.container}">${c.children.map((style,i)=>`<div id="${"abcd"[i]}" style="${style}">${c.texts?.[i] ?? ""}</div>`).join("")}</div>`;
    await page.setContent(`<!doctype html><style>${base}</style><div id="write" style="padding:0;width:800px">${c.html}</div>`);
    if (await page.evaluate(() => document.compatMode) !== "CSS1Compat") throw new Error("Browser oracle must use standards mode");
    c.expected = await page.evaluate(()=>{
      const outer=document.querySelector("#case").getBoundingClientRect();
      return Object.fromEntries([...document.querySelectorAll("#case,#case [id]")].map(el=>{
        const r=el.getBoundingClientRect();return [el.id,{x:r.left-outer.left,y:r.top-outer.top,width:r.width,height:r.height}];
      }));
    });
    delete c.container; delete c.children; delete c.texts;
  }
  fs.writeFileSync(process.argv[2] ?? path.join(root,"tests/fixtures/theme/flex-layout-browser.json"),JSON.stringify({browser:await browser.version(),cases},null,2)+"\n");
} finally {await browser.close();}

import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const outDir = path.resolve(process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "sequence-pixel"));
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (pkg.version !== "11.16.0") throw new Error(`Expected Mermaid 11.16.0, found ${pkg.version}`);
const notoDir = path.resolve("third_party", "noto", "fonts");
const fonts = [["Noto Sans","NotoSans-Regular.ttf"],["Noto Sans CJK SC","NotoSansCJKsc-Regular.otf"],
  ["Noto Sans Arabic","NotoSansArabic-Regular.ttf"],["Noto Sans Hebrew","NotoSansHebrew-Regular.ttf"]];
const faces = fonts.map(([family,file]) => `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir,file)).href}")}`).join("\n");
const stack = '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';
const cases = [
  { id: "basic", source: "sequenceDiagram\nparticipant A as Alice\nactor B as Bob\nA->>B:Hello\nB-->>A:Return" },
  { id: "activation-note", source: "sequenceDiagram\nA->>+B:Call\nNote over A,B:Working 中文\nB-->>-A:Done" },
  { id: "nested-fragment", source: "sequenceDiagram\nalt Success\nloop Retry\nA->>B:Request\nend\nelse Failure\nA-xB:Error\nend" },
  { id: "participant-types", source: ["sequenceDiagram", "participant P as Plain", "actor A as Actor",
      'participant B@{ "type": "boundary" } as Boundary', 'participant C@{ "type": "control" } as Control',
      'participant E@{ "type": "entity" } as Entity', 'participant D@{ "type": "database" } as Database',
      'participant S@{ "type": "collections" } as Collections', 'participant Q@{ "type": "queue" } as Queue',
      "P->>Q:All types"].join("\n") },
  { id: "create-destroy-markers", source: ["sequenceDiagram", "participant A as Alice",
      "participant B as Bob", "A->>B:solid", "B-->>A:dotted", "A<<->>B:both",
      "A-)B:point", "B-xA:cross", "create participant C as Created", "A->>C:create",
      "destroy C", "C-->>A:destroy"].join("\n") },
  { id: "central-autonumber", source: ["sequenceDiagram", "autonumber 10 5",
      "A->>()B:forward central", "A()->>B:reverse central", "A()->>()B:dual central",
      "B<<-->>A:bidirectional", "autonumber off", "A-->>B:unnumbered"].join("\n") },
  { id: "self-autonumber", source: ["sequenceDiagram", "autonumber", "A->>A:self solid",
      "A-->>A:self dotted", "A-xA:self cross", "A<<->>A:self both"].join("\n") },
  { id: "label-participant-html-cjk", source: ["sequenceDiagram",
      "participant A as <b>Client</b><br/>\u5ba2\u6237\u7aef", "participant B as Server",
      "A->>B:ping"].join("\n") },
  { id: "label-message-wrap-bidi", source: [
      '%%{init: {"sequence": {"wrap": true, "wrapPadding": 10}}}%%', "sequenceDiagram",
      "A->>B:wrap:alpha beta gamma delta epsilon zeta eta theta",
      "B-->>A:Hello \u0645\u0631\u062d\u0628\u0627 \u05e9\u05dc\u05d5\u05dd"].join("\n") },
  { id: "label-note-markdown-math", source: ["sequenceDiagram", "A->>B:start",
      "Note over A,B:`**Speed** $$x^2$$`", "B-->>A:done"].join("\n") },
  { id: "label-fragment-html-rtl", source: ["sequenceDiagram",
      "alt <b>Success</b><br/>\u0646\u062c\u0627\u062d", "A->>B:ok", "else fallback",
      "B-->>A:return", "end"].join("\n") },
  { id: "label-box-markdown-math", source: ["sequenceDiagram",
      "box rgb(238, 246, 255) `**Services** $$x$$`", "participant A", "participant B", "end",
      "A->>B:call"].join("\n") },
];
const { default: puppeteer } = await import(pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")));
const browser = await puppeteer.launch({headless:true, executablePath:chrome, args:["--allow-file-access-from-files"]});
fs.mkdirSync(outDir, {recursive:true});
try {
  const page = await browser.newPage();
  await page.setViewport({width:2200,height:1600,deviceScaleFactor:1});
  const harness = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;
  const module = pathToFileURL(path.join(mermaidRoot,"dist","mermaid.esm.mjs")).href;
  const manifestCases = [];
  for (let i=0;i<cases.length;++i) {
    await page.goto(harness);
    const dimensions = await page.evaluate(async ({fixture,i,module,faces,stack}) => {
      const style=document.createElement("style"); style.textContent=faces; document.head.appendChild(style);
      await document.fonts.load('16px "Noto Sans"'); await document.fonts.ready;
      const {default:mermaid}=await import(module);
      mermaid.initialize({startOnLoad:false,securityLevel:"strict",theme:"default",fontFamily:stack,sequence:{useMaxWidth:false}});
      const {svg}=await mermaid.render(`sequence-pixel-${i}`,fixture.source);
      document.getElementById("container").innerHTML=svg;
      const root=document.querySelector("svg"); const b=root.getBBox();
      root.setAttribute("viewBox",`${b.x} ${b.y} ${b.width} ${b.height}`);
      root.setAttribute("width",Math.ceil(b.width)); root.setAttribute("height",Math.ceil(b.height));
      root.style.maxWidth="none"; root.style.width=`${Math.ceil(b.width)}px`; root.style.height=`${Math.ceil(b.height)}px`;
      document.body.style.margin="0"; document.documentElement.style.margin="0";
      return {width:Math.ceil(b.width),height:Math.ceil(b.height)};
    }, {fixture:cases[i],i,module,faces,stack});
    const file=`${cases[i].id}.png`;
    const svg = await page.$("svg");
    await svg.screenshot({path:path.join(outDir,file),omitBackground:true});
    manifestCases.push({...cases[i],file,...dimensions});
  }
  const payload={mermaidVersion:pkg.version,fontMode:"bundled-noto",cases:manifestCases};
  payload.fixtureSha256=createHash("sha256").update(JSON.stringify(payload)).digest("hex");
  fs.writeFileSync(path.join(outDir,"manifest.json"),`${JSON.stringify(payload,null,2)}\n`);
  console.log(`Wrote ${cases.length} sequence pixel goldens to ${outDir}`);
  console.log(`fixtureSha256=${payload.fixtureSha256}`);
} finally { await browser.close(); }

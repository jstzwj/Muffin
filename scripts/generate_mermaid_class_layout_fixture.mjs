import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "class-layout.json"),
);
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected Mermaid 11.16.0, found ${packageJson.version}`);
}

const notoDir = path.resolve("third_party", "noto", "fonts");
const fontFiles = [
  ["Noto Sans", "NotoSans-Regular.ttf", "U+0000-024F,U+1E00-1EFF"],
  ["Noto Sans CJK SC", "NotoSansCJKsc-Regular.otf", "U+2E80-9FFF,U+3040-30FF,U+AC00-D7AF"],
  ["Noto Sans Arabic", "NotoSansArabic-Regular.ttf", "U+0600-06FF,U+0750-077F,U+08A0-08FF"],
  ["Noto Sans Hebrew", "NotoSansHebrew-Regular.ttf", "U+0590-05FF"],
];
const fontStack = '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';
const fontFaces = fontFiles.map(([family, file, range]) =>
  `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}");font-weight:400;font-style:normal;unicode-range:${range};}`
).join("\n");

const cases = [
  {
    id: "class-compartments",
    source: [
      "classDiagram",
      "class Service {",
      "  <<interface>>",
      "  +String name",
      "  +run(input) Result*",
      "}",
      "class Empty",
    ].join("\n"),
  },
  {
    id: "nested-namespaces-notes",
    source: [
      "classDiagram",
      "namespace Company.Core[\"Core services\"] {",
      "  class Api",
      "  note for Api \"API note\"",
      "  namespace Internal {",
      "    class Worker",
      "  }",
      "}",
    ].join("\n"),
  },
  {
    id: "single-namespace-edge",
    source: [
      "classDiagram",
      "namespace Domain {",
      "  class A",
      "  class B",
      "}",
      "A --> B : uses",
    ].join("\n"),
  },
  {
    id: "nested-namespace-chain",
    source: [
      "classDiagram",
      "namespace Outer {",
      "  class A",
      "  namespace Inner {",
      "    class B",
      "    class C",
      "  }",
      "}",
      "B --> C : inner",
    ].join("\n"),
  },
  {
    id: "relation-marker-label-matrix",
    source: [
      "classDiagram",
      "direction LR",
      "A \"1\" <|-- \"many\" B : extends",
      "C *.. D : composition",
      "E o-- F : aggregation",
      "G ..> H : dependency",
    ].join("\n"),
  },
  {
    id: "lollipop-and-note-edge",
    source: [
      "classDiagram",
      "Port ()-- Service",
      "Client --() Socket",
      "note for Service \"bound note\"",
    ].join("\n"),
  },
  {
    id: "styled-unicode-generic",
    source: [
      "classDiagram",
      "class `订单 服务`~列表~[\"显示 名称\"] {",
      "  +编号",
      "  +处理(输入) 输出$",
      "}",
      "cssClass \"订单 服务\" styled",
      "classDef styled fill:#123,color:#fff",
    ].join("\n"),
  },
  {
    id: "multi-row-compartments",
    source: [
      "classDiagram",
      "class Ledger {",
      "  <<entity>>",
      "  <<audited>>",
      "  +String id",
      "  -金额 balance$",
      "  +post(entry) Result*",
      "  #find(id) Entry",
      "}",
    ].join("\n"),
  },
  {
    id: "bidi-cjk-class",
    source: [
      "classDiagram",
      "class Mixed[\"中文 مرحبا שלום\"] {",
      "  +中文 value",
      "  +مرحبا() שלום",
      "}",
    ].join("\n"),
  },
  {
    id: "hidden-empty-compartments",
    hideEmptyMembersBox: true,
    source: "classDiagram\nclass EmptyHidden",
  },
  {
    id: "compact-namespaces",
    hierarchicalNamespaces: false,
    source: [
      "classDiagram",
      "namespace Company.Core {",
      "  class Api",
      "}",
    ].join("\n"),
  },
  {
    id: "generic-member-compartments",
    source: [
      "classDiagram",
      "class Repository~T~[\"Repository<T>\"] {",
      "  <<interface>>",
      "  +List~T~ items",
      "  -Map~String,T~ cache$",
      "  +find(id) Optional~T~*",
      "  #save(value) Result~T~",
      "}",
    ].join("\n"),
  },
  {
    id: "note-label-matrix",
    source: [
      "classDiagram",
      "direction LR",
      "class Service",
      "note for Service \"Bound note 42\"",
      "note \"Detached CJK \u4e2d\u6587\"",
    ].join("\n"),
  },
  {
    id: "nested-namespace-label-matrix",
    source: [
      "classDiagram",
      "namespace Platform[\"Platform \u4e2d\u6587\"] {",
      "  namespace Core[\"Core \u0645\u0631\u062d\u0628\u0627\"] {",
      "    namespace Internal[\"Internal \u05e9\u05dc\u05d5\u05dd\"] {",
      "      class Store~T~[\"Store<T>\"]",
      "      note for Store \"Nested note\"",
      "    }",
      "  }",
      "}",
    ].join("\n"),
  },
  {
    id: "inert-spacing-rl",
    nodeSpacing: 72,
    rankSpacing: 96,
    source: [
      "classDiagram",
      "direction RL",
      "A --> B : first",
      "A --> C : second",
      "B --> D : third",
    ].join("\n"),
  },
  {
    id: "inert-spacing-bt",
    nodeSpacing: 36,
    rankSpacing: 84,
    source: [
      "classDiagram",
      "direction BT",
      "A --> B",
      "B --> C",
    ].join("\n"),
  },
  {
    id: "svg-label-compartments",
    htmlLabels: false,
    source: [
      "classDiagram",
      "class Service {",
      "  <<interface>>",
      "  +String name",
      "  +run(input) Result*",
      "}",
    ].join("\n"),
  },
  {
    id: "svg-multiline-cjk-rtl",
    htmlLabels: false,
    source: [
      "classDiagram",
      "class Mixed[\"Service<br/>中文 مرحبا\"] {",
      "  +名称",
      "  +שלום(value) نتيجة",
      "}",
    ].join("\n"),
  },
  {
    id: "svg-classifier-styles",
    htmlLabels: false,
    source: [
      "classDiagram",
      "class Styled {",
      "  +staticValue$",
      "  +abstractMethod()*",
      "}",
    ].join("\n"),
  },
  {
    id: "compound-self-parallel-relations",
    source: [
      "classDiagram",
      "direction LR",
      "namespace Outer {",
      "  class A",
      "  namespace Inner {",
      "    class B",
      "  }",
      "}",
      "class C",
      "A \"self\" --> \"one\" A : recursive",
      "A --> B : first",
      "A ..> B : second",
      "B o-- C : cross",
    ].join("\n"),
  },
];

const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")),
);
const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});

try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: 1 });
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const results = await page.evaluate(async ({ cases, mermaidModule, fontFaces, fontStack }) => {
    const { default: mermaid } = await import(mermaidModule);
    const style = document.createElement("style");
    style.textContent = fontFaces;
    document.head.appendChild(style);
    await Promise.all([
      document.fonts.load('16px "Noto Sans"', "Class"),
      document.fonts.load('16px "Noto Sans CJK SC"', "\u4e2d\u6587"),
      document.fonts.load('16px "Noto Sans Arabic"', "\u0645\u0631\u062d\u0628\u0627"),
      document.fonts.load('16px "Noto Sans Hebrew"', "\u05e9\u05dc\u05d5\u05dd"),
    ]);
    await document.fonts.ready;
    const list = (value) => Array.isArray(value) ? value : value ? [value] : [];
    const member = (value) => ({
      text: value.text ?? "",
      cssStyle: value.classifier === "*" ? "font-style:italic;" :
        value.classifier === "$" ? "text-decoration:underline;" : "",
    });
    const node = (value) => ({
      id: value.id,
      label: value.label ?? "",
      text: value.text ?? "",
      shape: value.shape ?? "",
      parentId: value.parentId ?? "",
      cssClasses: value.cssClasses ?? "",
      cssStyles: list(value.cssStyles),
      styles: list(value.styles),
      annotations: list(value.annotations),
      members: list(value.members).map(member),
      methods: list(value.methods).map(member),
      padding: value.padding ?? null,
      isGroup: value.isGroup ?? false,
      look: value.look ?? "classic",
    });
    const edge = (value) => ({
      id: value.id,
      start: value.start,
      end: value.end,
      label: value.label ?? "",
      pattern: value.pattern ?? "",
      arrowTypeStart: value.arrowTypeStart ?? "none",
      arrowTypeEnd: value.arrowTypeEnd ?? "none",
      startLabelRight: value.startLabelRight ?? "",
      endLabelLeft: value.endLabelLeft ?? "",
      style: list(value.style),
      labelStyle: list(value.labelStyle),
      classes: value.classes ?? "",
      look: value.look ?? "classic",
    });

    const output = [];
    for (const fixture of cases) {
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        look: "classic",
        htmlLabels: fixture.htmlLabels ?? true,
        fontFamily: fontStack,
        class: {
          padding: 12,
          nodeSpacing: fixture.nodeSpacing ?? 50,
          rankSpacing: fixture.rankSpacing ?? 50,
          hierarchicalNamespaces: fixture.hierarchicalNamespaces ?? true,
          hideEmptyMembersBox: fixture.hideEmptyMembersBox ?? false,
        },
      });
      const diagram = await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
      const data = diagram.db.getData();
      const { svg } = await mermaid.render(`class-layout-${output.length}`, fixture.source);
      document.getElementById("container").innerHTML = svg;
      await document.fonts.ready;
      await new Promise((resolve) => requestAnimationFrame(resolve));
      const round = (value) => Math.round(value * 1000) / 1000;
      const root = document.querySelector("svg");
      const bbox = (element) => {
        if (!element) return null;
        const value = element.getBBox();
        return { x: round(value.x), y: round(value.y),
          width: round(value.width), height: round(value.height) };
      };
      const classNodes = data.nodes.filter((value) => value.shape === "classBox");
      const classElements = [...document.querySelectorAll("g.node")]
        .filter((element) => element.querySelector(":scope > .annotation-group"));
      if (classElements.length !== classNodes.length) {
        throw new Error(`${fixture.id}: expected ${classNodes.length} class nodes, rendered ${classElements.length}`);
      }
      const renderedNodes = classNodes.map((value) => {
          const element = classElements.find((candidate) =>
            candidate.id.includes(`classId-${value.id}-`));
          if (!element) throw new Error(`${fixture.id}: cannot associate rendered class ${value.id}`);
          if (!element) throw new Error(`${fixture.id}: missing rendered node ${value.id}`);
          const group = (name) => element.querySelector(`.${name}-group`);
          const groupSnapshot = (name) => {
            const current = group(name);
            const labels = [...(current?.querySelectorAll(":scope > .label") ?? [])];
            const itemSnapshot = (label) => {
              const foreign = label.querySelector("foreignObject");
              const html = foreign?.querySelector("div")?.innerHTML ?? "";
              const mathRows = html.includes("</math>")
                ? (html.match(/<mrow>/g)?.length ?? 0) : 0;
              return {
                ...bbox(label),
                lineCount: foreign
                  ? Math.max(1, html.split(/<br\s*\/?\s*>/i).length + mathRows)
                  : Math.max(1, label.querySelector("text")?.children.length ?? 0),
                svgText: !foreign,
              };
            };
            return {
              bbox: bbox(current),
              transform: current?.getAttribute("transform") ?? "",
              items: labels.map(itemSnapshot),
              itemDetails: labels.map((label) => ({
                bbox: bbox(label),
                transform: label.getAttribute("transform") ?? "",
                text: label.textContent ?? "",
              })),
            };
          };
          return {
            id: value.id,
            measured: {
              annotation: groupSnapshot("annotation").items,
              label: groupSnapshot("label").items,
              members: groupSnapshot("members").items,
              methods: groupSnapshot("methods").items,
            },
            geometry: {
              bbox: bbox(element),
              outer: bbox(element.querySelector(".outer-path")),
              annotation: groupSnapshot("annotation"),
              label: groupSnapshot("label"),
              members: groupSnapshot("members"),
              methods: groupSnapshot("methods"),
              dividers: [...element.querySelectorAll(".divider")].map(bbox),
            },
          };
        });
      const translate = (element) => {
        const value = element?.getAttribute("transform") ?? "";
        const match = /^translate\(([-+0-9.eE]+)(?:, | )([-+0-9.eE]+)\)$/.exec(value);
        if (!match) throw new Error(`${fixture.id}: invalid node transform ${value}`);
        return { x: Number(match[1]), y: Number(match[2]) };
      };
      const rootPosition = (element, x = 0, y = 0) => {
        const point = new DOMPoint(x, y).matrixTransform(element.getCTM());
        return { x: point.x, y: point.y };
      };
      const regularNodes = data.nodes.filter((value) => !value.isGroup);
      const positionedNodes = regularNodes.map((value) => {
        const element = [...document.querySelectorAll("g.node")].find((candidate) =>
          candidate.id.includes(`classId-${value.id}-`) ||
          candidate.id.includes(`-${value.id}-`) || candidate.id.endsWith(`-${value.id}`));
        if (!element) throw new Error(`${fixture.id}: missing positioned node ${value.id}; ` +
          [...document.querySelectorAll("g.node")].map((candidate) => candidate.id).join(","));
        const position = rootPosition(element);
        const labels = [...element.querySelectorAll(".label")].map((label) => {
          const box = bbox(label);
          return { bbox: box,
            position: rootPosition(label, box.x + box.width / 2,
                                    box.y + box.height / 2) };
        });
        return { id: value.id, position, bbox: bbox(element), labels };
      });
      const origin = positionedNodes[0]?.position ?? { x: 0, y: 0 };
      const allEdgeLabels = [...document.querySelectorAll("g.edgeLabel")];
      let edgeLabelIndex = 0;
      const semanticEdgeLabels = data.edges.map((edge) => {
        const count = edge.start === edge.end ? 3 : 1;
        const group = allEdgeLabels.slice(edgeLabelIndex, edgeLabelIndex + count);
        edgeLabelIndex += count;
        const element = group[Math.floor(count / 2)];
        if (!element) throw new Error(`${fixture.id}: missing edge label group for ${edge.id}`);
        const position = element.getAttribute("transform") ? rootPosition(element) : null;
        return {
          domId: element.id,
          text: element.textContent ?? "",
          bbox: bbox(element),
          dx: position ? round(position.x - origin.x) : null,
          dy: position ? round(position.y - origin.y) : null,
        };
      });
      const placement = {
        nodes: positionedNodes.map((value) => ({
          id: value.id,
          dx: round(value.position.x - origin.x),
          dy: round(value.position.y - origin.y),
          width: value.bbox.width,
          height: value.bbox.height,
          labels: value.labels.map((label) => ({
            bbox: label.bbox,
            dx: round(label.position.x - origin.x),
            dy: round(label.position.y - origin.y),
          })),
        })),
        edgeLabels: semanticEdgeLabels,
        clusters: data.nodes.filter((value) => value.isGroup).map((value) => {
          const element = [...document.querySelectorAll("g.cluster")].find((candidate) =>
            candidate.id.endsWith(`-${value.id}`));
          if (!element) throw new Error(`${fixture.id}: missing cluster ${value.id}`);
          const box = bbox(element);
          const position = rootPosition(element, box.x + box.width / 2,
                                         box.y + box.height / 2);
          const labels = [...element.querySelectorAll(".cluster-label")].map((label) => {
            const labelBox = bbox(label);
            const labelPosition = rootPosition(
              label, labelBox.x + labelBox.width / 2,
              labelBox.y + labelBox.height / 2);
            return { bbox: labelBox,
              dx: round(labelPosition.x - origin.x),
              dy: round(labelPosition.y - origin.y) };
          });
          return {
            id: value.id,
            domId: element.id,
            text: element.textContent ?? "",
            bbox: box,
            labels,
            dx: round(position.x - origin.x),
            dy: round(position.y - origin.y),
          };
        }),
      };
      const attributeSnapshot = (element, names) => Object.fromEntries(
        names.map((name) => [name, element?.getAttribute(name) ?? ""]),
      );
      const edgePaths = [...document.querySelectorAll("g.edgePaths path")];
      let edgePathIndex = 0;
      const edgePathGroups = data.edges.map((edge) => {
        const count = edge.start === edge.end ? 3 : 1;
        const group = edgePaths.slice(edgePathIndex, edgePathIndex + count);
        edgePathIndex += count;
        return group;
      });
      if (edgePathIndex !== edgePaths.length || edgePathGroups.some((group) => group.length === 0))
        throw new Error(`${fixture.id}: cannot map ${edgePaths.length} DOM paths to ` +
          `${data.edges.length} semantic edges`);
      const terminalGroups = [...document.querySelectorAll("g.edgeTerminals")];
      let terminalIndex = 0;
      const structuralEdges = data.edges.map((value, index) => {
        const pathSnapshots = edgePathGroups[index].map((pathElement) => {
          const encodedPoints = pathElement.getAttribute("data-points") ?? "";
          const rawPoints = encodedPoints
            ? JSON.parse(new TextDecoder().decode(Uint8Array.from(
                atob(encodedPoints), (char) => char.charCodeAt(0))))
            : [];
          const pathMatrix = pathElement.getCTM();
          const pathTokens = (pathElement.getAttribute("d") ?? "")
            .match(/[A-Za-z]|[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?/g) ?? [];
          const normalizedPath = [];
          let pendingX = null;
          for (const token of pathTokens) {
            if (/^[A-Za-z]$/.test(token)) {
              if (pendingX !== null) throw new Error(`${fixture.id}: incomplete path coordinate pair`);
              normalizedPath.push(token);
            } else if (pendingX === null) {
              pendingX = Number(token);
            } else {
              const point = new DOMPoint(pendingX, Number(token)).matrixTransform(pathMatrix);
              normalizedPath.push(round(point.x - origin.x), round(point.y - origin.y));
              pendingX = null;
            }
          }
          return {
            path: attributeSnapshot(pathElement,
              ["id", "class", "d", "style", "marker-start", "marker-end", "data-points"]),
            normalizedPoints: rawPoints.map((point) => {
              const transformed = new DOMPoint(point.x, point.y).matrixTransform(pathMatrix);
              return { x: round(transformed.x - origin.x),
                       y: round(transformed.y - origin.y) };
            }),
            normalizedPath,
          };
        });
        const terminals = {};
        for (const name of ["startLabelLeft", "startLabelRight", "endLabelLeft", "endLabelRight"]) {
          if (!value[name]) continue;
          const element = terminalGroups[terminalIndex++];
          const position = rootPosition(element);
          terminals[name] = {
            text: element.textContent ?? "",
            dx: round(position.x - origin.x),
            dy: round(position.y - origin.y),
            bbox: bbox(element),
            transform: element.getAttribute("transform") ?? "",
          };
        }
        return {
          id: value.id,
          path: pathSnapshots[0].path,
          normalizedPoints: pathSnapshots.length === 1 ? pathSnapshots[0].normalizedPoints : [],
          normalizedPath: pathSnapshots.length === 1 ? pathSnapshots[0].normalizedPath : [],
          segments: pathSnapshots.length > 1 ? pathSnapshots : [],
          terminals,
        };
      });
      if (terminalIndex !== terminalGroups.length) {
        throw new Error(`${fixture.id}: consumed ${terminalIndex} terminal labels, rendered ${terminalGroups.length}`);
      }
      const markers = [...root.querySelectorAll("marker")].map((element) => ({
        attributes: attributeSnapshot(element,
          ["id", "class", "refX", "refY", "markerWidth", "markerHeight",
           "markerUnits", "orient", "viewBox"]),
        children: [...element.children].map((child) => ({
          tag: child.tagName,
          attributes: attributeSnapshot(child,
            ["d", "points", "cx", "cy", "r", "fill", "class", "style", "stroke-width"]),
        })),
      }));
      const structure = {
        origin: { x: round(origin.x), y: round(origin.y) },
        edges: structuralEdges,
        markers,
        order: [...(root.querySelector(":scope > g.root") ?? root.querySelector("g.root")).children].map((element) =>
          element.getAttribute("class") ?? element.tagName),
      };
      output.push({
        ...fixture,
        expected: {
          nodes: data.nodes.map(node),
          edges: data.edges.map(edge),
          direction: data.direction,
          // Mermaid's class renderer does not forward these class config fields
          // to Dagre. Keep the non-default inputs above as an explicit inertness oracle.
          nodeSpacing: 50,
          rankSpacing: 50,
          markers: ["aggregation", "extension", "composition", "dependency", "lollipop"],
          renderedNodes,
          placement,
          structure,
        },
      });
    }
    return output;
  }, { cases, mermaidModule, fontFaces, fontStack });

  const payload = {
    mermaidVersion: packageJson.version,
    oracle: "ClassDB.getData+classBox.svg+dagre.svg+structure.svg",
    cases: results,
  };
  const fixtureSha256 = createHash("sha256").update(JSON.stringify(payload)).digest("hex");
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify({ ...payload, fixtureSha256 }, null, 2)}\n`);
  console.log(`Wrote ${results.length} class layout cases to ${output}`);
} finally {
  await browser.close();
}

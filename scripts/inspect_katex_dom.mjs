import path from "node:path";
import {pathToFileURL} from "node:url";
import {chromium} from "playwright";

const formulas = [
  String.raw`\begin{array}{cc}a&b\\c&d\end{array}`,
  String.raw`\begin{pmatrix}a&b\\c&d\end{pmatrix}`,
  String.raw`\begin{cases}x^2,&x>0\\0,&x=0\end{cases}`,
];

const css = pathToFileURL(path.resolve("tests/vendor/katex/dist/katex.min.css")).href;
const browser = await chromium.launch();
const page = await browser.newPage({viewport: {width: 1200, height: 800}, deviceScaleFactor: 1});
await page.setContent(`<!doctype html>
<html>
<head>
  <style>body{font-size:16px}.wrap{display:inline-block;padding:24px}</style>
  <link rel="stylesheet" href="${css}">
</head>
<body><div class="wrap"><span id="math"></span></div></body>
</html>`, {waitUntil: "load"});
await page.addScriptTag({path: path.resolve("tests/vendor/katex/dist/katex.min.js")});

for (const tex of formulas) {
  await page.evaluate(({tex}) => {
    const math = document.getElementById("math");
    math.innerHTML = "";
    katex.render(tex, math, {displayMode: true, throwOnError: false, strict: "ignore", output: "html"});
  }, {tex});

  const data = await page.evaluate(() => Array.from(
    document.querySelectorAll(".katex-html,.mtable,.arraycolsep,.col-align-c,.col-align-l,.mopen,.mclose,.delimsizing")
  ).map((el, i) => {
    const rect = el.getBoundingClientRect();
    return {
      i,
      cls: el.className,
      text: el.textContent,
      x: Number(rect.x.toFixed(3)),
      width: Number(rect.width.toFixed(3)),
      height: Number(rect.height.toFixed(3)),
      styleWidth: getComputedStyle(el).width,
    };
  }));
  console.log(`\n${tex}`);
  console.log(JSON.stringify(data, null, 2));
}

await browser.close();

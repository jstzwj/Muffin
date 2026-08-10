import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

// Dumps mermaid's RESOLVED themeVariables (the exact hsl/hex/rgba strings the
// flowchart `getStyles` interpolates into CSS) for every supported theme plus
// one themeVariables-override case. The native FlowTheme port (milestone F1)
// must reproduce these strings byte-for-byte.
//
// `mermaid.mermaidAPI.getConfig().themeVariables` after `initialize({theme})`
// holds the fully-resolved set (not just the user override): `initialize` runs
// the theme's `calculate(overrides)` — apply overrides → updateColors →
// re-apply overrides — and stores the result back into config.themeVariables.
// getComputedStyle is deliberately NOT used: the browser normalises
// `hsl(240,100%,96.5%)` → `rgb(238,238,255)`, which would hide format bugs and
// lose the exact khroma output the port must match.

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "flowchart-theme.json"),
);
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
}
const { default: puppeteer } = await import(
  pathToFileURL(
    path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"),
  )
);

const themes = [
  "base",
  "dark",
  "default",
  "forest",
  "neutral",
  "neo",
  "neo-dark",
  "redux",
  "redux-dark",
  "redux-color",
  "redux-dark-color",
];

// Verify the override two-pass calculate: primaryColor + lineColor override the
// default theme. Derived fields (secondaryColor = adjust(primary, {h:-120}),
// etc.) must re-derive from the overridden primaryColor; the overridden
// lineColor must survive updateColors (re-applied after).
const overrideCase = {
  name: "default+override",
  theme: "default",
  themeVariables: { primaryColor: "#ff0000", lineColor: "#00ff00" },
};

const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const result = await page.evaluate(
    async ({ themes, overrideCase, mermaidModule }) => {
      const { default: mermaid } = await import(mermaidModule);
      const dump = (theme, themeVariables) => {
        mermaid.initialize({
          startOnLoad: false,
          theme,
          securityLevel: "strict",
          ...(themeVariables ? { themeVariables } : {}),
        });
        // getConfig().themeVariables is the RESOLVED set after calculate().
        const tv = mermaid.mermaidAPI.getConfig().themeVariables;
        // Drop non-string/non-number values (functions, nested objects) — only
        // serialize the leaf color/font/size fields the port reproduces.
        const flat = {};
        for (const [key, value] of Object.entries(tv)) {
          if (typeof value === "string" || typeof value === "number") flat[key] = value;
        }
        // Packet is the reviewed nested exception. Only Dark and Forest
        // construct this object without a user override.
        if (tv.packet && typeof tv.packet === "object" && !Array.isArray(tv.packet)) {
          flat.packet = Object.fromEntries(
            Object.entries(tv.packet).filter(
              ([, value]) => typeof value === "string" || typeof value === "number",
            ),
          );
        }
        return flat;
      };
      const themeEntries = themes.map((theme) => ({ name: theme, variables: dump(theme) }));
      const override = { name: overrideCase.name, variables: dump(overrideCase.theme, overrideCase.themeVariables) };
      return { themeEntries, override };
    },
    { themes, overrideCase, mermaidModule },
  );
  const fixture = {
    upstream: { package: "mermaid", version: packageJson.version },
    themes: result.themeEntries,
    overrideCase: result.override,
  };
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(fixture, null, 2)}\n`);
  console.log(`Wrote ${output} (${result.themeEntries.length} themes + 1 override)`);
} finally {
  await browser.close();
}

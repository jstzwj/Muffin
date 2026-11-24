// The four Noto faces the app bundles (third_party/noto/fonts), for mermaid
// fixture generators whose numbers must not depend on the machine's fonts.
// The native side measures text in exactly this stack:
// MermaidFontRegistry::cssFamilyStack().
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const notoDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "third_party", "noto", "fonts");

export const bundledFontFaces = [
  ["Noto Sans", "NotoSans-Regular.ttf"],
  ["Noto Sans CJK SC", "NotoSansCJKsc-Regular.otf"],
  ["Noto Sans Arabic", "NotoSansArabic-Regular.ttf"],
  ["Noto Sans Hebrew", "NotoSansHebrew-Regular.ttf"],
]
  .map(
    ([family, file]) =>
      `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}")}`,
  )
  .join("\n");

export const bundledFontFamily =
  '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';

// What a page.evaluate callback needs to register the faces before rendering.
export const bundledFontArguments = { fontFaces: bundledFontFaces, fontFamily: bundledFontFamily };

// First lines of such a callback.
export const bundledFontPrelude = `
      // The bundled Noto faces: what the port measures text in.
      const fontStyle = document.createElement("style");
      fontStyle.textContent = fontFaces;
      document.head.appendChild(fontStyle);
      await document.fonts.load('16px "Noto Sans"');
      await document.fonts.ready;
`;

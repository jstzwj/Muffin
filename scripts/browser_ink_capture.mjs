export const scanInkBBox = async buffer => {
  const {PNG} = await import("pngjs");
  const png = PNG.sync.read(buffer);
  let left = png.width, top = png.height, right = -1, bottom = -1;
  for (let y = 0; y < png.height; ++y) {
    for (let x = 0; x < png.width; ++x) {
      const i = (png.width * y + x) * 4;
      const r = png.data[i], g = png.data[i + 1], b = png.data[i + 2], alpha = png.data[i + 3];
      if (alpha === 0 || (r > 248 && g > 248 && b > 248)) continue;
      left = Math.min(left, x);
      top = Math.min(top, y);
      right = Math.max(right, x);
      bottom = Math.max(bottom, y);
    }
  }
  return right < left ? null : {x: left, y: top, width: right - left + 1, height: bottom - top + 1};
};

// A layout box does not bound painted ink (e.g. KaTeX's zero-width \mathllap).
// Start with descendant/text bounds, then grow until the image has a clear border.
// Move the whole stage by integer pixels when needed: clamping the screenshot at
// page origin would silently clip left/top overflow, and fractional moves alter rasterization.
export const captureElementInk = async (page, {target, stage, maxAttempts = 8}) => {
  const bounds = await page.evaluate(({target, stage}) => {
    const container = document.querySelector(stage);
    const root = document.querySelector(target);
    if (!container || !root || !container.contains(root)) throw new Error("Missing ink capture target/stage");
    container.style.transform = "";
    const rect = root.getBoundingClientRect();
    let left = rect.left, top = rect.top, right = rect.right, bottom = rect.bottom;
    const include = (rect, element) => {
      let {left: x0, top: y0, right: x1, bottom: y1} = rect;
      // Stretchy KaTeX SVGs can be hundreds of em wide and deliberately clipped
      // by an ancestor. Invisible geometry must not move/enlarge the capture.
      for (let ancestor = element; ancestor; ancestor = ancestor.parentElement) {
        const style = getComputedStyle(ancestor);
        const clipX = /^(hidden|clip|scroll|auto)$/.test(style.overflowX);
        const clipY = /^(hidden|clip|scroll|auto)$/.test(style.overflowY);
        if (clipX || clipY) {
          const clip = ancestor.getBoundingClientRect();
          if (clipX) { x0 = Math.max(x0, clip.left); x1 = Math.min(x1, clip.right); }
          if (clipY) { y0 = Math.max(y0, clip.top); y1 = Math.min(y1, clip.bottom); }
        }
        if (ancestor === root) break;
      }
      if (x1 <= x0 || y1 <= y0) return;
      left = Math.min(left, x0);
      top = Math.min(top, y0);
      right = Math.max(right, x1);
      bottom = Math.max(bottom, y1);
    };
    for (const element of root.querySelectorAll("*")) include(element.getBoundingClientRect(), element);
    const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
    for (let node = walker.nextNode(); node; node = walker.nextNode()) {
      const range = document.createRange();
      range.selectNodeContents(node);
      for (const rect of range.getClientRects()) include(rect, node.parentElement);
    }
    return {left, top, right, bottom};
  }, {target, stage});

  let padding = 4;
  for (let attempt = 0; attempt < maxAttempts; ++attempt, padding *= 2) {
    const dx = Math.max(0, padding - Math.floor(bounds.left));
    const dy = Math.max(0, padding - Math.floor(bounds.top));
    await page.evaluate(({stage, dx, dy}) => {
      document.querySelector(stage).style.transform = dx || dy ? `translate(${dx}px, ${dy}px)` : "";
    }, {stage, dx, dy});
    const clip = {
      x: Math.floor(bounds.left + dx - padding),
      y: Math.floor(bounds.top + dy - padding),
      width: Math.ceil(bounds.right + dx + padding) - Math.floor(bounds.left + dx - padding),
      height: Math.ceil(bounds.bottom + dy + padding) - Math.floor(bounds.top + dy - padding)
    };
    const buffer = await page.screenshot({clip, animations: "disabled"});
    const inkBBox = await scanInkBBox(buffer);
    const border = 2;
    if (!inkBBox || (inkBBox.x >= border && inkBBox.y >= border &&
        inkBBox.x + inkBBox.width <= clip.width - border &&
        inkBBox.y + inkBBox.height <= clip.height - border)) {
      return {buffer, inkBBox, clip};
    }
  }
  throw new Error(`Painted ink still touches the capture boundary for ${target}; refusing a clipped golden`);
};

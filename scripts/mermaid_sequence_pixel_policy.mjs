export const sequenceScenePixelCaseIds = new Set([
  "basic", "activation-note", "nested-fragment", "participant-types",
  "create-destroy-markers", "central-autonumber", "self-autonumber",
  "redux-color-actors-activations", "redux-dark-color-actors-activations",
  "label-participant-html-cjk", "label-message-wrap-bidi",
  "label-note-markdown-math", "label-fragment-html-rtl",
  "label-box-markdown-math", "label-dpr-125-html-cjk",
  "label-dpr-150-math-rtl", "label-dpr-200-dark-box-fragment",
  "label-dpr-125-dark-html", "label-math-fraction-ops",
  "label-math-radical-script-fraction-ops",
  "label-math-accent-array-recursive", "label-math-array-body-recursive",
  "label-math-fallback-delimiter-assembly",
  "label-math-root-mixed-sum-limits", "structural-aria",
  "structural-combined-order",
]);

const representativeLabelCropIds = new Set([
  "label-participant-html-cjk", "label-message-wrap-bidi",
  "label-note-markdown-math", "label-fragment-html-rtl",
  "label-box-markdown-math", "label-dpr-125-html-cjk",
  "label-dpr-150-math-rtl", "label-dpr-200-dark-box-fragment",
  "label-dpr-125-dark-html", "label-dpr-150-dark-math",
  "label-dpr-200-default-box", "label-math-genfrac",
  "label-math-fraction-ops", "label-math-stack-ops",
  "label-math-fraction-cross-recursive-ops", "label-math-root-index-fraction",
  "label-math-accent-fraction-recursive",
  "label-math-accent-radical-recursive",
  "label-math-accent-array-recursive", "label-math-array-body-recursive",
  "label-math-matrix-recursive", "label-math-cases",
  "label-math-bidi-isolates", "label-math-fallback-fraction",
  "label-math-fallback-array",
]);

export function hasDedicatedMathRaster(caseData) {
  return Boolean(caseData.mathAccent || caseData.mathGlyph ||
    caseData.verticalDelimiter || caseData.mathTokenOracle ||
    caseData.mathPhaseOracle || caseData.mathBodyFile ||
    caseData.mathAccentFile || caseData.mathGlyphFile ||
    caseData.mathTokenGroups?.length || caseData.mathRasterPhases?.length ||
    caseData.mathDelimiters?.length);
}

export function keepSequenceLabelCrop(caseData) {
  return representativeLabelCropIds.has(caseData.id) || hasDedicatedMathRaster(caseData);
}

export function collectPngReferences(value, references = new Set()) {
  if (typeof value === "string" && value.endsWith(".png")) references.add(value);
  else if (Array.isArray(value)) value.forEach((item) => collectPngReferences(item, references));
  else if (value && typeof value === "object")
    Object.values(value).forEach((item) => collectPngReferences(item, references));
  return references;
}

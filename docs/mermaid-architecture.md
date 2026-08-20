# Mermaid 原生渲染架构与 1:1 parity 路线

> 目标：用纯 C++/Qt 优雅地、严格地对齐 `mermaid-cli`（mermaid 11.x）的渲染行为。
> 本文是**目标架构 + 落地计划**，不是现状描述。每一步落地都以「golden 测试零回归」为安全网。

---

## 1. parity 的定义（先把「一比一」说清楚）

| 维度 | 目标 | 验证手段 |
|---|---|---|
| **几何/布局** | 节点、边、簇的坐标与 dagre/ELK 输出一致 | dagre-snapshots JSON oracle |
| **结构/语义** | SVG 的元素树、class、可访问性属性对齐 | 语义 SVG diff（结构 + 容差） |
| **视觉** | 像素级在容差内一致 | golden pixel 对比 |
| **配置** | 每个 config key 的效果与上游一致 | config-effect-matrix（533 行，逐 key 标 parity/partial/deferred） |

**不追求「字节同」的 SVG**：Muffin 的 SVG 由 `QSvgGenerator`（经 painter）产出，再由 `MermaidSvgExporter` 归一化成 mermaid 形态。字节级与 mermaid 手写 SVG 不同是必然的；parity 以**视觉 + 结构 + 几何**为准。

---

## 2. 初始诊断（五族阶段，现已解决）

以下内容保留当时的设计动机。当前实现已经落地统一 `MermaidScene`、`Diagram`
registry、族无关的 paint/export/canvas/interaction 产品边界，并增加了第六族
Requirement；准确现状见第 7 节及 `mermaid-native-port.md`。

**已有的好设计（保留）：**
- 模块化目录：`flowchart / sequence / class / state / erdiagram` 各有 tokenizer/db/layout/scene/painter。
- `MermaidDiagramDetector` + `MermaidPreprocessor` 已与渲染分离。
- **Scene 是单一真源**：painter 只消费 scene，不回读 db、不重算 layout（比 mermaid 本身更干净）。
- **双后端同源**：`MermaidSvgExporter` 用 `QSvgGenerator` 包 `QPainter`，raster 与 SVG 共享一条渲染路径。

**三个结构性痛点（耦合源）：**
1. **没有统一 `Diagram` 契约**：五套异构 `parse/measure/layout/buildScene` 签名，五个 Scene struct 无共同基类。
2. **`MermaidRenderCache::renderSource` 是 god-orchestrator**：5 个 `if (type==…)` 把每图整条管线内联进一个函数 → 任何特性都要改这个文件，特性在 hunk 级别纠缠（这次 commit 切不干净的根因）。
3. **painter 间无共享渲染原语**：handDrawn 要改 5 个 painter；marker/label/曲线/阴影各自重写。

> 一句话：**有「图维度」切分，缺「横切维度」切分与「统一契约」。** 而 mermaid 的优雅来自后两者。

---

## 3. 目标分层架构

```
L0  Config + Theme          JSON-schema 校验、themeVariables、统一 style cascade
L1  Diagram 契约 + Registry  每图实现统一接口；Registry 取代 if 链
L2  共享渲染原语             Node/Edge/Label/Marker/Curve/Rough Renderer
L3  薄编排器                 preprocess → detect → registry.dispatch → scene
L4  双后端（同源 scene）      scene → painter → { QImage | QSvgGenerator-SVG }
```

### L1 — 统一 Diagram 契约（最关键的一刀）

```cpp
// src/mermaid/MermaidScene.h —— 所有图 scene 的共同基类
struct MermaidScene {
  virtual ~MermaidScene() = default;
  virtual QRectF sceneBounds() const = 0;
  virtual void paint(QPainter&, const MermaidPaintOptions&) const = 0;
  virtual QJsonObject toJson() const = 0;
};

// src/mermaid/MermaidDiagram.h —— 每图实现一个，注册进 Registry
struct RenderOutcome {
  std::shared_ptr<const MermaidScene> scene;
  QSizeF naturalSize;
  std::any backendHints;   // 例：sequence 的 viewport options
};
struct Diagram {
  virtual ~Diagram() = default;
  virtual QStringList ids() const = 0;                 // {"er"} / {"state","stateDiagram"} ...
  virtual RenderOutcome render(const PreprocessedSource&, const Config&) = 0;
};
```

`renderSource` 瘦成：`preprocess → Detector → Registry::get(type).render(...) → entry`。
**god-orchestrator 消失**；新增第 N 种图 = 加一个 `Diagram` 实现 + 一行注册，不碰编排器。

### L2 — 共享渲染原语（让横切特性只改一处）

把 5 个 painter 里重复的抽成 `LabelRenderer / MarkerRenderer / CurveRenderer / RoughRenderer / NodeBoxRenderer`。
对应 mermaid 的 `insertNode / insertEdge / insertCluster`。
handDrawn 就是 `RoughRenderer` 的一个开关，所有图自动生效。

### L4 — Scene 作真源（保留这层优于 mermaid 的设计）

scene 同时是 culling、hit-test、双后端的依据。给所有 Scene 一个共同基类（`MermaidScene`），
让 registry/cache 统一持有 `shared_ptr<const MermaidScene>`，而不是 5 个并列指针。

---

## 4. 关键设计抉择

| 抉择 | 决定 | 理由 |
|---|---|---|
| scene 这层 | **保留** | mermaid 没有，但 culling/交互/双后端靠它 |
| SVG parity | **视觉+结构对齐，非字节同** | QSvgGenerator 产不出字节级一致 SVG |
| dagre | 抽**统一 dagre 适配器**，所有图投影到同一 node/edge/cluster input | 现在 5 套 measure/layout 各写一遍 |
| style cascade | `FlowStyleResolve` → `MermaidStyleResolve`，所有图共用 `compileStyles` | 现在 cascade 是 flowchart 专属 |
| htmlLabels | **「语义对齐、非像素同卵」**，必要时评估移植极小 CSS 盒模型 | parity 最难点；mermaid 用真 HTML 渲染 label |
| look（classic/neo/handDrawn） | 进 `MermaidPaintOptions`，所有 painter 经 L2 原语统一消费 | 现在每 painter 各写分支 |

---

## 5. parity 验证：golden oracle pipeline

把已有的种子（`flowchart-geometry.json`、`dagre-snapshots.json`、`config-effect-matrix.json`）泛化成跨所有图的 oracle：
1. 跑 headless `mermaid-cli`（mmdc）对同一批 source 产 reference SVG/几何；
2. Muffin 渲染同 source，做**语义 diff**（结构树 + 几何容差）；
3. 每个 config key 在 matrix 里标 parity/partial/deferred，测试守护。

**重构期间 golden 不变 = 行为零回归。** 这是整个路线的安全网。

---

## 6. 落地计划（增量、每步 golden 守护）

| 步 | 内容 | 风险 | 验证 |
|---|---|---|---|
| **1** | 引入 `MermaidScene` 基类；5 个 scene 继承并实现 `paint/toJson/sceneBounds`（委托现有 free painter） | 低（纯加法） | 编译 + golden 100% |
| **2** | 抽 L2 共享原语：先 `LabelRenderer / MarkerRenderer / RoughRenderer`，5 painter 改调它们 | 中 | golden 100% |
| **3** | 定义 `Diagram` 接口 + `Registry`；`renderSource` 的 5 个 `if` 迁成 5 个 `Diagram` 实现；cache 持 `shared_ptr<const MermaidScene>` | 中高 | golden 100% + cache smoke |
| **4** | 统一 dagre input shape + 泛化 `MermaidStyleResolve` | 中 | dagre-snapshots + golden |
| **5** | 跨图 golden oracle（mmdc reference + 语义 diff） | 低（仅测试） | 新 oracle 通过 |

每步独立可 commit、可 review。**第 1 步风险最低、是后续的地基**，先做。

---

## 7. 进度（2026-07-28）

落地后探查发现 **par­ity 机器已远比本文初稿以为的多**：11 个 oracle 测试 + 23 个真-mermaid（11.16.0，puppeteer/Chrome 捕获）reference fixture，覆盖 flowchart（dagre/geometry/scene/pixel/svg/config 全套）、class/sequence/state（db/layout/scene/pixel/svg）、ER 仅有 db 级。所以「跨图 oracle」不是从零搭建，而是**统一地基 + 补 ER 唯一空白**。

| 步 | 状态 | 备注 |
|---|---|---|
| 1 | ✅ | `MermaidScene` 基类（`sceneBounds` + `paint`）。 |
| 3 | ✅ | `Diagram` 契约 + `findMermaidDiagram` registry；`renderSource` 瘦编排器。 |
| 5（地基） | ✅ | **`toJsonObject()` 纯虚 + 5 scene 实现**（commit 3f29e55）；`MermaidSvgExporter` digest 扩展为全 5 图；`ParityDiff.h` 共享语义比对库（header-only）；**ER scene 回归 oracle**（`MermaidErSceneRegressionTest` + `er-scene.json`，commit 5791db8）。FlowScene `toJson()` 字节稳定，53 个 mermaid 测试全绿。 |
| 2（phase 1） | ✅ | **SvgPathParser** 抽取为首个 L2 原语（commit 788386b）：class/state/er 三 painter 的匿名 SVG path 解析拷贝合并为 `scene::parseSvgPath`（ER 超集，M/L/H/V/C/Q/Z），行为保持（170 测试全绿）。探查发现 Label（`paintFlowLabel`）与 Rough（`rough::`）**已是跨图共享**，无需再抽。NodeBox/Edge 变体纠缠、Marker 形状数据来自 5 源需框架——均暂缓（且只被像素 golden 覆盖、reference 不可重生成）。 |
| 4 | ✅ | **style cascade 全部完成**：`MermaidStyleResolve` family-agnostic 模块 + class 边 + state transition + ER entity 接入（所有 4 个有 classDef 语法的图）。**dagre input 100% 统一**：class flat 分支移除（19c4a97），`d::runDagreLayout` 现仅从共享管线 `layoutFlowchartNodesDagre` 内部调用——5 个图族全部经单一 dagre 入口。投影 helper 抽取（边际 DRY，各图 node/edge 类型不同）暂缓。 |

**ER 真-mermaid reference 已捕获**（commits 1d176c6 + 9d0db26）：`scripts/generate_mermaid_er_geometry_fixture.mjs` + `tests/fixtures/mermaid/er-geometry.json`（真 mermaid 11.16.0 ER entity bounds + relationship path + cardinality，首-entity 归一化）+ `MermaidErGeometryOracleTest`（按 id 比 entity bounds、按 cardinality tuple 比 relationship path）。sibling checkout 已一键可复现（`node scripts/setup_mermaid_reference_toolchain.mjs`，见 `docs/mermaid-reference-toolchain.md`）。

**ER geometry oracle 已翻成 fail-on-divergence**（commit 9d26947）。**Phase 2 完成**：`measureErLayoutInput` 重写为 mermaid erBox 模型（空 fast-path + 4 列宽 + 行高 + 重分配，padding diagramPadding=20/entityPadding=15，SVG 模式 ×1.25），entity **高度现精确匹配** mermaid。Oracle 断言 **font 无关 par­ity**（entity 高度 + cardinality + identifying）；宽度/x/y 与 relationship path 是 **Qt-vs-Chrome 字体光栅化耦合**（~5px/文本，与像素 golden 同源），报告不断言；`entity-alias` 跳过（mermaid 按 id 而非 alias 定尺寸，边缘怪癖）。**`config-effect-matrix.scope.families` 已加 `er`**（commit b933d04，13 个 ErDiagramConfig 字段全分类 + ER spacing probe）。

**已关闭的误判**：后续审计证实 graphlib 的迭代面由 insertion-order
`OrderedMap` 承载，原先推测的「ER dagre QHash 抖动」并不存在；0.01 是几何
oracle 的坐标容差，不是随机性掩码。ER 已加入字节级 SVG 双渲染确定性测试。

**2026-08-14 现状**：全部 38 个 Mermaid 11.16 detector ID 均通过单一 scene 指针和 `Diagram` registry
进入族无关的 editor/PNG/SVG/canvas/interaction 路径；各 adapter 分离在各自
TU。新增的 Pie、Quadrant、Journey、Radar、XYChart、Timeline、Packet、Kanban、Mindmap、Block、Swimlane、GitGraph、C4、TreeView、Event Modeling、Ishikawa、Venn、Sankey、Treemap、Cynefin、Wardley、Architecture、Gantt 和 Info 均有真实 Mermaid 11.16.0 语法、几何和像素
oracle。完整 Release 门禁为 292/292。配置矩阵现为 533 行（363 parity /
0 partial / 125 upstream-inert /
19 legacy-only / 25 api-only / 1 security-fixed）。上游最先注册的 error 图族
（字面 `error` 源的灯泡 SVG + parse/detector 失败的 fallback 场景 + `---`
frontmatter 守卫消息）现已有原生 scene/adapter 与 11 主题
errorBkgColor/errorTextColor 接线。themeCSS 已从
unsupported 升级为 parity：34 个 family interface 全部经
`MermaidCssCascade` 消费用户 CSS，对照 `mermaid-theme-css.json`
117 案真实 DOM oracle（Wardley 上游惰性，native 天然 parity）。
themeVariables.\* 已完成收口：`theme-variables-inventory.json`
285 key × 11 主题全量 golden，**全部经 `FlowThemeVariables::get()` 逐值锁定**
（原 56 个 remaining 清零——26 个活键按 11.16 每主题 `||` 派生链/构造器
字面量建模并接入家族消费者；30 个上游死键同样建模锁值并注明零消费证据）。
本轮（P2）同步修复了多处以 IoU 容差掩盖的真实渲染发散：sequence 样式表
从「dark 硬编码 + 原始 override 直读」改为消费 resolved 主题（dark
activation/note/labelBox 精确值、非 default 主题全部 11 主题正确、
loopLine 边框 = labelBoxBorderColor 虚线 2,2、loop 无背景矩形、
`rect` 片段走 config.themeVariables 合并读）、state 的
transition/note/stateLabel/edge-label 背景色（含 0.5 opacity）改接中央派生、
class 的 classText/note 调色板、C4 `.person` 规则按主题取
personBorder/personBkg、gitGraph redux 分支标签字重改读 `noteFontWeight`
正键、flowchart/Swimlane 圆角矩形的 `themeVariables.radius`（合并读：
用户 override ?: 主题字面量——neo 3 / redux 12 现在真实生效）。
scaleLabelColor/branchLabelColor 的 override 传播与 dark 的 labelColor
"calculated" 哨兵泄漏均有断言锁定。此前的 state 特殊形状修复
（start 圆 `specialStateColor`、end 内点 `stateBorder ?? nodeBorder`、
end 环 `mainBkg` 填充）与 redux 族构造器 gradient 字面量缺失一并保留。
Requirement 的全局 `htmlLabels:false` 已收口（P3，08-15）：false 路径按上游
`createFormattedText` 精确实现——SVG `<text>` 行距固定 1.1em dy（CSS
line-height 对 SVG tspan 无效）、±2px 背景 rect、getBBox（advance × hhea
字胞高 + (rows-1)·1.1em）+4 喂 Dagre 与绘制；像素 oracle 追加
`html-labels-false` 案（**画布 253×718 精确一致，IoU 0.999**，收口前
710/0.978）。Linux 6 个字体 golden 的 skip 理由已精确化：bundled Noto
已经跨平台注册、几何走 OpenType design metrics，剩余分歧是 FreeType vs
DirectWrite 光栅边缘像素——收口需在 Linux 上重生成/双平台浏览器金图（平台
基建项）。外部 `mermaid.initialize()` 配置不属于当前
Markdown source API。

**Codex 审核修复（08-15 第二轮，292/292）**：① sequence 笔级 parity 根因
收口——QPen dash 单位是笔宽的倍数，且 Qt 光栅引擎会把周期 <5px 的
CustomDashLine 固化成实线、默认 SquareCap 又把每段两头各延长半个笔宽，
因此 loopLine（2,2@2px）、section 分隔（3,3@2px）、dotted 消息（3,3@1.5px）
改为**手工分段绘制**（`drawDashedEdge`，FlatCap，逐边相位重置——与上游 4 条
独立 `<line class="loopLine">` 同构；浏览器金图实测 2on/2off 无 AA 补隙）；
lifeline 按 CSS 裸 `line{stroke-width:2px}` 规则改 **2px 实线**（attr 0.5px
被覆盖、无 dasharray）、消息线宽改 **1.5px**（`.messageLine0/1` 赢 attr 2）；
`look: handDrawn` 对 sequence **上游无效**（classic/handDrawn SVG 仅差 render
id 计数器，浏览器实证），sequence 场景删除 rough 分支并以 toJsonObject 相等
断言锁定惰性；② error 图族 themeCSS 等价性——内容 `<g>` 改为 svg 根直接
子节点（含 `<style>` 占位，`g:nth-of-type(2)` 结构选择器经浏览器 oracle
锁定）、图标补 stroke/strokeWidth 通道、文本 fill/stroke **双通道绘制**
（drawText 填充 + QPainterPath 描边，像素 IoU 0.926→0.950）；③ error SVG
viewBox 高度改 **108.671875**（LayoutUnit floor64 金值，新
`svgClientViewBox()` 通道），PNG 光栅仍 109；④ state 边标签背景
`opacity:0.5` 与 fill 自带 alpha **相乘**（rgba 0.2→0.1）；⑤ error fallback
scene 的产品声明收窄为 PNG/SVG 导出（编辑器画布保留源码+诊断面板的产品
路径，RenderMermaidBlockTest 锁定）。后续审计项：class/journey/requirement/
gitGraph/c4 的 linkStyle dash 仍有未按笔宽归一化的写法（各有金图锁定，
未在本轮范围内盲改）。

**Codex 审核修复（08-15 第三轮，292/292）**：error 图标的 themeCSS
逐路径闭环——此前 adapter 虽为六条灯泡 path 各建了 `icon0..icon5`
CSS 元素，却只读取 `icon0` 折叠进单一 `scene.css.icon`，painter 再用
这份样式画全部六条 path，`.error-icon:nth-of-type(2)`、
`.error-icon + .error-icon` 这类合法规则因此与浏览器不一致（第二到第
六条 path 的差异化 computed 值被丢弃）。现 `ErrorElementCss.icons`
是与 `iconPaths` 索引对齐的 `QVector<ErrorIconCss>`，adapter 逐个读
取 `iconN`，painter 逐 path 消费 fill/stroke/stroke-width/opacity/
display。fixture 新增 `themeCssPerPath` 浏览器案（:nth-of-type(2)
单独填色、相邻选择器只描 1..5 号、单 path opacity 0.5 / stroke-width
4px / display:none），`themeCssStructure` 的图标 computed 值也改为
六条全捕获（此前只读首条 path，正是漏检根因）；测试逐 path 断言
+差异锁（icon0/1 fill 不同、仅 1..5 有描边）。fixture 重生成双跑
字节一致（`6a7f55da…`）。

**State 图族审核修复（08-15 第四轮，state browser oracle 全面升级）**：
Codex 复审指出 state 族存在 6 项 P1 + 3 项 P2 发散（箭头形状、并发
divider、note 连线、click、linkStyle 幻影实现、themeCSS 虚拟折叠；
neo look、多行裁剪、光栅取整），且旧 pixel oracle 用 400×400 拉伸 +
0.84/0.70 阈值掩盖了尺寸与形状差异。本轮全部收口：

- **P1 箭头**：raster marker 从实心三角形改为上游 concave barb
  （`M 19,7 L9,13 L14,7 L9,1 Z`，refX 19 锚在端点、fill+stroke
  transitionColor 1px——`defs [id$="-barbEnd"]` 特异性高于
  `[id$="-barbEnd"]`）；neo 参照 `-margin` 克隆（refX 17、
  `M 19,7 L11,14 L13,7 L11,0 Z`）。结构 fixture 新增 `childPathD`
  逐字锁定 + `svgMarkerProjection()` 一致性断言。
- **P1 并发 divider**：`--` 分区从「紫框复合态+竖线」改为上游单
  rect（class divider，rx 0，fill altBackground、dash 10/10、
  stroke stateBorder，无 inner/无标题）；neo 下级联让 fill 变
  mainBkg。复合态本体（outer rx5 + inner 带 stroke）与上游一致保留。
- **P1 note 连线**：`.note-edge { stroke-dasharray:5 }` 落地
  （5on/5off FlatCap；basis 曲线本就正确）；结构 fixture 边 computed
  dash 锁定（note-edge 唯一虚线）。
- **P1 click**：parser 已存 URL/tooltip 但下游全未消费——现在
  adapter 剥离首尾引号（上游 `replace(/^"+|"+$/g,"")`）后传入
  `buildStateScene`，scene 预计算 `interactionRegions_`（族无关
  hit-test/SVG overlay 自动生效）。结构 fixture 新增 anchors 捕获
  （上游渲染后把节点 g 包进 `<a xlink:href title>`）+ region 断言。
  注意：孤 state（无 relation）上游不渲染任何节点（quirk，native
  同构跳过）。
- **P1 linkStyle 幻影**：11.16 state 语法无 linkStyle 产生式——
  tokenizer 删除关键字、`linkStyle 0 stroke:x` 按上游解析为三个普通
  state 令牌（"linkStyle"、"0"、"stroke" 以余文为 label；bare id 走
  jison case 44/45 包成 state 对象，`state X` 仍返回字符串）；边也
  不再有 classDef 通道（上游只编译节点的 cssCompiledStyles）。
  state-db fixture 新增 `linkstyle-line-tokens` 案（上游 db+layout
  双锁）+ StateLinkStyleTest 改锁「与基线逐字段相等」。
- **P1 themeCSS 真实 DOM**：adapter 从单虚拟节点折叠改为完整 11.16
  DOM 树（g.root>[clusters/edgePaths/edgeLabels/nodes]、每节点形状
  元素+label span 栈、边 path.transition、边标签 fo/div/span/p、
  divider/inner/cluster-label、start 圆、end 双 path、noteGroup），
  builtInCss 为 getStyles 活规则子集（死规则注明原因），逐元素盖章
  `StateElementCss`（fill/stroke/sw/visible/opacity/color/font），
  逐节点测量反馈（label 字体、`.node rect{display:none}` 0×0——
  note 除外：note 走 labelHelper 度量不塌缩，浏览器实证）。state-layout
  fixture 新增 themeCss 三案（structural 逐元素差异/hidden/font
  反馈）双跑一致。
- **P2**：① neo look——barbNeo marker + `[data-look=neo]` cluster CSS
  （fill mainBkg、rx themeVariables.radius），pixel 新增 neo-look 案
  首跑即 IoU 0.937/RGBA 0.969（梯度/阴影仅 neo 主题出现，未在本轮
  范围）；② rectWithTitle 文本栈按上游：label 组 top+3、divider=
  titleHeight+padding/2、描述块 top+3+titleHeight+9 连续行盒不再逐行
  裁剪（descriptions IoU 0.84→1.000）；③ 光栅尺寸 qCeil→qRound
  （Chromium element screenshot 取最近设备像素：131.125→131、
  108.67→109），五案尺寸全部精确一致（132/131、1105/1104、
  289/288 全消）。
- **连带根因**：① 边标签背景实为 `.edgeLabel p` 的
  background-color=edgeLabelBackground（default rgba(232,232,232,0.8)
  自带 alpha；旧模型错用 labelBackgroundColor×0.5——SVG-label rect
  规则对 html 标签恒死）；② scene.bounds 现并入边标签盒（上游
  svg.getBBox union 含标签，宽于节点的标签会撑宽 viewBox）；③
  upstream html 节点每标签另带 0×0 svg-background rect（`.node rect`
  每节点命中 2 条——theme-css 比对改用 `.node .label-container`）。
- **阈值收紧**：state pixel 尺寸从 1% 容差改精确相等，IoU 0.84→0.90、
  RGBA 0.70→0.93；最低 IoU 0.898→0.916。
- **新发现的既有布局发散（未在本轮修，后续项）**：① 外部边入复合簇
  （A→簇内 B）时 native 簇高度 +50（一 ranksep）；② note-group 与
  复合簇同图时 note 落在 B/C 之间的中间 rank。themeCss structural
  案已避开两者并注明。

**State 图族第五轮审核修复（08-16，292/292，dist 已刷新，未提交）**：
Codex 复审第四轮后发现 5 项残留（4 P1 + 2 P2），本轮全部根因收口：

- **P1 生产路径取整**：`StateScene` 覆盖 `roundRasterExtentToNearestPixel()`
  （生产 PNG 渲染走 `renderMermaidSceneToImage` 的 qCeil 分支，131.25px
  仍会输出 132——测试专用的 `renderStateSceneToImage` 已 qRound 但产品
  路径没跟上）+ 覆盖 `svgClientViewBox()`（导出 SVG 的 viewBox 从整数
  canvas 退化为整数改为携带 getBBox 的精确小数，如 76.96875；error 图族
  同款通道）。结构测试新增 6 案 viewBox 逐分量锁。
- **P1 neo-look 真渲染**：原 pixel 案 look 只喂给浏览器 initialize()、
  source 不带指令——native 走 classic，0.937 是 classic-vs-neo 的假绿。
  现在 look 进 `%%{init}%%` 指令（生产 parse 语义）+ 新增
  neo-pseudostates 案。**neo 真实渲染面**：① 上游 neo 的
  `[data-look="neo"].node rect/circle/path` 通用样式表规则（nodeBorder
  描边、1px 宽）进 builtInCss neo 段；② drop-shadow 真绘制——theme 的
  dropShadow 字符串（默认 `drop-shadow( 1px 2px 2px rgba(185,185,185,1))`）
  解析出 dx/dy/blur（σ=blur/2），灰度 mask + 可分离高斯 + 预乘合成，
  应用于 node rect/circle、note 与 end 的 `g.outer-path`、cluster outer；
  ③ `arrow_barb_neo` 的 `markerOffsets` 5.5px 端点回缩（上游
  `getLineFunctionsWithOffset` 同构）进共享 `clipFlowEdgeForMarkers`，
  marker tip 在缩短端 +2px（-margin 语义）。**像素结果**：neo-look
  0.937→0.981、新增 neo-pseudostates 0.939、全套最低 IoU 0.898→0.939。
  **上游非确定性**：neo rough 描边（roughness 0）控制点仍带随机参数
  （共线、几何等价），Skia 三次曲线光栅 AA 在渲染间抖动——fixture 以
  `handDrawnSeed:42` 钉死浏览器 RNG。
- **P1 光栅箭头第四点**：上翼符号反了（classic (29,1) 应为 (9,1)——
  `tip - wingBack` 把 x 也镜像；正确是只翻 y 分量 `wingUp=(-10,-6)`；
  neo (27,0)→(11,0) 同理）。箭头颜色改走全局 marker 槽
  （`defs [id$="-barbEnd"]` 的 transitionColor，CSS DOM 新增 defs>
  marker>path 元素×2 使 themeCSS 可覆写），不再用逐边 stroke——
  `path.transition{stroke:red}` 只改线不改箭头。
- **P1 themeCSS DOM/绘制语义**：① label 链补 `span>p`（`.nodeLabel p`
  可命中）+ g.label 首子 0×0 背景 rect + 边标签 div.labelBkg；②
  note/choice/fork/stateEnd 的真实子树是「中间 g > 填充 path + 描边
  path」rough 双路径（roughness 0、几何上直线）——CSS DOM 逐 path
  建模（`shape`/`shape-stroke`/`inner`/`inner-stroke` 槽），`userNodeOverrides`
  默认描边宽 1.3（非 CSS 的 1px）同步落地；`.node polygon` 规则删除
  （11.16 state DOM 无 polygon，choice 是 path）；③ `visibility:hidden`
  只藏文字不藏框（StateElementCss 拆 `displayed`（display:none——布局
  塌缩）与 `painted`（visibility——仅绘制跳过）双通道）；④
  `fill:none`/`stroke:none` 经 `resolveSvgPaint` 走 NoBrush/NoPen 而非
  颜色解析降级成黑；⑤ `background-color:transparent` 原样写入
  `edge.labelBackground`，painter 按 alpha==0 跳过绘制（可清除边标签
  底色）。
- **P2 CSS 测量反馈断层**：cluster 标题用 `.cluster-label` span 的
  computed 字体测量（`.cluster-label{font-size:31px}` 使复合簇盒与
  viewBox 真实增长——roundedWithTitle 渲染期
  `max(node.width, bbox+padding)` 的同构，经
  `options.measuredClusterLabels` 进 dagre 提取）；scene 构建期
  titleHeight/描述行盒/边标签盒全部改用同一 css 字体（不再被基础字体
  重算）。
- **P2 SVG 残留箭头**：`svgMarkerProjection` 对 `display:none` 或
  `visibility:hidden` 的 transition 不再生成 markerEnd 覆盖边（marker
  只随引用 path 渲染）。结构测试对每个 themeCss 案断言「投影边数 ==
  可见边数」，structural 案加 `g.edgePaths path:nth-of-type(2){display:none}`
  实锁。
- **themeCss oracle 扩容**：state-layout 3→6 案（+cluster-font /
  paint-semantics / marker；node 捕获从「首个 rect」改为「首个
  rect/circle/path 形状元素」+ span visibility + cluster label 字体 +
  marker path computed），fixtureSha `c23036d3…` 双跑一致；state-pixel
  6→7 案（+neo-pseudostates，neo 两案 source 带 look+handDrawnSeed
  指令），fixtureSha `cf4ec676…` 双跑一致。pixel 测试加
  `MUFFIN_SAVE_NATIVE` 落盘钩子（requirement 同款）。

**State 图族第六轮审核修复（08-16，292/292，dist 已刷新，未提交）**：
Codex 复审第五轮后的 4 P1 + 1 P2 全部根因收口：

- **P1 标题带与导出尺寸**：浏览器实证 `insertTitle`（标题基线在内容
  bbox 上方 titleTopMargin、text-anchor:middle）+
  `setupViewPortForSVG`（union bbox ±8 padding）：titled viewBox =
  `0 -52 41.4375 196`——标题带 = 25 + round(Noto18 ascent=19) + 8 = 52。
  实现：共享 `titleHeight` 公式从「40 近似」改为
  `titleTopMargin + round(QFontMetricsF.ascent) + bandPadding`（state 经
  新 `titleBandPadding=8` 参数传入——其 8px 折在 scene.bounds 里，
  diagramPadding 仍 0）；`paintMermaidTitle` 从条带垂直居中改为**基线
  定位**（baseline = strip.bottom - titleTopMargin，drawText 基线
  重载）；`measureMermaidTitleWidth` 从 QFontMetricsF 有 hinting 的
  advance（差 1.6px@18px）改为 `measureOpenTypeDesignAdvance`+ceil/64
  （getComputedTextLength parity）；`finalizeReadyEntry` 标题加宽从
  qCeil 改 qRound（Chromium 最近像素）。**导出器**：SvgCanvas 新增
  `clientSize`（svgClientViewBox 家族带小数、含标题带、按标题盒加宽），
  根 viewBox 与 max-width 全部走 `formatSvgLength` 小数
  （`0 0 104.390625 198` / `max-width: 104.390625px`），sceneOffset 按
  小数宽度居中。结构测试新增**导出字节级**断言（renderMermaidSourceToSvg
  的根 viewBox+max-width 对 fixture 逐值）+ titled 布局案
  （viewBox `-30.9921875 -52 104.390625 198` 锁定）+ titled pixel 案
  走**生产 PNG 合成路径**（104×198 精确一致，IoU 0.944）。
- **P1 Neo 阴影三处**：① 高斯核标准差此前按 2σ 计算（模糊放大一倍），
  改为 σ（radius=3σ 截断）；② mask alpha 先乘 RGB 再 qPremultiply =
  alpha² 双乘（软边发暗）——RGB 保持全色、仅 alpha 通道携带 coverage；
  ③ `url(#drop-shadow)` 变体（redux-dark 族）不再视为无阴影——
  adapter 用 FlowTheme 的 shadowColor/shadowOpacity/shadowOffsetX/Y 合成
  等价 flat `drop-shadow(4px 4px 0px rgba(...))`（浏览器实证 defs 为
  feDropShadow dx4 dy4 stdDeviation0 flood 白 6%）；阴影 tint 自身的
  alpha（0.06）与 coverage 相乘后参与预乘。新增 redux-dark-neo pixel 案
  （IoU 0.987/RGBA 0.970）。
- **P1 stroke:none/0**：transition 画线对 `stroke:none` 或宽度≤0 显式
  `Qt::NoPen`（QPen(invalid QColor, w) 在 Qt 里画黑 cosmetic 线）；
  paintArrow 的 pen/brush 分通道（`fill:green;stroke:none` = 无描边）；
  StateElementCss 新增 `strokeWidthSet` 区分「未声明宽度」与声明
  `stroke-width:0`（0 = 禁笔，不回退主题宽）。marker 案 themeCSS 加
  `path.transition{stroke:none}` 锁定（comparator 对 "none" 逐字比较）。
- **P1 display/visibility 全链拆分**：测量的 shapeHidden 改为**只看
  display:none**（引擎 `displayed()` 折叠了 visibility，
  `.node rect{visibility:hidden}` 会错误塌缩 dagre 盒——浏览器实证
  viewBox 与 40px 节点盒不变）；折叠扩展到伪状态（start/end 圆、
  fork/join/choice 路径 display:none → 0×0 temp group）；painter 为
  start/choice/fork/end 补 per-element 门（rough 双 path 的 fill/stroke
  各自按其元素 display/visibility 独立隐藏）。paint-semantics 案加
  `.node rect{visibility:hidden}`（client/viewBox 不变 + 框隐藏双锁）。
- **P2 CSS DOM 补全**：① click 节点在 CSS DOM 里包 `<a>`
  （g.nodes > a > g.node，上游 wrap after layout）——`a .node rect`
  类选择器现在逐节点生效；② rectWithTitle 描述区建模：**勘误**——
  描述 foreignObject 在 g.label **内部**（createLabel_default(label, …)
  的第二个 fo，非节点级；第五轮误读 dump），DOM 链
  g.label > [rect 0×0, fo(title), fo(desc) > div > span>p ×行]，场景
  `descriptionCss` 门控描述行绘制。新增 state-theme-dom 案
  （`a .node rect` 染红被 click 节点 +
  `g.label foreignObject:nth-of-type(2){visibility:hidden}` 隐藏描述区）。
- **fixture**：state-layout 7 案（+titled）+ themeCss 7 案
  （+dom；paint-semantics/marker 扩语义）`1dda0132…` 双跑一致；
  state-pixel 9 案（+titled/redux-dark-neo）`1cab6955…` 双跑一致。
  LayoutOracleTest 改经 preprocessDiagram（titled 案的 frontmatter
  不再裸喂 family parser——此前直接 0xc0000409 崩溃）。
- **像素**：transitions 0.998、descriptions/compound 1.000、
  pseudostates-dark 0.990、note 0.976、neo-look 0.973、
  neo-pseudostates 0.925、titled 0.944、redux-dark-neo 0.987——
  最低 0.925。
- **教训**：① 标题带是 font-metric 相关（25+ascent+8），Chrome 文本
  getBBox 用的 ascent/descent 是 round 后的整型字体度量；②
  `Qt::AlignBottom` 对齐的是文本盒底非基线——基线定位用 drawText
  的 QPointF（左基线）重载；③ QImage==比较含格式（ARGB32 vs
  Premultiplied 不等）；④ CSS DOM 重建须从元素创建代码
  （createLabel_default 的 parent 参数）读结构，dump 的缩进会误导；
  ⑤ 阴影 tint 自带 alpha（flood-opacity）必须与 coverage 相乘。
- **教训**：① neo 的 look 只在浏览器 initialize() 时，source 不带指令
  = native 永远渲染 classic——fixture 的 look 必须进 `%%{init}%%`；
  ② rough.js roughness 0 仍随机化（共线）控制点参数，浏览器金图须
  handDrawnSeed 钉死；③ QPainter 画 Grayscale8 mask 必须用白刷（黑刷
  写 0=无墨，阴影静默消失）；④ `QString::arg` 顺序替换按「最低占位符」
  逐参进行——模板里删掉一个 `%N` 会让后面全部参数左移一位（本轮
  label color 变 noteBorderColor 即此）；⑤ 上游 marker 颜色是全局
  defs 规则，与逐边 path 样式分属两个通道。

**State 图族第七轮审核修复（08-17，292/292，dist 已刷新，未提交）**：
Codex 复审第六轮后的 4 P1 + 2 P2 全部根因收口：

- **P1-1 titled viewBox 原点**：上游语义实证——`setupViewPortForSVG`
  写 `viewBox = svgBBox(content ∪ title) ± 8` 且**不做任何 translate**
  （viewBox 原点携带内容 raw 坐标：pseudostates `2 0 …`、titled
  `-30.9921875 -52 …`），`insertTitle` 的基线是**绝对 -titleTopMargin**
  （text y 属性）、x 居中于**插入时**的 content bbox。三个 native 根因：
  ① state 布局缺共享 dagre-wrapper `render()` 硬编码的
  `marginx/marginy = 8`（dagre translateGraph 把内容 bbox 放到 (8,8)），
  且提取时首节点重定心抹掉了绝对坐标——现在
  `options.diagramPadding = 8` + `preserveDagreCoordinates`，7 个布局案
  的 bounds 原点与浏览器逐分量一致；② fork/join 的 dagre 盒是
  **74×14**（painted 70×10 每边 +2 padding——pseudostates 案 viewBox.x=2
  的来源）；③ note 侧反射是 native 特有的后处理，镜像后绝对原点漂移——
  反射后重新锚定（最左节点/簇边回到 margin 8）。实现：共享
  `mermaidClientBox(entry)` helper（content ∪ titleBox±padding，标题盒
  用 round 后整型字体度量），导出器 clientBox 家族改在**场景自身坐标**
  里绘制（generator viewBox = clientBox，根 viewBox 四分量走
  formatSvgLength），PNG 合成路径按同一映射放置内容与标题（标题中心
  = content bbox center 平移，titled 像素 0.944→0.950）。测试盲区修复：
  scene 级 clientBox 与导出字节（viewBox 四分量 + max-width）全部对
  fixture 逐值锁（此前只比 [2]/[3]）。
- **P1-2 handDrawn 消费 CSS 通道**：rough 分支全面消费与平滑路径相同
  的门——边 rough 对 `stroke:none`/宽度 0 显式 NoPen（QPen(invalid
  QColor) = 黑 cosmetic 线陷阱）；start/end/fork/join/choice/note/rect/
  簇/divider 的 handDrawn 分支逐元素门控（display/visibility 独立于
  none）；hachure 描边（FillSketch 第 4 参 pen）改用
  `drawable.options.fillWeight`（4px，此前误用轮廓宽度）。CSS DOM 按
  look 分叉：handDrawn 下节点类 token **node → rough-node**（`.node`
  选择器不再命中，浏览器实证），plain rect 变 rough 对
  `g.basic.label-container > [fill:none;stroke:<fill>;width 4,
  stroke:<border>;1.3px]`；painter 在 handDrawn 下把填充通道解析到
  填充 path 的 computed **stroke**（hachure 载体）。
  新 themeCss 案 `state-theme-handdrawn`（`.rough-node
  path:nth-of-type(2){stroke:none}` 锁逐 path 门 + marker opacity +
  边宽；rough **ink 外扩**的 canvas parity 是开放项，该案跳过 client
  断言并注明）。**教训**：`%%{init}%%` 里的 themeCSS 不能用单引号——
  mermaid 的指令扫描先全局 `'`→`"` 再 JSON.parse，`[id$='-barbEnd']`
  会把整个指令炸掉（浏览器静默回落 classic）；fixture 指令一律
  JSON.stringify 构造。
- **P1-3 Neo 阴影按实际 alpha**：`paintNeoShadow` 的输入改为元素
  **实际渲染**——fill 区按 fill alpha×fill-opacity×元素 opacity、stroke
  区按 stroke alpha×stroke-opacity，源上叠加（Grayscale8 mask 逐通道
  setOpacity 白刷）；两通道都不画（`fill:none;stroke:none` 或元素隐藏）
  时无阴影；end 的 `g.outer-path` 组滤镜输入 = 环+内点逐通道
  source-over 合成。新 pixel 案 `neo-shadow-off`
  （`.node rect{visibility:hidden}`：框与阴影同灭，IoU 0.909/RGBA 0.967）。
- **P1-4 marker 全通道**：StateElementCss 增加
  fillOpacity/strokeOpacity；raster 箭头消费 markerCss 的
  displayed/painted/opacity/strokeWidthPx（声明 0 宽 = NoPen）；
  `svgMarkerProjection` 的 marker path 序列化 display/visibility/
  opacities/stroke-width。**语义细节**：`defs [id$="-barbEnd"]` 命中的是
  **marker 元素**（id 载体）——opacity 不继承，path 的 computed 仍是 1，
  但被引用 marker 的渲染按 marker 自身 opacity 合成：fixture 同时捕获
  两者，测试断言乘积（引擎的 effectiveOpacity 祖先链恰好给出该 used
  值）。
- **P2-5 CSS DOM 真实化**（浏览器逐元素探针定版）：① `<a>` 带真实
  属性（xlink:href/title，包着的 g.node 也拿 title）——
  `a[title="tip"] .node rect` 属性选择器生效（theme-dom 案改用之）；
  ② rectWithTitle 的 rect+divider 包进无类 `<g>`，rect 类收敛为
  `outer title-state`（无 basic/label-container），divider 是独立元素
  （dividerCss 槽进 painter）；③ 描述区 = 单个
  `fo > div > span.nodeLabel > p×行`（不再逐行虚构 g+rect+fo）；
  ④ stateEnd 内点对嵌套在 outer-path 内的无类 `<g>`；⑤ 边标签组带
  `label` 类；0×0 背景 rect 只存在于 markdown 标签（plain rect + note）；
  ⑥ 外层无类 `<g>` 包裹（marker defs 在其中、每 marker 一个 defs；neo
  才有 -margin 克隆、两 path 同为 neo 几何）+ 根级两个 filter defs。
- **P2-6 跨图族小数 viewBox**：ArchitectureScene 暴露
  `svgClientViewBox()`（bounds = 末端 getBBox+padding 的精确小数），
  导出根 viewBox/max-width 走小数原点；MermaidSvgExportTest 新增
  architecture-paint 案对浏览器 fixture 四分量断言（0.2px 容差 =
  Qt/Chromium shaper 残差，与 theme-css comparator 同参）。
- **fixture**：state-layout 8 themeCss 案（+handdrawn；marker 案扩
  opacity/stroke-width；dom 案改属性选择器；捕获 markerOpacity 与
  rough 第二 path）`5e34ab0e…` 双跑一致；state-pixel 10 案
  （+neo-shadow-off）`02a80318…` 双跑一致。像素：transitions 0.998 /
  descriptions·compound 1.000 / pseudostates-dark 0.990 / note 0.976 /
  neo-look 0.973 / neo-pseudostates 0.925 / titled **0.950** /
  redux-dark-neo 0.987 / neo-shadow-off 0.909。
- **新开放项**（记录在案）：① handDrawn rough ink 外扩的 canvas parity
  （sharp-vs-rounded 角、逐形状 options 序列——state handDrawn 像素案
  因此未加）；② fork/join + note 同图时 B 落点在浏览器呈对角
  zig-zag（native 直列）——相对布局发散，非本轮六项之一；③
  handDrawn 下 rectWithTitle 上游渲染为单 fo 的 plain 形（quirk），
  native 仍按 titled 模型绘制。

**State 图族第八轮审核修复（08-17，292/292，dist 已刷新，未提交）**：
Codex 复审第七轮后的 5 P1 + 1 P2 全收口：

- **P1-1 透明度重复相乘**：csscascade 的
  `effectiveFillOpacity/effectiveStrokeOpacity` **已包含**
  `effectiveOpacity`，而 adapter 既存 effective 三件、painter
  （`StateScenePainter`）/SVG projection（`StateScene.cpp`）又乘一次
  → `opacity: 0.2` 渲成 0.04；普通节点只消费 opacity，显式
  fill-opacity/stroke-opacity 被忽略；Neo shadow 同样双乘。根因修复：
  `StateElementCss.opacity` = effective（祖先链折叠）、
  `fillOpacity/strokeOpacity` = **纯声明因子**（`cssOpacity(raw)`，与
  Cynefin 既有约定一致），painter 按「颜色 alpha × opacity × 通道」
  **各乘恰好一次**（`withFillOpacity/withStrokeOpacity`）；handDrawn
  hachure 走填充 path 的 stroke-opacity 通道（此前还存在
  handDrawn 分支 opacity² 的隐藏双乘）；`shadowChannelAlpha` 改为直接
  取通道折叠后的槽 alpha（阴影输入=元素真实渲染）；SVG projection
  三个因子分开序列化（opacity + fill-opacity 组合=浏览器乘积）。
  **锁定**：state-layout 新 themeCss 案 state-theme-opacity
  （`.node rect{opacity:0.2}` + `path.transition{stroke-opacity:0.4}` +
  `rect.outer{fill-opacity:0.6}` + `defs [id$=-barbEnd]{fill-opacity:0.3}`
  ——fill-opacity 经 marker 元素**继承**到 path）逐元素捕获
  opacity/fillOpacity/strokeOpacity + 结构测试断言模型存的是
  effective+raw（双乘模型会得 0.04 立即失败）+ StateSceneTest
  **最终像素 alpha** 四点锁（255/51/128/26 = 255×1/0.2/0.5/0.1）。
- **P1-2 Architecture 源码 title**：上游 architecture 的 `title` 语句
  只进 DB，draw() **从不调用 insertTitle**（architectureDiagram chunk
  无 title 渲染代码；浏览器 oracle 的 title 案根无 title 元素、
  viewBox 与无标题 single-default 案完全相同 `-40 -22 160 185.28125`）。
  Adapter 改传空 diagramTitle 并强制 `metadata.title = {}`（否则
  renderMetadata 回落 frontmatter title 画幻影标题条并撑宽
  clientBox）；accTitle/accDescr 保留。SvgExportTest 新增
  architecture-title 四分量 oracle。
- **P1-3 Flowchart 小数 viewBox**：浏览器 oracle 明确
  `viewBox="0 0 426.75 70"`（flowchart-geometry.json 全 70 案几乎全
  小数），而 Flowchart 虽注释「保留小数」却 qRound 成
  naturalSize=427、导出器对非 client-box 家族写整数 canvas。
  根因修复：`FlowScene::svgClientViewBox()` = `bounds ± clientPadding`
  （clientPadding = flowchart.diagramPadding，adapter 注入；swimlane
  同），Flowchart/Swimlane 进入共享 client-box 通道（SVG 根
  viewBox/max-width 携带小数、titled 走 insertTitle+bbox∪ 绝对原点
  模型、PNG 合成按同映射）。SvgExportTest 对 flowchart-geometry 全
  70 案逐案断言导出 viewBox 四分量（0.2 容差）。
- **P1-4 handDrawn note-edge 虚线**：rough 分支重建裸 QPen 画实线
  （scene 已记录 5,5、平滑分支正确）。修复：rough 边与平滑路径共用
  同一 `edgePen`（含 dasharray，FlatCap）。StateSceneTest 新增
  40,40 大间距 raster 锁（实线会全列着墨）。
- **P1-5 cluster 逐元素 visibility/display**：rect.outer / label /
  rect.inner 是**兄弟元素**，outer `display:none` 时 painter 入口整体
  跳过——浏览器只藏外框、标题与内体仍画。修复：拆 outerPaints /
  innerPaints / labelCss 三道独立门（divider 仍是单元素整体门）。
  StateSceneTest 新增 raster 锁（outer 隐藏时 inner 绿体必须在、
  外框带必须空白）。
- **P2-6 CSS DOM 真实化**：① 根下补真实 `<style>` 元素（svg >
  [style, g wrapper, …]——`svg > style + g` 结构选择器可解析）；②
  rectWithTitle 描述改为**单 `<p>`**（上游
  `description.join("<br/>")` 喂给一次 createLabel，
  chunk-ZGVPDNZ5.mjs——不再逐行虚构多个 p）。
- **fixture**：state-layout 9 themeCss 案（+opacity 案；shape/
  strokeShape/cluster-outer/edge/marker 捕获扩 opacity 三通道）
  `7c6b75f3…` 双跑一致。门禁：292/292（213s）+ dist 刷新；
  `git diff --check` 仅既有 LF/CRLF 警告。

**State/Flowchart 第九轮审核修复（08-17，292/292 双跑 175s/——、dist 已刷新，未提交）**：
Codex 复审第八轮后的 1 P0 + 5 P1 全收口：

- **P0 陈旧 exe 假绿 + 6 个编译错误**：第八轮的「292/292」跑的是
  MSB8028 跳过重编的旧测试二进制——第八轮新增断言（flowchart
  viewBox oracle、state raster 锁）从未真正执行。修复 6 处编译错误
  （`muffin::mermaid::rough::` 限定、`QColor::name(QColor::HexArgb)`
  枚举重载、`renderSvg(...).svg` 漏取）后首次真跑，暴露并收口了
  flowchart client-box 的三重根因（见下）。另修两处被 qCritical 吞没
  的失败信息（SvgExportTest/LabelOracle 改 fprintf 直写 stderr）。
- **flowchart 小数 viewBox 三根因**：①布局提取把首节点中心归到
  (0,0)（`preserveDagreCoordinates` 未设）→ flowchart adapter 保留
  dagre 绝对坐标（wrapper translateGraph 锚 (8,8)）；②margin 语义：
  上游 wrapper 硬编码 `marginx/marginy=8` 与 diagramPadding 无关——
  新增 `FlowLayoutOptions.dagreWrapperMargin`（flowchart/swimlane
  设 8；class 等共享管线的族保持 diagramPadding 语义，其 fixture
  锁定旧锚定）；③测试环境平价：生成器用外部 initialize（Arial +
  `flowchart.htmlLabels:false`——该 key 上游已 deprecated，节点标签
  走根 htmlLabels=true 的 HTML 路径、边/簇走 SVG 的**混合模式**），
  测试镜像同一 init 指令。
- **富格式标签测量的 Qt 深坑**：QTextLayout 对 weighted/italic
  QFont 仍按 base face 的 advance 表排位（setFormats 与复制加 weight
  都不换 face；`PreferNoHinting` 还会额外破坏 DirectWrite 的 face
  解析；`QRawFont::fromFont` 在 offscreen 下无效）。根因实现：
  `styledRangeWidth` 按 CSS inline-box 语义分段求和——plain 间隙走
  legacy 全行 design 度量，styled 段经 **DirectWrite 系统字体集**
  （GetGlyphIndices + GetDesignGlyphMetrics）取真实 face 的 hmtx
  设计 advance（Chromium 同一张表）；全有或全无（混脚本缺字形/
  family 切换即回退 legacy）。runs/绘制保持 legacy 全行 shaping
  （分段 shaping 会破坏 Noto 双趟同步与绘制位置——class 像素回归
  证实）。Noto 合成路径（webfont 只注册 Regular、advance 不变）
  完全不动。flowchart-geometry 70 案 viewBox oracle：63 案 0.2 容差
  精确 + 7 案已知分歧**逐值锁定**（bidi shaper 残差、CJK 回退
  字体栈、形状 ink 外扩——按 ctest 规范环境录值，外部
  QT_QPA_PLATFORM 会改变回退引擎）。
- **P1 useMaxWidth:false 小数尺寸**：导出器固定尺寸模式写小数
  clientBox（`width="207.84375" height="70.5"`，formatSvgLength），
  测试锁 width/height == max-width 模式 viewBox 分量（`toInt()` 对
  小数串返回 0 的陷阱改 `toDouble`）。
- **P1 cluster inner 通道**：rect.inner 是单一元素（fill+stroke 同槽
  `innerCss`，cluster 无 innerStrokeCss 对应物）；inner fill/stroke
  补通道 opacity 合成；display:none/visibility:hidden 时 brush 也
  关（不再只关 pen）；outer/inner/edge 的**声明 stroke-width:0 经
  strokeWidthSet 禁笔**（不再回落主题宽）。
- **P1 边标签 p 通道**：`StateSceneEdge.labelBackgroundCss`（p 元素在
  span **内部**——`.edgeLabel p { opacity:.5 }` 同时作用于背景与文
  字、display:none 隐藏整个标签，span 槽位折叠不到它）。背景 alpha ×
  p effective；文字乘数改 p effective（折叠 span 链）；painter 入口
  加 p display/visibility 整体门。state-layout 的 state-theme-opacity
  案追加 `.edgeLabel p { opacity: 0.5 }`，edgeLabelP 捕获扩
  opacity/display/visibility（fixtureSha `e777fcc8…` 双跑一致）；
  StateSceneTest 补 p 透明度/隐藏 raster 锁。
- **P1 stateEnd 阴影空间分离**：`paintNeoShadow` 泛化为多区域
  `ShadowPart` 列表——环（fill 盘 + stroke 环带）与内点（盘 + 描边
  带）各按自身通道 alpha 进入同一 source-over mask；环透明内点可见
  时只投点阴影。raster 锁（环带空白 + 点阴影在场）。
- **P1 titled 小数 padding 光栅**：`finalizeReadyEntry` client-box 分支
  ——naturalSize = qRound(分数总 clientBox)（一次取整；分段
  qCeil 把 diagramPadding 8.25 的 119.5 光栅成 121）；PNG 导出改
  **单趟** clientBox 映射（内容与标题经 translate(-clientBox.
  topLeft()) 一次绘制，标题基线场景绝对锚定——不再内容 71px + 标题
  带 qCeil 53 拼接、基线低 0.75px）；BlockLayout 编辑器绘制与命中
  测试统一走 mermaidClientBox(scene, metadata) 映射（culling 逆映射
  同步）；分趟路径仅保留给非 client-box 族。swimlane Sugiyama 坐标
  本就与浏览器 ink 对齐（lane 框超出 dagre margin），不加再锚定。
- 门禁：**292/292 双跑全绿**（174.68s/——）、dist 已刷新（两 exe
  同 13:08）、`git diff --check` 仅既有 LF/CRLF 警告。
- **教训**：①「ctest 全绿」必须以 exe mtime > source mtime 为前提
  （本轮再现：第八轮六处编译错误被陈旧 exe 完全掩盖）；②Qt 文本
  度量对 weighted 字体不换 face——样式化测量要走真实 face 的
  OpenType 表（DirectWrite），别信 QTextLayout/QFontMetricsF 的
  表面 resolution；③测试与生成器的环境必须逐配置镜像（deprecated
  key 的混合语义、字体族），否则对比的是两个世界；④已知分歧要
  **录值锁定**而非放宽容差（漂移双向失败）。

**State/Flowchart 第十轮审核修复（08-17，293/293 双跑 222s/176s、dist 已刷新，未提交）**：
Codex 复审第九轮后的 4 P1 + 1 P2 全部收口：

- **P1 State `<p>` 样式全贯通**：标签 DOM 的文本载体是 span 内部的
  `<p>`——此前仅边标签 p 的背景通道贯通，测量/绘制的字体字号颜色
  仍读外层 span，display:none 只藏绘制不塌缩布局，节点/cluster/desc
  的 *-p 槽位在模型映射中被直接丢弃。现在：①测量反馈全走 p 槽位
  （CSS 引擎继承折叠 span 规则——`.nodeLabel p{font-size:24px}` 节点
  盒 31.3×52 与浏览器逐值一致）；②绘制字体/颜色/字号 p 优先（边标签
  红 6px 墨水宽度 raster 锁）；③p display:none 塌缩标签盒——节点收缩
  为 padding-only 16×16、cluster 标题带归零、边标签不再占位（viewBox
  42.406×146 收缩一致；`preparedEdgeLabel` 把测量 0×0 视为权威值而非
  「未提供」）；④rectWithTitle 描述行是第二个 fo 自己的 p——分体测量
  （宽=max(标题,行)、高=标题+行；同字体时与合并测量恒等）+ 行绘制
  字体/颜色独立通道。fixture 新增 5 个 themeCss 案（edge-p 字体/颜色、
  node+cluster p、edge p display:none、node p display:none、desc-p），
  fixtureSha `0050318a…` 双跑一致；结构测试对 p 的
  font/color/display 逐值断言。
- **P1 styled-width 真实 shaping**：逐字 nominal glyph 求和改为
  DirectWrite 全 shaping（AnalyzeScript→GetGlyphs→GetGlyphPlacements，
  fontEmSize=像素）——kerning/连字/GSUB/GPOS 生效，GSUB 缺字形仍回退
  legacy；**逐 glyph 1/64 量化**（HarfBuzz 26.6 定点同构：Arial Bold
  "AV" 的 DWrite 浮点和 21.0390625，量化后=Chrome 的 21.046875 精确
  命中；nominal 和=22.234375）；letter/word spacing 补入（Percentage
  对像素解析、逐字符/逐分隔符计数）；混方向（SetBidiLevel 非 0）回退。
  非 Windows 显式文档化回退（无 shaping 后端——Linux 字体平台工作
  流）。AV 反例固化为 LabelOracle 回归锁（Q_OS_WIN）。
- **P1 门禁白名单 → 待修失败项**：7 案 8 分量的已知分歧从静默白名单
  改为 **PENDING-FAIL 登记**——每条 stderr 响亮输出根因（bidi shaper
  残差/CJK 回退栈/形状 ink 外扩）、精确 delta 双向漂移锁（修复即自我
  引爆）、登记计数锁（fixture 变化藏案即失败）、`MUFFIN_STRICT_PARITY=1`
  硬失败模式（实测 8 条全红）。70 案 oracle 与登记表 Q_OS_WIN 门控
  （DirectWrite 录值；mac/Linux shaping 后端是已记录的平台工作流，
  防 CI 假红）。
- **P2 构建新鲜度自动门禁**：新增 `MuffinBuildFreshnessTest`（第 293
  个测试）——按依赖域校验：MuffinCore.lib ≥ 全部 src/mermaid 源、每个
  mermaid 测试 exe ≥ 自己的源 + lib、Muffin.exe ≥ 双 lib；陈旧即 FAIL
  并列出（跳过重编/跳过重链两条路径都实测触发：touch StateScene.h 不
  构建→FAIL，构建后→PASS）。改单个测试源只要求该测试重链（正常增量
  不误报；VS 增量图对无关子系统确实不重链，故门控范围收在 mermaid 套
  件+app）。
- 门禁：**293/293 双跑**、dist 刷新（16:59）、`git diff --check` 仅既有
  换行警告。
- **教训**：①`DWRITE_SCRIPT_ANALYSIS` 不携带 bidi level（SetBidiLevel
  回调才有——混方向检测挂 sink）；②IDWriteTextAnalysisSink 的
  SetLineBreakpoints/SetBidiLevel 参数序与直觉不同（textLength 在指针
  前、SetBidiLevel 是 explicit+resolved 双字节、GetNumberSubstitution 带
  textLength 出参）；③Qt 空文本 measureLabel 仍带一行行盒（高 1.5em
  ——display:none 塌缩须整体跳过测量而非量空串）；④Chromium 行内盒
  宽度=HarfBuzz 26.6 逐 glyph 量化取整（DWrite 浮点和会差半格 1/128
  ——723.5/64 的 A+kern advance 取整方向决定最后一位）。

**State/Flowchart 第十一轮审核修复（08-17，293/293 三跑 186s/175s/——、dist 已刷新，未提交）**：
Codex 复审第十轮后的 4 P1 + 1 P2 全收口：

- **P1 rectWithTitle 描述 p 的 display:none 塌缩**：Adapter 此前只传
  描述字体、无 descHidden 通道——painter 隐字但 dagre 仍保留描述高。
  浏览器 DOM 探针定谳：描述 fo 的 div 量得 0×0，而 **0×0 的
  foreignObject 被 label.getBBox() 并集排除**——titled 盒=标题独自
  （`state-theme-desc-p-hidden` 案：节点 68.984375×65→×32、viewBox
  高 271→238），垂直加成 17→8（9px 标题-行距只在行渲染时存在）；
  divider 仍绘制（自己的元素，标题下方 top+titleHeight+4，仍在 32 高
  盒内）。实现：`StateMeasureCss.descHidden` + adapter 读 desc p 的
  display + 测量分体（descLines 空或 descHidden → 仅标题）。fixture
  15 案 `37694df5…` 双跑一致；结构测试 desc-p-display 逐值断言。
- **P1 DWrite RTL 检测分支不可达 + 换引擎**：原代码只跑 AnalyzeScript
  ——SetBidiLevel 由 AnalyzeBidi 产生，anyBidi 恒 false、粗体希伯来文
  被按 LTR shaping。修复走了根因路线：**styled-width 的 shaping 引擎
  从 DWrite GetGlyphs/GetGlyphPlacements 换成 HarfBuzz**（MuffinCore
  本就链接 hb；经 `IDWriteFontFace::TryGetFontTable` 回调把同一 face
  的表喂给 `hb_face_create_for_tables`）。DWrite 管线的三处不等价就此
  消失：①AnalyzeScript 不产生 bidi 电平（现 AnalyzeScript+AnalyzeBidi
  双扫，resolved level 驱动每 run 的 hb direction）；②DWrite 每脚本
  默认特性 kern 拉丁文但不 kern 希伯来文（Arial 的希伯来 GPOS 对在
  DWrite 下静默丢失）；③DWrite 浮点 advance 需手工量化。
- **P1 Chromium 行内盒算术（本轮最大发现）**：canvas measureText
  （浮点 shaping）对照 span getBoundingClientRect（LayoutUnit）揭示
  Chrome 的真模型——**hb 字体函数返回浮点 advance（Arial Bold alef
  =1193/128=9.3203125px，半格 1/64 存在），每个 itemized run 的浮点
  和各自 LayoutUnit 取整一次（round-half-away），从不逐 glyph**：
  AV run=(1479+1366−152)/128=21.0390625→round64→21.046875；希伯来
  对 2372/128=18.53125 整；混合「אA」=597+740=20.890625（每 run 独立
  取整）；第十轮的「逐 glyph 26.6 量化」模型在 AV 上巧合命中但本质
  错误（希伯来单字 vs 对的「−2 单位 kern」其实是单字被取整+0.5 的
  假象——font-kerning:none 不改变它）。实现：hb scale=像素×128（1
  字体单位=1/128px，整数 advance 无损），每 run `round(和/2)` 映射
  到 1/64。kern 特性显式开启（HB 水平默认不含 kern；Chromium
  font-kerning:auto=开）。
- **P1 styled spacing 算术**：else-if 同时声明 letter/word spacing 时
  word 被整体丢弃；letter 按 Unicode scalar 计数（A+U+0301 加两次，
  浏览器按 grapheme cluster 加一次）。修复：独立相加+combining mark
  （Mn/Mc/Me）并入基字符簇。Chrome 实录（Arial Bold 16px）："A V"
  26.078125 plain / 29.078125 letter 1px（3 簇，含尾簇与空格）/
  29.078125 word 3px（1 分隔符）/ 32.078125 both（相加）；A+U+0301
  恰 +1 letter 单元；NBSP 计入 word 分隔符。全部固化为 LabelOracle
  Q_OS_WIN 回归锁。
- **P2 freshness 门禁盲区 + 平台门控**：①注册收进 `if(WIN32)`——
  测试硬编码 MuffinCore.lib/MuffinUi.lib/*.exe，macOS/Linux CI 会因
  找不到 .lib 直接红；②exe 名反推源码名漏掉
  MuffinMermaidC4EdgeParityTest/MuffinMermaidRailroadEdgeParityTest
  （源码是 *GeometryOracleTest.cpp）且 lib 屏障只扫 src/mermaid（漏
  src/theme 等 MuffinCore 编译的 9 棵树）。改为 **configure 期生成
  manifest**（muffin_add_test 每次注册追加 TEST 行=name/sources/links；
  文件尾追加 LIB MuffinCore（404 个 src 文件）/LIB MuffinUi/APP
  Muffin 的精确源列表）——测试读 manifest 校验：lib≥其源列表、每个
  exe≥own 源+所链一方库、app≥双库。
- 门禁：**293/293 三跑全绿**（186.54s/175s/——）、dist 刷新（21:00）、
  `git diff --check` 仅既有换行警告、MUFFIN_STRICT_PARITY=1 登记项
  如常输出。
- **教训**：①**TryGetFontTable 的第 5 参 exists 是必填 _Out_ BOOL\***
  ——传 nullptr 会在 DWrite.dll 内部 AV 崩溃（事件日志定位 faulting
  module 才破案）；②python heredoc 往 C++ 里写调试 fprintf 的 `\n`
  三犯——且会顺手把整个文件行尾改成 LF（本轮 FlowLabel.cpp 实际中
  招，已修复回 CRLF）；③**判别 shaping 层 vs 布局层用 canvas
  measureText 对照 span rect**——前者浮点无量化、后者 LayoutUnit，
  一组对照即可分离两层语义；④grep BRE 的 `\t`/`|` 不是字面 tab/或
  ——manifest 校验要用 $'\t' 或 -P；⑤「凑巧吻合的模型」要怀疑：
  第十轮 AV 逐 glyph 量化碰对了值，希伯来数据一出来就证伪了。

**State/Flowchart 第十二轮审核修复（08-17 深夜，293/293 双跑、dist 已刷新 12:04，未提交）**：
Codex 复审第十一轮后的 2 P1 + 1 P2 全收口：

- **P1 letter-spacing 计数改真 UAX #29**：类别扫描（surrogate +
  Mn/Mc/Me 跳过）不等价 grapheme boundary——行首孤立组合符
  （`́A` 浏览器计 2 单元、旧码计 1）、ZWJ（`A‍B` 浏览器
  2、旧码 3）、emoji ZWJ 序列/肤色修饰/regional indicator 全都数
  错。实现改 `QTextBoundaryFinder::Grapheme`（Qt 的 UAX #29 实现）
  + 一个 Blink 语义精化：**全由 default-ignorable 格式字符（Cf——
  bidi 嵌入控制、孤立 ZWJ/ZWNJ）构成的簇不收 spacing**（shaper 丢弃
  它们；Chrome 实测 RLO+AB+PDF 恰 +2 单位非 +4，A+ZWJ+B 恰 +2——
  该精化对边界器是否把 ZWJ 并入前簇不敏感，两种切法都得 2）。锁：
  A+ZWJ+B（plain 23.109375/letter 25.109375）、行首孤立符
  （11.5625 半格进位/13.5625）、RLO 嵌入控制（25.109375）。
- **P2（升级实现）script/bidi 交集 itemization**：此前只读 script
  run 首字符的 level——同一 script run 内多 resolved level 时整段
  用首 level，破坏「每 itemized run 独立 LayoutUnit 取整」。改为按
  script runs 与 bidi ranges 的**全部边界并集切原子 runs**，各自
  取 analysis+level、独立取整。Chrome 铁证：`א1ב`（欧洲数字在 RTL
  上下文 level=2）= 597+570+590 = 27.453125——三次独立取整；整段
  一次取整得 1756/64 = 27.4375 ≠ 实测。锁：plain 27.453125 +
  letter-spacing 30.453125（3 单元）。
- **P1 freshness manifest 链接闭包**：manifest 只记直接 LINK——98
  个 MuffinUi 测试实际经 Ui 的 PUBLIC 链接也链 Core；Core 更新+Ui
  .lib 不变+MSBuild 跳过某 Ui 测试重链时门禁会放行。CMake 端
  `muffin_freshness_link_closure`（递归读 INTERFACE/LINK_LIBRARIES、
  只展开 ^Muffin 目标、expanded 集防环）——98 个 Ui 测试现在记录
  `MuffinUi;MuffinCore`，consumer 不变即生效。
- 门禁：**293/293 双跑全绿**、dist 刷新（12:04）、`git diff --check`
  干净。

**State/Flowchart 第十三轮审核修复（08-20，293/293 双跑、dist 已刷新，未提交）**：
Codex 复审第十二轮后的 1 P1（variation selector spacing）收口：

- **P1 letter 接收者的真规则 = Blink 的 UTF-16 单元级测试**。旧实现
  「全 Cf 簇不收 spacing」两个方向都错：VS16/VS1/180B 是 **Mn 非 Cf**
  → 被计为单元（Chrome 实测三者 standalone plain/letter 均 0 宽）；
  U+E0001 是 Cf → 被排除（Chrome 实测 +1 单元）。源码级根因
  （shape_result_spacing.cc + character.h）：Blink 在 glyph cluster
  起始处读**单个 UTF-16 code unit** 做 `TreatAsZeroWidthSpace` 判定
  （FF/CR/ORC ∪ Default_Ignorable BMP 区间）——非 BMP ignorable（tags/
  VS17）读到高代理项、永非 ignorable、**仍收单元**；而 HB 隐藏区间
  （180B..180E 不含 180F）与 ICU 属性（180F 是 ignorable）在 FVS4 上
  分道——180F tofu 存活但无单元，正是「glyph 存活与否」与「单元
  授予与否」两个正交机制的探针级证据。Chrome 全量实录（Arial Bold
  16px）：FE0F/FE00/FE0E/180B/200B/2060/FEFF/00AD/034F 全 0/0；
  180F 12/12（tofu 无单元）；E0001 12/13、E0020 4.453125/5.453125、
  E0100 12/13、1BCA0 16.3125/17.3125（存活且 +1）；孤立 U+0301 0/1；
  A+VS16 11.5625/12.5625（VS 并入基簇一个单元）；第十二轮锚点
  （A+ZWJ+B、RLO+AB+PDF、א1ב）全部精确复现。实现：
  `letterSpacingReceiverUnit(char32_t)`（BMP 区间硬编码，代理项永
  不排除）+ `graphemeClusterCount` 改测**簇首 UTF-16 unit**；oracle 新
  锁：VS16/VS1/180B standalone 0/0、A+VS16 11.5625/12.5625。
- **教训**：跨 UTF-16/UTF-32 边界的 Unicode 属性查询要核对浏览器读的
  是 code unit 还是 code point（Blink 这里读 unit——非 BMP 全部「漏
  网」）；「ignorable」要区分 shaper 隐藏（HB 列表，180F 缺席）与
  spacing 排除（ICU 属性）两个独立机制。
- 门禁：**293/293 双跑全绿**、dist 刷新、`git diff --check` 干净
  （仅既有 LF/CRLF 提示）。
- **教训**：①**「每 run 独立取整」的正确性依赖 itemization 粒度**——
  切分必须取 script×bidi 边界交集，只按 script 切会在 RTL 内嵌数字
  时差 1/64；②**letter-spacing 的接收者是 grapheme 簇但排除纯
  ignorable 簇**——纯 UAX #29 与纯类别扫描都不对，两个 Chrome 反例
  分别证伪二者；③manifest 记录传递依赖要在 CMake 端闭包展开（读取
  方只认名字表，展开一次写死即可）。

---

## 8. Oracle 深度跨图（2026-07-30 起）

5 步架构落地后，parity 的剩余工作不是「更多架构」，而是**把 ER 的「真-mermaid geometry oracle + fail-on-divergence」模式上提到其余图族**——parity 证据真正沉淀的地方。每族：generator（headless Chrome 捕获真 mermaid 11.16.0 几何）+ fixture（冻结）+ oracle test（断言 font 无关 parity，报告 font 耦合 delta）。

**canonical reference 已升级**：`G:\github\mermaid-cli` 是上游 mermaid-cli 源码 clone（自带 `node_modules/mermaid@11.16.0`、`dagre-d3-es`、puppeteer shim、`#container` index.html），生成器开箱即用，无需 setup 脚本。生成器支持 `MERMAID_REFERENCE_ROOT` 环境变量覆盖路径。

**font 无关 vs 耦合的判定**（class oracle 实测确认）：**高度**依赖字体 ascent/descent，Qt 与 Chrome 间稳定（≤0.5px）→ 可断言；**宽度**依赖 per-glyph advance，差 ~1px/文本 → 仅报告。这与 ER 的宽度耦合是同一面墙的较温和形式。

| 图族 | 真几何 oracle | 状态 |
|---|---|---|
| flowchart | dagre-snapshots + geometry | ✅（既有） |
| er | er-geometry + ErGeometryOracleTest | ✅（Phase 2，fail-on-divergence） |
| **class** | **class-geometry + ClassGeometryOracleTest** | **✅** 断言 topology + edge-tuple multiset（pattern/markerStart/markerEnd）+ node height + dividers；报告 width |
| **state** | **state-geometry + StateGeometryOracleTest** | **✅** 断言 topology + **transition multiset（from→to）** + node height；报告 width |
| **sequence** | **sequence-geometry + SequenceGeometryOracleTest** | **✅** 纯结构断言：participant multiset + message count + **ordered (from,to,dashed,markerEnd)** 元组列表 |

class oracle 关键设计：edge 的 markerStart/markerEnd 在 mermaid 侧从 `marker-start`/`marker-end` url 提取 bare type 并丢弃 Start/End 后缀（字段位置已编码端侧），与 Muffin `markerName` 输出的 bare type 对齐；`none`/空/absent 三者归一为 null。断言的 edge-tuple multiset 证明 Muffin 对每种 class 关系（继承/实现/组合/聚合/关联/依赖，solid/dashed，单/双端）画出与 mermaid 完全相同的箭头。

state oracle 关键设计：state 边**无** `LS-/LE-` class 编码（区别于 flowchart），from/to 通过匹配每条边的 `data-points`（base64 dagre 路径点，首=源/末=目标）到最近节点中心恢复；节点 id（root_start/root_end/`<name>`）与 Muffin 直接对齐。断言的 transition multiset 覆盖分支/fork-join 扇出收敛/自环/start-end，证明 Muffin 复现 mermaid 的状态机结构。

sequence oracle 关键设计：sequence 用 legacy flat renderer，**位置**（actor anchorX / message lineY / lifeline Y）全是 font 耦合，故 oracle 是**纯结构**断言（不比几何）。mermaid 的 message `<line>` 带 `data-from`/`data-to`/`data-id`/`marker-end`/`style`，from/to/顺序/箭头/虚线直接读取（无需坐标匹配）。断言 participant multiset + message count + **有序** (from,to,dashed,markerEnd) 列表——顺序即对话流，故用有序列表而非 multiset。覆盖 `->>`/`-->>`/`->`/`-x`/`--x` 箭头种类。自环（`A->>A`）渲染为 loop path 而非 messageLine，暂排除。60 mermaid 测试全绿，**5 图族真-mermaid geometry oracle 全部完成**。

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
| **配置** | 每个 config key 的效果与上游一致 | config-effect-matrix（347 行，逐 key 标 parity/partial/deferred） |

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

**2026-08-12 现状**：二十八个生产图族均通过单一 scene 指针和 `Diagram` registry
进入族无关的 editor/PNG/SVG/canvas/interaction 路径；二十八个 adapter 分离在各自
TU。新增的 Pie、Quadrant、Journey、Radar、XYChart、Timeline、Packet、Kanban、Mindmap、Block、Swimlane、TreeView、Event Modeling、Ishikawa、Venn、Sankey、Treemap、Cynefin、Wardley、Architecture、Gantt 和 Info 均有真实 Mermaid 11.16.0 语法、几何和像素
oracle。完整 Release 门禁为 276/276。配置矩阵现为 347 行（205 parity /
8 partial / 7 unsupported / 99 upstream-inert / 5 deferred /
19 legacy-only / 3 api-only / 1 security-fixed）。Requirement 的全局
`htmlLabels:false` 保持 partial；外部 `mermaid.initialize()` 配置不属于当前
Markdown source API。

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

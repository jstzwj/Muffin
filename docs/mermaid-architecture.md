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
| **配置** | 每个 config key 的效果与上游一致 | config-effect-matrix（102 行，逐 key 标 parity/partial/deferred） |

**不追求「字节同」的 SVG**：Muffin 的 SVG 由 `QSvgGenerator`（经 painter）产出，再由 `MermaidSvgExporter` 归一化成 mermaid 形态。字节级与 mermaid 手写 SVG 不同是必然的；parity 以**视觉 + 结构 + 几何**为准。

---

## 2. 现状诊断

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

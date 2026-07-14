# Mermaid Flowchart 原生移植剩余工作计划

## 1. 目标与边界

本计划以 `mermaid` 11.16.0 和其锁定的 `dagre-d3-es` 实现为唯一兼容
基线。版本来源是：

```text
C:\Users\jstzw\Documents\github\mermaid-cli\package-lock.json
```

最终目标是在 Muffin 中使用纯 C++20/Qt 实现 Mermaid flowchart 的解析、
布局和绘制。Node、JavaScript、Puppeteer 和浏览器只能用于离线生成上游
golden，不得成为 Muffin 构建、运行、测试或发布依赖。

“完成”不是指常见示例看起来相似，而是同时满足：

1. FlowDB/AST 可观察状态与锁定上游一致；
2. Dagre 节点、cluster、边、标签几何与上游 golden 一致；
3. 形状路径、交点、marker、文字和样式语义一致；
4. 固定平台、字体、DPR 和绘制参数下通过完整主题像素 golden；
5. 无 JS/浏览器运行时依赖，并通过资源、错误输入和性能保护测试；
6. 只有以上门槛全部通过后才接入 Muffin 编辑器和导出路径。

## 2. 当前基线

已经具备：

- Mermaid 预处理、diagram detection 和 FlowDB 的第一阶段原生实现；
- cycle removal、network-simplex、dummy edge chain、crossing sweep、四向 BK
  坐标平衡、基础曲线、矩形交点和原生文字测量；
- 普通 cluster 和嵌套 cluster 的边界计算；
- TB、BT、LR、RL 四方向 self-edge；
- parallel edge 及现有 fixture 的 label center position；
- 13 个 legacy flowchart 形状及 alpha silhouette golden；
- AST/DB、几何和形状 silhouette 三类离线上游 fixture；
- Release 全量构建和 119 项测试通过。

当前不得关闭的已知差距：

- `compound-crossing` 仍标记为 `pendingNative`；
- cluster 仍有布局完成后计算包围盒的成分，尚未成为完整 Dagre compound
  图参与者；
- BK compound type-2 conflicts 尚未接入坐标分配；
- parallel edge 的 `labelpos=l/r/c`、`labeloffset`、反向边和 compound 组合
  尚未形成完整矩阵；
- Mermaid 11.16.0 的扩展 shape registry 尚未移植完；
- 尚无完整 flowchart painter、marker、主题级 RGBA golden；
- parser 的剩余产生式、错误分支和深度保护尚未封口；
- 尚未接入编辑器。

## 3. 总体依赖顺序

```text
兼容基线冻结
    -> Compound 图模型
    -> Dagre compound 完整流水线
    -> BK type-2 与 compound 坐标
    -> 边路由和标签完整矩阵
    -> 扩展节点形状
    -> 原生 painter 与主题解析
    -> AST/几何/像素三级全量 golden
    -> 编辑器、导出与交互接入
```

不能把节点形状或主题 painter 提前接入编辑器。布局边界仍变化时生成的像素
golden 会反复失效，也会把错误几何固化为 UI 行为。

## 4. 里程碑 A：冻结上游契约与审计清单

### 上游来源

- Mermaid 配置、FlowDB、flow parser 和 dagre wrapper；
- `node_modules/dagre-d3-es/src/dagre/layout.js`；
- `node_modules/dagre-d3-es/src/dagre/nesting-graph.js`；
- `node_modules/dagre-d3-es/src/dagre/add-border-segments.js`；
- `node_modules/dagre-d3-es/src/dagre/parent-dummy-chains.js`；
- `node_modules/dagre-d3-es/src/dagre/order/*`；
- `node_modules/dagre-d3-es/src/dagre/position/bk.js`；
- `node_modules/mermaid/dist/rendering-util/rendering-elements/*`；
- `node_modules/mermaid/dist/themes/*`。

### 工作项

- 在 fixture 元数据中记录 Mermaid、dagre-d3-es、Chromium、字体文件、DPR、
  viewport 和平台版本；
- 为每个上游模块建立“源函数 -> C++ 函数 -> fixture case”映射表；
- 固定数值序列化规则：有限小数、负零、路径命令、稳定 node/edge 顺序；
- fixture 生成脚本连续运行两次必须产生相同 SHA-256；
- 禁止升级 Mermaid 版本时顺便修改原生实现。升级必须先独立提交 fixture
  差异和兼容审计。

### 完成标准

- golden 能说明由哪个精确上游版本、浏览器和字体生成；
- 任意 pending case 必须带唯一 issue/里程碑名称，测试拒绝未知 pending；
- 当前唯一允许的 geometry pending 是 `compound-crossing`，完成里程碑 C 后
  必须删除，而不是长期跳过。

## 5. 里程碑 B：原生 compound 图模型

目前的扁平 `WorkGraph` 应扩展为能表达 Dagre multigraph + compound graph 的
内部模型。不要直接把 cluster padding 继续堆在最终坐标后处理上。

### 数据结构

- 稳定 node ID 和 named multiedge ID；
- parent、children、root children；
- in/out edge、predecessor、successor、neighbor 查询；
- node label 字段：`rank`、`order`、`dummy`、`minRank`、`maxRank`、
  `borderTop`、`borderBottom`、`borderLeft[rank]`、`borderRight[rank]`；
- edge label 字段：`minlen`、`weight`、`width`、`height`、`labelpos`、
  `labeloffset`、`nestingEdge`、`reversed`；
- 可确定复现的临时 ID 生成器，避免哈希迭代顺序改变布局。

### 单元测试

- parent 重设、递归删除、祖先和最低公共祖先；
- 同端点 parallel named edge 的插入、查询和删除；
- compound node 与 leaf node 混合遍历；
- `__proto__` 等特殊 Mermaid ID 不得污染映射；
- 深层 parent chain 必须使用迭代或显式深度限制，不能栈溢出。

### 完成标准

- 能无损构造 `layout.js::buildLayoutGraph` 所需全部字段；
- 图查询在固定输入下顺序稳定；
- 现有非 compound geometry golden 全部保持通过。

## 6. 里程碑 C：完整 Dagre compound 流水线

严格按照上游 `runLayout` 顺序移植。顺序本身属于兼容契约：

1. `makeSpaceForEdgeLabels`；
2. `removeSelfEdges`；
3. `acyclic.run`；
4. `nestingGraph.run`；
5. 对 non-compound view 执行 rank；
6. `injectEdgeLabelProxies` 和 `removeEmptyRanks`；
7. `nestingGraph.cleanup`、`normalizeRanks`、`assignRankMinMax`；
8. `removeEdgeLabelProxies`、`normalize.run`；
9. `parentDummyChains`；
10. `addBorderSegments`；
11. compound-aware ordering；
12. `insertSelfEdges`；
13. coordinate adjust、position、positionSelfEdges；
14. `removeBorderNodes`、`normalize.undo`；
15. label fixup、coordinate undo、translateGraph；
16. node intersection、reverse points、`acyclic.undo`。

### C1. Nesting graph

- 移植 tree depth、virtual root、border top/bottom dummy；
- 按上游公式放大原始 edge `minlen`；
- 添加不同权重和 minlen 的 nesting edge；
- rank 完成后清理 virtual root 和 nesting edge；
- 覆盖空 cluster、cluster 内 cluster、跨祖先边、cluster 到 leaf 的边。

### C2. Dummy chain parent

- 移植 compound tree postorder low/lim；
- 为长边 dummy chain 查找 LCA；
- dummy chain 上行到 LCA、再下行到目标 parent；
- 覆盖边从内到外、外到内、兄弟 cluster、祖孙 cluster 四类路径。

### C3. Border segments

- 为 `minRank..maxRank` 每层创建左右 border dummy；
- 设置 border chain parent 和连接 edge；
- cluster 最终宽高必须由 border top/bottom/left/right 计算；
- 删除 border dummy 后，普通节点和原始边的身份及顺序保持稳定。

### C4. Compound ordering

逐文件映射移植：

- `order/build-layer-graph.js`；
- `order/sort-subgraph.js`；
- `order/add-subgraph-constraints.js`；
- `order/barycenter.js`、`resolve-conflicts.js`、`sort.js`；
- `order/cross-count.js`、`init-order.js`、`index.js`。

必须覆盖 border 节点夹持、递归 subgraph barycenter、前后 sweep、bias 切换、
最小 crossing layering 保存和 multiedge weight 聚合。

### C5. BK compound type-2 conflicts

- 保留现有 type-1 conflict 测试，新增独立 conflict 集合 golden；
- 逐行移植 `findType2Conflicts` 的 border 分段扫描；
- type-1/type-2 集合合并后用于四种 `ul/ur/dl/dr` vertical alignment；
- horizontal compaction 必须识别 `borderLeft`/`borderRight`，不得把 border
  当普通 dummy；
- 四向 alignment 对齐、smallest-width 选择和 balance 均建立中间状态 golden。

### 几何 fixture 矩阵

- 当前 `compound-crossing`；
- 两层和三层嵌套；
- 左右、上下并列 cluster；
- 边跨一个、两个和三个 compound 边界；
- long edge dummy 穿过 cluster；
- self-edge 位于内层 cluster；
- parallel edge 跨 cluster；
- cluster title 为空、单行、多行、Markdown；
- TB、BT、LR、RL 全方向；
- cyclic compound graph 和 reversed edge；
- 输入 subgraph 顺序不同但拓扑相同的确定性用例。

### 完成标准

- 删除 `compound-crossing.pendingNative` 和测试中的专用跳过逻辑；
- 所有 case 的 node/cluster 中心和宽高误差不超过 0.002 px；
- 每条 path 命令类型、数量和坐标误差不超过 0.002 px；
- BK 的 rank/order/conflict/alignment 中间 golden 全部通过；
- 不再使用布局后平移 leaf node 来模拟 compound spacing。

## 7. 里程碑 D：边、marker 和标签完整兼容

### 边语义矩阵

- normal、thick、dotted、invisible；
- arrow open、point、circle、cross，以及 start/end 双向组合；
- short、long、extra-rank link；
- cyclic/reversed edge；
- self-edge、parallel self-edge；
- parallel named/unnamed edge；
- compound 边界穿越；
- `curve=basis/linear/cardinal/step/stepBefore/stepAfter/monotoneX/monotoneY`
  中 flowchart 实际支持的全部配置。

### 标签位置

- `labelpos=c/l/r`；
- 默认和自定义 `labeloffset`；
- TB/BT 与 LR/RL 的宽高扩展差异；
- reverse edge 后 label 不反向漂移；
- 空标签、纯文本、Markdown、HTML label、多行 label；
- edge label background、class/style 覆盖；
- parallel edge 标签不重叠且 ID 对应稳定。

### 路由与裁剪

- 每个形状实现真实 border intersection，不能继续全部回退到矩形；
- marker clipping 使用 marker 类型对应的 refX/尺寸；
- 起点和终点 marker 独立裁剪；
- 曲线路径和用于 hit-test 的几何必须来自同一数据，不允许 painter 重算。

### 完成标准

- edge AST、未裁剪 points、最终 path、label box/anchor、marker geometry 五类
  golden 同时通过；
- parallel edge/label position 测试覆盖上述矩阵，而不是只有一个示例；
- 无标签 edge 不产生伪 label box。

## 8. 里程碑 E：Mermaid 11.16.0 扩展节点形状

以 `rendering-elements/shapes.d.ts` 导出的 registry 为准，不手工猜测 shape
名称。alias 只映射到 canonical shape，几何实现只保留一份。

### 分组移植顺序

1. 基础和 framed：double circle、framed circle、filled/small circle、card、
   lined/shaded process；
2. 文档和存储：document、multi-document、tagged/lined document、database、
   horizontal/lined cylinder、stored data；
3. 流程控制：triangle、flipped triangle、hourglass、fork/join、junction、
   notched pentagon；
4. 输入输出：curved trapezoid、sloped rectangle、window pane、divided rect、
   half-rounded rect；
5. 标注类：brace left/right/both、lightning bolt、cloud、bang、summary、flag；
6. 复合装饰：stacked rect、bow-tie rect、tagged process；
7. icon/image shapes：在无外部浏览器前提下定义 icon pack 和图片加载边界；
8. `classic`、固定 seed 的 `handDrawn`、`neo` look 差异。

### 每个 canonical shape 的测试合同

- alias/shortName/semanticName 解析；
- 空、短、长、多行、Markdown label 的测量和 padding；
- node width/height；
- canonical path/polygon/ellipse 参数；
- 8 个方向射线的 border intersection；
- stroke width 改变后的外包围盒；
- 透明 alpha silhouette；
- 至少 default/dark 两个主题的完整 RGBA crop。

### 完成标准

- registry 中所有可由 flowchart `@{ shape: ... }` 选择的 canonical shape 和
  alias 均有 fixture；
- 未知 shape 与上游产生相同类别的错误，不得静默回退矩形；
- shape registry 测试自动检查“上游 shape 列表 - native shape 列表”为空；
- icon/image 的网络行为明确禁用，或由 Muffin 已有安全资源加载器实现；
- 所有 shape 的尺寸、路径和交点 geometry golden 通过。

## 9. 里程碑 F：原生 flowchart painter 与主题系统

### 渲染场景模型

在 layout 和 QPainter 之间增加不可变 scene 层，至少包含：

- 按绘制顺序排列的 cluster、edge、edge label、node、node label、marker；
- 已解析的 fill、stroke、stroke width/dash、opacity、font、text alignment；
- shape path 和 edge path；
- clip、z-order、链接和 tooltip hit region；
- diagram 背景和 viewBox/内容边距。

这样测试可以在不画像素时比较 scene JSON，也能保证屏幕与导出共享同一语义。

### 主题兼容

Mermaid 11.16.0 配置允许：`default`、`base`、`dark`、`forest`、`neutral`、
`neo`、`neo-dark`、`redux`、`redux-dark`、`redux-color`、
`redux-dark-color` 和 `null`。逐一建立：

- flowchart 使用到的 themeVariables 原生强类型子集；
- 上游颜色派生、透明度、border、line、arrowhead、cluster、label background；
- `themeVariables` override；
- `themeCSS`、classDef、class、style、linkStyle 的层叠与优先级；
- `look=classic/handDrawn/neo`；
- fontFamily、fontSize、fontWeight 和 HTML/Markdown label 样式继承。

不应直接复用 Muffin 文档 CSS 选择器含义。Mermaid 样式先在自己的受限模型中
解析，再由明确映射进入 scene，避免任意 CSS 影响编辑器 UI。

### 文字

- 固定测试字体文件，不依赖系统字体 fallback 顺序；
- 使用 `QTextLayout` design metrics 复现浏览器 label box；
- plain、Markdown、HTML label 分开建模；
- bidi、CJK、emoji、组合字符、上下标和换行建立专项 fixture；
- 屏幕和图片导出固定 DPR 规则。

### 完成标准

- scene semantic golden 覆盖所有 style 来源和优先级；
- painter 不读取 FlowDB，也不重新做 layout；
- 同一 scene 的屏幕和导出路径只允许 viewport transform 不同；
- 所有支持主题均有完整图像 golden。

## 10. 里程碑 G：三级 golden 完整化

### Level 1：AST/DB golden

比较解析后的可观察语义，不包含布局：

- vertices、edges、subgraphs、classes、styles、directives；
- ID、edge ID、link、tooltip、callback、安全等级处理；
- shape alias 归一化和 metadata；
- 错误类型、错误位置、最大 edge/text 限制。

### Level 2：几何 golden

分层保存，便于定位失败：

- rank/order/parent/dummy/border/conflict 中间状态；
- node、cluster、label boxes；
- 原始 edge points、裁剪后 points、marker anchors、最终 path；
- shape path 参数和 border intersections；
- diagram bounds/viewBox。

数值统一到 0.001 px 序列化，比较容差最多 0.002 px。不得通过扩大统一容差
掩盖单个算法差异。

### Level 3：像素 golden

建立三种比较，而不是只使用单一“相似度”：

1. alpha silhouette：验证形状边界和占位；
2. semantic color mask：按 node fill、stroke、edge、marker、label 分层验证；
3. full RGBA：验证最终主题图。

Chrome 与 Qt 的字体栅格和抗锯齿不保证逐字节相同，因此严格性定义为：

- 非文字几何先由 Level 2 精确约束；
- 颜色值在非抗锯齿内部区域必须精确相等；
- 边界使用固定的线性 RGBA 差异、最大偏差像素数和结构相似度阈值；
- 文字单独比较 glyph mask、baseline 和 ink bounds；
- 失败输出 expected、actual、absolute diff 和放大后的边界 diff；
- 阈值必须按像素类别固定在测试代码中，禁止按 fixture 单独放宽。

### 像素矩阵

- 所有主题；
- classic、handDrawn 固定 seed、neo；
- TB/BT/LR/RL；
- 所有 canonical shape；
- 所有 marker 和 edge style；
- nested compound、self/parallel/cycle/long edge；
- plain/Markdown/HTML/CJK/bidi label；
- 1x 和 2x DPR；
- Windows 为首个基准平台，macOS/Linux 各自维护平台 golden，不能混用。

## 11. 里程碑 H：parser 封口与安全保护

- 从 `flow.jison` 产生式建立覆盖清单，所有分支至少一条成功或失败 fixture；
- 错误 token、行列、异常类别与上游建立 golden；
- 长 chain、深 nested subgraph、超长 label、超多 edge/node 使用显式限制；
- parser、layout、paint 均支持取消和总工作量预算；
- fuzz 测试验证不崩溃、不越界、不无限循环；
- URL、callback、HTML label、image/icon 遵守 Muffin 安全策略；
- 不执行 Mermaid callback JavaScript，仅保存或安全忽略其语义。

完成标准是 parser coverage 清单无“未审计”分支，已发现的长 chain 栈保护
问题有回归测试并修复。

## 12. 里程碑 I：Muffin 编辑器与导出接入

### 接入步骤

1. 新增 Mermaid code fence 的异步 parse/layout cache；
2. source hash、主题、宽度、DPR 和字体共同组成 cache key；
3. parsing 状态显示稳定占位，不改变文档滚动位置；
4. 错误时显示原始源码和定位信息，绝不丢失 code fence 内容；
5. 复用 scene painter 进行编辑器、打印和图片/PDF 导出；
6. 链接和 tooltip 使用 Muffin 现有安全交互路径；
7. 大图采用 viewport culling，不在 paint event 重新布局；
8. 保留“以源码显示”回退开关。

### 编辑器验收

- 输入过程中旧 scene 保持可见，完成后原子替换；
- 撤销/重做、主题切换、窗口宽度变化、缩放和打印均正确失效 cache；
- malformed Mermaid 不导致编辑器卡死或布局抖动；
- 大图解析和布局不阻塞 UI 线程；
- HTML/PDF/图片导出与编辑器 scene 语义一致；
- 无 Node、Chromium、JS 引擎 DLL 或运行时进程。

## 13. 测试与提交策略

每个里程碑应采用同一提交顺序：

1. 先添加或扩展上游 fixture generator；
2. 生成并审阅 immutable golden；
3. 添加会失败的原生测试；
4. 移植对应上游算法；
5. 通过定向测试；
6. 连续生成 fixture 两次并比较 SHA-256；
7. 执行 Release 全量构建和测试；
8. 更新源函数映射和兼容状态文档。

本工作区的最终验证命令固定为：

```powershell
cmake --build --preset conan-release
ctest --preset conan-release --output-on-failure
```

新增 golden generator 可以使用 Node/Puppeteer，但 native `ctest` 不得调用它们。

## 14. 最终 Definition of Done

Flowchart 原生移植只有在以下条件全部满足时才算完成：

- 没有 `pendingNative`、skip 或 expected-failure geometry/pixel case；
- parser 产生式和错误分支覆盖清单关闭；
- Dagre compound 中间状态及最终几何 golden 全部通过；
- Mermaid 11.16.0 flowchart 可选 shape registry 无缺项；
- edge、marker、label、style 和所有支持主题矩阵通过；
- 完整 scene semantic 和平台像素 golden 通过；
- fuzz、深度、资源、取消和性能测试通过；
- 编辑器、屏幕、导出使用同一个原生 scene/painter；
- Release 全量构建和测试通过；
- 发布产物和运行进程不包含 JS 或浏览器引擎依赖；
- `docs/mermaid-native-port.md` 从“阶段性兼容”更新为精确列出的正式兼容范围。


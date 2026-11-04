# Muffin 编辑快捷键对照

以下对应当前命令表。可在「偏好设置 → 键盘快捷键」搜索命令名或命令 ID，修改、清除或恢复默认按键。更改立即应用到已打开的窗口。

表内 `Ctrl` 在 macOS 上通常对应 `⌘`；`Alt` 对应 `Option`。单独列出的平台例外优先。

块结构编辑（标题、列表、表格、代码块、公式块等）主要面向实时编辑模式；源码模式可直接输入 Markdown。可用条件仍由对应命令统一判断，改绑快捷键不会绕过只读状态或选区限制。

## 文本格式

| 操作 | Windows / Linux | macOS | 状态与说明 |
| --- | --- | --- | --- |
| 加粗 | Ctrl+B | ⌘B | 已支持；实时编辑和源码模式 |
| 斜体 | Ctrl+I | ⌘I | 已支持；实时编辑和源码模式 |
| 下划线 | Ctrl+U | ⌘U | 已支持，保存为 HTML `<u>` |
| 删除线 | Alt+Shift+5 | ⌘Shift+5 | 已支持 |
| 行内代码 | Ctrl+Shift+` | ⌘Shift+` | 已支持 |
| 超链接 | Ctrl+K | ⌘K | 已支持；选区需在同一可编辑块/单元格内；源码模式为同一行 |
| 插入图片 | Ctrl+Shift+I | ⌘Shift+I | 已支持；实时编辑模式 |
| 跳出空括号或引号 | Tab | Tab | 本次补齐；适用于实时编辑中的相邻空符号对，并遵循自动配对设置 |

自动配对由「偏好设置 → 编辑器」中的括号/Markdown 符号设置控制。输入左括号时直接补齐右括号，Tab 跳过光标右侧的空符号对。表格中的 Tab 继续切换单元格，代码块中的 Tab 继续缩进。

## 标题、段落、列表和公式

| 操作 | Windows / Linux | macOS | 状态与说明 |
| --- | --- | --- | --- |
| 标题 1–6 | Ctrl+1…6 | ⌘1…6 | 已支持 |
| 转为正文 | Ctrl+0 | ⌘0 | 已支持 |
| 引用 | Ctrl+Shift+Q | ⌘Shift+Q | 已支持 |
| 无序列表 | Ctrl+Shift+] | ⌘Shift+] | 已支持，保留 Muffin 原有按键 |
| 有序列表 | Ctrl+Shift+[ | ⌘Shift+[ | 已支持，保留 Muffin 原有按键 |
| 任务列表 | Ctrl+Shift+X | ⌘Shift+X | 已支持；也可输入 `- [ ] ` / `- [x] ` |
| 列表增加/减少层级 | Tab / Shift+Tab | Tab / Shift+Tab | 已支持；缩进、减少缩进命令也可另外绑定组合键 |
| 分割线 | Ctrl+Alt+Shift+- | ⌘Option+Shift+- | 本次补默认键；Ctrl+Shift+- 已用于缩小 |
| 代码块 | Ctrl+Shift+K | ⌘Option+C | 已支持，本次校准 macOS 默认键 |
| 行内公式 | Ctrl+Alt+M | ⌘Option+M | 本次消除与公式块的默认键冲突 |
| 公式块 | Ctrl+Shift+M | ⌘Option+B | 已支持 |
| 左对齐 | Ctrl+Alt+Shift+L | ⌘Option+Shift+L | 本次新增 |
| 居中 | Ctrl+Alt+Shift+C | ⌘Option+Shift+C | 本次新增 |
| 右对齐 | Ctrl+Alt+Shift+R | ⌘Option+Shift+R | 本次新增 |
| 两端对齐 | Ctrl+Alt+Shift+J | ⌘Option+Shift+J | 本次新增 |

段落对齐在实时编辑中对顶层段落、标题以及独占段落的图片生效，光标需折叠。它使用带 `text-align` 的 HTML 容器保存，保留内部 Markdown、源码映射和撤销记录；再次修改对齐会复用这个段落专用容器。列表项、引用内部段落、跨块选区和源码模式会禁用该命令并显示原因，防止破坏 Markdown 容器结构。可以直接在源码模式中编辑 HTML。

给出的 Typora 清单有按键冲突：`Ctrl+Shift+L` 同时写成左对齐和无序列表，`Ctrl+Shift+O` 同时写成有序列表和最近文件，`Ctrl+Shift+M` 同时写成行内和块公式。因此没有原样复制整套默认键。Muffin 的 `Ctrl+Shift+L` 仍切换侧栏，`Ctrl+Shift+C` 仍复制 Markdown；需要时可在偏好设置中重新分配，界面会先指出冲突。

## 表格

| 操作 | 默认键 | 状态与说明 |
| --- | --- | --- |
| 插入表格 | Ctrl+T | 已支持 |
| 下方插入行 | Ctrl+Enter 或 Alt+Shift+↓ | 本次加入兼容键 |
| 删除当前行 | Ctrl+Shift+Backspace 或 Alt+Shift+↑ | 本次加入兼容键 |
| 上移/下移当前行 | Alt+↑ / Alt+↓ | 本次修正上下文；表格内移动行，普通正文中移动块 |
| 前后单元格 | Tab / Shift+Tab | 已支持 |
| 合并单元格 | 无 | 不支持；标准 GFM Markdown 表格不能表达合并单元格 |

macOS 上本表中的 Ctrl 对应 ⌘、Alt 对应 Option。表格独立的「上移行」「下移行」命令同样可以自行指定按键；默认 Alt+↑/↓ 属于会按上下文处理的「上移行/下移行」编辑命令。

有多个默认键的命令，在偏好设置中会显示所有默认键。自定义或清除该命令时会一起移除所有默认键；恢复默认时一起恢复。冲突检查也覆盖兼容键。

## 视图、文件和查找

| 操作 | Windows / Linux | macOS | 状态与说明 |
| --- | --- | --- | --- |
| 专注模式 | F8 | F8 | 已支持 |
| 打字机模式 | F9 | F9 | 已支持 |
| 源码模式 | Ctrl+/ | ⌘/ | 已支持 |
| 大纲 | Ctrl+Shift+1 | ⌘Shift+1 | 已支持；点击标题跳转 |
| 全屏 | F11 | ⌃⌘F | 已支持，本次校准 macOS 默认键 |
| 只读阅读 | F12 或 Ctrl+Shift+P | F12 或 ⌘Shift+P | 本次把预览按键接到已有「锁定编辑」命令；再按退出 |
| 新建 | Ctrl+N | ⌘N | 已支持 |
| 打开 | Ctrl+O | ⌘O | 已支持 |
| 保存 | Ctrl+S | ⌘S | 已支持 |
| 另存为 | Ctrl+Shift+S | ⌘Shift+S | 已支持；PDF 等格式使用「文件 → 导出」 |
| 最近文件 | Ctrl+Shift+O | ⌘Shift+O | 本次补齐，打开现有最近文件/文件夹菜单 |
| 关闭标签页 | Ctrl+W | ⌘W | 已支持 |
| 关闭窗口 / 退出 | Alt+F4 | ⌘Q | 已支持；Windows 的 Alt+F4 是系统窗口操作 |
| 查找 | Ctrl+F | ⌘F | 已支持；Enter 下一项、Shift+Enter 上一项 |
| 替换 | Ctrl+H | ⌘Option+F | 已支持，本次校准 macOS 默认键 |
| 文档开头/结尾 | Ctrl+Home / Ctrl+End | ⌘↑ / ⌘↓ | 已支持；本次统一平台按键与自定义配置 |

`Enter`、`Tab`、方向键在查找输入框、表格、代码块、列表中有各自的编辑语义，不能作为任意全局快捷键抢占普通输入。它们不等同于全局命令快捷键。只读阅读沿用已有锁定模式，不会创建第二套文档视图或丢失编辑状态。

清单附带的功能描述也需区分：查找目前提供区分大小写和正则选项，没有单独的全字匹配开关；「另存为」不承担 PDF 导出；Markdown 表格没有单元格合并。Windows/Linux 的 Alt+F4 是系统关闭窗口操作；应用内「退出」命令则可在快捷键偏好设置中另外绑定。

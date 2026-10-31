# FLTK 与 wx 的原生输入与编辑区实验

本实验以“用多少项目维护的代码，才能在原生 Wayland 和 macOS 保住 Notepad4 的编辑行为”为判断条件。Windows 的成品体积基线见[前一轮结果](../RESULTS.md)。

## 实验口径

- Linux 的 FLTK 1.4.5 来自 `a9b1113516ffd15fc7602a6d425a317df30f4720`。GTK 和 Cocoa 的对照控件均取自 SHA-256 为 `ca7fda23125289bbf473afd6fcd4959f65e828bff1a8ae60c603b34dfcb3a777` 的 [Scintilla 5.6.8 发布源码](https://www.scintilla.org/ScintillaDownload.html)。它们只负责回答宿主怎样分派事件。Linux 的两个 FLTK 拼音端点将编辑写入 [Notepad4 的 `Document`](https://github.com/zufuliu/notepad4/blob/bc2fb59cd927288fc07bcdae48e91780cc5139d6/scintilla/src/Document.cxx)，沿用之前的 UTF-8 构建适配。原版 Notepad4 的语言词表、文档词收集和自动补全触发策略尚待接入完整非 Windows 控件。[应用层补全](https://github.com/zufuliu/notepad4/blob/bc2fb59cd927288fc07bcdae48e91780cc5139d6/src/EditAutoC.cpp#L1690-L1747)
- 路线之间共享的 fork 私有接口适配、文档和 lexer 可移植性修改单列。改第三方源码的差异、自有宿主适配、测试代码分别计量。本实验没有估计一个完整三平台编辑器的总代码量。

## macOS 宿主

同一台 arm64 macOS runner、同一原生 Scintilla Cocoa 视图，两个宿主分别使用 FLTK 1.4.5 与 wxWidgets 3.2.12。FLTK 调公开 `fl_mac_xid` 取得父窗口，把控件加入 `contentView` 后，将 first responder 交给 `SCIContentView`。wx 用 `wxNativeWindow` 承载它。两者的普通输入、鼠标、范围和补全均取同一份探针。[两个宿主和测试源](mac/probe.mm) · [FLTK 原生窗口接口](https://www.fltk.org/doc-1.4/group__group__macosx.html)

第一轮成功运行的共同结果包括 `A中😀B` 的 `[1,8)` 字节选区被 Cocoa 读成 `[1,4)` UTF-16 范围、候选锚矩形位于窗口内、点命中、点击、普通字母、Cmd+A、手工设置折叠级别后的显示行映射、固定候选列表的打开与接受。直接调用 `NSTextInputClient` 的标记和上屏方法后，选区实际被替换为 `A文B`。原始 FLTK 宿主的左方向键 `leftKeyDown=0, moved=0`。wx 为 `1,1`。AppKit 对普通键和潜在快捷键采用不同的分派路径，而 FLTK 的父视图处理 `performKeyEquivalent:`。[首次对照运行](https://github.com/1298345095/notepadprototype/actions/runs/41251301716) · [FLTK 1.4.5 分派源码](https://github.com/fltk/fltk/blob/release-1.4.5/src/Fl_cocoa.mm#L2516-L2557) · [AppKit 键盘分派](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/EventOverview/HandlingKeyEvents/HandlingKeyEvents.html)

原始进程内投递 `NSEvent` 时，两个宿主都成功选中了 Apple 简体拼音，但文档写入 `nihao `。在同一个可投递系统事件的原生 runner 上改为通过 Quartz 向 WindowServer 投递按键后，测试记录到 `n`、`ni`、`ni h`、`ni ha`、`ni hao` 五步标记文本，空格写入“你好”，单次撤销回到空文档。FLTK 应用侧仅将属于本窗口、当前聚焦在编辑区且无修饰键的方向键送给 `SCIContentView`，普通左键恢复为 `leftKeyDown=1, moved=1`，无需修改 FLTK Cocoa 源码。wx 的普通左键原本就能到达。[系统级输入对照](https://github.com/1298345095/notepadprototype/actions/runs/41251539577) · [应用侧方向键分派](mac/probe.mm#L62-L74)

在随后独立的系统输入验证中，FLTK 和 wx 都能把 `A中😀B` 中选中的 `中😀` 替换为 `你好`，所得为 `A你好B`。`al` 的固定补全列表激活后输入拼音并按空格，两者均得到 `al你好`，列表关闭。第二次输入 `ni` 后按 Esc 均清空预编辑。若先对 `nihao` 按左、右再空格，两者均显示 `你好ao` 且仍有标记文本，相同宿主里的系统 `NSTextView` 也显示相同中间态。但在按方向键前、左键后、右键后三个时点，两个 Scintilla 宿主读出的文档光标都是 `6 → 6 → 7`，系统 `NSTextView` 选区起点则是 `6 → 2 → 4`。该段文本全是 ASCII，可以直接比较单位。这些读数尚不能替代候选窗实际屏幕位置测量。[带系统文本控件的完整对照](https://github.com/1298345095/notepadprototype/actions/runs/41251942042)

在上游 `setMarkedText:selectedRange:replacementRange:` 中，原条件只处理长度大于零的选区，会跳过表示插入点的零长选区。在同版 Cocoa 视图里将该条件改为接受有效的零长范围，再跑上述流程，两个宿主都得到 **6 → 2 → 4**，与同窗 `NSTextView` 一致。替换、取消与补全列表冲突用例继续通过。附加左右键会改变 Apple 拼音分词，第一次空格后的 `你好ao` 仍有标记文本，在系统文本框上亦如此。日志中的普通拼音分类按一次空格结束组词判定，本用例另外使用 `check.py` 与系统控件逐项比对。这是两种外壳共享的 Cocoa 修改。[源码差异](cocoa-caret/marked-range.patch) · [最终独立验证](https://github.com/1298345095/notepadprototype/actions/runs/41252107608)

## Wayland 输入补丁

原版 `text_input_preedit_string` 只保存文本，丢弃 `cursor_begin/end`。`text_input_done` 会合并相同预编辑文本，`delete_surrounding_text` 仅打印参数。定点测试先放入 `nihao`，保持文本不变将光标从 5 移至 2，再检查控件事件。单独给 `A中|😀B` 发前 3 后 4 字节的删除请求，可检查是否在 `done` 边界只删除一次。[驱动源码](https://github.com/fltk/fltk/blob/release-1.4.5/src/drivers/Wayland/Fl_Wayland_Screen_Driver.cxx#L968-L1018) · [原版观察器](wayland/probe.cxx)

实验补丁只修改 FLTK 的 `FL/wayland.H` 和 Wayland 屏幕驱动，合计新增 41 行、删除 2 行。它将暂存文本、提交、光标区间和删除范围作为一笔输入交给聚焦编辑区，若被消费，抑制重复的合成键。用驱动回调和真实 fork `Document` 回放，预编辑光标能从 5 移到 2，删除后得到 `AB`，提交后得到 `A你好B`，注销钩子后普通按键恢复原路径。[补丁](patch/fltk-wayland-edit.patch) · [回放与字节断言](patch/cases.hpp) · [真实输入端点](patch/pinyin.cxx)

[未改驱动的原生窗口回放](https://github.com/1298345095/notepadprototype/actions/runs/41251832148)也记录到成功绘制、预编辑与上屏可见、同一文本光标变化和周围删除各触发 0 次控件事件。

41 行不含将文档前后文与选区回报给输入法、同一 surface 内不同焦点间的交接、完整布局、弹窗及维护测试。实验的实际编辑端点与补丁分列，`pinyin.cxx` 中的协议消费逻辑也需要项目维护。

原生 Wayland 作业使用 Ubuntu 24.04、Sway、Fcitx5 拼音，客户端没有 DISPLAY，Sway 禁用 XWayland。外层 Xvfb 给嵌套的 Sway 提供键盘座席，XTest 只向该合成器送键。上屏“你好”、第二次输入 ni 然后 Esc、最终文档只有“你好”，在原版和修改后的 FLTK fork 文档端点以及 wxGTK 上游控件均通过。原生协议产生了相同 `ni hao` 的光标 `6 → 5 → 6`。左右键期间，原版 FLTK 控件每次收到 0 次编辑事件，修改版每次收到 1 次，位置分别精确为 5 和 6。不同控件的通知粒度有别，事件条数不能当作性能指标。[原版 FLTK](https://github.com/1298345095/notepadprototype/actions/runs/41251539570) · [修改版与 wxGTK 同环境对照](https://github.com/1298345095/notepadprototype/actions/runs/41251539622)

wxGTK 正对照的实际输出为 `GdkWaylandDisplay`、编辑区获得焦点、折叠和展开行映射正确、混合文本的坐标命中字节 11（期望 11）、固定补全列表激活。FLTK 的 Linux 端点记录了输入与文档事务，其代码没有接入完整 `EditView`、`ScintillaBase` 或补全列表。[wxGTK 对照源码](gtk/probe.cpp) · [对应日志归档](https://github.com/1298345095/notepadprototype/actions/runs/41251539622/artifacts/13061650758)

## 鼠标选区与排版

旧 [Fl_Scintilla 输入处理](https://github.com/cyantreeguo/Fl_Scintilla/blob/b9808aea1cb5d011bf76d9cd421de636dce442c9/fltk/Fl_Scintilla.cxx#L450-L514) 和[逐码点测宽](https://github.com/cyantreeguo/Fl_Scintilla/blob/b9808aea1cb5d011bf76d9cd421de636dce442c9/fltk/PlatFLTK.cxx#L900-L920)可作移植参考。在 Linux Pango 1.52.1、DejaVu Sans Mono 12 上，`A中😀B` 的 B 前光标整行及逐码点算法都是 44 px。`Ae\u0301B` 的 B 前光标实际为 20 px，逐码点为 30 px。绘制、鼠标命中和输入法锚点应使用同一行排版结果。可运行 `python3 experiments/input-host/layout.py`。[复现实验](layout.py) · [Pango 光标位置](https://docs.gtk.org/Pango/method.Layout.get_cursor_pos.html)

## 仍需用实际端口验证的项目

- 在 Linux 的 FLTK 完整 `EditView` 中让 Pango 的整行布局同时驱动绘制、鼠标拖选和候选锚点，在软换行、折叠后重复拖选。
- 让两条实际 fork 后端都运行 Notepad4 的候选收集与 `SCN_CHARADDED.characterSource` 处理。这里运行过的固定补全列表只用于检查弹窗和输入法之间的键盘归属。
- 用完整功能相同的发布包记录包体、输入延迟与维护差异。现有窗口工具包测试未度量候选面板的多显示器屏幕坐标，也未度量可发布三平台编辑器的成本。

## 选型依据

FLTK 可以进入下一轮完整编辑区验证。macOS 的改动集中在聚焦控件的按键分派。Linux 的两个入站协议缺口可由小补丁修复，光标变化通过真实拼音核对，周围删除通过协议回放核对。现有 Linux 测试中，wxGTK 承载上游编辑控件已取得折叠、点命中和列表的基线，而 FLTK Linux 端点只到文档输入。若现在决定哪一版先做可交付编辑器，优先 wx。若决定哪条路线还值得以体积为目标继续，保留 FLTK。下一个比较应让两者共用同版 fork 的 `Editor/EditView` 和 Notepad4 补全通知策略，对比软换行及折叠后拖选的字节、双弹窗的按键归属、实际发布包以及项目要维护的差异。

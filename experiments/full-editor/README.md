# Notepad4 的两个 Linux 编辑区原型

两套 Wayland 编辑区由不同开发任务独立实现，公用同一个 Notepad4 fork、同一个 JSON 补全实现和同一套桌面输入测试。FLTK 通过 Cairo/Pango 绘制 fork 的编辑视图。wxGTK 把由上游 GTK3 平台层适配而来的 fork 控件放入一个可聚焦的 GTK 子控件树。两者均编译 fork 的 `Editor`、`EditView`、`ScintillaBase`、`ContractionState` 和 JSON lexer。

## 实测与选型

最终 Ubuntu 24.04 x86-64 原生作业在同一 runner 按 FLTK/wx、wx/FLTK、FLTK/wx 的顺序交替运行。每个宿主独立启动三次，每次通过 15/15 项检查。[原生运行](https://github.com/1298345095/notepadprototype/actions/runs/41253628027) · [可执行下载包](https://github.com/1298345095/notepadprototype/actions/runs/41253628027/artifacts/13062068585) · [协议、截图与逐轮原始结果](https://github.com/1298345095/notepadprototype/actions/runs/41253628027/artifacts/13062312695) · [稳定保存的实测数据](evidence/measurements.json)

| 指标 | FLTK Cairo 窗口装饰 | wxWidgets GTK3 |
|---|---:|---:|
| 剥离符号后的 ELF | 1.32 MiB | 5.96 MiB |
| 单个程序的 tar.xz，含许可与依赖清单 | 0.50 MiB | 1.62 MiB |
| 工作负载完成后的 RSS，中位数 | 41.4 MiB | 52.9 MiB |
| 256 次同一热行的坐标命中，中位数 / P95 | 201 / 212 µs | 356 / 363 µs |
| 1,048,673 字节文档内找词并打开列表，中位数 / P95 | 1.09 / 1.14 ms | 1.33 / 1.53 ms |
| 输入法预编辑事务至下一次主窗口提交重绘缓冲，中位数 / P95 | 1.56 / 1.62 ms | 2.56 / 2.74 ms |

两边静态链接各自的 GUI 框架，动态使用系统图形、字体、C++ 等库。最终 FLTK 选择内置 Cairo 装饰，ELF 不再直接依赖 GTK3 或 GDK3，wx 仍使用它们。RSS 包含共享页面，不等于独占内存。命中和找词先预热，再分别取 31 和 17 个样本，表中统计取三个独立进程结果的中位数。找词测的是同步调用，不含桌面呈现。最后一行用 Wayland trace 对每个宿主的 21 次预编辑事务计时，终点是编辑器主 surface 提交带 damage 的新缓冲，不含输入法引擎在发送前的耗时和显示器扫描。[计算脚本](runner/frame_times.py)

外部鼠标点击边栏后，目标长行换为 10 个显示行。两种宿主拖选都返回 UTF-8 区间 `[159,167)`，正好是 `B中😀`，前方是 e 与组合重音。文档中 Beta 改为 Zeta 后候选即时更新，语言前缀 `tr` 则从应用词表补为 `true`。列表打开时拼音光标与协议都为 `6 → 5 → 6`，上屏后的文本为 `np4ProofCan你好`，没有误选代码候选。每轮 fork 记录 2 个正式 IME 字符通知。[FLTK 整屏截图](evidence/fltk-selection.png) · [wxGTK 整屏截图](evidence/wx-selection.png)

FLTK 的 3 个自写平台实现共 411 个物理源代码行，另有两个 FLTK 驱动文件的 +41/−2 行。wx 有 76 行宿主，另在上游 GTK 的 4 个文件上做 +200/−210 行适配。删除行、新增行和新写完整功能代表不同维护工作，不能相加换算成人天。两条路线共用的 fork 可移植性、补全适配和测试均单列。就当前验证的轻量 Linux 编辑区而言，FLTK 的体积收益已实测；若维护目标涵盖原生滚动条、输入法前后文、辅助功能及更广泛平台行为，完整产品优先 wx。macOS 的完整 fork 没有在本轮重建。[上轮系统输入研究](../input-host/README.md)

## 运行

原型面向 Ubuntu 24.04 x86-64 或具有兼容系统库的 Linux，使用原生 Wayland 图形会话。解开 Actions 的下载包，其中有两个独立 tar.xz，任选其一。

```bash
tar -xf fltk-study.tar.xz
./np4-fltk/fltk-study
# 打开自己的 UTF-8 文件
./np4-fltk/fltk-study example.json

tar -xf wx-study.tar.xz
./np4-wx/wx-study example.json
```

不带参数时两者都打开内置示例。可以点击左侧折叠标记，拖选混排文本，输入 `np4ProofCan` 后用方向键和回车接受候选，按 Ctrl+Z 撤销。程序是验证编辑区的原型，未包含 Notepad4 完整的文件保存菜单。包内的 `*-needed.txt` 和 `*-ldd.txt` 列出测试机器上的运行库。

## 构建与复现

固定来源为 [Notepad4 bc2fb59](https://github.com/zufuliu/notepad4/tree/bc2fb59cd927288fc07bcdae48e91780cc5139d6)、[FLTK 1.4.5](https://github.com/fltk/fltk/tree/a9b1113516ffd15fc7602a6d425a317df30f4720)、[wxWidgets 3.2.11](https://github.com/wxWidgets/wxWidgets/tree/749625e0ea4d70d1f75494aa52231d835531a618) 及 [Scintilla 5.6.8 发布源码](https://www.scintilla.org/ScintillaDownload.html)。Scintilla 发布包只提供 GTK 平台层，系统的 wxStyledTextCtrl 没有参与编辑。为保证可以重建及核对 fork 版本，Notepad4、FLTK 和 wxWidgets 使用指定提交的 Git 工作树。`build.sh` 在独立目录准备依赖和补丁，两套宿主均使用 GCC、Release `-O2`、分节链接回收及符号剥离。Pango、Cairo 等系统库动态加载。

```bash
bash experiments/full-editor/build.sh deps/notepad4 deps/fltk deps/scintilla deps/wx out
bash experiments/full-editor/runner/run.sh out/bin/fltk-study results/fltk-1
bash experiments/full-editor/runner/run.sh out/bin/wx-study results/wx-1
python3 experiments/full-editor/runner/summary.py results out/bin
```

[GitHub Actions 工作流](../../.github/workflows/full-editor-wayland.yml) 包含完整依赖、校验和与复现命令。`run.sh` 需要普通用户，并创建独立的 Sway/Fcitx5 会话。Xvfb 为嵌套的 Sway 提供外部输入座席。编辑器本身没有 DISPLAY，Sway 关闭 XWayland。桌面实际发送鼠标点击和拖选、按键、拼音，验证脚本通过原型定时器读取 fork 的字节状态。坐标测试用独立 Pango 实例，以整段文本排版生成拖选点，再比对最终选区。测量输入时序使用真实 Wayland 协议记录。

## 代码和适用范围

| 项目 | 内容 |
|---|---|
| [共享核心](core/prepare.py) | 在临时构建树移除或替换 Win32 计时、线程池和旧编码路径，布局统一在 UI 线程执行。词法器目录限定为 JSON 和 Null。 |
| [应用补全](qa/extract.py) | 从固定提交的 `src/EditAutoC.cpp` 编译词树、去重排序、`AutoC_AddDocWord`、`EditCompleteWordCore` 和 `EditCompleteWord`，由 fork 自己的查找消息扫描当前文档。读取 `stlJSON.cpp` 的语言词表，保留通知来源门控。特殊语言前缀提供器、其他语言、用户的全套配置未移植。 |
| [FLTK 编辑区](fltk/) | 自写 `Surface`、窗口、输入和候选列表适配。绘制与测宽共享 Pango 布局。滚轮可以滚动，补全列表在客户区内显示。依赖[上一轮的输入补丁](../input-host/patch/fltk-wayland-edit.patch)。 |
| [wxGTK 编辑区](wx/) | GTK 平台层通过 `fork-gtk.patch` 对接 fork 的签名、绘制接口与输入来源，wx 仅负责窗口及子控件承载。保留 GTK 后端的 Pango、原生候选列表和可访问性代码。 |
| [验收和性能](runner/) | 证据包含命令与回复、真实预编辑与提交协议、屏幕截图、进程内存快照、热命中和文档候选耗时。 |

本轮限定 UTF-8、从左向右的文本和 JSON 场景。FLTK 原型还缺可见滚动条、系统拖出、辅助功能及函数参数提示。wxGTK 继承了较宽的平台代码覆盖面，未逐项验收。两个原型的临时视图都不能代表含 79 个 lexer、全部语言策略、文件管理和多平台打包的完整产品。

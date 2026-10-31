# Notepad4 的两个 Linux 编辑区原型

两套 Wayland 编辑区由不同开发任务独立实现，公用同一个 Notepad4 fork、同一个 JSON 补全实现和同一套桌面输入测试。FLTK 通过 Cairo/Pango 绘制 fork 的编辑视图。wxGTK 把由上游 GTK3 平台层适配而来的 fork 控件放入一个可聚焦的 GTK 子控件树。两者均编译 fork 的 `Editor`、`EditView`、`ScintillaBase`、`ContractionState` 和 JSON lexer。

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

# Notepad4 其他 GUI 宿主的评估

Windows 原生试验使用 `windows-2022`、x64、Notepad4 `bc2fb59cd927288fc07bcdae48e91780cc5139d6`。四个静态宿主均包含 fork 的 Win32 编辑器和全套 lexer。FXRuby、Kivy 实验经 C ABI 加载同一来源的完整 Win32 编辑器。macOS 试验采用 Apple Silicon 原生工具链。最后一项只覆盖文档与绘制，属于不同实现范围。

| Windows 宿主 | 单 EXE / MiB | 暖启动可读 A / B，ms | 工作集约 MiB | 全量 JSON 着色 A / B，ms |
|---|---:|---:|---:|---:|
| FOX 1.6.59 | 1.66 | 27.6 / 36.6 | 13.4 | 12.2 / 23.6 |
| FLTK 1.4.5 | 1.84 | 27.6 / 42.0 | 15.1 | 12.3 / 24.8 |
| wxMSW 3.2.11 | 5.83 | 26.8 / 40.1 | 16.7 | 12.6 / 24.6 |
| Tcl/Tk 9.0.4 | 6.92 | 63.1 / 92.6 | 22.1 | 12.0 / 22.0 |

A、B 是两轮独立 Windows runner。每轮在同一台机器上交替测四种宿主。暖启动取 3 次新进程的中位数，终点为可见的 Scintilla 子窗能返回默认文档的 22 字节长度。工作集在此后 300 ms 采样。着色取 15 个样本的中位数，输入为内存中的 4,194,288 字节 JSON，暂停绘制。这些数据不度量屏幕呈现与输入法延迟。[A 轮](https://github.com/1298345095/notepadprototype/actions/runs/41249417284/job/124679601450) · [B 轮](https://github.com/1298345095/notepadprototype/actions/runs/41249570098/job/124680056915)

- FOX 的实际宿主为静态 C++，已验 fork 消息 2067、JSON、选区替换/撤销、原生子窗文字键输入和文件保存。FXScintilla 的旧桥需对照 fork 的接口适配。[构建](https://github.com/1298345095/notepadprototype/actions/runs/41249208529/job/124678989646)
- Tcl/Tk 的实现直接使用 C API，Tcl/Tk 初始化脚本与资源通过可执行文件尾部的 ZipFS 提供。它提供了 Tcl 脚本扩展的起点。[构建](https://github.com/1298345095/notepadprototype/actions/runs/41249208529/job/124678989818)
- FXRuby 1.6.50 用已安装的 Ruby 3.3 取得 FOX HWND，经原生适配器验证 fork 消息、着色、替换与撤销。实验没有构建 Ruby 自包含发行包。[运行记录](https://github.com/1298345095/notepadprototype/actions/runs/41248950504/job/124678201750)
- Kivy 2.3.1、CPython 3.12、SDL2/ANGLE 的 PyInstaller 单文件探针为 28,465,640 字节，解包 270 个文件共 52,842,506 字节。已验真实子 HWND、父子窗口缩放、emoji 宽度和撤销。它与 C++ 四方负载的功能覆盖不同。[打包记录](https://github.com/1298345095/notepadprototype/actions/runs/41249467592/job/124679754927)
- AppKit 的原生 NSView 绘制 fork Document 和 JSON lexer，插入、撤销、emoji 和有色像素验收通过。Mach-O 为 218,336 字节，内层 .app.zip 为 77,735 字节；还需移植完整 Editor/EditView、输入法和辅助功能。[macOS 运行记录](https://github.com/1298345095/notepadprototype/actions/runs/41249196773/job/124678956285)

三平台的轻量成品继续以 FLTK 为基线。Windows 与 X11 Linux 优先时，FOX 值得与 FLTK 一起进入真实项目验证。需要 Tcl 脚本外壳才优先选择 Tk。macOS 原生体验优先时，Cocoa 平台代码值得复用为 Mac 编辑后端。FXRuby 与 Kivy 的优势分别在已有 Ruby 应用和触控/跨设备外观。

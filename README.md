# Notepad4 Windows prototypes

两个 Windows x64 编辑器均使用 Notepad4 修改版 Scintilla 的原生 Win32 控件。GitHub Actions 在 Windows Server 2022 上使用 Visual Studio 2022 编译，应用、GUI 框架和 C/C++ 运行时均静态链接。

在 [Actions](https://github.com/1298345095/notepadprototype/actions) 中打开成功的 Windows 构建，下载 `np4-fltk-win64` 或 `np4-wx-win64`。解压后运行其中唯一的 EXE，可把现有 UTF-8 文件拖到 EXE 图标上，再用 Ctrl+S 保存。

构建使用 Notepad4 提交 `bc2fb59cd927288fc07bcdae48e91780cc5139d6` 的编辑核心、Win32 后端和全部 lexer。FLTK 1.4.5 与 wxWidgets 3.2.11 仅提供宿主窗口。原型包含 `--self-test`、`--file-self-test` 和 `--bench` 入口。

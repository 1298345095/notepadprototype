# Tcl/Tk 9.0.4 Windows x64 host

Run `pwsh -File experiments/tk/build.ps1` at the repository root in an x64 Visual Studio 2022 developer shell. This experiment expects these source checkouts:

| Directory | Source commit |
|---|---|
| `deps/notepad4` | `zufuliu/notepad4` at `bc2fb59cd927288fc07bcdae48e91780cc5139d6` |
| `deps/tcl` | `tcltk/tcl` at `c655b4770b1d6d32a8cbffd6cef59db6029fe19e` |
| `deps/tk` | `tcltk/tk` at `584f8fcf62c320d7c341e77171188cb4d79c3725` |

The Tcl and Tk upstream NMake files build static libraries with `OPTS=static,nomsvcrt`. CMake links those libraries and the repository's `np4_engine` target into a single MSVC `/MT` GUI executable. The build appends Tk's combined `wish.zip` to that executable and stores it at `out/tk/np4-tk.exe`. Startup explicitly checks that the Tcl and Tk script libraries were found in ZipFS.

The app uses Tcl/Tk's C API. A realized `ttk::frame` provides a Windows parent through `Tk_GetHWND`. The fork's native Scintilla child owns document storage, lexer styles, undo and keyboard input. The experiment opens a UTF-8 file from argv; Ctrl+S writes back a named file. `--self-test` checks the fork-specific message 2067, JSON5 styling, selection replacement and undo. `--file-self-test <existing-file>` overwrites its test fixture and verifies the saved UTF-8 bytes. `--bench` runs the common native HWND workload.

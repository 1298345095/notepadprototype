# FOX host experiment

This software uses the FOX Toolkit Library (http://www.fox-toolkit.org).

The Windows x64 host uses FOX 1.6.59 and the same Notepad4 Win32 control, fonts, JSON palette, file I/O and notification settings as the FLTK baseline. It translates key messages for the native editor then dispatches them through FOX. `--self-test` posts a space key through the actual FOX event loop and verifies the new byte alongside the fork-specific 2067 message, JSON styling, selection and undo. `--file-self-test` and `--bench` have the same contracts as the other hosts. The prototype does not test an X11 or macOS port.

From an x64 MSVC developer shell, check out the pinned Notepad4 source at `deps/notepad4`, then run `./experiments/fox/build.ps1`. The script checks the original source archive against the [Homebrew formula](https://github.com/Homebrew/homebrew-core/blob/HEAD/Formula/f/fox.rb), configures the upstream VC static-library source set using CMake and produces `out/fox/np4-fox.exe`.

The FOX release archive contains `LICENSE` and `LICENSE_ADDENDUM`; the Notepad4 checkout contains its license and Scintilla's license. These notices apply to their respective components.

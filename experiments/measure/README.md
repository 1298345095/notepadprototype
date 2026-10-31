# Reproducing the desktop toolkit comparison

This experiment uses binaries built from Notepad4 commit `bc2fb59cd927288fc07bcdae48e91780cc5139d6`. The input run IDs pin both GUI baselines and the added FOX/Tk builds. The Actions workflow downloads the single EXE for each from this repository and runs them in alternating order on one Windows x64 runner.

`windows.py` checks the five unskipped trials from each process, after the executable's own warm-up. It reports the median of 15 trials for loading 4,194,288 UTF-8 bytes already in memory, colouring the entire JSON document and performing 64 selection replacements plus undo. The warm-start check runs three additional fresh processes per host after an unrecorded warm-up, polling at 5 ms for a visible native Scintilla control containing the same short text. The 300 ms working set is sampled separately. These timings do not include compositor presentation or real IME latency.

The Kivy result comes from its independently verified one-file build. It bundles Python, Kivy, SDL2/ANGLE and a DLL containing the complete Windows fork. Its size and unpacked contents are reported separately from the static host comparison. The FXRuby job tested hosted functionality with an installed Ruby runtime. The Apple Silicon AppKit app is a document-layer and lexer experiment with a custom view, not a port of the whole Scintilla editor view.

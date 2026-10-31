"""Build the JSON word-completion slice from the pinned Notepad4 checkout."""
import pathlib
import re
import sys
src, out = map(pathlib.Path, sys.argv[1:])
edit = (src / 'src/EditAutoC.cpp').read_text()
calls = (src / 'src/SciCall.h').read_text()
style = (src / 'src/EditLexers/stlJSON.cpp').read_text()
def section(text, start, end):
    offset = text.index(start)
    return text[offset:text.index(end, offset + len(start))]
common = section(edit, '#define NP2_AUTOC_CACHE_SORT_KEY', '\nenum {\n\t// see LexInno')
scan = section(edit, 'static void AutoC_AddDocWord', 'static void AutoC_AddKeyword')
main = section(edit, 'static bool EditCompleteWordCore', 'static bool CanAutoCloseSingleQuote')
words = section(style, 'static const char * const Keywords_JSON', 'static const EDITSTYLE')
# JSON has one keyword list and no language-specific special-prefix provider.
keyword = """
static void AutoC_AddKeyword(WordList &words, int) noexcept { words.AddListEx(Keywords_JSON[0]); }
enum AddWordResult { AddWordResult_None, AddWordResult_Finish, AddWordResult_IgnoreLexer };
static AddWordResult AutoC_AddSpecWord(WordList &, int, int, int, int) noexcept { return AddWordResult_None; }
"""
body = words + common + scan + keyword + main
used = set(re.findall(r'\bSciCall_\w+',re.sub(r'//[^\n]*','',body)))
wrappers = [f.group() for f in re.finditer(r'^inline [^\n]*? (SciCall_\w+)\([^\n]+?\n.*?^\}',calls,re.M|re.S)
            if f.group(1) in used]
if len(wrappers) != len(used):
    raise ValueError(used - set(re.findall(r'SciCall_\w+', '\n'.join(wrappers))))
out.write_text('\n'.join(wrappers) + '\n' + body)

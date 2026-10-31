import json
import re
import sys
from pathlib import Path

for path in map(Path, sys.argv[1:]):
    result = next(json.loads(s[7:]) for s in path.read_text().splitlines() if s.startswith('PINYIN '))
    states = result['nativeTextViewControl']['states']
    expected = [int(re.search(r'\d+', s['selection'])[0]) for s in states[:3]]
    actual = result['preeditCaretsBeforeLeftRight']
    print(path.name, 'Scintilla', actual, 'NSTextView', expected, 'after-space', result['committed'])
    if expected != actual or result['committed'] != states[3]['text'] or result['ended'] == states[3]['marked']:
        sys.exit('The Cocoa adapter still differs from NSTextView')
    if not result['cancelledSecondPreedit'] or result['replacement'] != 'A你好B' or result['compositionWithCodeList'] != 'al你好':
        sys.exit('A previously working operation regressed')

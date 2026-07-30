#!/usr/bin/env python3
"""Normalize literal anchors and quantized motion contracts in the transformer."""

from pathlib import Path

path = Path('.github/scripts/experience_pass.py')
text = path.read_text(encoding='utf-8')
raw_replacements = {
    '    """        !append_config_text(text, sizeof(text), length, "\\nanimations=") ||':
        '    r"""        !append_config_text(text, sizeof(text), length, "\\nanimations=") ||',
    '    """void refresh_desktop() {':
        '    r"""void refresh_desktop() {',
    '    """    if (status_center_open) {':
        '    r"""    if (status_center_open) {',
}
for old, new in raw_replacements.items():
    count = text.count(old)
    if count != 2:
        raise SystemExit(f'experience-prepare: expected two occurrences of {old!r}, got {count}')
    text = text.replace(old, new)

old_contract = '            if (offset < 0 || offset >= previous) return false;'
new_contract = '            if (offset < 0 || offset > previous) return false;'
if text.count(old_contract) != 1:
    raise SystemExit('experience-prepare: motion monotonicity contract anchor mismatch')
text = text.replace(old_contract, new_contract, 1)

path.write_text(text, encoding='utf-8')
print('experience-prepare: preserved literal escapes and quantized monotonic easing')

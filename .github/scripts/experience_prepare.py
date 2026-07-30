#!/usr/bin/env python3
"""Normalize raw literal anchors in the generated experience transformer."""

from pathlib import Path

path = Path('.github/scripts/experience_pass.py')
text = path.read_text(encoding='utf-8')
replacements = {
    '    """        !append_config_text(text, sizeof(text), length, "\\nanimations=") ||':
        '    r"""        !append_config_text(text, sizeof(text), length, "\\nanimations=") ||',
    '    """void refresh_desktop() {':
        '    r"""void refresh_desktop() {',
    '    """    if (status_center_open) {':
        '    r"""    if (status_center_open) {',
}
for old, new in replacements.items():
    count = text.count(old)
    if count != 2:
        raise SystemExit(f'experience-prepare: expected two occurrences of {old!r}, got {count}')
    text = text.replace(old, new)
path.write_text(text, encoding='utf-8')
print('experience-prepare: preserved C++ newline and CRLF escape anchors')

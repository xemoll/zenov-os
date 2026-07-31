#!/usr/bin/env python3
"""Remove the pre-profile fixed four-frame transition assertion."""

from pathlib import Path

path = Path("tests/qemu_display_ui.sh")
text = path.read_text(encoding="utf-8")
old = 'UI_REDUCED_MOTION_ON "UI_TRANSITION_FRAMES 4" "UI_TRANSITION_FRAMES 1"'
new = 'UI_REDUCED_MOTION_ON "UI_TRANSITION_FRAMES 1"'
count = text.count(old)
if count != 1:
    raise SystemExit(
        f"experience-marker-cleanup-post: expected one obsolete transition marker, got {count}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("experience-marker-cleanup-post: removed obsolete fixed four-frame assertion")

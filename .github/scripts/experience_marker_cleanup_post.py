#!/usr/bin/env python3
"""Remove obsolete pre-profile fixed four-frame transition assertions."""

from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"experience-marker-cleanup-post: {label}: expected one match, got {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"experience-marker-cleanup-post: {label}")


replace_once(
    "tests/qemu_display_ui.sh",
    'UI_REDUCED_MOTION_ON "UI_TRANSITION_FRAMES 4" "UI_TRANSITION_FRAMES 1"',
    'UI_REDUCED_MOTION_ON "UI_TRANSITION_FRAMES 1"',
    "removed obsolete fixed four-frame runtime assertion",
)

replace_once(
    ".github/workflows/adaptive-display.yml",
    "UI_REDUCED_MOTION_ON 'UI_TRANSITION_FRAMES 4' 'UI_TRANSITION_FRAMES 1' \\",
    "'UI_TRANSITION_FRAMES 7' UI_REDUCED_MOTION_ON 'UI_TRANSITION_FRAMES 1' \\",
    "made permanent workflow assert Fluid and Reduced profiles",
)

print("experience-marker-cleanup-post: complete")

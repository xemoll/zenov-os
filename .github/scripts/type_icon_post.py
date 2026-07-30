#!/usr/bin/env python3
"""Compatibility and cleanup stages for the native typography/icon integration pass."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

TEST_PATH = Path("tests/qemu_display_ui.sh")
GRAPHICS_PATH = Path("kernel/parts/ui_graphics_base.inc")
FONT_PATH = Path("kernel/parts/ui_font.inc")
LICENSE_PATH = Path("third_party/inter/OFL.txt")
LICENSE_SHA256 = "262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a"

COMPAT_START = "# type-icon-transform-compat-start"
COMPAT_END = "# type-icon-transform-compat-end"
LEGACY_MARKERS = (
    "UI_LAUNCHER_MORPH_OK UI_FONT_ATLAS_OK UI_FONT_METRICS_OK "
    "UI_BORDER_STROKE_OK"
)
CURRENT_MARKERS = (
    "UI_LAUNCHER_MORPH_OK UI_MINIMAL_SHELL_OK UI_FONT_ATLAS_OK "
    "UI_FONT_METRICS_OK UI_BORDER_STROKE_OK"
)
EXTENDED_MARKERS = (
    "UI_LAUNCHER_MORPH_OK UI_MINIMAL_SHELL_OK UI_FONT_ATLAS_OK "
    "UI_FONT_WEIGHT_OK UI_FONT_METRICS_OK UI_ICON_SYSTEM_OK "
    '\"UI_FONT_GLYPH_COUNT 95\" \"UI_ICON_COUNT 13\" UI_BORDER_STROKE_OK'
)

LEGACY_HELPERS = """void draw_text_right(int32_t right, int32_t y, const char* text, uint32_t color, uint32_t scale = 1U) { draw_text(right - text_width(text, scale), y, text, color, scale); }

void draw_uint(int32_t x, int32_t y, uint32_t value, uint32_t color) {
    char buffer[11]; uint32_t count = 0;
    do { buffer[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(buffer));
    while (count) { draw_char(x, y, buffer[--count], color, 1); x += 6; }
}

"""

FONT_NOTICE = """/*
 * UI bitmap glyph subset derived from Inter Regular and SemiBold.
 * Copyright (c) 2016 The Inter Project Authors.
 * Licensed under the SIL Open Font License 1.1; see third_party/inter/OFL.txt.
 * This modified bitmap subset is not exposed under the reserved font family name.
 */
"""


def read_text(path: Path) -> str:
    if not path.is_file():
        raise SystemExit(f"required file is missing: {path}")
    return path.read_text(encoding="utf-8")


def write_text(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def remove_compat_block(text: str) -> str:
    start = text.find(COMPAT_START)
    end = text.find(COMPAT_END)
    if start < 0 and end < 0:
        return text
    if start < 0 or end < 0 or end < start:
        raise SystemExit("type/icon compatibility block is malformed")
    line_start = text.rfind("\n", 0, start) + 1
    line_end = text.find("\n", end)
    if line_end < 0:
        line_end = len(text)
    else:
        line_end += 1
    if text.find(COMPAT_START, start + len(COMPAT_START)) >= 0 or text.find(COMPAT_END, end + len(COMPAT_END)) >= 0:
        raise SystemExit("type/icon compatibility block is duplicated")
    return text[:line_start] + text[line_end:]


def stage_pre() -> None:
    text = read_text(TEST_PATH)
    if COMPAT_START in text or COMPAT_END in text:
        raise SystemExit("type/icon compatibility block already exists")
    if text.count(CURRENT_MARKERS) != 1:
        raise SystemExit(
            f"current display marker sequence: expected one match, got {text.count(CURRENT_MARKERS)}"
        )
    block = f"\n{COMPAT_START}\n# {LEGACY_MARKERS}\n{COMPAT_END}\n"
    write_text(TEST_PATH, text + block)
    print("type-icon-post: staged legacy marker compatibility")


def patch_markers() -> None:
    text = remove_compat_block(read_text(TEST_PATH))
    current_count = text.count(CURRENT_MARKERS)
    extended_count = text.count(EXTENDED_MARKERS)
    if current_count == 1 and extended_count == 0:
        text = text.replace(CURRENT_MARKERS, EXTENDED_MARKERS, 1)
    elif current_count == 0 and extended_count == 1:
        pass
    else:
        raise SystemExit(
            "type/icon display marker sequence is ambiguous: "
            f"current={current_count} extended={extended_count}"
        )
    write_text(TEST_PATH, text)
    print("type-icon-post: installed type/icon runtime marker requirements")


def remove_legacy_helpers() -> None:
    text = read_text(GRAPHICS_PATH)
    legacy_count = text.count(LEGACY_HELPERS)
    if legacy_count == 1:
        text = text.replace(LEGACY_HELPERS, "", 1)
    elif legacy_count != 0:
        raise SystemExit(f"legacy text helper block is ambiguous: count={legacy_count}")

    if text.count("void draw_text_right(") != 1 or text.count("void draw_uint(") != 1:
        raise SystemExit(
            "modern text helper cardinality failed: "
            f"draw_text_right={text.count('void draw_text_right(')} "
            f"draw_uint={text.count('void draw_uint(')}"
        )
    write_text(GRAPHICS_PATH, text)
    print("type-icon-post: removed superseded fixed-width text helpers")


def install_font_notice() -> None:
    text = read_text(FONT_PATH)
    notice_count = text.count("UI bitmap glyph subset derived from Inter Regular and SemiBold")
    if notice_count == 0:
        text = FONT_NOTICE + text
    elif notice_count != 1:
        raise SystemExit(f"font attribution notice is ambiguous: count={notice_count}")
    write_text(FONT_PATH, text)
    print("type-icon-post: retained human-readable OFL attribution")


def verify_license() -> None:
    data = LICENSE_PATH.read_bytes() if LICENSE_PATH.is_file() else b""
    digest = hashlib.sha256(data).hexdigest()
    if digest != LICENSE_SHA256:
        raise SystemExit(
            f"Inter OFL notice digest mismatch: got={digest or '<missing>'} expected={LICENSE_SHA256}"
        )
    print(f"type-icon-post: verified OFL notice sha256={digest}")


def stage_post() -> None:
    patch_markers()
    remove_legacy_helpers()
    install_font_notice()
    verify_license()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", choices=("pre", "post"))
    args = parser.parse_args()
    if args.stage == "pre":
        stage_pre()
    else:
        stage_post()

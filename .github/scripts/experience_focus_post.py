#!/usr/bin/env python3
"""Add deterministic settings navigation evidence and synchronized QEMU input."""

from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"experience-focus-post: {label}: expected one match, got {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"experience-focus-post: {label}")


replace_once(
    "kernel/parts/ui_runtime.inc",
    """void change_settings_page(bool forward) {
    settings_page = forward ? static_cast<uint8_t>((settings_page + 1U) % settings_page_count)
                            : static_cast<uint8_t>((settings_page + settings_page_count - 1U) % settings_page_count);
    settings_focus = 0U;
    system_sound::play(system_sound::Event::select);
}
""",
    """const char* settings_page_name() {
    if (settings_page == 0U) return "General";
    if (settings_page == 1U) return "Style";
    if (settings_page == 2U) return "Access";
    if (settings_page == 3U) return "Experience";
    return "About";
}

void report_settings_navigation() {
    serial::write("UI_SETTINGS_PAGE ");
    serial::write(settings_page_name());
    serial::write("\r\n");
    serial::write("UI_SETTINGS_FOCUS ");
    serial_uint(settings_focus);
    serial::write("\r\n");
}

void change_settings_page(bool forward) {
    settings_page = forward ? static_cast<uint8_t>((settings_page + 1U) % settings_page_count)
                            : static_cast<uint8_t>((settings_page + settings_page_count - 1U) % settings_page_count);
    settings_focus = 0U;
    system_sound::play(system_sound::Event::select);
    report_settings_navigation();
}
""",
    "reported selected settings page and reset focus",
)

replace_once(
    "kernel/parts/ui_runtime.inc",
    """        if (key == '\t' || key == key_down) settings_focus = static_cast<uint8_t>((settings_focus + 1U) % count);
        else if (key == key_up) settings_focus = settings_focus == 0U ? static_cast<uint8_t>(count - 1U) : static_cast<uint8_t>(settings_focus - 1U);
        else if (key == key_left || key == key_right) return apply_settings_control(key == key_right);
""",
    """        if (key == '\t' || key == key_down) {
            settings_focus = static_cast<uint8_t>((settings_focus + 1U) % count);
            report_settings_navigation();
        } else if (key == key_up) {
            settings_focus = settings_focus == 0U ? static_cast<uint8_t>(count - 1U) : static_cast<uint8_t>(settings_focus - 1U);
            report_settings_navigation();
        } else if (key == key_left || key == key_right) return apply_settings_control(key == key_right);
""",
    "reported every keyboard focus transition",
)

replace_once(
    "tests/qemu_display_ui.sh",
    '''  echo "sendkey end 10"
  sleep 0.25
  capture_mode settings-experience-1024x768
  echo "sendkey tab 10"
  sleep 0.10
  echo "sendkey tab 10"
  sleep 0.10
  echo "sendkey tab 10"
  sleep 0.10
  echo "sendkey tab 10"
  sleep 0.10
  echo "sendkey tab 10"
  sleep 0.10
  echo "sendkey ret 10"
''',
    '''  echo "sendkey end 10"
  wait_for_serial "UI_SETTINGS_PAGE Experience" || { echo quit; return 1; }
  wait_for_serial "UI_SETTINGS_FOCUS 0" || { echo quit; return 1; }
  sleep 0.15
  capture_mode settings-experience-1024x768
  echo "sendkey tab 10"
  wait_for_serial "UI_SETTINGS_FOCUS 1" || { echo quit; return 1; }
  echo "sendkey tab 10"
  wait_for_serial "UI_SETTINGS_FOCUS 2" || { echo quit; return 1; }
  echo "sendkey tab 10"
  wait_for_serial "UI_SETTINGS_FOCUS 3" || { echo quit; return 1; }
  echo "sendkey tab 10"
  wait_for_serial "UI_SETTINGS_FOCUS 4" || { echo quit; return 1; }
  echo "sendkey tab 10"
  wait_for_serial "UI_SETTINGS_FOCUS 5" || { echo quit; return 1; }
  echo "sendkey ret 10"
''',
    "synchronized Experience preview through runtime navigation evidence",
)

print("experience-focus-post: complete")

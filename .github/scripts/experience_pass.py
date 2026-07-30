#!/usr/bin/env python3
"""Deterministic ZenovOS experience settings, motion and sound integration."""

from __future__ import annotations

from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    file_path = Path(path)
    text = file_path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match in {path}, got {count}")
    file_path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"experience-pass: {label}")


# Kernel integration: PC speaker engine is defined after low-level I/O primitives
# and ticked from the existing 100 Hz PIT interrupt without busy waiting.
replace_once(
    "kernel/kernel.cpp",
    '#include "parts/core.inc"\n#include "parts/memory_compare.inc"\n#include "parts/hardware.inc"\n',
    '#include "parts/core.inc"\n#include "parts/memory_compare.inc"\n#include "parts/system_sound.inc"\n#include "parts/hardware.inc"\n',
    "included bounded system sound engine",
)
replace_once(
    "kernel/kernel.cpp",
    "    pit_init(100);\n    enable_interrupts();\n",
    "    pit_init(100);\n    system_sound::init();\n    enable_interrupts();\n",
    "initialized system sound before interrupts",
)
replace_once(
    "kernel/parts/hardware.inc",
    '    if (frame->vector == 32) { ++timer_ticks; pic_eoi(0); return 0; }\n',
    '    if (frame->vector == 32) { ++timer_ticks; system_sound::tick(); pic_eoi(0); return 0; }\n',
    "ticked sound state machine from IRQ0",
)

# Settings model and persistent preferences.
replace_once(
    "kernel/parts/ui_theme.inc",
    "    settings_page_general, settings_page_style, settings_page_access, settings_page_about,\n",
    "    settings_page_general, settings_page_style, settings_page_access, settings_page_experience, settings_page_about,\n",
    "added Experience settings page target",
)
replace_once(
    "kernel/parts/ui_theme.inc",
    "    settings_contrast, settings_pointer, settings_access_motion,\n",
    "    settings_contrast, settings_pointer, settings_access_motion,\n"
    "    settings_motion_profile, settings_motion_speed, settings_sound_theme,\n"
    "    settings_feedback, settings_animation_quality, settings_sound_preview,\n",
    "added Experience control targets",
)
replace_once(
    "kernel/parts/ui_theme.inc",
    "constexpr uint8_t settings_page_count = 4U;\n",
    "constexpr uint8_t settings_page_count = 5U;\n",
    "expanded settings page count",
)
replace_once(
    "kernel/parts/ui_theme.inc",
    "bool runtime_large_pointer = false;\nbool keyboard_focus_mode = false;\n",
    """bool runtime_large_pointer = false;
constexpr uint8_t motion_profile_reduced = 0U;
constexpr uint8_t motion_profile_calm = 1U;
constexpr uint8_t motion_profile_fluid = 2U;
constexpr uint8_t motion_profile_snappy = 3U;
constexpr uint8_t motion_profile_count = 4U;
constexpr uint8_t motion_speed_relaxed = 0U;
constexpr uint8_t motion_speed_normal = 1U;
constexpr uint8_t motion_speed_fast = 2U;
constexpr uint8_t motion_speed_count = 3U;
constexpr uint8_t animation_quality_auto = 0U;
constexpr uint8_t animation_quality_full = 1U;
constexpr uint8_t animation_quality_count = 2U;
uint8_t runtime_motion_profile = motion_profile_fluid;
uint8_t runtime_motion_speed = motion_speed_normal;
uint8_t runtime_animation_quality = animation_quality_auto;
uint8_t runtime_sound_theme = system_sound::theme_soft;
uint8_t runtime_feedback_level = system_sound::feedback_normal;
bool keyboard_focus_mode = false;
""",
    "added bounded motion and sound preference model",
)
replace_once(
    "kernel/parts/ui_theme.inc",
    "bool is_hovered(HoverTarget target) { return !keyboard_focus_mode && hovered_target == target; }\n\n",
    """bool is_hovered(HoverTarget target) { return !keyboard_focus_mode && hovered_target == target; }

const char* motion_profile_name() {
    if (runtime_motion_profile == motion_profile_reduced) return "Reduced";
    if (runtime_motion_profile == motion_profile_calm) return "Calm";
    if (runtime_motion_profile == motion_profile_snappy) return "Snappy";
    return "Fluid";
}

const char* motion_speed_name() {
    if (runtime_motion_speed == motion_speed_relaxed) return "Relaxed";
    if (runtime_motion_speed == motion_speed_fast) return "Fast";
    return "Normal";
}

const char* animation_quality_name() {
    return runtime_animation_quality == animation_quality_full ? "Full" : "Auto";
}

const char* sound_theme_name() {
    if (runtime_sound_theme == system_sound::theme_off) return "Off";
    if (runtime_sound_theme == system_sound::theme_clear) return "Clear";
    return "Soft";
}

const char* feedback_level_name() {
    if (runtime_feedback_level == system_sound::feedback_quiet) return "Quiet";
    if (runtime_feedback_level == system_sound::feedback_pronounced) return "Pronounced";
    return "Normal";
}

void synchronize_motion_compatibility() {
    desktop_animations = runtime_motion_profile != motion_profile_reduced;
}

""",
    "added preference labels and legacy motion synchronization",
)
replace_once(
    "kernel/parts/ui_theme.inc",
    """    if (parse_decimal_value(config_value(text, size, "animations"), parsed) && parsed <= 1U) {
        desktop_animations = parsed != 0U; recognized = true;
    }
""",
    """    if (parse_decimal_value(config_value(text, size, "animations"), parsed) && parsed <= 1U) {
        runtime_motion_profile = parsed != 0U ? motion_profile_fluid : motion_profile_reduced;
        synchronize_motion_compatibility();
        recognized = true;
    }
    if (config_value_equal(text, size, "motion", "reduced")) { runtime_motion_profile = motion_profile_reduced; recognized = true; }
    else if (config_value_equal(text, size, "motion", "calm")) { runtime_motion_profile = motion_profile_calm; recognized = true; }
    else if (config_value_equal(text, size, "motion", "fluid")) { runtime_motion_profile = motion_profile_fluid; recognized = true; }
    else if (config_value_equal(text, size, "motion", "snappy")) { runtime_motion_profile = motion_profile_snappy; recognized = true; }
    if (config_value_equal(text, size, "motion_speed", "relaxed")) { runtime_motion_speed = motion_speed_relaxed; recognized = true; }
    else if (config_value_equal(text, size, "motion_speed", "normal")) { runtime_motion_speed = motion_speed_normal; recognized = true; }
    else if (config_value_equal(text, size, "motion_speed", "fast")) { runtime_motion_speed = motion_speed_fast; recognized = true; }
    if (config_value_equal(text, size, "animation_quality", "auto")) { runtime_animation_quality = animation_quality_auto; recognized = true; }
    else if (config_value_equal(text, size, "animation_quality", "full")) { runtime_animation_quality = animation_quality_full; recognized = true; }
    if (config_value_equal(text, size, "sounds", "off")) { runtime_sound_theme = system_sound::theme_off; recognized = true; }
    else if (config_value_equal(text, size, "sounds", "soft")) { runtime_sound_theme = system_sound::theme_soft; recognized = true; }
    else if (config_value_equal(text, size, "sounds", "clear")) { runtime_sound_theme = system_sound::theme_clear; recognized = true; }
    if (config_value_equal(text, size, "feedback", "quiet")) { runtime_feedback_level = system_sound::feedback_quiet; recognized = true; }
    else if (config_value_equal(text, size, "feedback", "normal")) { runtime_feedback_level = system_sound::feedback_normal; recognized = true; }
    else if (config_value_equal(text, size, "feedback", "pronounced")) { runtime_feedback_level = system_sound::feedback_pronounced; recognized = true; }
    synchronize_motion_compatibility();
""",
    "parsed legacy and advanced experience preferences",
)
replace_once(
    "kernel/parts/ui_theme.inc",
    "    materialize_theme(custom_colors);\n    runtime_preferences_loaded = true;\n",
    "    materialize_theme(custom_colors);\n"
    "    synchronize_motion_compatibility();\n"
    "    system_sound::configure(runtime_sound_theme, runtime_feedback_level);\n"
    "    runtime_preferences_loaded = true;\n",
    "applied sound and motion preferences during runtime load",
)
replace_once(
    "kernel/parts/ui_theme.inc",
    "    char data[384]{}; uint32_t size = 0;\n",
    "    char data[512]{}; uint32_t size = 0;\n",
    "expanded runtime preference read buffer",
)
replace_once(
    "kernel/parts/ui_theme.inc",
    "    char text[384]{}; uint32_t length = 0;\n",
    "    char text[512]{}; uint32_t length = 0;\n",
    "expanded runtime preference write buffer",
)
replace_once(
    "kernel/parts/ui_theme.inc",
    """        !append_config_text(text, sizeof(text), length, "\nanimations=") ||
        !append_config_text(text, sizeof(text), length, desktop_animations ? "1\n" : "0\n") ||
        !append_config_text(text, sizeof(text), length, "cursor=") ||
""",
    """        !append_config_text(text, sizeof(text), length, "\nanimations=") ||
        !append_config_text(text, sizeof(text), length, desktop_animations ? "1\n" : "0\n") ||
        !append_config_text(text, sizeof(text), length, "motion=") ||
        !append_config_text(text, sizeof(text), length,
            runtime_motion_profile == motion_profile_reduced ? "reduced" :
            (runtime_motion_profile == motion_profile_calm ? "calm" :
            (runtime_motion_profile == motion_profile_snappy ? "snappy" : "fluid"))) ||
        !append_config_text(text, sizeof(text), length, "\nmotion_speed=") ||
        !append_config_text(text, sizeof(text), length,
            runtime_motion_speed == motion_speed_relaxed ? "relaxed" :
            (runtime_motion_speed == motion_speed_fast ? "fast" : "normal")) ||
        !append_config_text(text, sizeof(text), length, "\nanimation_quality=") ||
        !append_config_text(text, sizeof(text), length,
            runtime_animation_quality == animation_quality_full ? "full" : "auto") ||
        !append_config_text(text, sizeof(text), length, "\nsounds=") ||
        !append_config_text(text, sizeof(text), length,
            runtime_sound_theme == system_sound::theme_off ? "off" :
            (runtime_sound_theme == system_sound::theme_clear ? "clear" : "soft")) ||
        !append_config_text(text, sizeof(text), length, "\nfeedback=") ||
        !append_config_text(text, sizeof(text), length,
            runtime_feedback_level == system_sound::feedback_quiet ? "quiet" :
            (runtime_feedback_level == system_sound::feedback_pronounced ? "pronounced" : "normal")) ||
        !append_config_text(text, sizeof(text), length, "\ncursor=") ||
""",
    "persisted advanced experience preferences",
)

# Add a dedicated sound glyph to the unified icon language.
replace_once(
    "kernel/parts/ui_theme.inc",
    "    theme, motion, contrast, density, taskbar, count\n",
    "    theme, motion, contrast, density, taskbar, sound, count\n",
    "added sound icon identity",
)
replace_once(
    "kernel/parts/ui_theme.inc",
    """    if (icon == ShellIcon::taskbar) {
        draw_icon_round_outline(x + 3, y + 4, 18, 16, 3, color);
        draw_icon_line(x + 4, y + 16, x + 19, y + 16, color);
        return;
    }
}
""",
    """    if (icon == ShellIcon::taskbar) {
        draw_icon_round_outline(x + 3, y + 4, 18, 16, 3, color);
        draw_icon_line(x + 4, y + 16, x + 19, y + 16, color);
        return;
    }
    if (icon == ShellIcon::sound) {
        draw_icon_line(x + 3, y + 10, x + 7, y + 10, color);
        draw_icon_line(x + 7, y + 10, x + 12, y + 5, color);
        draw_icon_line(x + 12, y + 5, x + 12, y + 19, color);
        draw_icon_line(x + 12, y + 19, x + 7, y + 14, color);
        draw_icon_line(x + 7, y + 14, x + 3, y + 14, color);
        draw_icon_line(x + 16, y + 9, x + 18, y + 12, color);
        draw_icon_line(x + 18, y + 12, x + 16, y + 15, color);
        draw_icon_line(x + 19, y + 6, x + 22, y + 12, color);
        draw_icon_line(x + 22, y + 12, x + 19, y + 18, color);
        return;
    }
}
""",
    "rendered dedicated sound icon",
)

# Settings surface: five pages and a focused Experience panel.
replace_once(
    "kernel/parts/ui_apps.inc",
    "constexpr int32_t settings_page_y_offsets[4] = {96, 134, 172, 210};\n",
    "constexpr int32_t settings_page_y_offsets[5] = {96, 134, 172, 210, 248};\n",
    "expanded sidebar geometry",
)
replace_once(
    "kernel/parts/ui_apps.inc",
    "constexpr int32_t settings_access_motion_y_offset = 200;\nconstexpr int32_t settings_reset_y_offset = 314;\n",
    """constexpr int32_t settings_access_motion_y_offset = 200;
constexpr int32_t settings_experience_motion_y_offset = 106;
constexpr int32_t settings_experience_speed_y_offset = 148;
constexpr int32_t settings_experience_sound_y_offset = 190;
constexpr int32_t settings_experience_feedback_y_offset = 232;
constexpr int32_t settings_experience_quality_y_offset = 274;
constexpr int32_t settings_experience_preview_y_offset = 326;
constexpr int32_t settings_reset_y_offset = 314;
""",
    "added aligned Experience control geometry",
)
replace_once(
    "kernel/parts/ui_apps.inc",
    """uint8_t settings_focus_count() {
    if (settings_page == 2U) return 3U;
    if (settings_page == 3U) return 1U;
    return 6U;
}
""",
    """uint8_t settings_focus_count() {
    if (settings_page == 2U) return 3U;
    if (settings_page == 3U) return 6U;
    if (settings_page == 4U) return 1U;
    return 6U;
}
""",
    "updated per-page focus counts",
)
replace_once(
    "kernel/parts/ui_apps.inc",
    """    static constexpr HoverTarget targets[settings_page_count] = {
        HoverTarget::settings_page_general, HoverTarget::settings_page_style,
        HoverTarget::settings_page_access, HoverTarget::settings_page_about
    };
""",
    """    static constexpr HoverTarget targets[settings_page_count] = {
        HoverTarget::settings_page_general, HoverTarget::settings_page_style,
        HoverTarget::settings_page_access, HoverTarget::settings_page_experience,
        HoverTarget::settings_page_about
    };
""",
    "updated sidebar navigation targets",
)
replace_once(
    "kernel/parts/ui_apps.inc",
    """    draw_settings_page_button(2, "Access", ShellIcon::display);
    draw_settings_page_button(3, "About", ShellIcon::start);
""",
    """    draw_settings_page_button(2, "Access", ShellIcon::display);
    draw_settings_page_button(3, "Experience", ShellIcon::sound);
    draw_settings_page_button(4, "About", ShellIcon::start);
""",
    "rendered Experience sidebar destination",
)
replace_once(
    "kernel/parts/ui_apps.inc",
    '    draw_settings_row(window_y + settings_motion_y_offset, "Motion", desktop_animations ? "Smooth" : "Reduced", 4U, HoverTarget::settings_motion);\n',
    '    draw_settings_row(window_y + settings_motion_y_offset, "Motion", motion_profile_name(), 4U, HoverTarget::settings_motion);\n',
    "showed selected motion profile in General",
)
replace_once(
    "kernel/parts/ui_apps.inc",
    '    draw_settings_row(window_y + settings_access_motion_y_offset + 34, "Motion", desktop_animations ? "Smooth" : "Reduced", 2U, HoverTarget::settings_access_motion);\n',
    '    draw_settings_row(window_y + settings_access_motion_y_offset + 34, "Motion", motion_profile_name(), 2U, HoverTarget::settings_access_motion);\n',
    "showed selected motion profile in Accessibility",
)
replace_once(
    "kernel/parts/ui_apps.inc",
    "\nvoid render_settings_about() {\n",
    """
void render_settings_experience() {
    draw_text(window_x + settings_content_x_offset, window_y + 68, "Experience", current_theme.text, 2);
    draw_settings_row(window_y + settings_experience_motion_y_offset, "Motion style", motion_profile_name(), 0U, HoverTarget::settings_motion_profile);
    draw_settings_row(window_y + settings_experience_speed_y_offset, "Transition speed", motion_speed_name(), 1U, HoverTarget::settings_motion_speed);
    draw_settings_row(window_y + settings_experience_sound_y_offset, "System sounds", sound_theme_name(), 2U, HoverTarget::settings_sound_theme);
    draw_settings_row(window_y + settings_experience_feedback_y_offset, "Feedback strength", feedback_level_name(), 3U, HoverTarget::settings_feedback);
    draw_settings_row(window_y + settings_experience_quality_y_offset, "Animation quality", animation_quality_name(), 4U, HoverTarget::settings_animation_quality);
    draw_settings_row(window_y + settings_experience_preview_y_offset, "Preview sound", "Play", 5U, HoverTarget::settings_sound_preview);
}

void render_settings_about() {
""",
    "added Experience settings content",
)
replace_once(
    "kernel/parts/ui_apps.inc",
    """void render_settings() {
    const char* section = settings_page == 0U ? "General" : (settings_page == 1U ? "Style" : (settings_page == 2U ? "Access" : "About"));
    draw_window_chrome("Settings", section);
    draw_settings_sidebar();
    if (settings_page == 0U) render_settings_general();
    else if (settings_page == 1U) render_settings_style();
    else if (settings_page == 2U) render_settings_access();
    else render_settings_about();
}
""",
    """void render_settings() {
    const char* section = settings_page == 0U ? "General" :
        (settings_page == 1U ? "Style" :
        (settings_page == 2U ? "Access" :
        (settings_page == 3U ? "Experience" : "About")));
    draw_window_chrome("Settings", section);
    draw_settings_sidebar();
    if (settings_page == 0U) render_settings_general();
    else if (settings_page == 1U) render_settings_style();
    else if (settings_page == 2U) render_settings_access();
    else if (settings_page == 3U) render_settings_experience();
    else render_settings_about();
}
""",
    "routed five settings pages",
)

# Runtime motion model: bounded profile-specific easing and explicit sound feedback.
replace_once(
    "kernel/parts/ui_runtime.inc",
    """constexpr uint8_t transition_frame_count(bool animations_enabled) {
    return animations_enabled ? 4U : 1U;
}

constexpr bool reduced_motion_contract_ok() {
    return transition_frame_count(true) == 4U && transition_frame_count(false) == 1U;
}
""",
    """constexpr uint8_t transition_frame_count_for(uint8_t profile, uint8_t speed, uint8_t quality, bool high_capacity) {
    if (profile == motion_profile_reduced) return 1U;
    uint8_t frames = profile == motion_profile_calm ? 5U : (profile == motion_profile_snappy ? 4U : 7U);
    if (speed == motion_speed_relaxed && frames < 8U) frames = static_cast<uint8_t>(frames + 1U);
    else if (speed == motion_speed_fast && frames > 3U) frames = static_cast<uint8_t>(frames - 1U);
    if (quality == animation_quality_auto && !high_capacity && frames > 5U) frames = 5U;
    return frames;
}

constexpr uint8_t transition_tick_delay_for(uint8_t profile, uint8_t speed) {
    if (profile == motion_profile_reduced) return 0U;
    return speed == motion_speed_relaxed ? 2U : 1U;
}

constexpr int32_t transition_distance_for(uint8_t profile) {
    return profile == motion_profile_calm ? 8 : (profile == motion_profile_snappy ? 10 : 14);
}

int32_t transition_offset_for_frame(uint8_t frame, uint8_t frame_count, uint8_t profile) {
    if (frame_count <= 1U || frame + 1U >= frame_count) return 0;
    const uint32_t denominator = static_cast<uint32_t>(frame_count - 1U);
    const uint32_t remaining = static_cast<uint32_t>(frame_count - 1U - frame);
    const uint32_t numerator = remaining * remaining * remaining;
    const uint32_t scale = denominator * denominator * denominator;
    return static_cast<int32_t>((static_cast<uint32_t>(transition_distance_for(profile)) * numerator + scale / 2U) / scale);
}

uint8_t transition_frame_count_current() {
    const bool high_capacity = available_framebuffer_bytes > fallback_framebuffer_bytes;
    return transition_frame_count_for(runtime_motion_profile, runtime_motion_speed, runtime_animation_quality, high_capacity);
}

uint8_t transition_tick_delay_current() {
    return transition_tick_delay_for(runtime_motion_profile, runtime_motion_speed);
}

bool motion_profile_contract_ok() {
    if (transition_frame_count_for(motion_profile_reduced, motion_speed_relaxed, animation_quality_full, true) != 1U) return false;
    if (transition_frame_count_for(motion_profile_fluid, motion_speed_normal, animation_quality_auto, true) != 7U) return false;
    if (transition_frame_count_for(motion_profile_fluid, motion_speed_normal, animation_quality_auto, false) != 5U) return false;
    if (transition_frame_count_for(motion_profile_snappy, motion_speed_fast, animation_quality_full, true) != 3U) return false;
    for (uint8_t profile = motion_profile_calm; profile < motion_profile_count; ++profile) {
        const uint8_t frames = transition_frame_count_for(profile, motion_speed_relaxed, animation_quality_full, true);
        if (frames < 3U || frames > 9U) return false;
        int32_t previous = transition_distance_for(profile) + 1;
        for (uint8_t frame = 0U; frame < frames; ++frame) {
            const int32_t offset = transition_offset_for_frame(frame, frames, profile);
            if (offset < 0 || offset >= previous) return false;
            previous = offset;
        }
        if (previous != 0) return false;
    }
    return transition_tick_delay_for(motion_profile_calm, motion_speed_relaxed) == 2U &&
        transition_tick_delay_for(motion_profile_fluid, motion_speed_normal) == 1U &&
        transition_tick_delay_for(motion_profile_snappy, motion_speed_fast) == 1U;
}

constexpr bool reduced_motion_contract_ok() {
    return transition_frame_count_for(motion_profile_reduced, motion_speed_normal, animation_quality_auto, true) == 1U;
}
""",
    "installed bounded motion profiles and cubic easing",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """void toggle_motion() {
    desktop_animations = !desktop_animations;
    if (!desktop_animations) {
        scene_transition_pending = false;
        ui_scene_offset_y = 0;
    }
    persist_personalization();
    serial::line(desktop_animations ? "UI_REDUCED_MOTION_OFF" : "UI_REDUCED_MOTION_ON");
}
""",
    """void apply_motion_profile(uint8_t profile) {
    runtime_motion_profile = profile < motion_profile_count ? profile : motion_profile_fluid;
    synchronize_motion_compatibility();
    if (runtime_motion_profile == motion_profile_reduced) {
        scene_transition_pending = false;
        ui_scene_offset_y = 0;
    }
    persist_personalization();
    serial::line(runtime_motion_profile == motion_profile_reduced ? "UI_REDUCED_MOTION_ON" : "UI_REDUCED_MOTION_OFF");
}

void toggle_motion() {
    apply_motion_profile(runtime_motion_profile == motion_profile_reduced ? motion_profile_fluid : motion_profile_reduced);
}

void cycle_motion_profile(bool forward) {
    const uint8_t next = forward
        ? static_cast<uint8_t>((runtime_motion_profile + 1U) % motion_profile_count)
        : static_cast<uint8_t>((runtime_motion_profile + motion_profile_count - 1U) % motion_profile_count);
    apply_motion_profile(next);
}

void cycle_motion_speed(bool forward) {
    runtime_motion_speed = forward
        ? static_cast<uint8_t>((runtime_motion_speed + 1U) % motion_speed_count)
        : static_cast<uint8_t>((runtime_motion_speed + motion_speed_count - 1U) % motion_speed_count);
    persist_personalization();
}

void cycle_animation_quality() {
    runtime_animation_quality = runtime_animation_quality == animation_quality_auto
        ? animation_quality_full : animation_quality_auto;
    persist_personalization();
}

void cycle_sound_theme(bool forward) {
    runtime_sound_theme = forward
        ? static_cast<uint8_t>((runtime_sound_theme + 1U) % system_sound::theme_count)
        : static_cast<uint8_t>((runtime_sound_theme + system_sound::theme_count - 1U) % system_sound::theme_count);
    system_sound::configure(runtime_sound_theme, runtime_feedback_level);
    persist_personalization();
    if (runtime_sound_theme != system_sound::theme_off) system_sound::play(system_sound::Event::toggle);
}

void cycle_feedback_level(bool forward) {
    runtime_feedback_level = forward
        ? static_cast<uint8_t>((runtime_feedback_level + 1U) % system_sound::feedback_count)
        : static_cast<uint8_t>((runtime_feedback_level + system_sound::feedback_count - 1U) % system_sound::feedback_count);
    system_sound::configure(runtime_sound_theme, runtime_feedback_level);
    persist_personalization();
    system_sound::play(system_sound::Event::toggle);
}
""",
    "added profile, speed, quality and sound controls",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """    runtime_high_contrast = false; runtime_large_pointer = false;
    desktop_radius = 12U; desktop_opacity = 240U; desktop_animations = true; terminal_cursor = "beam";
    select_runtime_theme(0U); persist_personalization();
""",
    """    runtime_high_contrast = false; runtime_large_pointer = false;
    runtime_motion_profile = motion_profile_fluid; runtime_motion_speed = motion_speed_normal;
    runtime_animation_quality = animation_quality_auto;
    runtime_sound_theme = system_sound::theme_soft; runtime_feedback_level = system_sound::feedback_normal;
    desktop_radius = 12U; desktop_opacity = 240U; terminal_cursor = "beam";
    synchronize_motion_compatibility();
    system_sound::configure(runtime_sound_theme, runtime_feedback_level);
    select_runtime_theme(0U); persist_personalization();
""",
    "reset Experience defaults safely",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    "    const bool iconography_ok = iconography_contract_ok();\n",
    "    const bool iconography_ok = iconography_contract_ok();\n"
    "    const bool motion_profiles_ok = motion_profile_contract_ok();\n"
    "    const bool sound_contract_ok = system_sound::contract_ok();\n"
    "    const bool experience_settings_ok = settings_page_count == 5U && motion_profile_count == 4U &&\n"
    "        motion_speed_count == 3U && animation_quality_count == 2U &&\n"
    "        system_sound::theme_count == 3U && system_sound::feedback_count == 3U;\n",
    "evaluated Experience runtime contracts",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    '    serial::line(iconography_ok ? "UI_ICON_SYSTEM_OK" : "UI_ICON_SYSTEM_FAILED");\n',
    '    serial::line(iconography_ok ? "UI_ICON_SYSTEM_OK" : "UI_ICON_SYSTEM_FAILED");\n'
    '    serial::line(motion_profiles_ok ? "UI_MOTION_PROFILES_OK" : "UI_MOTION_PROFILES_FAILED");\n'
    '    serial::line(sound_contract_ok ? "UI_SOUND_CONTRACT_OK" : "UI_SOUND_CONTRACT_FAILED");\n'
    '    serial::line(experience_settings_ok ? "UI_EXPERIENCE_SETTINGS_OK" : "UI_EXPERIENCE_SETTINGS_FAILED");\n',
    "reported Experience runtime contracts",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    '    serial::write("UI_ICON_COUNT "); serial_uint(static_cast<uint8_t>(ShellIcon::count)); serial::write("\\r\\n");\n',
    '    serial::write("UI_ICON_COUNT "); serial_uint(static_cast<uint8_t>(ShellIcon::count)); serial::write("\\r\\n");\n'
    '    serial::write("UI_MOTION_PROFILE "); serial::write(motion_profile_name()); serial::write("\\r\\n");\n'
    '    serial::write("UI_SOUND_THEME "); serial::write(sound_theme_name()); serial::write("\\r\\n");\n',
    "reported active Experience defaults",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    "    const bool navigation_ok = launcher_item_count == 6U && settings_page_count == 4U && quick_settings_count == 6U &&\n",
    "    const bool navigation_ok = launcher_item_count == 6U && settings_page_count == 5U && quick_settings_count == 6U &&\n",
    "updated navigation page contract",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """        font_atlas_ok && font_weight_ok && font_metrics_ok && iconography_ok &&
        text_ellipsis_ok && clipping_safety_ok && color_mix_ok && accessibility_v4_ok &&
""",
    """        font_atlas_ok && font_weight_ok && font_metrics_ok && iconography_ok &&
        motion_profiles_ok && sound_contract_ok && experience_settings_ok &&
        text_ellipsis_ok && clipping_safety_ok && color_mix_ok && accessibility_v4_ok &&
""",
    "made Experience contracts boot-critical",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """void wait_animation_tick() {
    const uint32_t start = timer_ticks;
    uint32_t spins = 0;
    while (timer_ticks == start && spins++ < 2000000U) asm volatile("pause");
}
""",
    """void wait_animation_ticks(uint8_t ticks) {
    for (uint8_t tick = 0U; tick < ticks; ++tick) {
        const uint32_t start = timer_ticks;
        uint32_t spins = 0U;
        while (timer_ticks == start && spins++ < 2000000U) asm volatile("pause");
    }
}
""",
    "bounded animation pacing by profile speed",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """void refresh_desktop() {
    const bool transition_requested = scene_transition_pending;
    const uint8_t frame_count = transition_frame_count(desktop_animations);
    if (transition_requested && frame_count > 1U) {
        static constexpr int32_t offsets[4] = {14, 8, 3, 0};
        for (uint32_t i = 0; i < frame_count; ++i) {
            ui_scene_offset_y = offsets[i]; render_scene(); present_full();
            if (i + 1U < frame_count) wait_animation_tick();
        }
        ui_scene_offset_y = 0;
        scene_transition_pending = false;
    } else {
        scene_transition_pending = false;
        ui_scene_offset_y = 0;
        render_scene();
        present_full();
    }
    if (transition_requested) {
        serial::write("UI_TRANSITION_FRAMES ");
        serial_uint(transition_requested ? frame_count : 1U);
        serial::write("\r\n");
    }
    serial::line("UI_FRAME_PRESENTED");
}
""",
    """void refresh_desktop() {
    const bool transition_requested = scene_transition_pending;
    const uint8_t frame_count = transition_frame_count_current();
    const uint8_t tick_delay = transition_tick_delay_current();
    if (transition_requested && frame_count > 1U) {
        for (uint8_t frame = 0U; frame < frame_count; ++frame) {
            ui_scene_offset_y = transition_offset_for_frame(frame, frame_count, runtime_motion_profile);
            render_scene();
            present_full();
            if (frame + 1U < frame_count) wait_animation_ticks(tick_delay);
        }
        ui_scene_offset_y = 0;
        scene_transition_pending = false;
    } else {
        scene_transition_pending = false;
        ui_scene_offset_y = 0;
        render_scene();
        present_full();
    }
    if (transition_requested) {
        serial::write("UI_TRANSITION_FRAMES ");
        serial_uint(frame_count);
        serial::write("\r\n");
        serial::write("UI_TRANSITION_TICKS ");
        serial_uint(tick_delay);
        serial::write("\r\n");
    }
    serial::line("UI_FRAME_PRESENTED");
}
""",
    "rendered profile-specific eased transitions",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """void toggle_start_menu() {
    if (active_view == View::launcher) set_active_view(launcher_return_view);
    else set_active_view(View::launcher);
    refresh_desktop();
""",
    """void toggle_start_menu() {
    const bool opening = active_view != View::launcher;
    if (!opening) set_active_view(launcher_return_view);
    else set_active_view(View::launcher);
    system_sound::play(opening ? system_sound::Event::surface_open : system_sound::Event::surface_close);
    refresh_desktop();
""",
    "added explicit launcher open-close sound feedback",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """    quick_settings_open = !quick_settings_open;
    hovered_target = HoverTarget::none;
    refresh_desktop();
""",
    """    quick_settings_open = !quick_settings_open;
    hovered_target = HoverTarget::none;
    system_sound::play(quick_settings_open ? system_sound::Event::surface_open : system_sound::Event::surface_close);
    refresh_desktop();
""",
    "added explicit control center sound feedback",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """    status_center_open = !status_center_open;
    hovered_target = HoverTarget::none;
    refresh_desktop();
""",
    """    status_center_open = !status_center_open;
    hovered_target = HoverTarget::none;
    system_sound::play(status_center_open ? system_sound::Event::surface_open : system_sound::Event::surface_close);
    refresh_desktop();
""",
    "added explicit status center sound feedback",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    "void open_launcher_item(uint8_t index) {\n",
    "void open_launcher_item(uint8_t index) {\n    system_sound::play(system_sound::Event::launch);\n",
    "added launch confirmation sound",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """void change_settings_page(bool forward) {
    settings_page = forward ? static_cast<uint8_t>((settings_page + 1U) % settings_page_count)
                            : static_cast<uint8_t>((settings_page + settings_page_count - 1U) % settings_page_count);
    settings_focus = 0U;
}
""",
    """void change_settings_page(bool forward) {
    settings_page = forward ? static_cast<uint8_t>((settings_page + 1U) % settings_page_count)
                            : static_cast<uint8_t>((settings_page + settings_page_count - 1U) % settings_page_count);
    settings_focus = 0U;
    system_sound::play(system_sound::Event::select);
}
""",
    "added settings page selection feedback",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """bool apply_quick_setting(uint8_t index, bool forward) {
    if (index == 0U) cycle_runtime_theme(forward);
    else if (index == 1U) return forward ? cycle_display_mode() : previous_display_mode();
    else if (index == 2U) toggle_motion();
    else if (index == 3U) toggle_high_contrast();
    else if (index == 4U) toggle_density();
    else if (index == 5U) toggle_taskbar_alignment();
    else return false;
    refresh_desktop(); return true;
}
""",
    """bool apply_quick_setting(uint8_t index, bool forward) {
    if (index == 0U) cycle_runtime_theme(forward);
    else if (index == 1U) {
        const bool changed = forward ? cycle_display_mode() : previous_display_mode();
        if (changed) system_sound::play(system_sound::Event::toggle);
        return changed;
    } else if (index == 2U) toggle_motion();
    else if (index == 3U) toggle_high_contrast();
    else if (index == 4U) toggle_density();
    else if (index == 5U) toggle_taskbar_alignment();
    else return false;
    system_sound::play(system_sound::Event::toggle);
    refresh_desktop(); return true;
}
""",
    "added invocation-only quick-setting sound feedback",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """    } else if (settings_page == 2U) {
        if (settings_focus == 0U) toggle_high_contrast();
        else if (settings_focus == 1U) toggle_large_pointer();
        else toggle_motion();
    } else reset_personalization();
    refresh_desktop(); return true;
}
""",
    """    } else if (settings_page == 2U) {
        if (settings_focus == 0U) toggle_high_contrast();
        else if (settings_focus == 1U) toggle_large_pointer();
        else toggle_motion();
    } else if (settings_page == 3U) {
        if (settings_focus == 0U) cycle_motion_profile(forward);
        else if (settings_focus == 1U) cycle_motion_speed(forward);
        else if (settings_focus == 2U) cycle_sound_theme(forward);
        else if (settings_focus == 3U) cycle_feedback_level(forward);
        else if (settings_focus == 4U) cycle_animation_quality();
        else { system_sound::preview(); refresh_desktop(); return true; }
    } else reset_personalization();
    system_sound::play(system_sound::Event::toggle);
    refresh_desktop(); return true;
}
""",
    "wired Experience controls",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """        } else if (settings_page == 2U) {
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_access_contrast_y_offset + 24, settings_content_width, 36)) return HoverTarget::settings_contrast;
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_access_pointer_y_offset + 24, settings_content_width, 36)) return HoverTarget::settings_pointer;
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_access_motion_y_offset + 24, settings_content_width, 36)) return HoverTarget::settings_access_motion;
        } else if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_reset_y_offset, settings_content_width, 46)) return HoverTarget::settings_reset;
""",
    """        } else if (settings_page == 2U) {
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_access_contrast_y_offset + 24, settings_content_width, 36)) return HoverTarget::settings_contrast;
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_access_pointer_y_offset + 24, settings_content_width, 36)) return HoverTarget::settings_pointer;
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_access_motion_y_offset + 24, settings_content_width, 36)) return HoverTarget::settings_access_motion;
        } else if (settings_page == 3U) {
            const int32_t rows[6] = {
                settings_experience_motion_y_offset, settings_experience_speed_y_offset,
                settings_experience_sound_y_offset, settings_experience_feedback_y_offset,
                settings_experience_quality_y_offset, settings_experience_preview_y_offset
            };
            const HoverTarget targets[6] = {
                HoverTarget::settings_motion_profile, HoverTarget::settings_motion_speed,
                HoverTarget::settings_sound_theme, HoverTarget::settings_feedback,
                HoverTarget::settings_animation_quality, HoverTarget::settings_sound_preview
            };
            for (uint8_t index = 0U; index < 6U; ++index)
                if (hit(x, y, window_x + settings_content_x_offset, window_y + rows[index] - 10, settings_content_width, 36))
                    return targets[index];
        } else if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_reset_y_offset, settings_content_width, 46)) return HoverTarget::settings_reset;
""",
    "added Experience pointer hit targets",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """        } else if (settings_page == 2U) {
            const int32_t content_x = window_x + settings_content_x_offset;
            const int32_t rows[3] = {settings_access_contrast_y_offset + 34, settings_access_pointer_y_offset + 34, settings_access_motion_y_offset + 34};
            for (uint8_t i = 0; i < 3U; ++i) if (hit(mouse_x, mouse_y, content_x, window_y + rows[i] - 10, settings_content_width, 36)) {
                settings_focus = i; return apply_settings_control(true);
            }
        } else if (hit(mouse_x, mouse_y, window_x + settings_content_x_offset, window_y + settings_reset_y_offset, settings_content_width, 46)) {
""",
    """        } else if (settings_page == 2U) {
            const int32_t content_x = window_x + settings_content_x_offset;
            const int32_t rows[3] = {settings_access_contrast_y_offset + 34, settings_access_pointer_y_offset + 34, settings_access_motion_y_offset + 34};
            for (uint8_t i = 0; i < 3U; ++i) if (hit(mouse_x, mouse_y, content_x, window_y + rows[i] - 10, settings_content_width, 36)) {
                settings_focus = i; return apply_settings_control(true);
            }
        } else if (settings_page == 3U) {
            const int32_t content_x = window_x + settings_content_x_offset;
            const int32_t rows[6] = {
                settings_experience_motion_y_offset, settings_experience_speed_y_offset,
                settings_experience_sound_y_offset, settings_experience_feedback_y_offset,
                settings_experience_quality_y_offset, settings_experience_preview_y_offset
            };
            for (uint8_t i = 0U; i < 6U; ++i) if (hit(mouse_x, mouse_y, content_x, window_y + rows[i] - 10, settings_content_width, 36)) {
                settings_focus = i; return apply_settings_control(true);
            }
        } else if (hit(mouse_x, mouse_y, window_x + settings_content_x_offset, window_y + settings_reset_y_offset, settings_content_width, 46)) {
""",
    "wired Experience pointer activation",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    '        if (key == key_escape) { quick_settings_open = false; refresh_desktop(); return true; }\n',
    '        if (key == key_escape) { quick_settings_open = false; system_sound::play(system_sound::Event::surface_close); refresh_desktop(); return true; }\n',
    "added keyboard close sound for control center",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """    if (status_center_open) {
        if (key == key_escape || key == '\n' || key == ' ') { status_center_open = false; refresh_desktop(); }
        return true;
    }
""",
    """    if (status_center_open) {
        if (key == key_escape || key == '\n' || key == ' ') {
            status_center_open = false;
            system_sound::play(system_sound::Event::surface_close);
            refresh_desktop();
        }
        return true;
    }
""",
    "added keyboard close sound for status center",
)
replace_once(
    "kernel/parts/ui_runtime.inc",
    """    if (key == key_escape) { set_active_view(active_view == View::launcher ? launcher_return_view : View::terminal); refresh_desktop(); return true; }
""",
    """    if (key == key_escape) {
        const bool closing_surface = active_view == View::launcher;
        set_active_view(closing_surface ? launcher_return_view : View::terminal);
        if (closing_surface) system_sound::play(system_sound::Event::surface_close);
        refresh_desktop();
        return true;
    }
""",
    "added keyboard launcher close sound",
)

# The icon contract now includes the dedicated sound icon.
replace_once(
    "kernel/parts/ui_runtime.inc",
    """        settings_taskbar_y_offset + 26 <= window_height &&
        settings_reset_y_offset + 46 <= window_height &&
""",
    """        settings_taskbar_y_offset + 26 <= window_height &&
        settings_experience_preview_y_offset + 26 <= window_height &&
        settings_reset_y_offset + 46 <= window_height &&
""",
    "extended focus visibility contract for Experience controls",
)

replace_once(
    "kernel/parts/ui_runtime.inc",
    "    return valid && icon_count == 13U && minimum_ink >= 30U &&\n",
    "    return valid && icon_count == 14U && minimum_ink >= 30U &&\n",
    "updated iconography contract count",
)

# QEMU evidence: verify default Fluid profile, all contracts and Experience controls.
replace_once(
    "tests/qemu_display_ui.sh",
    '  wait_for_serial "UI_ICON_COUNT 13" || { echo quit; return 1; }\n',
    '  wait_for_serial "UI_ICON_COUNT 14" || { echo quit; return 1; }\n'
    '  wait_for_serial "UI_SOUND_ENGINE_OK" || { echo quit; return 1; }\n'
    '  wait_for_serial "UI_SOUND_CONTRACT_OK" || { echo quit; return 1; }\n'
    '  wait_for_serial "UI_MOTION_PROFILES_OK" || { echo quit; return 1; }\n'
    '  wait_for_serial "UI_EXPERIENCE_SETTINGS_OK" || { echo quit; return 1; }\n'
    '  wait_for_serial "UI_MOTION_PROFILE Fluid" || { echo quit; return 1; }\n'
    '  wait_for_serial "UI_SOUND_THEME Soft" || { echo quit; return 1; }\n',
    "required Experience boot evidence",
)
replace_once(
    "tests/qemu_display_ui.sh",
    '  wait_for_serial "UI_TRANSITION_FRAMES 4" || { echo quit; return 1; }\n',
    '  wait_for_serial "UI_TRANSITION_FRAMES 7" || { echo quit; return 1; }\n'
    '  wait_for_serial "UI_TRANSITION_TICKS 1" || { echo quit; return 1; }\n',
    "verified default Fluid motion profile",
)
replace_once(
    "tests/qemu_display_ui.sh",
    """  capture_mode reduced-motion-start-1024x768
  echo "sendkey esc 10"
  sleep 0.15

  echo "sendkey f8 10"
""",
    """  capture_mode reduced-motion-start-1024x768
  echo "sendkey esc 10"
  sleep 0.15

  echo "sendkey end 10"
  sleep 0.25
  capture_mode settings-experience-1024x768
  echo "sendkey tab 10"
  echo "sendkey tab 10"
  echo "sendkey tab 10"
  echo "sendkey tab 10"
  echo "sendkey tab 10"
  echo "sendkey ret 10"
  wait_for_serial "UI_SOUND_PREVIEW_OK" || { echo quit; return 1; }
  sleep 0.2
  capture_mode settings-experience-preview-1024x768

  echo "sendkey f8 10"
""",
    "exercised Experience page and sound preview",
)
replace_once(
    "tests/qemu_display_ui.sh",
    'check_ppm "$OUT/reduced-motion-start-1024x768.ppm" "1024 768"\n',
    'check_ppm "$OUT/reduced-motion-start-1024x768.ppm" "1024 768"\n'
    'check_ppm "$OUT/settings-experience-1024x768.ppm" "1024 768"\n'
    'check_ppm "$OUT/settings-experience-preview-1024x768.ppm" "1024 768"\n',
    "validated Experience screenshots",
)
replace_once(
    "tests/qemu_display_ui.sh",
    """check_distinct "$OUT/high-contrast-1024x768.ppm" "$OUT/large-pointer-1024x768.ppm"
check_distinct "$OUT/large-pointer-1024x768.ppm" "$OUT/reduced-motion-start-1024x768.ppm"
""",
    """check_distinct "$OUT/high-contrast-1024x768.ppm" "$OUT/large-pointer-1024x768.ppm"
check_distinct "$OUT/large-pointer-1024x768.ppm" "$OUT/reduced-motion-start-1024x768.ppm"
check_distinct "$OUT/settings-accessibility-1024x768.ppm" "$OUT/settings-experience-1024x768.ppm"
""",
    "proved Experience page is visually distinct",
)

# Adaptive display workflow checks the new contracts on every future PR.
replace_once(
    ".github/workflows/adaptive-display.yml",
    """            UI_FONT_WEIGHT_OK UI_ICON_SYSTEM_OK 'UI_FONT_GLYPH_COUNT 95' 'UI_ICON_COUNT 13' \\
""",
    """            UI_FONT_WEIGHT_OK UI_ICON_SYSTEM_OK 'UI_FONT_GLYPH_COUNT 95' 'UI_ICON_COUNT 14' \\
            UI_SOUND_ENGINE_OK UI_SOUND_CONTRACT_OK UI_MOTION_PROFILES_OK UI_EXPERIENCE_SETTINGS_OK \\
            'UI_MOTION_PROFILE Fluid' 'UI_SOUND_THEME Soft' \\
""",
    "extended permanent Adaptive Display gates",
)

# Durable architecture note.
Path("docs/UX_EXPERIENCE_0.1.1.md").write_text(
    """# ZenovOS 0.1.1 Experience Settings

## Scope

This pass adds a bounded native experience layer without importing Hyprland,
Quickshell, QML, PulseAudio or a Linux compositor runtime.

## Motion

The renderer exposes four profiles:

- Reduced: one frame and no animation wait.
- Calm: short low-distance easing.
- Fluid: the default seven-frame cubic ease-out profile.
- Snappy: fewer frames and a shorter travel distance.

Transition speed is independently configurable as Relaxed, Normal or Fast.
Animation quality is Auto by default and caps expensive transitions on the
16 MiB framebuffer fallback. Full keeps the selected profile's complete frame
budget. Every profile is bounded to at most nine frames and two PIT ticks per
frame.

## Sound

The PC speaker engine uses PIT channel 2 and runs from the existing 100 Hz IRQ0
tick. UI code only queues the latest event and never busy-waits for sound.

Sound themes:

- Off
- Soft
- Clear

Feedback levels:

- Quiet
- Normal
- Pronounced

Sounds are emitted only for explicit invocation, opening, closing, toggling and
launching. Pointer hover remains silent. Every audible event also has a visible
state change, and the global Off setting stops playback immediately.

## Persistence

The settings are stored in `/data/config/ui.cfg`:

- `motion`
- `motion_speed`
- `animation_quality`
- `sounds`
- `feedback`

The legacy `animations=0|1` key remains accepted and maps to Reduced or Fluid.
Unknown or invalid values are ignored and defaults remain safe.
""",
    encoding="utf-8",
)
print("experience-pass: wrote UX architecture note")

# Production tree must remain free from temporary compatibility tags.
for path in ("kernel/kernel.cpp", "kernel/parts/hardware.inc", "kernel/parts/ui_theme.inc",
             "kernel/parts/ui_apps.inc", "kernel/parts/ui_runtime.inc",
             "tests/qemu_display_ui.sh", ".github/workflows/adaptive-display.yml"):
    if "experience-pass-compat" in Path(path).read_text(encoding="utf-8"):
        raise SystemExit(f"temporary marker leaked into {path}")

print("experience-pass: transformed experience settings, motion, sound and evidence")

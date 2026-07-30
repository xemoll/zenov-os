#include "div64_runtime.inc"

namespace graphics {
#include "ui_graphics_base.inc"

uint32_t configured_desktop_canvas = desktop_canvas;
uint32_t configured_desktop_surface = desktop_surface;
uint32_t configured_desktop_accent = desktop_accent;
uint8_t configured_desktop_radius = desktop_radius;
uint8_t configured_desktop_opacity = desktop_opacity;
bool configured_desktop_animations = desktop_animations;
const char* configured_desktop_theme = desktop_theme;
const char* configured_terminal_cursor = terminal_cursor;

#define desktop_canvas configured_desktop_canvas
#define desktop_surface configured_desktop_surface
#define desktop_accent configured_desktop_accent
#define desktop_radius configured_desktop_radius
#define desktop_opacity configured_desktop_opacity
#define desktop_animations configured_desktop_animations
#define desktop_theme configured_desktop_theme
#define terminal_cursor configured_terminal_cursor

#include "ui_theme.inc"
#include "ui_shell.inc"
#include "ui_apps.inc"

#define render_scene render_scene_base
#include "ui_files.inc"
#undef render_scene

#include "ui_scaler.inc"

void render_scene();

#define init init_base
#define handle_navigation_key handle_navigation_key_base
#define on_mouse_packet on_mouse_packet_base
#include "ui_runtime.inc"
#undef on_mouse_packet
#undef handle_navigation_key
#undef init

#define init init_files_base
#define handle_navigation_key handle_navigation_key_files_base
#define on_mouse_packet on_mouse_packet_files_base
#include "ui_files_runtime.inc"
#undef on_mouse_packet
#undef handle_navigation_key
#undef init

#define handle_navigation_key handle_navigation_key_productivity_base
#include "ui_productivity.inc"
#undef handle_navigation_key

bool productivity_launcher_query_is(const char* name) {
    if (!name || launcher_query_length != string_length(name)) return false;
    for (uint32_t index = 0U; index < launcher_query_length; ++index)
        if (!productivity_ascii_equal_ci(launcher_query[index], name[index])) return false;
    return true;
}

void productivity_open_launcher_result(ProductivityApp app) {
    launcher_query_length = 0U;
    launcher_query[0] = 0;
    launcher_focus = 0U;
    set_active_view(launcher_return_view);
    productivity_open(app);
    refresh_desktop();
}

bool handle_navigation_key(uint16_t key) {
    if (!productivity_overlay_open) {
        if (active_view == View::launcher && (key == '\n' || key == ' ')) {
            if (productivity_launcher_query_is("notes") || productivity_launcher_query_is("notepad")) {
                productivity_open_launcher_result(ProductivityApp::notes); return true;
            }
            if (productivity_launcher_query_is("calendar")) {
                productivity_open_launcher_result(ProductivityApp::calendar); return true;
            }
            if (productivity_launcher_query_is("clock") || productivity_launcher_query_is("time")) {
                productivity_open_launcher_result(ProductivityApp::clock); return true;
            }
        }
        if (key == key_f1 || key == key_f2 || key == key_f3)
            return handle_navigation_key_files_base(key);
    }
    return handle_navigation_key_productivity_base(key);
}

#undef terminal_cursor
#undef desktop_theme
#undef desktop_animations
#undef desktop_opacity
#undef desktop_radius
#undef desktop_accent
#undef desktop_surface
#undef desktop_canvas
} // namespace graphics

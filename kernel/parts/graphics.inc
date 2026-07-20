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
#include "ui_files.inc"
#include "ui_scaler.inc"

#define init init_base
#define handle_navigation_key handle_navigation_key_base
#define on_mouse_packet on_mouse_packet_base
#include "ui_runtime.inc"
#undef on_mouse_packet
#undef handle_navigation_key
#undef init

#include "ui_files_runtime.inc"

#undef terminal_cursor
#undef desktop_theme
#undef desktop_animations
#undef desktop_opacity
#undef desktop_radius
#undef desktop_accent
#undef desktop_surface
#undef desktop_canvas
} // namespace graphics

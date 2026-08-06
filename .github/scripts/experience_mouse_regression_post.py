#!/usr/bin/env python3
"""Retarget the PS/2 self-test to the deferred IRQ-to-main input contract."""

from pathlib import Path


path = Path("kernel/parts/mouse_regression.inc")
text = path.read_text(encoding="utf-8")
signature = "bool mouse_decoder_regression() {"
if text.count(signature) != 1:
    raise SystemExit(
        f"experience-mouse-regression-post: expected one decoder regression, got {text.count(signature)}")

start = text.index(signature)
depth = 0
end = -1
for offset, character in enumerate(text[start:], start):
    if character == "{":
        depth += 1
    elif character == "}":
        depth -= 1
        if depth == 0:
            end = offset + 1
            if end < len(text) and text[end] == "\n":
                end += 1
            break
if end < 0:
    raise SystemExit("experience-mouse-regression-post: unterminated decoder regression")

old = text[start:end]
for marker in (
    "inject_mouse_packet_to_decoder(0x08U, 1U, 1U);",
    "const bool packet_ok = !graphics::first_mouse_packet",
    "return packet_ok && drag_ok;",
):
    if marker not in old:
        raise SystemExit(f"experience-mouse-regression-post: legacy marker missing: {marker}")

new = r'''bool dispatch_mouse_event_for_regression() {
    int32_t delta_x = 0;
    int32_t delta_y = 0;
    uint8_t buttons = 0U;
    disable_interrupts();
    const bool popped = mouse_event_pop_locked(delta_x, delta_y, buttons);
    enable_interrupts();
    if (!popped) return false;
    graphics::on_mouse_packet(delta_x, delta_y, buttons);
    return true;
}

void reset_mouse_event_queue_for_regression() {
    disable_interrupts();
    mouse_event_head = 0U;
    mouse_event_tail = 0U;
    mouse_event_overflowed = false;
    mouse_packet_index = 0U;
    enable_interrupts();
}

bool mouse_decoder_regression() {
    if (!mouse_online || !graphics::active || !mouse_event_queue_contract_ok()) return false;

    reset_mouse_event_queue_for_regression();
    disable_interrupts();
    mouse_byte(0x00U);
    const bool malformed_packet_ok = mouse_packet_index == 0U && mouse_event_head == mouse_event_tail;
    for (uint32_t index = 0U; index + 1U < mouse_event_queue_capacity; ++index)
        mouse_event_push(static_cast<int32_t>(index), -static_cast<int32_t>(index), 0U);
    const uint32_t full_head = mouse_event_head;
    mouse_event_push(1, 1, 0U);
    const bool overflow_bounds_ok = mouse_event_overflowed && mouse_event_head == full_head &&
        ((mouse_event_head + 1U) & mouse_event_queue_mask) == mouse_event_tail;
    mouse_event_head = 0U;
    mouse_event_tail = 0U;
    mouse_event_overflowed = false;
    enable_interrupts();

    graphics::mouse_screen_x = mouse_screen_from_logical_x(static_cast<int32_t>(graphics::width / 2U));
    graphics::mouse_screen_y = mouse_screen_from_logical_y(static_cast<int32_t>(graphics::height / 2U));
    graphics::update_logical_mouse();
    const int32_t start_x = 150;
    const int32_t start_y = graphics::window_drag_min_y();
    graphics::window_x = start_x;
    graphics::window_y = start_y;
    graphics::mouse_buttons = 0U;
    graphics::dragging = false;
    graphics::first_mouse_packet = true;
    graphics::drag_reported = false;
    graphics::render_scene();
    graphics::present_full();

    disable_interrupts();
    inject_mouse_packet_to_decoder(0x08U, 1U, 1U);
    const bool first_enqueued = mouse_packet_index == 0U && mouse_event_head != mouse_event_tail &&
        graphics::first_mouse_packet;
    enable_interrupts();
    const bool first_dispatched = dispatch_mouse_event_for_regression() && !graphics::first_mouse_packet;

    graphics::mouse_screen_x = mouse_screen_from_logical_x(start_x + 50);
    graphics::mouse_screen_y = mouse_screen_from_logical_y(start_y + 16);
    graphics::update_logical_mouse();
    disable_interrupts();
    inject_mouse_packet_to_decoder(0x09U, 0U, 0U);
    enable_interrupts();
    const bool press_dispatched = dispatch_mouse_event_for_regression();
    const int32_t press_x = graphics::mouse_x;
    const int32_t press_y = graphics::mouse_y;

    disable_interrupts();
    inject_mouse_packet_to_decoder(0x29U, 80U, 226U);
    enable_interrupts();
    const bool move_dispatched = dispatch_mouse_event_for_regression();
    int32_t expected_x = start_x + graphics::mouse_x - press_x;
    int32_t expected_y = start_y + graphics::mouse_y - press_y;
    if (expected_x < graphics::window_drag_min_x()) expected_x = graphics::window_drag_min_x();
    if (expected_x > graphics::window_drag_max_x()) expected_x = graphics::window_drag_max_x();
    if (expected_y < graphics::window_drag_min_y()) expected_y = graphics::window_drag_min_y();
    if (expected_y > graphics::window_drag_max_y()) expected_y = graphics::window_drag_max_y();
    const bool signed_delta_ok = graphics::mouse_y > press_y;

    disable_interrupts();
    inject_mouse_packet_to_decoder(0x08U, 0U, 0U);
    enable_interrupts();
    const bool release_dispatched = dispatch_mouse_event_for_regression();

    disable_interrupts();
    const bool queue_drained = mouse_event_head == mouse_event_tail && !mouse_event_overflowed;
    enable_interrupts();
    const bool bounds_ok = graphics::window_drag_min_x() <= graphics::window_drag_max_x() &&
        graphics::window_drag_min_y() <= graphics::window_drag_max_y();
    const bool drag_ok = press_dispatched && move_dispatched && release_dispatched &&
        graphics::drag_reported && mouse_packet_index == 0U && signed_delta_ok && bounds_ok &&
        graphics::window_x == expected_x && graphics::window_y == expected_y;
    return malformed_packet_ok && overflow_bounds_ok && first_enqueued && first_dispatched &&
        queue_drained && drag_ok;
}
'''

path.write_text(text[:start] + new + text[end:], encoding="utf-8")
print("experience-mouse-regression-post: deferred decoder, overflow and main-dispatch contract installed")

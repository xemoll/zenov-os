#!/usr/bin/env python3
"""Eliminate IRQ-side rendering and CPU-spinning motion waits."""

from __future__ import annotations

from pathlib import Path
import subprocess


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"experience-responsiveness-post: {label}: expected one match, got {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"experience-responsiveness-post: {label}")


replace_once(
    "kernel/parts/hardware.inc",
    r'''constexpr uint32_t key_queue_capacity = 1024;
volatile uint16_t key_queue[key_queue_capacity]; volatile uint32_t key_head = 0, key_tail = 0;
bool shift_pressed = false, caps_lock = false, extended_scancode = false;
void key_push(uint16_t key) { const uint32_t next = (key_head + 1) % key_queue_capacity; if (next != key_tail) { key_queue[key_head] = key; key_head = next; } }
uint16_t key_pop_blocking() {
    for (;;) { disable_interrupts(); if (key_tail != key_head) { const uint16_t key = key_queue[key_tail]; key_tail = (key_tail + 1) % key_queue_capacity; enable_interrupts(); return key; } asm volatile("sti; hlt; cli"); }
}
''',
    r'''constexpr uint32_t key_queue_capacity = 1024U;
volatile uint16_t key_queue[key_queue_capacity]; volatile uint32_t key_head = 0U, key_tail = 0U;

struct MouseInputEvent { int32_t delta_x; int32_t delta_y; uint8_t buttons; };
constexpr uint32_t mouse_event_queue_capacity = 256U;
constexpr uint32_t mouse_event_queue_mask = mouse_event_queue_capacity - 1U;
static_assert((mouse_event_queue_capacity & mouse_event_queue_mask) == 0U);
static_assert(mouse_event_queue_capacity >= 128U);
volatile MouseInputEvent mouse_event_queue[mouse_event_queue_capacity];
volatile uint32_t mouse_event_head = 0U, mouse_event_tail = 0U;
volatile bool mouse_event_overflowed = false;

constexpr bool mouse_event_queue_contract_ok() {
    return mouse_event_queue_capacity >= 128U &&
        (mouse_event_queue_capacity & (mouse_event_queue_capacity - 1U)) == 0U;
}

void mouse_event_push(int32_t delta_x, int32_t delta_y, uint8_t buttons) {
    const uint32_t head = mouse_event_head;
    const uint32_t next = (head + 1U) & mouse_event_queue_mask;
    if (next == mouse_event_tail) { mouse_event_overflowed = true; return; }
    mouse_event_queue[head].delta_x = delta_x;
    mouse_event_queue[head].delta_y = delta_y;
    mouse_event_queue[head].buttons = buttons;
    mouse_event_head = next;
}

bool mouse_event_pop_locked(int32_t& delta_x, int32_t& delta_y, uint8_t& buttons) {
    const uint32_t tail = mouse_event_tail;
    if (tail == mouse_event_head) return false;
    delta_x = mouse_event_queue[tail].delta_x;
    delta_y = mouse_event_queue[tail].delta_y;
    buttons = mouse_event_queue[tail].buttons;
    mouse_event_tail = (tail + 1U) & mouse_event_queue_mask;
    return true;
}

bool shift_pressed = false, caps_lock = false, extended_scancode = false;
void key_push(uint16_t key) { const uint32_t next = (key_head + 1U) % key_queue_capacity; if (next != key_tail) { key_queue[key_head] = key; key_head = next; } }
uint16_t key_pop_blocking() {
    for (;;) {
        disable_interrupts();
        if (key_tail != key_head) {
            const uint16_t key = key_queue[key_tail];
            key_tail = (key_tail + 1U) % key_queue_capacity;
            enable_interrupts();
            return key;
        }
        int32_t delta_x = 0;
        int32_t delta_y = 0;
        uint8_t buttons = 0U;
        if (mouse_event_pop_locked(delta_x, delta_y, buttons)) {
            const bool overflowed = mouse_event_overflowed;
            mouse_event_overflowed = false;
            enable_interrupts();
            if (overflowed) serial::line("PS2_MOUSE_DEFERRED_QUEUE_OVERFLOW");
            graphics::on_mouse_packet(delta_x, delta_y, buttons);
            continue;
        }
        asm volatile("sti; hlt; cli" ::: "memory");
    }
}
''',
    "deferred decoded mouse events to the main input pump",
)

replace_once(
    "kernel/parts/hardware.inc",
    r'''    int32_t delta_x = mouse_packet[1]; if (mouse_packet[0] & 0x10U) delta_x -= 256;
    int32_t delta_y = mouse_packet[2]; if (mouse_packet[0] & 0x20U) delta_y -= 256;
    graphics::on_mouse_packet(delta_x, -delta_y, static_cast<uint8_t>(mouse_packet[0] & 0x07U));
''',
    r'''    int32_t delta_x = mouse_packet[1]; if (mouse_packet[0] & 0x10U) delta_x -= 256;
    int32_t delta_y = mouse_packet[2]; if (mouse_packet[0] & 0x20U) delta_y -= 256;
    mouse_event_push(delta_x, -delta_y, static_cast<uint8_t>(mouse_packet[0] & 0x07U));
''',
    "made IRQ12 decode-only",
)

replace_once(
    "kernel/parts/hardware.inc",
    r'''    if (!mouse_send(0xF6) || !mouse_send(0xF4)) return false;
    mouse_online = true; serial::line("PS2_MOUSE_OK"); return true;
''',
    r'''    if (!mouse_send(0xF6) || !mouse_send(0xF4)) return false;
    mouse_online = true;
    serial::line("PS2_MOUSE_OK");
    serial::line(mouse_event_queue_contract_ok()
        ? "PS2_MOUSE_DEFERRED_QUEUE_OK" : "PS2_MOUSE_DEFERRED_QUEUE_FAILED");
    return true;
''',
    "reported bounded deferred mouse queue",
)

replace_once(
    "kernel/parts/ui_runtime.inc",
    r'''void wait_animation_ticks(uint8_t ticks) {
    for (uint8_t tick = 0U; tick < ticks; ++tick) {
        const uint32_t start = timer_ticks;
        uint32_t spins = 0U;
        while (timer_ticks == start && spins++ < 2000000U) asm volatile("pause");
    }
}
''',
    r'''void wait_animation_ticks(uint8_t ticks) {
    if (!ticks) return;
    const uint32_t start = timer_ticks;
    while (static_cast<uint32_t>(timer_ticks - start) < ticks)
        asm volatile("sti; hlt" ::: "memory");
}
''',
    "replaced CPU spin pacing with interruptible idle waits",
)

replace_once(
    "tests/qemu_display_ui.sh",
    '  wait_for_serial "UI_ICON_COUNT 14" || { echo quit; return 1; }\n',
    '  wait_for_serial "UI_ICON_COUNT 14" || { echo quit; return 1; }\n'
    '  wait_for_serial "PS2_MOUSE_DEFERRED_QUEUE_OK" || { echo quit; return 1; }\n',
    "required deferred mouse queue at boot",
)

replace_once(
    "tests/qemu_display_ui.sh",
    '"UI_FONT_GLYPH_COUNT 95" "UI_ICON_COUNT 13" UI_BORDER_STROKE_OK',
    '"UI_FONT_GLYPH_COUNT 95" "UI_ICON_COUNT 14" PS2_MOUSE_DEFERRED_QUEUE_OK UI_BORDER_STROKE_OK',
    "updated icon count and responsiveness marker",
)

replace_once(
    "tests/qemu_display_ui.sh",
    r'''printf 'qemu-display-ui: OK modes=32 max=4096x2160 vram=64MiB shell=start+quick+system-center+accessibility+packages+security serial=%s screenshots=%s\n' "$SERIAL" "$OUT/*.ppm"
''',
    r'''if grep -q "PS2_MOUSE_DEFERRED_QUEUE_OVERFLOW" "$SERIAL"; then
  echo "qemu-display-ui: deferred mouse queue overflowed" >&2
  exit 1
fi
if grep -Eq 'spins\+\+ < 2000000U|asm volatile\("pause"\)' kernel/parts/ui_runtime.inc; then
  echo "qemu-display-ui: blocking animation spin loop remains" >&2
  exit 1
fi
if grep -Fq 'graphics::on_mouse_packet(delta_x, -delta_y' kernel/parts/hardware.inc; then
  echo "qemu-display-ui: IRQ-side mouse renderer remains" >&2
  exit 1
fi

printf 'qemu-display-ui: OK modes=32 max=4096x2160 vram=64MiB input=deferred-irq-safe motion=interrupt-idle shell=start+quick+system-center+accessibility+packages+security serial=%s screenshots=%s\n' "$SERIAL" "$OUT/*.ppm"
''',
    "made responsiveness regressions gate-fatal",
)

replace_once(
    "docs/UX_EXPERIENCE_0.1.1.md",
    '''## Persistence
''',
    '''## Responsiveness

IRQ12 now performs only PS/2 packet validation, decoding and enqueueing. A
bounded 256-event single-producer/single-consumer queue dispatches pointer
updates from the normal input pump, so framebuffer rendering, hit testing and
settings persistence never run inside the hardware interrupt handler.

Motion pacing no longer consumes CPU in a fixed spin loop. Between bounded
frames the renderer enters an interruptible `HLT` idle state and wakes on the
100 Hz timer or another device interrupt. Keyboard and pointer events remain
queued during a transition and are processed immediately after its final
frame. CI fails on queue overflow, direct IRQ-side rendering or reintroduced
spin pacing.

## Persistence
''',
    "documented deferred input and idle animation pacing",
)

subprocess.run(["bash", "-n", "tests/qemu_display_ui.sh"], check=True)
print("experience-responsiveness-post: shell syntax verified")
print("experience-responsiveness-post: complete")

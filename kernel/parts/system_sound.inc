namespace system_sound {

enum class Event : uint8_t {
    surface_open,
    surface_close,
    select,
    toggle,
    launch,
    warning,
    count
};

enum : uint8_t {
    theme_off = 0U,
    theme_soft = 1U,
    theme_clear = 2U,
    theme_count = 3U,
    feedback_quiet = 0U,
    feedback_normal = 1U,
    feedback_pronounced = 2U,
    feedback_count = 3U
};

constexpr uint32_t pit_input_hz = 1193182U;
constexpr uint8_t speaker_gate_mask = 0x03U;
constexpr uint8_t inter_note_gap_ticks = 1U;
constexpr uint8_t maximum_note_count = 3U;
constexpr uint8_t maximum_note_ticks = 3U;
constexpr uint8_t maximum_event_ticks = 12U;

volatile uint8_t configured_theme = theme_soft;
volatile uint8_t configured_feedback = feedback_normal;
volatile bool initialized = false;
volatile bool queued = false;
volatile bool playing = false;
volatile bool tone_enabled = false;
volatile Event queued_event = Event::select;
volatile Event active_event = Event::select;
volatile uint8_t active_note = 0U;
volatile uint8_t tone_ticks = 0U;
volatile uint8_t gap_ticks = 0U;

uint8_t note_count(Event event) {
    switch (event) {
        case Event::surface_open: return 2U;
        case Event::surface_close: return 2U;
        case Event::select: return 1U;
        case Event::toggle: return 2U;
        case Event::launch: return 3U;
        case Event::warning: return 3U;
        default: return 0U;
    }
}

uint16_t note_frequency(Event event, uint8_t note) {
    switch (event) {
        case Event::surface_open: return note == 0U ? 392U : 523U;
        case Event::surface_close: return note == 0U ? 466U : 349U;
        case Event::select: return 587U;
        case Event::toggle: return note == 0U ? 440U : 554U;
        case Event::launch:
            if (note == 0U) return 392U;
            if (note == 1U) return 523U;
            return 659U;
        case Event::warning:
            if (note == 0U) return 330U;
            if (note == 1U) return 247U;
            return 330U;
        default: return 0U;
    }
}

uint16_t themed_frequency(Event event, uint8_t note, uint8_t theme) {
    const uint16_t base = note_frequency(event, note);
    if (!base) return 0U;
    if (theme == theme_soft) return static_cast<uint16_t>((static_cast<uint32_t>(base) * 3U) / 4U);
    return base;
}

uint8_t note_duration(uint8_t feedback) {
    if (feedback == feedback_quiet) return 1U;
    if (feedback == feedback_pronounced) return 3U;
    return 2U;
}

void speaker_off() {
    const uint8_t state = in8(0x61U);
    out8(0x61U, static_cast<uint8_t>(state & ~speaker_gate_mask));
    tone_enabled = false;
}

void speaker_on(uint16_t frequency) {
    if (frequency < 120U || frequency > 4000U) { speaker_off(); return; }
    uint32_t divisor = pit_input_hz / frequency;
    if (!divisor) divisor = 1U;
    if (divisor > 0xFFFFU) divisor = 0xFFFFU;
    out8(0x43U, 0xB6U);
    out8(0x42U, static_cast<uint8_t>(divisor & 0xFFU));
    out8(0x42U, static_cast<uint8_t>((divisor >> 8U) & 0xFFU));
    const uint8_t state = in8(0x61U);
    out8(0x61U, static_cast<uint8_t>(state | speaker_gate_mask));
    tone_enabled = true;
}

void stop() {
    speaker_off();
    queued = false;
    playing = false;
    active_note = 0U;
    tone_ticks = 0U;
    gap_ticks = 0U;
}

void configure(uint8_t theme, uint8_t feedback) {
    configured_theme = theme < theme_count ? theme : theme_soft;
    configured_feedback = feedback < feedback_count ? feedback : feedback_normal;
    if (configured_theme == theme_off) stop();
}

void play(Event event) {
    if (!initialized || configured_theme == theme_off || event >= Event::count) return;
    queued_event = event;
    queued = true;
}

void preview() {
    play(Event::launch);
    serial::line(configured_theme == theme_off ? "UI_SOUND_PREVIEW_MUTED" : "UI_SOUND_PREVIEW_OK");
}

void start_next_note() {
    const uint8_t count = note_count(active_event);
    if (active_note >= count) {
        playing = false;
        active_note = 0U;
        return;
    }
    const uint16_t frequency = themed_frequency(active_event, active_note, configured_theme);
    ++active_note;
    if (!frequency) {
        playing = false;
        speaker_off();
        return;
    }
    speaker_on(frequency);
    tone_ticks = note_duration(configured_feedback);
}

void tick() {
    if (!initialized || configured_theme == theme_off) {
        if (tone_enabled) speaker_off();
        return;
    }
    if (tone_ticks) {
        --tone_ticks;
        if (!tone_ticks) {
            speaker_off();
            gap_ticks = inter_note_gap_ticks;
        }
        return;
    }
    if (gap_ticks) { --gap_ticks; return; }
    if (!playing) {
        if (!queued) return;
        active_event = queued_event;
        queued = false;
        active_note = 0U;
        playing = true;
    }
    start_next_note();
}

bool contract_ok() {
    if (static_cast<uint8_t>(Event::count) != 6U || theme_count != 3U || feedback_count != 3U) return false;
    for (uint8_t event_index = 0U; event_index < static_cast<uint8_t>(Event::count); ++event_index) {
        const Event event = static_cast<Event>(event_index);
        const uint8_t count = note_count(event);
        if (!count || count > maximum_note_count) return false;
        for (uint8_t note = 0U; note < count; ++note) {
            const uint16_t soft = themed_frequency(event, note, theme_soft);
            const uint16_t clear = themed_frequency(event, note, theme_clear);
            if (soft < 120U || clear > 4000U || soft >= clear) return false;
        }
        const uint8_t bounded_ticks = static_cast<uint8_t>(count * maximum_note_ticks + (count - 1U) * inter_note_gap_ticks);
        if (bounded_ticks > maximum_event_ticks) return false;
    }
    return note_duration(feedback_quiet) == 1U &&
        note_duration(feedback_normal) == 2U &&
        note_duration(feedback_pronounced) == 3U;
}

void init() {
    stop();
    initialized = true;
    serial::line(contract_ok() ? "UI_SOUND_ENGINE_OK" : "UI_SOUND_ENGINE_FAILED");
}

} // namespace system_sound

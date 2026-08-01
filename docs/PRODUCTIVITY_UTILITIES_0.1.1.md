# ZenovOS 0.1.1 productivity utilities

This document covers the native Calculator and Reminders/Agenda utilities layered on top of Notes, Tasks, Calendar and Clock. The applications use ZenovOS-native storage, input and rendering rather than embedding a foreign UI toolkit.

## Calculator

Open Start with `Super` or `F8`, type `calculator` or `calc`, and press Enter.

The calculator is bounded to signed 32-bit integer arithmetic and provides:

- standard expressions with precedence, parentheses, checked addition, subtraction, multiplication, division and remainder;
- programmer mode with decimal, `0x` hexadecimal, `0b` binary, bitwise AND/OR/XOR/NOT and shifts limited to 0–31;
- persistent eight-entry history and one memory value in `/utilities/calculator.state`;
- Gregorian date difference and bounded date adjustment from 2000 through 2099;
- length, temperature and binary-storage conversions rendered with three decimal places.

The calculator does not claim arbitrary precision, floating-point scientific functions, graphing, currencies or network exchange rates.

## Reminders and integrated Agenda

Open Start, type `reminders`, `reminder`, `agenda` or `alarm`, and press Enter.

### Persistent record format

New writes use the bounded R2 line format:

```text
R2|0|YYYY-MM-DD|HH:MM|N|DD|title
```

Fields after the time are the repeat code (`N`, `D`, `W`, `M`) and the original day-of-month anchor. The anchor preserves monthly intent across short months: a reminder created for the 31st advances to February's last valid day and then returns to the 31st in March.

Existing R1 records remain readable:

```text
R1|0|YYYY-MM-DD|HH:MM|title
```

They are treated as one-shot reminders and migrate to R2 on the next successful save. The database remains bounded to 24 records. Titles are printable ASCII without the field delimiter, all timestamps are validated, and mutations use guarded ZenovFS writes.

### Quick Capture

Press `A` in Reminders. The capture surface accepts a title plus an optional schedule and repeat suffix:

```text
Drink water @+10m
Review release @+2h !daily
Backup notes @2026-08-03 09:30 !weekly
Pay invoice @2026-08-31 18:00 !monthly
```

Supported relative units are minutes, hours and days. Without an explicit schedule, the default is ten minutes from the current RTC time. `Tab` cycles the default repeat mode; an explicit suffix overrides it. The preview validates the parsed date, time and recurrence before saving.

### Recurrence semantics

Supported repeat modes are one-shot, daily, weekly and monthly. Completing a recurring reminder advances it to the first valid future occurrence instead of moving it to Done. Daily and weekly catch-up is computed from ordinal day distance. Monthly recurrence preserves the original day anchor and clamps only when the target month is shorter. The supported date range remains 2000–2099.

This is a deliberate local subset, not a claim of complete iCalendar RRULE support. There is no `COUNT`, `UNTIL`, `BYDAY`, exception-date or timezone rule engine.

### Agenda views and commands

The application now uses a stable left navigation rail, central agenda list and persistent details pane. Views are:

- `F1` Today;
- `F2` Overdue;
- `F3` Next 7 Days;
- `F4` Scheduled;
- `F5` Done.

Agenda continues to merge three canonical local sources without duplicating them:

- reminder records;
- due-dated Markdown tasks from Notes;
- Calendar events.

Commands include complete/advance, repeat-mode cycling, ten-minute snooze, deletion and source navigation. Due reminders emit a local desktop banner with the repeat mode and a serial audit marker.

## Verification boundary

The focused workflow validates:

- GCC and Clang sanitizer models covering backward parsing, R2 validation, quick scheduling, recurrence catch-up, leap years, monthly anchors and seven-day boundaries;
- strict native image build and existing system checks;
- guarded ZenovFS reads and writes;
- two-phase QEMU interaction with reboot persistence;
- an independent runtime-image verifier that checks one completed one-shot reminder and one advanced daily reminder;
- framebuffer evidence for Quick Capture, active alarms, recurring state, the seven-day view and rebooted state;
- deterministic rebuild.

Not implemented: shared lists, cloud synchronization, location triggers, background audio, timezone databases, natural-language scheduling, full RFC 5545 recurrence, arbitrary precision, scientific graphing or currency feeds.

# ZenovOS 0.1.1 productivity utilities

This document covers the native Calculator and Reminders/Agenda utilities added after the first Notes, Tasks, Calendar and Clock application layer. The implementation copies workflows, not branding or visual identity.

## Calculator

Open Start with `Super` or `F8`, type `calculator` or `calc`, and press Enter.

The calculator is bounded to signed 32-bit integer arithmetic and provides:

- standard expressions with precedence, parentheses, checked addition, subtraction, multiplication, division and remainder;
- programmer mode with decimal, `0x` hexadecimal, `0b` binary, bitwise AND/OR/XOR/NOT and shifts limited to 0–31;
- persistent eight-entry history and one memory value in `/utilities/calculator.state`;
- Gregorian date difference and bounded date adjustment from 2000 through 2099;
- length, temperature and binary-storage conversions rendered with three decimal places.

The calculator deliberately does not claim arbitrary precision, floating-point scientific functions, graphing, currencies or network exchange rates.

## Reminders and integrated Agenda

Open Start, type `reminders`, `reminder`, `agenda` or `alarm`, and press Enter.

Reminder records are stored locally in `/reminders/reminders.db` with the canonical line format:

```text
R1|0|YYYY-MM-DD|HH:MM|title
```

The bounded reminder database supports 24 records. Titles are printable ASCII without the field delimiter. Dates are Gregorian and validated before use.

Smart views:

- Today;
- Overdue;
- Scheduled;
- Done.

Agenda merges three existing local sources without duplicating them:

- reminder records;
- due-dated Markdown tasks from Notes;
- Calendar events.

Actions include quick add, complete/reopen, delete, ten-minute snooze and opening a source task or Calendar date. Due reminders emit a local desktop banner and a serial audit marker. Notification acknowledgement is intentionally volatile; reminder state itself is persistent.

## Verification boundary

The focused workflow validates:

- GCC and Clang sanitizer models;
- strict native image build;
- guarded ZenovFS reads and writes;
- two-phase QEMU interaction with reboot persistence;
- an independent runtime-image verifier;
- framebuffer evidence for all Calculator modes and reminder states;
- deterministic rebuild.

Not implemented: recurrence, shared lists, cloud synchronization, location triggers, background audio, timezone databases, natural-language scheduling, arbitrary precision, scientific graphing or currency feeds.

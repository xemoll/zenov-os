# ZenovOS 0.1.1 native system applications

This document defines the first productivity-application layer for the existing ZenovOS **0.1.1** desktop. It adds real local state and native interaction; it does not change the release version and does not imitate another product's visual identity.

## Functional sources

The implementation borrows workflows, not branding or screen composition:

- local-first note files, daily notes, search, lightweight properties and backlinks from knowledge-base applications such as Obsidian;
- bounded local workspaces and explicit persistent artifacts from project-oriented tools such as Antigravity;
- keyboard-first editing and inspectable plain-text state appropriate for low-level development tools. No compatibility with Cremniy, IDA, Obsidian or Antigravity is claimed.

## Applications

### Zen Notes

`F1` opens Zen Notes from any normal desktop surface.

Implemented behavior:

- local Markdown vault in `/notes`;
- bounded filename search with `/`;
- note creation with `F1`;
- save/open with `F2` or Enter;
- persistent scratchpad at `/notes/scratch.txt` with `F3`;
- daily note at `/notes/YYYY-MM-DD.md` with `F4`;
- generated YAML-like frontmatter containing `date` and `tags`;
- `[[note-name]]` backlink discovery across the bounded vault;
- word count;
- guarded ZenovFS writes and deletes;
- fixed 2 KiB editor buffer and 12-entry visible vault cap.

The editor is deliberately plain-text and append-oriented in this pass. It is not a full Markdown renderer, plugin host, graph database, Canvas implementation or multi-pane editor.

### Zen Calendar

`F2` opens Zen Calendar.

Implemented behavior:

- Gregorian month grid derived locally;
- correct leap-year and month-boundary navigation;
- RTC-backed Today action;
- selected-day navigation with arrow keys;
- local events stored in `/calendar/events.db` as canonical `YYYY-MM-DD|TITLE` records;
- add event with `F4`, delete the first event on the selected date with Delete;
- Enter opens or creates the selected day's note in Zen Notes.

The calendar is local-only. Recurrence, invitations, CalDAV, time zones, notifications and network sync are not implemented.

### Zen Clock

`F3` opens Zen Clock.

Implemented behavior:

- local CMOS RTC date and time;
- monotonic stopwatch based on the existing 100 Hz PIT;
- bounded countdown timer up to 60 minutes;
- explicit running, paused and expired states.

No alarm delivery while the UI is inactive, world-clock database, NTP synchronization or suspend-aware timing is claimed.

## Persistence and security

The application layer uses the existing ZenovFS primitives:

- directories are created only when absent;
- note and event files use transactional copy-on-write replacement;
- reads use the synchronous security read path;
- writes and deletes use ZenovGuard/ZRWP guarded operations;
- fixed path, file-size and entry-count limits are preserved.

The implementation does not bypass signed executable trust, syscall capability policy, malware classification, controlled-folder policy or authenticated-read policy.

## Verification

The dedicated `ZenovOS 0.1.1 System Apps` workflow:

1. verifies the exact source SHA and clean checkout;
2. runs the full strict build;
3. boots QEMU and exercises note creation, scratchpad persistence, daily-note creation, calendar event persistence, stopwatch and countdown controls;
4. captures six real `1024x768` framebuffer screenshots;
5. verifies the mutated runtime ZenovFS image structurally and checks live file contents and checksums with a dedicated host verifier;
6. runs the deterministic rebuild gate;
7. uploads images, logs, runtime data image, verifier and source evidence.

Expected final runtime marker:

```text
ZENOV_PRODUCTIVITY_RUNTIME_IMAGE_OK notes=markdown+scratch+daily calendar=events checksum=valid
```

## Boundaries

This is the first native application substrate, not a general GUI toolkit. The desktop remains kernel-rendered on the existing logical `800x600` canvas. Multi-window composition, clipboard services, Unicode/vector text, undo history, rich text, background alarms, extension APIs, cross-device sync and agent execution remain future work.

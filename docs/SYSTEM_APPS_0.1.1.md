# ZenovOS 0.1.1 native system applications

This document defines the first productivity-application layer for the existing ZenovOS **0.1.1** desktop. It adds real local state and native interaction; it does not change the release version and does not imitate another product's visual identity.

## Functional sources

The implementation borrows workflows, not branding or screen composition:

- local-first note files, daily notes, search, lightweight properties and backlinks from knowledge-base applications such as Obsidian;
- task aggregation from Markdown checkboxes, explicit priorities, due dates and waiting state;
- bounded local workspaces and explicit persistent artifacts from project-oriented tools such as Antigravity;
- keyboard-first editing and inspectable plain-text state appropriate for low-level development tools.

No compatibility with Cremniy, Obsidian or Antigravity is claimed.

## Opening applications

System applications are opened through the existing Start search rather than by taking over shell function keys:

```text
Super / F8 → type NOTES or NOTEPAD → Enter
Super / F8 → type TASKS, TODO or PLANNER → Enter
Super / F8 → type CALENDAR → Enter
Super / F8 → type CLOCK or TIME → Enter
```

This preserves the established terminal shortcuts `F1` through `F4`. Once a system application is active, its documented function keys are handled locally by that application.

## Applications

### Zen Notes

Open Zen Notes from Start search with `NOTES` or `NOTEPAD`.

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

### Zen Tasks

Open Zen Tasks from Start search with `TASKS`, `TODO` or `PLANNER`.

The task index is derived from ordinary Markdown files in `/notes`; it does not maintain a second opaque task database. Supported task records are:

```text
- [ ] Write release notes #P1 #D-2026-08-05
- [ ] Wait for review #P2 #W
- [x] Verify deterministic build
```

Implemented behavior:

- bounded scan of up to 12 Markdown files and 24 task records;
- exact checkbox recognition for `- [ ]`, `- [x]` and `- [X]`;
- inline priority metadata `#P1`, `#P2`, `#P3`;
- canonical due-date metadata `#D-YYYY-MM-DD` with Gregorian validation;
- waiting-state metadata `#W`;
- deterministic ordering: incomplete, actionable, priority, due date, source and line;
- Open, All and Done filters with `F1`, `F2` and `F3`;
- quick add to `/notes/tasks.md` with `F4`;
- Enter toggles the exact source checkbox through a guarded full-file replacement;
- `O` opens the source note and `R` rescans the vault;
- overdue indication against the local RTC date.

The index is intentionally bounded and synchronous. It does not implement recurrence, dependencies, Kanban drag-and-drop, natural-language dates or a background indexing service.

### Zen Calendar

Open Zen Calendar from Start search with `CALENDAR`.

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

Open Zen Clock from Start search with `CLOCK` or `TIME`.

Implemented behavior:

- local CMOS RTC date and time;
- monotonic stopwatch based on the existing 100 Hz PIT;
- bounded countdown timer up to 60 minutes;
- explicit running, paused and expired states.

No alarm delivery while the UI is inactive, world-clock database, NTP synchronization or suspend-aware timing is claimed.

## Persistence and security

The application layer uses the existing ZenovFS primitives:

- directories are created only when absent;
- note, task and event files use transactional copy-on-write replacement;
- reads use the synchronous security read path;
- writes and deletes use ZenovGuard/ZRWP guarded operations;
- fixed path, file-size and entry-count limits are preserved;
- task toggling re-reads the source and validates the exact checkbox bytes before mutation.

The implementation does not bypass signed executable trust, syscall capability policy, malware classification, controlled-folder policy or authenticated-read policy.

## Verification

The dedicated `ZenovOS 0.1.1 System Apps` workflow:

1. verifies the exact source SHA and clean checkout;
2. runs the Markdown-task parser and mutation model under strict GCC and Clang ASan/UBSan plus integer-conversion sanitizers;
3. runs the full strict freestanding build;
4. opens each application through the real Start search path;
5. boots QEMU and exercises note creation, scratchpad persistence, daily-note creation, task creation and checkbox mutation, calendar event persistence, stopwatch and countdown controls;
6. captures eight real `1024x768` framebuffer screenshots after the complete frame is presented;
7. verifies the mutated runtime ZenovFS image, bounded geometry, checksums and exact live file contents with a dedicated host verifier;
8. runs the deterministic rebuild gate and uploads images, logs, runtime image, verifier and source evidence.

The existing Security Defense workflow remains a required independent regression gate. It proves that system-app routing does not steal the established shell `F1`–`F4` shortcuts used by the security lifecycle harness.

Expected final runtime marker:

```text
ZENOV_PRODUCTIVITY_RUNTIME_IMAGE_OK notes=markdown+scratch+daily tasks=checkbox+metadata+toggle calendar=events checksum=valid geometry=bounded
```

## Boundaries

This is the first native application substrate, not a general GUI toolkit. Multi-window composition, clipboard services, Unicode/vector text, undo history, rich text, background alarms, extension APIs, cross-device sync and agent execution remain future work.

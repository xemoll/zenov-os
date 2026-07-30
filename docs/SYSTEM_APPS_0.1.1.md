# ZenovOS 0.1.1 system apps

The native Workspace groups four small local applications behind one launcher entry:

- **Clock** reads a bounded CMOS RTC snapshot and shows hardware time, date and session uptime.
- **Calendar** implements a Gregorian month grid with leap-year and day/week navigation.
- **Notes** stores one daily plain-text Markdown-style document at `/docs/note-YYYY-MM-DD.md`.
- **Notepad** stores a persistent scratch document at `/docs/scratchpad.txt`.

## Storage and limits

Notes and Notepad use the existing guarded ZenovFS read/write surface and require metadata synchronization before a save is reported as successful. Each editor is bounded to 2048 bytes. Unsupported control bytes are rejected when a document is loaded.

## Interaction

- `Tab` or `F3`: switch Workspace page.
- `F2`: save the active text document.
- `Esc`: close Workspace.
- Calendar arrows: move one day or one week.
- Calendar `Home`: return to the RTC date.
- Calendar `Enter`: open the selected daily note.

## Product boundary

The Notes surface borrows the useful local-file and daily-note concepts found in mature knowledge tools, but it is not an Obsidian-compatible vault. ZenovOS 0.1.1 does not yet provide a Markdown renderer, backlinks graph, plugins, version history, cloud synchronization, alarms, recurring events, timezone rules or background notifications.

Clock seconds update whenever the desktop is repainted; the current kernel UI does not schedule a dedicated one-second redraw.

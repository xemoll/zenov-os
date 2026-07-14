# ZenovOS interface direction

ZenovOS uses a restrained text-mode system console rather than decorative
ASCII art or verbose boot narration.

The interface must follow these rules:

- fixed product header with version and current mode;
- one primary content region with preserved header and footer;
- neutral VGA palette: navy, gray, white and cyan accents;
- short operational language instead of promotional or generated-sounding text;
- boot diagnostics mirrored to COM1 and available through `info`/`bootlog`;
- startup screen focused on readiness, navigation and useful commands;
- release screenshots must come from the verified QEMU image, never a mockup.

Release assets are installation-oriented only. Debug symbols, kernel maps,
serial logs and screenshots remain CI evidence and are not published as
end-user downloads.

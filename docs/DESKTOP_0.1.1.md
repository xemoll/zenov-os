# ZenovOS 0.1.1 desktop

This document describes the native graphical desktop shipped by the current ZenovOS **0.1.1** source tree. It does not define or imply a new release version.

![ZenovOS 0.1.1 desktop](screenshots/zenov-os-0.1.1-graphical-desktop.png)

The image above is a real `1024×768` QEMU Standard VGA framebuffer captured by the adaptive-display CI workflow. It is not a mockup.

## Rendering architecture

ZenovOS renders the UI into a stable logical `800×600` software backbuffer. A physical presentation stage maps that scene into the active VBE framebuffer while preserving aspect ratio.

```text
logical UI canvas: 800×600
        │
        ├── centered aspect-preserving viewport
        ├── physical → logical pointer translation
        ├── crisp sampling for exact integer scales
        └── fixed-point bilinear sampling for fractional/downscaled modes
        │
        ▼
physical QEMU/Bochs VBE framebuffer
```

The framebuffer MMIO window is supervisor-only and can map up to 16 MiB. User applications do not receive direct framebuffer access.

## Verified display modes

The automated QEMU display matrix boots the same 0.1.1 kernel and cycles through every mode below. A mode is accepted only after VBE register read-back confirms the requested width, height, 32-bit depth and enabled state.

```text
640x480   720x480   800x600    960x540
960x600   1024x576  1024x600   1024x768
1152x648  1152x720  1152x864   1280x720
1280x768  1280x800  1280x960   1280x1024
1360x768  1368x768  1440x900   1536x864
1600x900  1600x1200
```

The default mode is `1024x768`. Unsupported requested modes enter a read-back-checked fallback sequence rather than continuing with incorrect framebuffer geometry.

## Native surfaces

The desktop contains four kernel-rendered surfaces:

- **Terminal** — live local shell mirror and system commands;
- **Files** — read-only ZenovFS metadata browser with directory navigation and bounded text/binary preview;
- **Settings** — theme, display mode, motion and terminal-cursor preferences;
- **Applications** — keyboard- and mouse-navigable launcher for native system surfaces.

The visual design deliberately uses restrained chrome, a quiet dark palette, compact status indicators and list-oriented navigation instead of large showcase cards.

## Controls

```text
F5  Terminal
F6  Files
F7  Settings
F8  Applications
F9  Next verified display mode
```

Settings controls:

```text
Tab / Up / Down     move focus
Left / Right        change the focused value
Enter / Space       apply or cycle the focused value
Escape              return to Terminal
```

Mouse hit testing is performed in logical coordinates after translating the physical cursor through the active viewport. Dock actions, Settings controls, Files rows and title-bar dragging therefore use the same geometry at every verified mode.

## Persistence

Desktop preferences are stored in:

```text
/data/config/ui.cfg
```

The persisted fields are:

```text
theme=<midnight|graphite|amber>
resolution=<width>x<height>
animations=<0|1>
cursor=<beam|underline|block>
```

The built-in 0.1.1 defaults remain available when the persistent file is absent or invalid.

## CI evidence

The adaptive-display workflow requires these runtime markers:

```text
UI_ADAPTIVE_DISPLAY_OK
UI_HYBRID_SCALER_OK
UI_DISPLAY_MODE_COUNT 22
UI_SETTINGS_CONTROLS_OK
UI_DISPLAY_CYCLE_OK
UI_DISPLAY_PERSIST_OK
```

It additionally verifies every `UI_DISPLAY_MODE_OK <width>x<height>` marker and validates the dimensions of representative framebuffer dumps, including `640×480`, `1024×768`, `1280×1024`, `1440×900`, `1600×1200` and the Settings surface.

The standard CI and syscall-capability workflows run independently and retain the complete boot, recovery, audit, signed-policy and deterministic-build contracts.

## Current limitations

- Automated evidence currently targets QEMU Standard VGA/Bochs VBE.
- Physical GPUs, VirtualBox, VMware and firmware-specific VBE implementations have not been verified.
- The desktop is still kernel-rendered; it is not a user-space compositor or reusable GUI toolkit.
- The logical scene remains `800×600`; higher physical modes improve presentation scale and aspect-ratio coverage but do not add more logical workspace.
- The current bitmap font is intentionally compact. Fractional scaling smooths edge stepping, but it is not equivalent to a vector-font rasterizer.

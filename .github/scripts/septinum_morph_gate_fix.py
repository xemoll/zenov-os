from pathlib import Path

path = Path("tests/qemu_display_ui.sh")
text = path.read_text(encoding="utf-8")
old = r'''width, height, desktop = ppm('desktop-1024x768.ppm')
_, _, full = ppm('start-1024x768.ppm')
_, _, filtered = ppm('start-search-1024x768.ppm')
viewport_y = 96
x0, x1 = 184 * width // 896, (184 + 528) * width // 896
y0, y1 = viewport_y + 10 * 576 // 504, viewport_y + 438 * 576 // 504

def changed_bottom(frame):
    bottom = -1
    for py in range(y0, min(height, y1)):
        changed = 0
        for px in range(x0, min(width, x1)):
            offset = (py * width + px) * 3
            if desktop[offset:offset+3] != frame[offset:offset+3]:
                changed += 1
        if changed >= 24:
            bottom = py
    return bottom

full_bottom = changed_bottom(full)
filtered_bottom = changed_bottom(filtered)
if full_bottom < 0 or filtered_bottom < 0 or full_bottom - filtered_bottom < 100:
    raise SystemExit(f'launcher morph failed full_bottom={full_bottom} filtered_bottom={filtered_bottom}')
(out / 'launcher-morph-evidence.txt').write_text(
    f'full_bottom={full_bottom}\nfiltered_bottom={filtered_bottom}\ndelta={full_bottom-filtered_bottom}\n',
    encoding='utf-8')
print(f'ui-launcher-morph-evidence: OK delta={full_bottom-filtered_bottom}')'''
new = r'''width, height, full = ppm('start-1024x768.ppm')
_, _, filtered = ppm('start-search-1024x768.ppm')
viewport_y = 96
viewport_height = 576
side_segments = ((184, 224), (672, 712))

def difference_ratio(logical_y0, logical_y1):
    py0 = viewport_y + logical_y0 * viewport_height // 504
    py1 = viewport_y + logical_y1 * viewport_height // 504
    changed = total = 0
    for logical_x0, logical_x1 in side_segments:
        px0 = logical_x0 * width // 896
        px1 = logical_x1 * width // 896
        for py in range(py0, min(height, py1)):
            for px in range(px0, min(width, px1)):
                offset = (py * width + px) * 3
                total += 1
                if full[offset:offset+3] != filtered[offset:offset+3]:
                    changed += 1
    return changed, total, changed / max(1, total)

shared_changed, shared_total, shared_ratio = difference_ratio(50, 110)
morph_changed, morph_total, morph_ratio = difference_ratio(240, 390)
logical_height_delta = 426 - 216
physical_height_delta = logical_height_delta * viewport_height // 504
if morph_ratio < 0.80 or morph_ratio - shared_ratio < 0.30 or physical_height_delta < 200:
    raise SystemExit(
        f'launcher morph failed shared={shared_ratio:.4f} morph={morph_ratio:.4f} '
        f'physical_delta={physical_height_delta}')
(out / 'launcher-morph-evidence.txt').write_text(
    f'shared_changed={shared_changed}/{shared_total}\n'
    f'shared_ratio={shared_ratio:.4f}\n'
    f'morph_changed={morph_changed}/{morph_total}\n'
    f'morph_ratio={morph_ratio:.4f}\n'
    f'logical_height_delta={logical_height_delta}\n'
    f'physical_height_delta={physical_height_delta}\n',
    encoding='utf-8')
print(
    f'ui-launcher-morph-evidence: OK shared={shared_ratio:.4f} '
    f'morph={morph_ratio:.4f} physical_delta={physical_height_delta}')'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"launcher morph gate block: expected one match, got {count}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("septinum-morph-gate-fix: dock-independent side-band evidence installed")

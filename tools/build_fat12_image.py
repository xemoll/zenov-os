#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import struct
import sys

BYTES_PER_SECTOR = 512
TOTAL_SECTORS = 2880
SECTORS_PER_FAT = 9
FAT_COUNT = 2
ROOT_ENTRIES = 224
ROOT_SECTORS = (ROOT_ENTRIES * 32 + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
FIRST_ROOT_SECTOR = 1 + FAT_COUNT * SECTORS_PER_FAT
FIRST_DATA_SECTOR = FIRST_ROOT_SECTOR + ROOT_SECTORS
IMAGE_SIZE = BYTES_PER_SECTOR * TOTAL_SECTORS


class ImageError(Exception):
    pass


def set_fat12_entry(fat: bytearray, cluster: int, value: int) -> None:
    offset = cluster + cluster // 2
    if cluster & 1:
        current = fat[offset] | (fat[offset + 1] << 8)
        current = (current & 0x000F) | ((value & 0x0FFF) << 4)
    else:
        current = fat[offset] | (fat[offset + 1] << 8)
        current = (current & 0xF000) | (value & 0x0FFF)
    fat[offset] = current & 0xFF
    fat[offset + 1] = (current >> 8) & 0xFF


def build(boot_path: pathlib.Path, kernel_path: pathlib.Path, output_path: pathlib.Path) -> None:
    boot = boot_path.read_bytes()
    kernel = kernel_path.read_bytes()
    if len(boot) != BYTES_PER_SECTOR:
        raise ImageError(f"boot sector must be 512 bytes, got {len(boot)}")
    if boot[510:512] != b"\x55\xaa":
        raise ImageError("boot signature 0xAA55 is missing")
    cluster_count = (len(kernel) + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
    max_clusters = TOTAL_SECTORS - FIRST_DATA_SECTOR
    if cluster_count < 1:
        raise ImageError("kernel is empty")
    if cluster_count > max_clusters:
        raise ImageError(f"kernel needs {cluster_count} clusters, only {max_clusters} available")

    image = bytearray(IMAGE_SIZE)
    image[:BYTES_PER_SECTOR] = boot

    fat = bytearray(SECTORS_PER_FAT * BYTES_PER_SECTOR)
    fat[0:3] = b"\xf0\xff\xff"
    first_cluster = 2
    for index in range(cluster_count):
        cluster = first_cluster + index
        value = 0x0FFF if index == cluster_count - 1 else cluster + 1
        set_fat12_entry(fat, cluster, value)

    fat1_offset = BYTES_PER_SECTOR
    fat2_offset = fat1_offset + len(fat)
    image[fat1_offset:fat1_offset + len(fat)] = fat
    image[fat2_offset:fat2_offset + len(fat)] = fat

    root_offset = FIRST_ROOT_SECTOR * BYTES_PER_SECTOR
    volume = bytearray(32)
    volume[0:11] = b"ZENOVOS    "
    volume[11] = 0x08
    image[root_offset:root_offset + 32] = volume

    entry = bytearray(32)
    entry[0:11] = b"KERNEL  BIN"
    entry[11] = 0x20
    struct.pack_into("<H", entry, 26, first_cluster)
    struct.pack_into("<I", entry, 28, len(kernel))
    image[root_offset + 32:root_offset + 64] = entry

    data_offset = FIRST_DATA_SECTOR * BYTES_PER_SECTOR
    image[data_offset:data_offset + len(kernel)] = kernel
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a deterministic ZenovOS FAT12 image")
    parser.add_argument("--boot", type=pathlib.Path, required=True)
    parser.add_argument("--kernel", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        build(args.boot, args.kernel, args.output)
    except (OSError, ImageError) as exc:
        print(f"fat12-image: error: {exc}", file=sys.stderr)
        return 1
    print(f"fat12-image: wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

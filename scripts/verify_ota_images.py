#!/usr/bin/env python3
"""Validate release OTA/combined images before publishing."""

import argparse
import struct
import sys
import zlib
from pathlib import Path

ESP_IMAGE_MAGIC = 0xE9
RBOOT_IMAGE_MAGIC_NEW1 = 0xEA
RBOOT_IMAGE_MAGIC_NEW2 = 0x04
ESP_FLASH_MODE_DOUT = 0x03
ESP_IMAGE_CHECKSUM_INIT = 0xEF
OTA_MANIFEST_MAGIC = b"IRACOTA1"
OTA_MANIFEST_SIZE = 64
OTA_MANIFEST_VERSION = 1
OTA_MANIFEST_FORMAT_RBOOT_EA04 = 1
OTA_MANIFEST_TARGET_ANY = 0xFF
OTA_MANIFEST_CRC_OFFSET = 16
OTA_BOARD_ID = b"GK01_IR_MINI_V105"
OTA_SLOT_MAX_SIZE = 0xFE000
COMBINED_ROM0_OFFSET = 0x2000
RBOOT_CONFIG_OFFSET = 0x1000
ROM0_ADDRESS = 0x002000
ROM1_ADDRESS = 0x102000


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def crc32_with_zeroed_field(data, field_offset, field_size):
    view = bytearray(data)
    view[field_offset:field_offset + field_size] = b"\x00" * field_size
    return zlib.crc32(view) & 0xFFFFFFFF


def fixed_text(raw):
    return raw.split(b"\x00", 1)[0]


def verify_rboot_ota_image(image):
    if len(image) < 16 + OTA_MANIFEST_SIZE:
        raise ValueError("image too small")
    if len(image) > OTA_SLOT_MAX_SIZE:
        raise ValueError(f"image exceeds OTA slot: {len(image)} > {OTA_SLOT_MAX_SIZE}")
    if image[0] != RBOOT_IMAGE_MAGIC_NEW1 or image[1] != RBOOT_IMAGE_MAGIC_NEW2:
        raise ValueError("missing EA04 rboot image header")
    if image[2] != ESP_FLASH_MODE_DOUT:
        raise ValueError("image is not DOUT flash mode")

    entry = u32(image, 4)
    irom_add = u32(image, 8)
    irom_len = u32(image, 12)
    if (
        irom_add != 0
        or irom_len < OTA_MANIFEST_SIZE
        or 16 + irom_len + 8 > len(image)
    ):
        raise ValueError("invalid IROM layout")

    manifest_offset = 16 + irom_len - OTA_MANIFEST_SIZE
    manifest = image[manifest_offset:manifest_offset + OTA_MANIFEST_SIZE]
    (
        magic,
        version,
        fmt,
        target,
        flash_mode,
        image_size,
        image_crc32,
        board,
        version_text,
        reserved,
    ) = struct.unpack("<8sBBBBII20s20sI", manifest)
    if magic != OTA_MANIFEST_MAGIC:
        raise ValueError("missing IRACOTA1 manifest")
    if version != OTA_MANIFEST_VERSION or fmt != OTA_MANIFEST_FORMAT_RBOOT_EA04:
        raise ValueError("unsupported manifest version or format")
    if target != OTA_MANIFEST_TARGET_ANY:
        raise ValueError("release OTA image must use target=any")
    if flash_mode != ESP_FLASH_MODE_DOUT:
        raise ValueError("manifest flash mode is not DOUT")
    if image_size != len(image):
        raise ValueError("manifest image_size mismatch")
    if fixed_text(board) != OTA_BOARD_ID:
        raise ValueError("manifest board mismatch")
    if reserved != 0:
        raise ValueError("manifest reserved field must be zero")

    crc_field_offset = manifest_offset + OTA_MANIFEST_CRC_OFFSET
    actual_crc32 = crc32_with_zeroed_field(image, crc_field_offset, 4)
    if actual_crc32 != image_crc32:
        raise ValueError(
            f"manifest CRC mismatch: image=0x{actual_crc32:08x}, manifest=0x{image_crc32:08x}"
        )

    normal_off = 16 + irom_len
    if image[normal_off] != ESP_IMAGE_MAGIC or image[normal_off + 2] != ESP_FLASH_MODE_DOUT:
        raise ValueError("invalid RAM image header")
    if u32(image, normal_off + 4) != entry:
        raise ValueError("entry address mismatch")

    section_count = image[normal_off + 1]
    if section_count == 0 or section_count > 16:
        raise ValueError("invalid section count")

    pos = normal_off + 8
    checksum = ESP_IMAGE_CHECKSUM_INIT
    for _ in range(section_count):
        if pos + 8 > len(image):
            raise ValueError("missing section header")
        section_addr = u32(image, pos)
        section_len = u32(image, pos + 4)
        pos += 8
        if section_len == 0 or 0x40200000 <= section_addr < 0x40300000:
            raise ValueError("invalid RAM section layout")
        if pos + section_len > len(image):
            raise ValueError("section exceeds image size")
        for byte in image[pos:pos + section_len]:
            checksum ^= byte
        pos += section_len

    checksum_pos = pos | 0x0F
    if checksum_pos >= len(image):
        raise ValueError("missing ESP image checksum")
    if image[checksum_pos] != checksum:
        raise ValueError(
            f"ESP checksum mismatch: image=0x{image[checksum_pos]:02x}, calculated=0x{checksum:02x}"
        )
    if len(image) != checksum_pos + 1:
        raise ValueError("unexpected trailing data")

    return fixed_text(version_text).decode("ascii", errors="replace") or "dev"


def verify_combined_image(data):
    if len(data) <= COMBINED_ROM0_OFFSET:
        raise ValueError("combined image is too small")
    if data[0] != ESP_IMAGE_MAGIC:
        raise ValueError("combined image does not start with rboot ESP image")
    config = data[RBOOT_CONFIG_OFFSET:RBOOT_CONFIG_OFFSET + 16]
    if len(config) != 16 or config[0] != 0xE1 or config[1] != 0x01:
        raise ValueError("missing rboot config at 0x1000")
    rom0 = u32(config, 8)
    rom1 = u32(config, 12)
    if rom0 != ROM0_ADDRESS or rom1 != ROM1_ADDRESS:
        raise ValueError(
            f"unexpected rboot config: ROM0=0x{rom0:x}, ROM1=0x{rom1:x}"
        )
    return verify_rboot_ota_image(data[COMBINED_ROM0_OFFSET:])


def verify_path(path):
    data = path.read_bytes()
    rom0_magic = bytes([RBOOT_IMAGE_MAGIC_NEW1, RBOOT_IMAGE_MAGIC_NEW2])
    has_embedded_rom0 = (
        len(data) > COMBINED_ROM0_OFFSET
        and data[COMBINED_ROM0_OFFSET:COMBINED_ROM0_OFFSET + 2] == rom0_magic
    )
    if has_embedded_rom0:
        version = verify_combined_image(data)
        print(f"OK combined: {path} ({len(data)} bytes, version={version})")
    else:
        version = verify_rboot_ota_image(data)
        print(f"OK ota:      {path} ({len(data)} bytes, version={version})")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("images", nargs="+", type=Path)
    args = parser.parse_args()

    failed = False
    for path in args.images:
        try:
            verify_path(path)
        except Exception as exc:
            failed = True
            print(f"ERROR {path}: {exc}", file=sys.stderr)
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()

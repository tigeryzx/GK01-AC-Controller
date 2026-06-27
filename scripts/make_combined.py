#!/usr/bin/env python3
"""
Generate combined flash image: rboot + ROM0 in a single .bin
so users flash with one esptool command instead of two.
"""
import sys
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RBOOT = ROOT / "firmware/rboot-bootloader/rboot.bin"
ROM0 = ROOT / "firmware/flash_images/rom0.bin"
OUTPUT = ROOT / "firmware/flash_images/combined.bin"

RBOOT_MAX_SIZE = 0x2000
RBOOT_CONFIG_OFFSET = 0x1000
ROM0_ADDRESS = 0x002000
ROM1_ADDRESS = 0x102000
ESP_IMAGE_MAGIC = 0xE9
RBOOT_IMAGE_MAGIC_NEW1 = 0xEA
RBOOT_IMAGE_MAGIC_NEW2 = 0x04
ESP_IMAGE_CHECKSUM_INIT = 0xEF
OTA_MANIFEST_MAGIC = b"IRACOTA1"
OTA_MANIFEST_SIZE = 64


def make_rboot_config():
    sector = bytearray(b"\xff" * 0x1000)
    sector[:16] = struct.pack("<BBBBBBBBII",
                              0xE1, 0x01, 0x00, 0x00,
                              0x00, 0x02, 0x00, 0x00,
                              ROM0_ADDRESS, ROM1_ADDRESS)
    return sector


def normalize_esp8266_checksum(image):
    if len(image) < 8 or image[0] != ESP_IMAGE_MAGIC:
        return False

    segment_count = image[1]
    pos = 8
    checksum = ESP_IMAGE_CHECKSUM_INIT
    for _ in range(segment_count):
        if pos + 8 > len(image):
            return False
        length = struct.unpack_from("<I", image, pos + 4)[0]
        pos += 8
        if pos + length > len(image):
            return False
        for b in image[pos:pos + length]:
            checksum ^= b
        pos += length

    checksum_pos = pos | 0x0F
    if len(image) <= checksum_pos:
        image.extend(b"\xff" * (checksum_pos + 1 - len(image)))
    else:
        del image[checksum_pos + 1:]
    image[checksum_pos] = checksum
    return True


def has_ota_manifest(image):
    if len(image) < 16 + OTA_MANIFEST_SIZE:
        return False
    if image[0] != RBOOT_IMAGE_MAGIC_NEW1 or image[1] != RBOOT_IMAGE_MAGIC_NEW2:
        return False
    irom_len = struct.unpack_from("<I", image, 12)[0]
    if irom_len < OTA_MANIFEST_SIZE or 16 + irom_len > len(image):
        return False
    manifest_offset = 16 + irom_len - OTA_MANIFEST_SIZE
    return image[manifest_offset:manifest_offset + len(OTA_MANIFEST_MAGIC)] == OTA_MANIFEST_MAGIC


def main():
    if not RBOOT.exists():
        print(f"ERROR: {RBOOT} not found. Build rboot first.")
        sys.exit(1)
    if not ROM0.exists():
        print(f"ERROR: {ROM0} not found. Run prepare_flash.py first.")
        sys.exit(1)

    rboot_data = bytearray(RBOOT.read_bytes())
    rom0_data = ROM0.read_bytes()

    if not normalize_esp8266_checksum(rboot_data):
        print(f"ERROR: unable to normalize rboot checksum: {RBOOT}")
        sys.exit(1)

    if (
        len(rom0_data) < 2
        or rom0_data[0] != RBOOT_IMAGE_MAGIC_NEW1
        or rom0_data[1] != RBOOT_IMAGE_MAGIC_NEW2
    ):
        print(
            f"ERROR: {ROM0} is not an rboot new image (expected EA 04). "
            "Run firmware/prepare_flash.py first."
        )
        sys.exit(1)
    if not has_ota_manifest(rom0_data):
        print(f"ERROR: {ROM0} is missing IRACOTA1 manifest. Run firmware/prepare_flash.py first.")
        sys.exit(1)

    if len(rboot_data) > RBOOT_MAX_SIZE:
        print(f"ERROR: rboot.bin too large: {len(rboot_data)} > {RBOOT_MAX_SIZE}")
        sys.exit(1)

    prefix = bytearray(b"\xff" * RBOOT_MAX_SIZE)
    prefix[:len(rboot_data)] = rboot_data
    prefix[RBOOT_CONFIG_OFFSET:RBOOT_CONFIG_OFFSET + 0x1000] = make_rboot_config()

    with open(OUTPUT, "wb") as f:
        f.write(prefix)
        f.write(rom0_data)

    total = OUTPUT.stat().st_size
    print(f"  rboot:   {len(rboot_data):>7} bytes")
    print(f"  config:  {0x1000:>7} bytes (ROM0=0x2000, ROM1=0x102000)")
    print(f"  ROM0:    {len(rom0_data):>7} bytes")
    print(f"  TOTAL:   {total:>7} bytes ({total / 1024:.1f} KB)")
    print(f"  Layout:  0x0000 rboot | 0x1000 config | 0x2000 ROM0")
    print(f"  Flash:   esptool write-flash -fm dout 0x0 {OUTPUT.name}")


if __name__ == "__main__":
    main()

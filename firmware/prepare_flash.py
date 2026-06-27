#!/usr/bin/env python3
"""
Post-build script for rboot dual-partition firmware.

Strips eboot from PlatformIO firmware.bin, converts the Arduino app image to
the rboot "new image" layout, and generates ready-to-flash binary images.

Usage: python prepare_flash.py [rom0|rom1|both]
"""

import sys
import struct
from pathlib import Path

FIRMWARE_DIR = Path(__file__).parent
BUILD_ROM0 = FIRMWARE_DIR / ".pio/build/rom0/firmware.bin"
BUILD_ROM1 = FIRMWARE_DIR / ".pio/build/rom1/firmware.bin"
BUILD_DIAG = FIRMWARE_DIR / ".pio/build/diag/firmware.bin"
OUTPUT_DIR = FIRMWARE_DIR / "flash_images"
RBOOT_BIN = FIRMWARE_DIR / "rboot-bootloader/rboot.bin"

EBOOT_SIZE = 0x1000
RBOOT_PAD_SIZE = 0x2000
RBOOT_CONFIG_OFFSET = 0x1000
ROM0_ADDRESS = 0x002000
ROM1_ADDRESS = 0x102000
ESP_IMAGE_MAGIC = 0xE9
ESP_IMAGE_MAGIC_NEW1 = 0xEA
ESP_IMAGE_MAGIC_NEW2 = 0x04
ESP_FLASH_MODE_DOUT = 0x03
ESP_IMAGE_CHECKSUM_INIT = 0xEF
IROM_VADDR_BASE = 0x40200000
IROM_BANK_SIZE = 0x100000


def make_rboot_config():
    # rboot_config with MAX_ROMS=2, BOOT_CONFIG_CHKSUM disabled:
    # magic, version, mode, current_rom, gpio_rom, count, unused[2], roms[2]
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


def parse_esp8266_image(image):
    if len(image) < 8 or image[0] != ESP_IMAGE_MAGIC:
        return None

    segment_count = image[1]
    flags1 = image[2]
    flags2 = image[3]
    entry = struct.unpack_from("<I", image, 4)[0]
    pos = 8
    sections = []
    for _ in range(segment_count):
        if pos + 8 > len(image):
            return None
        addr, length = struct.unpack_from("<II", image, pos)
        pos += 8
        if pos + length > len(image):
            return None
        sections.append((addr, bytes(image[pos:pos + length])))
        pos += length
    return flags1, flags2, entry, sections


def is_irom_addr(addr):
    return IROM_VADDR_BASE <= addr < IROM_VADDR_BASE + IROM_BANK_SIZE


def make_rboot_new_image(app_data, rom_address):
    parsed = parse_esp8266_image(app_data)
    if parsed is None:
        return None, "invalid ESP8266 app image"

    flags1, flags2, entry, sections = parsed
    if not sections:
        return None, "app image has no sections"

    irom_addr, irom_data = sections[0]
    if not is_irom_addr(irom_addr):
        return None, f"first section is not IROM: 0x{irom_addr:08x}"

    if any(is_irom_addr(addr) for addr, _ in sections[1:]):
        return None, "multiple IROM sections are not supported"

    expected_irom_addr = IROM_VADDR_BASE + (rom_address % IROM_BANK_SIZE) + 0x10
    if irom_addr != expected_irom_addr:
        return None, (
            f"IROM address mismatch: image=0x{irom_addr:08x}, "
            f"expected=0x{expected_irom_addr:08x}"
        )

    ram_sections = sections[1:]
    if len(ram_sections) > 255:
        return None, "too many RAM sections"

    out = bytearray()
    out += struct.pack("<BBBBIII",
                       ESP_IMAGE_MAGIC_NEW1, ESP_IMAGE_MAGIC_NEW2,
                       flags1, flags2, entry, 0, len(irom_data))
    out += irom_data
    out += struct.pack("<BBBBI",
                       ESP_IMAGE_MAGIC, len(ram_sections),
                       flags1, flags2, entry)

    checksum = ESP_IMAGE_CHECKSUM_INIT
    for addr, data in ram_sections:
        out += struct.pack("<II", addr, len(data))
        out += data
        for b in data:
            checksum ^= b

    checksum_pos = len(out) | 0x0F
    if len(out) <= checksum_pos:
        out.extend(b"\xff" * (checksum_pos + 1 - len(out)))
    out[checksum_pos] = checksum
    return out, None


def strip_eboot(src, dst, rom_address):
    if not src.exists():
        print(f"ERROR: {src} not found. Build first with: pio run -e rom0")
        return False
    data = src.read_bytes()
    app_data = bytearray(data[EBOOT_SIZE:])
    if len(app_data) < 4 or app_data[0] != ESP_IMAGE_MAGIC:
        print(f"ERROR: stripped image has invalid ESP8266 header: {src}")
        return False
    if app_data[2] != ESP_FLASH_MODE_DOUT:
        print(f"  forcing {dst.name} flash mode {app_data[2]} -> DOUT")
        app_data[2] = ESP_FLASH_MODE_DOUT
    rboot_image, error = make_rboot_new_image(app_data, rom_address)
    if rboot_image is None:
        print(f"ERROR: unable to convert app image for rboot: {error}")
        return False
    dst.write_bytes(rboot_image)
    print(
        f"  {src.name} ({len(data)}B) -> {dst.name} "
        f"({len(rboot_image)}B, rboot new image)"
    )
    return True


def make_combined(rom0_path, name="combined.bin"):
    dst = OUTPUT_DIR / name
    rboot_data = bytearray(RBOOT_BIN.read_bytes())
    if not normalize_esp8266_checksum(rboot_data):
        print(f"ERROR: unable to normalize rboot checksum: {RBOOT_BIN}")
        return False
    rom0_data = rom0_path.read_bytes()

    if len(rboot_data) > RBOOT_PAD_SIZE:
        print(f"ERROR: rboot.bin too large: {len(rboot_data)} > {RBOOT_PAD_SIZE}")
        return False

    prefix = bytearray(b"\xff" * RBOOT_PAD_SIZE)
    prefix[:len(rboot_data)] = rboot_data
    prefix[RBOOT_CONFIG_OFFSET:RBOOT_CONFIG_OFFSET + 0x1000] = make_rboot_config()

    with dst.open("wb") as f:
        f.write(prefix)
        f.write(rom0_data)

    print(
        f"  {name} ({dst.stat().st_size}B, "
        "0x0000 rboot + 0x1000 config + 0x2000 ROM0)"
    )
    return True


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "both"

    OUTPUT_DIR.mkdir(exist_ok=True)

    if not RBOOT_BIN.exists():
        print(f"ERROR: {RBOOT_BIN} not found. Build first: python rboot-bootloader/build_rboot.py")
        sys.exit(1)

    print(f"rboot.bin: {RBOOT_BIN.stat().st_size} bytes")

    rom0_path = OUTPUT_DIR / "rom0.bin"
    rom0_ready = False

    if mode in ("rom0", "both"):
        print("Preparing ROM 0...")
        rom0_ready = strip_eboot(BUILD_ROM0, rom0_path, ROM0_ADDRESS)

    if mode in ("rom1", "both"):
        print("Preparing ROM 1...")
        strip_eboot(BUILD_ROM1, OUTPUT_DIR / "rom1.bin", ROM1_ADDRESS)

    if mode in ("diag",):
        print("Preparing diagnostic ROM...")
        diag_path = OUTPUT_DIR / "diag.bin"
        if strip_eboot(BUILD_DIAG, diag_path, ROM0_ADDRESS):
            print("Preparing diagnostic combined image...")
            make_combined(diag_path, "diag-combined.bin")

    if rom0_ready:
        print("Preparing combined first-flash image...")
        make_combined(rom0_path)

    print()
    print(f"Output: {OUTPUT_DIR}/")
    for f in OUTPUT_DIR.iterdir():
        print(f"  {f.name}: {f.stat().st_size} bytes")
    print()
    print("Flash commands (USB-TTL, IO0 shorted to GND):")
    print()
    print("  # Recovery / first flash (recommended)")
    print("  # 1. Erase entire flash")
    print("  python -m esptool --port COMx --baud 115200 --before no-reset erase-flash")
    print()
    print("  # 2. Flash combined image at 0x0 (includes rboot config)")
    print(
        "  python -m esptool --port COMx --baud 115200 --before no-reset "
        "write-flash -fm dout 0x0 flash_images/combined.bin"
    )
    print()
    print("  # Advanced split flash only (erase first, or stale rboot config may boot a bad slot)")
    print("  # Flash rboot bootloader at 0x0000")
    print(
        "  python -m esptool --port COMx --baud 115200 --before no-reset "
        "write-flash -fm dout 0x0000 rboot-bootloader/rboot.bin"
    )
    print()
    print("  # Flash ROM 0 application at 0x2000")
    print(
        "  python -m esptool --port COMx --baud 115200 --before no-reset "
        "write-flash -fm dout 0x2000 flash_images/rom0.bin"
    )
    print()
    print("  # Optional: flash ROM 1 for testing at 0x102000")
    print(
        "  python -m esptool --port COMx --baud 115200 --before no-reset "
        "write-flash -fm dout 0x102000 flash_images/rom1.bin"
    )
    print()
    print("Do NOT flash .pio/build/.../firmware.bin to 0x0 for this rboot layout.")
    print("Do NOT upload combined.bin, rboot.bin, or .pio/build/.../firmware.bin in WebUI OTA.")
    print("For WebUI OTA, upload the alternate ROM shown by the WebUI: flash_images/rom0.bin or flash_images/rom1.bin.")
    print()
    print("combined.bin includes rboot config: ROM0=0x2000, ROM1=0x102000.")
    print("OTA updates will write to the alternate ROM automatically.")


if __name__ == "__main__":
    main()

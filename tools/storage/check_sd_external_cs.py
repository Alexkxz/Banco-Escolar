#!/usr/bin/env python3
"""Source-level regression checks for the external SD chip-select path."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DRIVER = (ROOT / "src/storage/sd_diskio_external_cs.cpp").read_text(encoding="utf-8")
MANAGER = (ROOT / "src/storage/sd_manager.cpp").read_text(encoding="utf-8")
SELECTOR = (ROOT / "src/storage/sd_external_cs.h").read_text(encoding="utf-8")


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unclosed function: {signature}")


select = body(DRIVER, "bool sdSelectCard(uint8_t pdrv)")
deselect = body(DRIVER, "void sdDeselectCard(uint8_t pdrv)")
init = body(DRIVER, "uint8_t sdcard_init_external_cs(")
manager_begin = body(MANAGER, "bool SDManager::begin(")
release_bus = body(MANAGER, "bool SDManager::releaseBus(")
cs_callback = body(MANAGER, "bool set_external_sd_cs(")
disk_write = body(DRIVER, "DRESULT ff_sd_write(")

assert "digitalPinToGPIONumber" not in DRIVER
assert "pinMode(" not in DRIVER and "digitalWrite(" not in DRIVER
assert "sdcard_init(" not in MANAGER and "SD_SPI_CS_GPIO" not in MANAGER
assert "card->chip_select.configure(cs_callback, cs_context)" in init
assert "callback_(context_, true)" in body(SELECTOR, "bool select()")
assert "callback_(context_, false)" in body(SELECTOR, "bool deselect()")
assert select.index("chip_select.select()") < select.index("sdWait(pdrv, 500)")
assert "sdDeselectCard(pdrv);" in select[select.index("if (!s)") :]
assert "card->chip_select.deselect()" in deselect and "card->cs_error = true" in deselect
assert "set_external_sd_cs, expander_" in manager_begin
assert "expander_->digitalWrite(SD_CS_EXIO, HIGH)" in manager_begin
assert "expander_->digitalWrite(SD_CS_EXIO, HIGH)" in release_bus
assert "selected ? LOW : HIGH" in cs_callback
assert "SD_FORMAT_IF_EMPTY = false" in MANAGER
assert "return RES_WRPRT;" in disk_write
assert "sdWriteSector(" not in disk_write and "sdWriteSectors(" not in disk_write

print("PASS: CS se activa antes de esperar/transmitir y se libera tras timeout/error.")
print("PASS: el driver no configura ni escribe un GPIO para CS (incluido GPIO 255).")
print("PASS: CH422G reutilizado; montaje sin formato y escritura FAT bloqueada.")

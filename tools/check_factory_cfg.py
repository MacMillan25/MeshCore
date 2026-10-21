#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Проверка, что собранные прошивки можно патчить настройками.

Смотрит образ каждой роли (UF2 из .pio/build или out/uf2) и проверяет блок
заводских настроек: метка `DARKTEC-FACTORY\\x01` должна встречаться РОВНО один
раз, заголовок — быть валидным (версия раскладки, длина, CRC32). Иначе
DarkFlasher откажется вшивать настройки, и это будет неприятной неожиданностью
уже на релизе.

Запуск (из корня репозитория):

    python tools/check_factory_cfg.py                # все env из platformio.ini
    python tools/check_factory_cfg.py -e Darktec_repeater
    python tools/check_factory_cfg.py --dir out/uf2  # готовые UF2
"""
from __future__ import annotations

import argparse
import glob
import os
import struct
import sys
import zlib

MARKER = b"DARKTEC-FACTORY\x01"
HEADER = 24
PAYLOAD = 128
VERSION = 1

# Роли, у которых своего блока нет по устройству (другая модель настроек).
NO_BLOCK = {"Darktec_kiss_modem", "Darktec_terminal_chat"}

ENVS = [
    "Darktec_companion_radio_ble",
    "Darktec_companion_radio_usb",
    "Darktec_repeater",
    "Darktec_repeater_bridge_rs232_serial1",
    "Darktec_room_server",
    "Darktec_sensor",
    "Darktec_terminal_chat",
    "Darktec_kiss_modem",
]


def uf2_to_image(path: str) -> tuple[bytes, int]:
    data = open(path, "rb").read()
    if len(data) % 512:
        raise ValueError("размер не кратен 512")
    chunks: dict[int, bytes] = {}
    base = None
    end = 0
    for i in range(len(data) // 512):
        block = data[i * 512:(i + 1) * 512]
        m0, m1, _flags, addr, size = struct.unpack_from("<5I", block, 0)
        if (m0, m1) != (0x0A324655, 0x9E5D5157):
            raise ValueError(f"блок {i}: не та сигнатура")
        size = min(size, 476)
        chunks[addr] = block[32:32 + size]
        base = addr if base is None else min(base, addr)
        end = max(end, addr + size)
    img = bytearray(b"\xff" * (end - base))
    for addr, chunk in chunks.items():
        img[addr - base:addr - base + len(chunk)] = chunk
    return bytes(img), base


def find_blob(image: bytes) -> list[int]:
    out = []
    start = 0
    while True:
        i = image.find(MARKER, start)
        if i < 0:
            return out
        start = i + 1
        if i + HEADER + PAYLOAD > len(image):
            continue
        if image[i + 16] != VERSION:
            continue
        length = image[i + 17]
        if length == 0:
            out.append(i)
        elif length == PAYLOAD:
            crc = int.from_bytes(image[i + 20:i + 24], "little")
            body = image[i + HEADER:i + HEADER + PAYLOAD]
            if (zlib.crc32(body) & 0xFFFFFFFF) == crc:
                out.append(i)


def check(path: str, env: str) -> tuple[bool, str]:
    try:
        image, base = uf2_to_image(path)
    except (OSError, ValueError) as e:
        return False, f"не читается: {e}"

    total = image.count(MARKER)
    blocks = find_blob(image)
    expect = env not in NO_BLOCK

    if not expect:
        if total == 0:
            return True, "блока нет — так и задумано"
        return True, f"блока нет по устройству, но меток {total} (не страшно)"
    if total != 1:
        return False, f"меток {total}, а должна быть ровно одна"
    if not blocks:
        return False, "метка есть, заголовок не сходится"
    pos = blocks[0]
    return True, f"ок: блок 0x{base + pos:X} (v{image[pos + 16]}, длина {image[pos + 17]})"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-e", "--env", action="append", help="env (можно несколько)")
    ap.add_argument("--dir", help="каталог с готовыми .uf2 вместо .pio/build")
    args = ap.parse_args()

    envs = args.env or ENVS
    bad = 0
    for env in envs:
        if args.dir:
            # В out/uf2 лежат файлы разных версий — берём самый свежий.
            found = glob.glob(os.path.join(args.dir, f"{env}-*.uf2")) + \
                    glob.glob(os.path.join(args.dir, f"{env}.uf2"))
            found = [f for f in found if os.path.isfile(f)]
            path = max(found, key=os.path.getmtime) if found else ""
        else:
            path = os.path.join(".pio", "build", env, "firmware.uf2")
        if not path or not os.path.isfile(path):
            print(f"{env:<42} НЕТ ФАЙЛА ({path or args.dir})")
            bad += 1
            continue
        ok, msg = check(path, env)
        print(f"{env:<42} {'OK ' if ok else 'FAIL'} {msg}")
        if not ok:
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

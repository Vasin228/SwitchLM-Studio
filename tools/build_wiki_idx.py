#!/usr/bin/env python3
"""
SwitchLM Studio - Zero-RAM Binary Wiki Index Builder
Compiles game guide keywords and snippets into binary .idx files for Nintendo Switch.
"""

import sys
import os
import struct
import json

MAGIC_SWWK = 0x4B575753 # "SWWK" in little endian

def fnv1a_64(text: str) -> int:
    """Calculates 64-bit FNV-1a hash matching wiki_fnv1a in wiki_rag.c."""
    h = 0xcbf29ce484222325
    prime = 0x100000001b3
    for b in text.encode('utf-8'):
        h ^= b
        h = (h * prime) & 0xFFFFFFFFFFFFFFFF
    return h

def build_index_file(output_path: str, entries_dict: dict):
    """
    entries_dict: { "keyword/stem": "Russian/English guide text", ... }
    """
    entries = []
    for keyword, text in entries_dict.items():
        h = fnv1a_64(keyword)
        text_bytes = text.encode('utf-8')
        entries.append((h, keyword, text_bytes))

    # Sort entries by hash ascending for binary search
    entries.sort(key=lambda x: x[0])
    entry_count = len(entries)

    # Calculate offset where data starts
    # Header: 4 bytes magic + 4 bytes count = 8 bytes
    # Entries: entry_count * 16 bytes
    data_start = 8 + (entry_count * 16)

    compiled_entries = []
    data_buffer = bytearray()

    current_offset = data_start
    for h, kw, tb in entries:
        length = len(tb)
        compiled_entries.append((h, current_offset, length))
        data_buffer.extend(tb)
        current_offset += length

    # Write binary file
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "wb") as f:
        # Write header
        f.write(struct.pack("<II", MAGIC_SWWK, entry_count))
        # Write index table
        for h, off, length in compiled_entries:
            f.write(struct.pack("<QIHH", h, off, length, 0))
        # Write payload
        f.write(data_buffer)

    print(f"[OK] Generated {output_path}: {entry_count} entries, {os.path.getsize(output_path)} bytes.")

if __name__ == "__main__":
    # Zelda Tears of the Kingdom (0100F2C0115B6000) sample wiki
    totk_wiki = {
        "Босс / Тактика": "Тактика на Ганона: Парируйте атаки щитом в идеальный тайминг. Используйте слияние с рогами белых линелей.",
        "Где найти предмет?": "Высший Меч (Master Sword): Находится на голове Светлого Дракона. Требуется 2 полных круга выносливости.",
        "Что даёт этот предмет?": "Меч восстанавливает прочность сам через 10 минут. Никогда не ломается навсегда.",
        "Секреты локации": "Подземелья: Бросайте яркоцветы для освещения. Колодцы содержат фей, бомбы и тайные проходы.",
        "ганон": "Финальный босс в Глубинах под замком Хайрул. Уклоняйтесь во время замаха для Flurry Rush.",
        "святилища": "Всего в игре 152 святилища. Дают Свет благословения для прокачки сердец и выносливости.",
        "выносливость": "Качайте в статуе богини за 4 сферы света. Рекомендуется сначала 2 полных круга для меча."
    }

    # Isaac (010021C000B6A000) sample wiki
    isaac_wiki = {
        "Босс / Тактика": "Мама (Mom): Держитесь в углах комнаты, бейте ногу когда она опускается, не стойте по центру.",
        "Где найти предмет?": "Сделка с Дьяволом: Не получайте урона красными сердцами на этаже (шанс до 100%).",
        "Что даёт этот предмет?": "Brimstone: Превращает слезы в мощный кровавый лазер через всю комнату.",
        "Секреты локации": "Секретные комнаты: Ищите между 3-4 обычными комнатами, взрывайте центр стены."
    }

    # Global Switch Help
    global_wiki = {
        "управление": "R - инвентарь/меню, L - способности/бомбы, ZR - стрельба/атака, B - спринт/отмена, A - действие.",
        "оверлей": "Для вызова Tesla Overlay зажмите L + D-Pad Down + R3 (правый стик)."
    }

    build_index_file("sdout/switch/SwitchLM/wikis/0100F2C0115B6000.idx", totk_wiki)
    build_index_file("sdout/switch/SwitchLM/wikis/010021C000B6A000.idx", isaac_wiki)
    build_index_file("sdout/switch/SwitchLM/wikis/global_switch.idx", global_wiki)

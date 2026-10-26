#!/usr/bin/env python3
"""Build language-pack.bin: the UI languages and hyphenation patterns left out of the firmware.

The firmware keeps English and Vietnamese (and English and German hyphenation) built in; every other language
lives in this pack, which the reader installs into its unused `spiffs` flash partition and reads in
place (lib/I18n/LanguagePack.h). Strings are stored by the FNV-1a hash of their key name, so a pack
keeps working with a newer or older firmware: a key the pack lacks shows in English.

Layout (little endian, every section 4-byte aligned):

    header (32 B)   magic "XLPK", u16 format, u16 languageCount, u16 hyphenationCount, u16 0,
                    u32 totalSize, u32 crc32 of bytes [32, totalSize), u32 contentId, u32 0, u32 0
    languages       languageCount x 16 B: char code[4], u32 tableOffset, u32 entryCount, u32 blobOffset
    hyphenation     hyphenationCount x 16 B: char tag[4], u32 dataOffset, u32 size, u32 rootOffset
    then per language a table of entryCount x (u32 keyHash, u32 offset into its blob), sorted by
    hash, and the blob (NUL-terminated UTF-8 strings); then each hyphenation trie.

Only strings that differ from English are stored, as in the built-in tables.

Usage: python3 scripts/build_language_pack.py [--out dist/language-pack.bin] [--builtin en,vi]
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from gen_i18n import (  # noqa: E402
    check_key_hashes,
    find_used_string_keys,
    key_hash,
    load_translations,
    parse_builtin_langs,
)

MAGIC = b"XLPK"
FORMAT_VERSION = 1
HEADER_BYTES = 32
DIRECTORY_ENTRY_BYTES = 16
TRIE_DIR = ROOT / "lib/Epub/Epub/hyphenation/generated"
# Hyphenation kept in the firmware (lib/Epub/Epub/hyphenation/LanguageRegistry.cpp). German's trie
# (206 KB) would not fit beside the strings in MAX_PACK_BYTES.
BUILTIN_HYPHENATION = {"en", "de"}
# The smallest `spiffs` partition met so far: X3 readers keep the partition table they shipped with
# (an OTA or SD update writes only the app), and theirs has 1 MB at 0xEF0000, not the 3.4 MB of
# partitions.csv.
MAX_PACK_BYTES = 1024 * 1024


def align4(data: bytearray) -> None:
    while len(data) % 4:
        data.append(0)


def code_bytes(code: str) -> bytes:
    raw = code.encode("ascii")
    if len(raw) > 4:
        raise ValueError(f"Code too long for the pack: {code}")
    return raw.ljust(4, b"\0")


def load_tries() -> list[tuple[str, bytes, int]]:
    tries = []
    for header in sorted(TRIE_DIR.glob("hyph-*.trie.h")):
        tag = header.name[len("hyph-") : -len(".trie.h")]
        if tag in BUILTIN_HYPHENATION:
            continue
        text = header.read_text()
        array = re.search(r"_trie_data\[\] = \{(.*?)\};", text, re.S)
        root = re.search(r"_patterns = \{\s*(0x[0-9A-Fa-f]+)u", text)
        if not array or not root:
            raise ValueError(f"Cannot read the trie in {header}")
        data = bytes(int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})", array.group(1)))
        tries.append((tag, data, int(root.group(1), 16)))
    return tries


def build(builtin_codes: str) -> bytes:
    languages, _names, string_keys, translations, _inherited = load_translations(str(ROOT / "lib/I18n/translations"))
    check_key_hashes(string_keys)
    # Only keys the firmware uses (as its own build strips the rest).
    used = find_used_string_keys([str(ROOT / "src"), str(ROOT / "lib")])
    string_keys = [k for k in string_keys if k in used]
    builtin = parse_builtin_langs(builtin_codes, languages) or set(languages)
    packed = [(i, code) for i, code in enumerate(languages) if code not in builtin]
    tries = load_tries()

    body = bytearray()
    directory_bytes = (len(packed) + len(tries)) * DIRECTORY_ENTRY_BYTES
    offset_base = HEADER_BYTES + directory_bytes
    language_entries = []
    for index, code in packed:
        entries = []
        blob = bytearray()
        for key in string_keys:
            text = translations[key][index]
            if text == translations[key][0]:
                continue  # same as English: the firmware falls back to it
            entries.append((key_hash(key), len(blob)))
            blob += text.encode("utf-8") + b"\0"
        entries.sort()
        table_offset = offset_base + len(body)
        for h, off in entries:
            body += struct.pack("<II", h, off)
        blob_offset = offset_base + len(body)
        body += blob
        align4(body)
        language_entries.append((code, table_offset, len(entries), blob_offset))

    trie_entries = []
    for tag, data, root in tries:
        data_offset = offset_base + len(body)
        body += data
        align4(body)
        trie_entries.append((tag, data_offset, len(data), root))

    directory = bytearray()
    for code, table_offset, count, blob_offset in language_entries:
        directory += code_bytes(code) + struct.pack("<III", table_offset, count, blob_offset)
    for tag, data_offset, size, root in trie_entries:
        directory += code_bytes(tag) + struct.pack("<III", data_offset, size, root)

    rest = bytes(directory + body)
    total = HEADER_BYTES + len(rest)
    crc = zlib.crc32(rest) & 0xFFFFFFFF
    content_id = zlib.crc32(rest, 0x5A5A5A5A) & 0xFFFFFFFF
    header = MAGIC + struct.pack(
        "<HHHHIIIII", FORMAT_VERSION, len(language_entries), len(trie_entries), 0, total, crc, content_id, 0, 0
    )
    assert len(header) == HEADER_BYTES
    if total > MAX_PACK_BYTES:
        raise ValueError(f"Language pack is {total} bytes, over the {MAX_PACK_BYTES} byte partition")
    return header + rest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", default=str(ROOT / "dist/language-pack.bin"))
    parser.add_argument("--builtin", default="vi", help="languages built into the firmware (English always is)")
    args = parser.parse_args()
    pack = build(args.builtin)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(pack)
    print(f"Wrote {out} ({len(pack)} bytes)")


if __name__ == "__main__":
    main()

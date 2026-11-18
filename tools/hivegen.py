#!/usr/bin/env python3
"""
hivegen.py — конвертер текстового INI в бинарный hive-формат LiveOS.

Формат hive:
  HiveHeader  (32 байта, packed)
    magic       uint32  0x4852764C ("LvRH" LE)
    version     uint32  1
    key_count   uint32
    val_count   uint32
    data_size   uint32
    reserved    12 байт

  HiveKey     (48 байт, packed, на каждый key)
    name        32 байта (null-terminated)
    parent_idx  int32   (-1 = root, иначе индекс родителя)
    first_val   uint32  (индекс первого value)
    val_count   uint32
    flags       uint32

  HiveValue   (26 байт, packed, на каждый value)
    name        16 байт (null-terminated)
    type        uint8   (0=u32, 1=u64, 2=string, 3=binary, 4=hex)
    flags       uint8
    data_offset uint32
    data_size   uint32

  data blob   (data_size байт)

Использование:
  hivegen.py input.ini output.hive
  hivegen.py --decompile input.hive   # вывести текст в stdout
"""

import struct
import sys

HIVE_MAGIC = 0x4852764C  # "LvRH" LE
HIVE_VERSION = 1
KEY_NAME_MAX = 32
VAL_NAME_MAX = 32

KEY_STRUCT = struct.Struct('<32s i I I I')   # 32 + 4 + 4 + 4 + 4 = 48
VAL_STRUCT = struct.Struct('<32s B B I I')   # 32 + 1 + 1 + 4 + 4 = 42
HDR_STRUCT = struct.Struct('<I I I I I 12s') # 4 + 4 + 4 + 4 + 4 + 12 = 32

TYPE_U32 = 0
TYPE_U64 = 1
TYPE_STRING = 2
TYPE_BINARY = 3
TYPE_HEX = 4


def parse_ini(text):
    """Парсит INI-подобный текст в дерево.
       Возвращает dict: { 'KEY/PATH': [ (name, type, value) ] }
    """
    tree = {}
    current = ''
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith('#') or line.startswith(';'):
            continue
        if line.startswith('[') and line.endswith(']'):
            current = line[1:-1].strip()
            tree.setdefault(current, [])
            continue
        if '=' not in line:
            continue
        k, v = line.split('=', 1)
        k = k.strip()
        v = v.strip()

        # Тип.
        if v.startswith('"') and v.endswith('"'):
            value = v[1:-1].encode('utf-8') + b'\0'
            t = TYPE_STRING
        elif v.startswith('0x') or v.startswith('0X'):
            value = struct.pack('<I', int(v, 16) & 0xFFFFFFFF)
            t = TYPE_HEX
        elif v.startswith('<') and v.endswith('>'):
            hexstr = v[1:-1]
            value = bytes.fromhex(hexstr)
            t = TYPE_BINARY
        elif v.isdigit():
            value = struct.pack('<I', int(v))
            t = TYPE_U32
        else:
            value = v.encode('utf-8') + b'\0'
            t = TYPE_STRING

        tree.setdefault(current, []).append((k, t, value))
    return tree


def build_hive(tree):
    """Строит бинарный hive из дерева.
       Root — виртуальный узел. Его дети — SYSTEM, DRIVERS, USER и т.д.
    """
    # Собираем список keys: root (idx 0) + все уникальные пути.
    # Каждый путь вида "SYSTEM/triggers" → отдельный key.
    paths = set()
    for k in tree:
        if k == 'ROOT':
            continue
        parts = k.split('/')
        for i in range(1, len(parts) + 1):
            paths.add('/'.join(parts[:i]))
    # Сортируем (по алфавиту).
    path_list = ['ROOT'] + sorted(paths)
    path_idx = {p: i for i, p in enumerate(path_list)}

    # Значения: для каждого key — список (name, type, data).
    values_per_key = {}
    for k, items in tree.items():
        if k == 'ROOT':
            key = 'ROOT'
        else:
            key = k
        values_per_key.setdefault(key, []).extend(items)

    # Собираем все values и data blob.
    all_values = []
    data_blob = bytearray()
    # Для каждого key — свой first_val, val_count.
    key_first_val = {}
    key_val_count = {}
    for i, path in enumerate(path_list):
        items = values_per_key.get(path, [])
        key_first_val[i] = len(all_values)
        for (name, t, data) in items:
            off = len(data_blob)
            data_blob.extend(data)
            all_values.append((name, t, off, len(data)))
        key_val_count[i] = len(items)

    # Собираем keys.
    key_bytes = bytearray()
    for i, path in enumerate(path_list):
        if i == 0:
            name = b'ROOT'
            parent = -1
        else:
            parts = path.rsplit('/', 1)
            if len(parts) == 1:
                name = parts[0].encode('utf-8')
                parent = 0
            else:
                name = parts[1].encode('utf-8')
                parent = path_idx[parts[0]]
        name = name[:KEY_NAME_MAX - 1]
        key_bytes.extend(KEY_STRUCT.pack(
            name, parent, key_first_val[i], key_val_count[i], 0
        ))

    # Собираем values.
    val_bytes = bytearray()
    for (name, t, off, sz) in all_values:
        nm = name.encode('utf-8')[:VAL_NAME_MAX - 1]
        val_bytes.extend(VAL_STRUCT.pack(nm, t, 0, off, sz))

    # Заголовок.
    hdr = HDR_STRUCT.pack(
        HIVE_MAGIC, HIVE_VERSION,
        len(path_list), len(all_values), len(data_blob), b'\x00' * 12
    )

    return bytes(hdr) + bytes(key_bytes) + bytes(val_bytes) + bytes(data_blob)


def decompile_hive(data):
    """Читает hive и печатает в текстовом виде (для проверки)."""
    if len(data) < HDR_STRUCT.size:
        raise ValueError('too short')
    magic, version, key_count, val_count, data_size = HDR_STRUCT.unpack_from(data, 0)[:5]
    if magic != HIVE_MAGIC:
        raise ValueError(f'bad magic 0x{magic:X}')
    if version != HIVE_VERSION:
        raise ValueError(f'bad version {version}')

    off = HDR_STRUCT.size
    keys = []
    for i in range(key_count):
        name, parent, first_val, val_count_, flags = KEY_STRUCT.unpack_from(data, off)
        off += KEY_STRUCT.size
        keys.append((name.rstrip(b'\0').decode('utf-8'),
                     parent, first_val, val_count_, flags))

    vals = []
    for i in range(val_count):
        name, t, flags, data_off, data_sz = VAL_STRUCT.unpack_from(data, off)
        off += VAL_STRUCT.size
        vals.append((name.rstrip(b'\0').decode('utf-8'),
                     t, flags, data_off, data_sz))

    blob_off = off

    # Восстановить пути.
    full_paths = ['ROOT'] * key_count
    for i, (name, parent, _, _, _) in enumerate(keys):
        if i == 0:
            full_paths[i] = 'ROOT'
        else:
            full_paths[i] = (full_paths[parent] + '/' + name) if parent > 0 else name

    # Печать.
    for i, (name, parent, first_val, val_count_, flags) in enumerate(keys):
        if i == 0:
            continue
        print(f'[{full_paths[i]}]')
        for v in range(val_count_):
            vname, t, vflags, data_off, data_sz = vals[first_val + v]
            raw = data[blob_off + data_off: blob_off + data_off + data_sz]
            if t == TYPE_STRING:
                val_str = '"' + raw.rstrip(b'\0').decode('utf-8', 'replace') + '"'
            elif t == TYPE_HEX:
                v = struct.unpack('<I', raw)[0]
                val_str = f'0x{v:02X}'
            elif t == TYPE_U32:
                v = struct.unpack('<I', raw)[0]
                val_str = str(v)
            elif t == TYPE_U64:
                v = struct.unpack('<Q', raw)[0]
                val_str = str(v)
            elif t == TYPE_BINARY:
                val_str = '<' + raw.hex() + '>'
            else:
                val_str = '<?>'
            print(f'  {vname} = {val_str}')
        print()


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    if sys.argv[1] == '--decompile':
        with open(sys.argv[2], 'rb') as f:
            data = f.read()
        decompile_hive(data)
        return 0
    inp, outp = sys.argv[1], sys.argv[2]
    with open(inp, 'r', encoding='utf-8') as f:
        text = f.read()
    tree = parse_ini(text)
    hive = build_hive(tree)
    with open(outp, 'wb') as f:
        f.write(hive)
    print(f'generated {outp} ({len(hive)} bytes, {len(tree)} keys)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
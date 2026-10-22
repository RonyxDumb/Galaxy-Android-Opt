"""Small original inputs for the real Android converter; no game disc bytes."""
import struct


def archive(files):
    root = {}
    for path, data in sorted(files.items()):
        node = root
        parts = path.split('/')
        for part in parts[:-1]:
            node = node.setdefault(part, {})
        node[parts[-1]] = data
    nodes = []

    def collect(name, node):
        nodes.append((name, node))
        for child_name, child in sorted(node.items()):
            if isinstance(child, dict):
                collect(child_name, child)
    collect('Root', root)
    indices = {id(node): i for i, (_, node) in enumerate(nodes)}
    strings, names = bytearray(), {}

    def name_offset(name):
        if name not in names:
            names[name] = len(strings)
            strings.extend(name.encode('ascii') + b'\0')
        return names[name]
    directories, entries, blobs = bytearray(), bytearray(), bytearray()
    count = 0
    for name, node in nodes:
        directories.extend(struct.pack('>IIHHI', 0x524f4f54, name_offset(name), 0, len(node), count))
        for child_name, child in sorted(node.items()):
            offset = name_offset(child_name)
            if isinstance(child, dict):
                entries.extend(struct.pack('>HHIIII', 0xffff, 0, 0x02000000 | offset,
                                           indices[id(child)], 0x10, 0))
            else:
                entries.extend(struct.pack('>HHIIII', count, 0, 0x11000000 | offset,
                                           len(blobs), len(child), 0))
                blobs.extend(child)
                blobs.extend(b'\0' * (-len(blobs) % 32))
            count += 1
    entry_offset = 0x20 + len(directories)
    string_offset = entry_offset + len(entries)
    data_start = (0x20 + string_offset + len(strings) + 31) & ~31
    result = bytearray(data_start)
    result[:4] = b'RARC'
    struct.pack_into('>7I', result, 4, data_start + len(blobs), 0x20, data_start - 0x20,
                     len(blobs), len(blobs), 0, 0)
    struct.pack_into('>6I', result, 0x20, len(nodes), 0x20, count, entry_offset,
                     len(strings), string_offset)
    result[0x40:0x40 + len(directories)] = directories
    result[0x20 + entry_offset:0x20 + string_offset] = entries
    result[0x20 + string_offset:0x20 + string_offset + len(strings)] = strings
    return bytes(result + blobs)


def jmap_hash(name):
    result = 0
    for byte in name.encode('ascii'):
        result = (result * 31 + byte) & 0xffffffff
    return result


def table(names):
    fields = len(names)
    result = bytearray(struct.pack('>4I', 1, fields, 16 + fields * 12, fields * 4))
    for i, name in enumerate(names):
        result.extend(struct.pack('>IIHBB', jmap_hash(name), 0xffffffff, i * 4, 0,
                                  6 if name in ('name', 'MapPaneName') else 0))
    result.extend(struct.pack('>%dI' % fields, *[0 if name in ('name', 'MapPaneName') else 1 for name in names]))
    result.extend(b'Synthetic\0')
    result.extend(b'\0' * (-len(result) % 32))
    return bytes(result)


def dol(missing_table=False):
    data = archive({'layoutdata/errormessagewindow.arc': b'original error layout'})
    data += table(('name', 'progress'))
    if not missing_table:
        data += table(('name', 'MapPaneName', 'PowerStarNum', 'a', 'b', 'c', 'd'))
    header = bytearray(0x100)
    struct.pack_into('>I', header, 0, 0x100)
    struct.pack_into('>I', header, 0x48, 0x80004000)
    struct.pack_into('>I', header, 0x90, len(data))
    return bytes(header) + data


def sound_animation(endian='>'):
    # JAUSoundAnimator.hpp: 8-byte header and one 0x20-byte sound record.
    return (struct.pack(endian + 'HHI5I', 1, 0, 0, 0x11223344,
                        0x3f800000, 0x40000000, 0x3f000000, 0x01020304)
            + bytes(range(8)) + struct.pack(endian + 'I', 0xaabbccdd))


def animation_with_sound():
    result = bytearray(0x60)
    result[:8] = b'J3D1bck1'
    struct.pack_into('>II', result, 8, len(result) + len(sound_animation()), 1)
    struct.pack_into('>I', result, 0x1c, 0x60)
    result[0x20:0x24] = b'ANK1'
    struct.pack_into('>I', result, 0x24, 0x24)
    return bytes(result) + sound_animation()

"""Extract the owned game's menu glyphs for the VR settings menu; standard library only."""
import hashlib
from pathlib import Path
import struct
import sys


def decompress(src):
    out, pos, bits, left = bytearray(), 0, 0, 0

    def bit():
        nonlocal pos, bits, left
        if not left:
            bits, left, pos = src[pos], 8, pos + 1
        result, bits, left = bits & 1, bits >> 1, left - 1
        return result

    while True:
        if bit():
            out.append(src[pos])
            pos += 1
            continue
        if bit():
            word = src[pos] | src[pos + 1] << 8
            pos += 2
            if not word:
                return out
            offset, count = (word >> 3) - 8192, word & 7
            if not count:
                count, pos = src[pos] + 1, pos + 1
            else:
                count += 2
        else:
            count = (bit() << 1 | bit()) + 2
            offset, pos = src[pos] - 256, pos + 1
        assert -len(out) <= offset < 0 and len(out) + count <= 64512
        for _ in range(count):
            out.append(out[len(out) + offset])


def main():
    game, output = map(Path, sys.argv[1:])
    exe = game.read_bytes()
    assert hashlib.sha256(exe).hexdigest() == 'c6b4116788b7f68c56860fb9cc8a94bf984e620907031e4bc3db43623dbe579a'
    pe = struct.unpack_from('<I', exe, 60)[0]
    sections = pe + 24 + struct.unpack_from('<H', exe, pe + 20)[0]
    ranges = [struct.unpack_from('<8sIIII', exe, sections + i * 40)
              for i in range(struct.unpack_from('<H', exe, pe + 6)[0])]

    def read(address, fmt):
        rva = address - 0x400000
        for _, virtual, start, size, raw in ranges:
            if start <= rva < start + min(virtual, size):
                return struct.unpack_from(fmt, exe, raw + rva - start)[0]
        raise ValueError(hex(address))

    source = (game.parent / 'tex/scr_opt_moji05.bin').read_bytes()
    pixels = decompress(source[4:])
    assert len(pixels) == struct.unpack_from('<I', source)[0] == 64512
    descriptors = read(0x55B9B8 + 0x15E * 4, '<I')
    count = read(0x55C0A8 + 0x15E * 2, '<h')
    glyphs = bytearray(128 * 32 * 32 * 4)
    for character in 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789':
        glyph = read(0x56AED0 + ord(character) * 2, '<h')
        assert read(0x57A5BC + glyph * 4, '<I') == 0x15E
        texture = read(0x57D448 + glyph * 4, '<I')
        matches = [descriptors + i * 16 for i in range(count)
                   if read(descriptors + i * 16 + 12, '<I') == texture]
        assert len(matches) == 1
        descriptor = matches[0]
        width, height = read(descriptor, '<h'), read(descriptor + 2, '<h')
        assert 0 < width <= 32 and 0 < height <= 32
        assert read(descriptor + 4, '<I') == 0x902  # Native linear ARGB4444.
        offset = read(descriptor + 8, '<I')
        for i in range(width * height):
            value = struct.unpack_from('<H', pixels, offset + i * 2)[0]
            rgba = bytes((((value >> 8) & 15) * 17, ((value >> 4) & 15) * 17,
                          (value & 15) * 17, ((value >> 12) & 15) * 17))
            # Native surface upload reverses the stored rows.
            target = (ord(character) * 32 * 32 + (height - 1 - i // width) * 32 + i % width) * 4
            glyphs[target:target + 4] = rgba
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b'H2PF' + glyphs)
    print(f'Native menu font: {output} ({len(glyphs)} RGBA bytes)')


if __name__ == '__main__':
    main()

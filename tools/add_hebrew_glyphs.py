# Adds Hebrew letters (from Noto Sans Hebrew, SIL OFL) to the VLW fonts in main/InterFonts.h.
# Usage: python tools/add_hebrew_glyphs.py main/InterFonts.h out.h <folder with NotoSansHebrew-{Regular,SemiBold,Bold}.ttf>
# Needs: pip install freetype-py
import re, struct, sys, freetype
src, dst, fontdir = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(src).read()
WEIGHT = {'R': 'Regular', 'SB': 'SemiBold', 'B': 'Bold'}
CODES = list(range(0x05D0, 0x05EB)) + [0x05BE, 0x05F3, 0x05F4]

def parse(data):
    n, ver, size, _, asc, desc = struct.unpack('>6i', data[:24])
    glyphs = []
    off = 24 + n * 28
    for i in range(n):
        u, h, w, adv, dy, dx, pad = struct.unpack('>7i', data[24 + i*28: 24 + (i+1)*28])
        bm = data[off: off + w*h]; off += w*h
        glyphs.append([u, h, w, adv, dy, dx, pad, bm])
    return (ver, size, asc, desc), glyphs

def build(hdr, glyphs):
    ver, size, asc, desc = hdr
    out = struct.pack('>6i', len(glyphs), ver, size, 0, asc, desc)
    for g in glyphs: out += struct.pack('>7i', *g[:7])
    for g in glyphs: out += g[7]
    return out

def render(face, code):
    face.load_char(chr(code), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
    s = face.glyph; b = s.bitmap
    rows = []
    for r in range(b.rows):
        rows.append(bytes(b.buffer[r*b.pitch: r*b.pitch + b.width]))
    return [code, b.rows, b.width, (s.advance.x + 32) >> 6, s.bitmap_top, s.bitmap_left, 0, b''.join(rows)]

def fix(m):
    name, body = m.group(1), m.group(2)
    mm = re.match(r'INTER_(R|SB|B)(\d+)$', name)
    data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', body))
    if not mm:
        return m.group(0), name, len(data)
    hdr, glyphs = parse(data)
    have = {g[0] for g in glyphs}
    face = freetype.Face(f'{fontdir}/NotoSansHebrew-{WEIGHT[mm.group(1)]}.ttf')
    face.set_pixel_sizes(0, hdr[1])
    for c in CODES:
        if c not in have: glyphs.append(render(face, c))
    glyphs.sort(key=lambda g: g[0])
    out = build(hdr, glyphs)
    lines = [','.join(f'0x{b:02x}' for b in out[i:i+32]) for i in range(0, len(out), 32)]
    return f'const uint8_t {name}[] PROGMEM = {{\n' + ',\n'.join(lines) + '\n};', name, len(out)

lens = {}
def sub(m):
    s, name, n = fix(m); lens[name] = n; return s
text = re.sub(r'const uint8_t (\w+)\[\] PROGMEM = \{(.*?)\};', sub, text, flags=re.S)
text = re.sub(r'const uint32_t (\w+)_LEN = \d+;', lambda m: f'const uint32_t {m.group(1)}_LEN = {lens[m.group(1)]};', text)
open(dst, 'w').write(text)
print(lens)

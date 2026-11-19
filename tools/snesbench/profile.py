"""Instructions and calls per function from profile.lua's counts.
   profile.py <tcc.sym | calypsi.map> <profile.txt> [ticks]
   Calls are the count of a function's first instruction; with ticks given,
   both are also shown per tick. With ASM_DIR=<folder of the Calypsi .s
   files>, the compiler's block labels (?Lnnn, split-off parts of a function)
   are counted for the function they belong to."""
import os
import bisect
import collections
import re
import sys

sym = {}
if sys.argv[1].endswith(".sym"):            # WLA (816-tcc): LoROM
    for line in open(sys.argv[1]):
        m = re.match(r'^([0-9a-f]{8}) (\S+)$', line.strip())
        if not m:
            continue
        a = int(m.group(1), 16)
        bank, addr = a >> 16, a & 0xFFFF
        name = m.group(2)
        if bank >= 0x7E or addr < 0x8000 or name.startswith(('SECTION', '_', '__', 'RAM_USAGE')):
            continue
        sym[(bank & 0x7F) * 0x8000 + (addr & 0x7FFF)] = re.sub(r'^tccs_[^_]*?\.asm_', '', name)
else:                                       # Calypsi map: HiROM
    text = open(sys.argv[1]).read()
    for m in re.finditer(r'^\s+(\S+) = ([0-9a-f]{6})$', text, re.M):
        a = int(m.group(2), 16)
        if a >= 0xC00000:
            sym[a - 0xC00000] = m.group(1)
        elif 0x8000 <= a < 0x10000:
            sym[a] = m.group(1)
    owner = {}                              # (object, ?Lnnn) -> function
    asm_dir = os.environ.get("ASM_DIR")
    if asm_dir:
        for fn in os.listdir(asm_dir):
            if not fn.endswith(".s"):
                continue
            cur = None
            for line in open(os.path.join(asm_dir, fn)):
                m = re.match(r'^`?(\??[A-Za-z_][\w?]*)`?:', line)
                if m:
                    if m.group(1).startswith('?L'):
                        owner[(fn[:-2], m.group(1))] = cur
                    else:
                        cur = m.group(1)
    for m in re.finditer(r"^`?([^`\n]+?)`? in section '(\w+)'\s+placed at address ([0-9a-f]{6})[^\n]*\n\(([^ )]+)",
                         text, re.M):
        a = int(m.group(3), 16)
        off = a - 0xC00000 if a >= 0xC00000 else a
        if m.group(2) in ('farcode', 'code'):
            name = m.group(1)
            obj = os.path.basename(m.group(4)).rsplit('.', 1)[0]
            if name.startswith('?L'):
                name = owner.get((obj, name)) or f"{obj}:{name}"
                sym[off] = name + "+"         # a block, not the entry: not counted as a call
            else:
                sym[off] = name
keys = sorted(sym)
total = 0
per = collections.Counter()
calls = collections.Counter()
for line in open(sys.argv[2]):
    off, n = map(int, line.split())
    i = bisect.bisect_right(keys, off) - 1
    name = sym[keys[i]] if i >= 0 else '?'
    block = name.endswith('+')
    name = name.rstrip('+')
    if name in ('hash_bytes', 'put_hash', 'main', 'script'):
        continue                            # bench.c, outside the measured part
    per[name] += n
    total += n
    if i >= 0 and keys[i] == off and not block:
        calls[name] += n
ticks = int(sys.argv[3]) if len(sys.argv) > 3 else 1
print(f"instructions: {total}, per tick {total / ticks:.0f}")
print(f"{'instr/tick':>10} {'share':>6} {'calls/tick':>10} {'instr/call':>10}  function")
for name, n in per.most_common(int(sys.argv[4]) if len(sys.argv) > 4 else 30):
    c = calls[name]
    print(f"{n / ticks:10.0f} {100 * n / total:5.1f}% {c / ticks:10.1f} {n / c if c else 0:10.0f}  {name}")

# int16check, step 2 (see int16check.sh): preprocesses each file of
# src/game (macros expanded), asks clang for the syntax tree with a 16-bit
# int (MSP430), finds the int arithmetic whose result is compared, shifted
# right, divided or widened to 32 bits, and wraps each such expression in a
# range probe (probe.c). sites.txt lists them with their original place.
#
# Usage: instrument.py <stub dir> <output dir> <generated dir>   (from the repository root)
import json, subprocess, sys, os, re
CLANG = os.environ.get('CLANG', '/opt/emscripten-llvm/bin/clang')
STUB = sys.argv[1]; OUT = sys.argv[2]; GEN = sys.argv[3]
SDK = os.environ['SUPERDEVKIT_HOME']   # the console API's headers (sdevkit/snes.h)
ARITH = {'*', '+', '-', '<<'}
USE = {'>>', '/', '%', '<', '>', '<=', '>=', '==', '!='}
WIDE = {'long', 'unsigned long', 'int32_t', 'uint32_t'}
def qt(n): return n.get('type', {}).get('qualType', '')
def desug(n): return n.get('type', {}).get('desugaredQualType', qt(n))
os.makedirs(OUT, exist_ok=True)
table = []; nid = 0
files = sorted('src/game/' + f for f in os.listdir('src/game') if f.endswith('.c'))
for path in files:
    pp = subprocess.run([CLANG, '--target=msp430', '-std=c99', '-E', '-nostdinc', '-I' + STUB, '-DBB_DEBUG=1',
                         '-Isrc', '-I' + GEN, '-I' + SDK + '/lib/include', path],
                        capture_output=True, text=True, check=True).stdout
    ppfile = OUT + '/' + os.path.basename(path)
    open(ppfile, 'w').write(pp)
    ast = json.loads(subprocess.run([CLANG, '--target=msp430', '-std=c99', '-fsyntax-only', '-nostdinc', '-Xclang',
                                     '-ast-dump=json', ppfile], capture_output=True, text=True).stdout)
    sites = []
    def walk(n, parents):
        if n.get('kind') == 'BinaryOperator' and n.get('opcode') in ARITH and desug(n) in ('int', 'unsigned int'):
            p = None; child = n
            for q in reversed(parents):
                if q.get('kind') == 'ParenExpr' or (q.get('kind') == 'ImplicitCastExpr' and desug(q) in ('int', 'unsigned int')):
                    child = q; continue
                p = q; break
            why = None
            if p is not None:
                if p.get('kind') == 'BinaryOperator' and p.get('opcode') in USE:
                    if p['opcode'] in ('>>', '/', '%'):
                        if p['inner'][0] is child: why = 'then ' + p['opcode']
                    else: why = 'compared ' + p['opcode']
                elif p.get('kind') in ('ImplicitCastExpr', 'CStyleCastExpr') and (qt(p) in WIDE or desug(p) in WIDE):
                    why = 'widened'
            if why:
                b, e = n['range']['begin'], n['range']['end']
                sites.append((b['offset'], e['offset'] + e['tokLen'], 'U' if desug(n) == 'unsigned int' else 'S', why))
        for c in n.get('inner', []) or []:
            walk(c, parents + [n])
    for d in ast.get('inner', []):
        walk(d, [])
    src = pp.encode()
    # original line of an offset: the last "# <line> "<file>"" marker before it
    markers = [(m.start(), int(m.group(1)), m.group(2)) for m in re.finditer(rb'^# (\d+) "([^"]+)"', src, re.M)]
    def origin(off):
        best = None
        for start, line, fname in markers:
            if start <= off: best = (start, line, fname)
            else: break
        if not best: return '?'
        return '%s:%d' % (best[2].decode(), best[1] + src[best[0]:off].count(b'\n') - 1)
    edits = []
    for (b, e, kind, why) in sorted(set(sites)):
        table.append((nid, origin(b), kind, why, src[b:e].decode(errors='replace').replace('\n', ' ')[:90]))
        edits.append((b, 0, e, ('I16%s(%d, ' % (kind, nid)).encode()))
        edits.append((e, 1, 0, b')'))
        nid += 1
    edits.sort(key=lambda t: (-t[0], t[1], t[2]))
    for off, typ, end, txt in edits:
        src = src[:off] + txt + src[off:]
    open(ppfile, 'wb').write(b'int i16s(int id, long v); unsigned int i16u(int id, unsigned long v);\n'
                             b'#define I16S(id, e) i16s(id, (long)(e))\n#define I16U(id, e) i16u(id, (unsigned long)(e))\n' + src)
with open(OUT + '/sites.txt', 'w') as fo:
    for t in table: fo.write('%d\t%s\t%s\t%s\t%s\n' % t)
print(nid, 'sites instrumented')

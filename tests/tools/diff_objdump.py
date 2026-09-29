#!/usr/bin/env python3
"""
Differential test: run ./riscv_decoder and a real RISC-V objdump on the same ELF,
compare instruction by instruction.

A difference is ACCEPTED when it is a known, intentional gap:
  * the decoder prints .word/.half  (F/D, V, Zcb ... not implemented, see README)
  * objdump uses a pseudo-instruction the decoder does not (beqz, sext.w, neg, ...)
  * cosmetic formatting only (jr/jalr with zero offset, plain 'fence', csr alias names)
Anything else is reported and the script exits 1.

usage: diff_objdump.py <riscv_decoder> <file.elf> <objdump>
exit:  0 ok, 1 unexplained differences, 77 skipped (CTest 'skip' code)
"""
import re, subprocess, sys, collections

if len(sys.argv) != 4: sys.exit("usage: diff_objdump.py <decoder> <elf> <objdump>")
dec, elf, objdump = sys.argv[1:4]

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: sys.exit("command failed: %s\n%s" % (" ".join(cmd), r.stderr))
    return r.stdout

ref  = run([objdump, "-d", elf])
mine = run([dec, elf])

def clean(t):
    t = re.sub(r'\s*#.*', '', t); t = re.sub(r'\s*<[^>]*>', '', t)
    t = re.sub(r'\s+', ' ', t).strip()
    return re.sub(r',\s*', ',', t)

R, M = {}, {}
for l in ref.splitlines():
    m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f]+)\s*\t(.*)', l)
    if m: R[int(m[1], 16)] = (m[2], clean(m[3]))
for l in mine.splitlines():
    m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f]+)\t(.*)', l)
    if m: M[int(m[1], 16)] = (m[2], clean(m[3]))

if not R: print("objdump produced no instructions"); sys.exit(77)

TARGET_OPS = {'j','jal','beq','bne','blt','bge','bltu','bgeu','beqz','bnez','bltz','bgez',
              'blez','bgtz','bgt','ble','bgtu','bleu'}
PSEUDO = {'beqz','bnez','bltz','bgez','blez','bgtz','bgt','ble','bgtu','bleu','sext.w','neg','negw',
          'not','snez','sltz','sgtz','seqz','zext.w','zext.b','frrm','frcsr','fscsr','frflags','fsflags',
          'fsrm','rdcycle','rdtime','rdinstret','rdcycleh','rdtimeh','rdinstreth','csrr','csrw','csrs','csrc'}
MASK = (1 << 64) - 1

def num(s):        # 0x1f -> 31 ; -0x1 -> -1
    return re.sub(r'-?0x[0-9a-f]+', lambda m: str(int(m[0], 16)), s)

def canon_ref(t):
    if t.split(' ')[0] in TARGET_OPS:       # objdump prints branch targets as bare hex
        t = re.sub(r'(?<![\w-])([0-9a-f]+)$', lambda m: '0x' + m[1], t)
    return num(t)

unsupported = collections.Counter(); accepted = collections.Counter(); bad = []
for a in sorted(M):
    if a not in R:
        if M[a][1] == 'unimp': continue            # objdump hides zero padding
        bad.append((a, "instruction only in decoder output", "", M[a][1])); continue
    rt, mt = canon_ref(R[a][1]), num(M[a][1])
    rmn, mmn = R[a][1].split(' ')[0], M[a][1].split(' ')[0]
    if rt == mt: continue
    if mmn in ('.word', '.half'):
        unsupported[rmn] += 1; continue
    if R[a][0].lstrip('0') != M[a][0].lstrip('0'):
        bad.append((a, "raw bits differ", R[a][0] + " " + R[a][1], M[a][0] + " " + M[a][1])); continue
    if rmn in PSEUDO and rmn != mmn: accepted["pseudo-op " + rmn] += 1; continue
    if {rmn, mmn} <= {'jr', 'jalr', 'ret'}: accepted["jr/jalr formatting"] += 1; continue
    if rmn == 'fence' and mmn == 'fence': accepted["fence formatting"] += 1; continue
    if rmn == 'unimp' or mmn == 'unimp': accepted["unimp"] += 1; continue
    bad.append((a, "different text", R[a][1], M[a][1]))
for a in sorted(set(R) - set(M)):
    bad.append((a, "instruction missing from decoder output", R[a][1], ""))

total = len(R)
print("%s: %d instructions compared" % (elf, total))
print("  exact matches       : %d" % (total - sum(unsupported.values()) - sum(accepted.values()) - len(bad)))
print("  accepted style diffs: %d" % sum(accepted.values()))
print("  not implemented     : %d  (top: %s)" % (sum(unsupported.values()),
      ", ".join("%s x%d" % kv for kv in unsupported.most_common(8))))
print("  UNEXPLAINED         : %d" % len(bad))
for a, why, r, m in bad[:25]:
    print("    0x%x  %s\n        objdump: %s\n        decoder: %s" % (a, why, r, m))
sys.exit(1 if bad else 0)

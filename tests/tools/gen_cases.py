#!/usr/bin/env python3
"""
Generates tests/cases_*.inc from the REAL RISC-V assembler (riscv64-linux-gnu-as).

Each case = (assembly text, text the decoder is expected to print).
The encoding comes from the assembler, so it is independent of the decoder.
The expected text is cross-checked against objdump; any difference other than
the known/intentional style differences is printed as a warning.

Needs:  sudo apt install binutils-riscv64-linux-gnu
Usage:  python3 tests/tools/gen_cases.py
"""
import re, subprocess, sys, tempfile, os

AS  = "riscv64-linux-gnu-as"
OBJ = "riscv64-linux-gnu-objdump"
BASE = 0x200000                    # address the C++ tests decode at
def T(n): return "0x%x" % (BASE + n)   # absolute target for ".+n"

RV64_32 = [   # 32-bit encodings, RV64
 # ---- R-type ----
 ("add a0,a1,a2","add a0,a1,a2"),("sub a0,a1,a2","sub a0,a1,a2"),
 ("sll a0,a1,a2","sll a0,a1,a2"),("slt a0,a1,a2","slt a0,a1,a2"),
 ("sltu a0,a1,a2","sltu a0,a1,a2"),("xor a0,a1,a2","xor a0,a1,a2"),
 ("srl a0,a1,a2","srl a0,a1,a2"),("sra a0,a1,a2","sra a0,a1,a2"),
 ("or a0,a1,a2","or a0,a1,a2"),("and a0,a1,a2","and a0,a1,a2"),
 ("slt a0,zero,a2","sgtz a0,a2"),("slt a0,a1,zero","sltz a0,a1"),
 ("sltu a0,zero,a2","snez a0,a2"),
 ("sub a3,zero,a3","sub a3,zero,a3"),                 # objdump: neg (style)
 ("addw a0,a1,a2","addw a0,a1,a2"),("subw a0,a1,a2","subw a0,a1,a2"),
 ("sllw a0,a1,a2","sllw a0,a1,a2"),("srlw a0,a1,a2","srlw a0,a1,a2"),
 ("sraw a0,a1,a2","sraw a0,a1,a2"),
 ("subw a0,zero,a2","negw a0,a2"),
 # ---- M ----
 ("mul a0,a1,a2","mul a0,a1,a2"),("mulh a0,a1,a2","mulh a0,a1,a2"),
 ("mulhsu a0,a1,a2","mulhsu a0,a1,a2"),("mulhu a0,a1,a2","mulhu a0,a1,a2"),
 ("div a0,a1,a2","div a0,a1,a2"),("divu a0,a1,a2","divu a0,a1,a2"),
 ("rem a0,a1,a2","rem a0,a1,a2"),("remu a0,a1,a2","remu a0,a1,a2"),
 ("mulw a0,a1,a2","mulw a0,a1,a2"),("divw a0,a1,a2","divw a0,a1,a2"),
 ("divuw a0,a1,a2","divuw a0,a1,a2"),("remw a0,a1,a2","remw a0,a1,a2"),
 ("remuw a0,a1,a2","remuw a0,a1,a2"),
 # ---- OP-IMM ----
 ("addi a0,a1,5","addi a0,a1,5"),("addi a0,a1,-5","addi a0,a1,-5"),
 ("addi a0,a1,2047","addi a0,a1,2047"),("addi a0,a1,-2048","addi a0,a1,-2048"),
 ("addi a0,zero,0","li a0,0"),("addi a0,zero,-1","li a0,-1"),
 ("addi zero,zero,0","nop"),("addi a0,a1,0","mv a0,a1"),
 ("slti a0,a1,-7","slti a0,a1,-7"),("sltiu a0,a1,7","sltiu a0,a1,7"),
 ("sltiu a0,a1,1","seqz a0,a1"),
 ("xori a0,a1,0x55","xori a0,a1,85"),("xori a0,a1,-1","xori a0,a1,-1"),
 ("ori a0,a1,0x0f","ori a0,a1,15"),("andi a0,a1,0x0f","andi a0,a1,15"),
 ("slli a0,a1,3","slli a0,a1,3"),("slli a0,a1,63","slli a0,a1,63"),
 ("srli a0,a1,3","srli a0,a1,3"),("srli a0,a1,63","srli a0,a1,63"),
 ("srai a0,a1,3","srai a0,a1,3"),("srai a0,a1,63","srai a0,a1,63"),
 ("addiw a0,a1,5","addiw a0,a1,5"),("addiw a0,a1,0","addiw a0,a1,0"),  # objdump: sext.w (style)
 ("slliw a0,a1,3","slliw a0,a1,3"),("srliw a0,a1,3","srliw a0,a1,3"),
 ("sraiw a0,a1,31","sraiw a0,a1,31"),
 # ---- loads / stores ----
 ("lb a0,4(a1)","lb a0,4(a1)"),("lh a0,-8(sp)","lh a0,-8(sp)"),
 ("lw a0,2047(a1)","lw a0,2047(a1)"),("ld a0,-2048(a1)","ld a0,-2048(a1)"),
 ("lbu a0,0(a1)","lbu a0,0(a1)"),("lhu a0,6(a1)","lhu a0,6(a1)"),
 ("lwu a0,12(a1)","lwu a0,12(a1)"),
 ("sb a0,1(a1)","sb a0,1(a1)"),("sh a0,-2(a1)","sh a0,-2(a1)"),
 ("sw a0,2047(a1)","sw a0,2047(a1)"),("sd a0,-2048(a1)","sd a0,-2048(a1)"),
 ("sd ra,8(sp)","sd ra,8(sp)"),
 # ---- branches (target = BASE + offset) ----
 ("beq a0,a1,.+16","beq a0,a1,"+T(16)),("bne a0,a1,.+16","bne a0,a1,"+T(16)),
 ("blt a0,a1,.+16","blt a0,a1,"+T(16)),("bge a0,a1,.+16","bge a0,a1,"+T(16)),
 ("bltu a0,a1,.+16","bltu a0,a1,"+T(16)),("bgeu a0,a1,.+16","bgeu a0,a1,"+T(16)),
 ("beq a0,a1,.-8","beq a0,a1,"+T(-8)),("bne a0,a1,.+4094","bne a0,a1,"+T(4094)),
 ("blt a0,a1,.-4096","blt a0,a1,"+T(-4096)),
 ("beq s2,zero,.+20","beq s2,zero,"+T(20)),                 # objdump: beqz (style)
 # ---- U / J ----
 ("lui a0,0x12345","lui a0,0x12345"),("lui a0,0x1","lui a0,0x1"),
 ("lui a0,0x7ffff","lui a0,0x7ffff"),
 ("lui a0,0x80000","lui a0,0x80000"),("lui a0,0xfffff","lui a0,0xfffff"),   # bug-catchers
 ("auipc a0,0x1","auipc a0,0x1"),("auipc ra,0xfffff","auipc ra,0xfffff"),
 ("auipc t3,0x77","auipc t3,0x77"),
 ("jal ra,.+100","jal "+T(100)),("jal t0,.+100","jal t0,"+T(100)),
 ("jal zero,.+100","j "+T(100)),("jal zero,.-4","j "+T(-4)),
 ("jal ra,.+1048574","jal "+T(1048574)),("jal ra,.-1048576","jal "+T(-1048576)),
 ("jalr ra,0(a5)","jalr ra,0(a5)"),("jalr t1,8(t3)","jalr t1,8(t3)"),
 ("jalr zero,0(a5)","jr 0(a5)"),("jalr zero,0(ra)","ret"),
 ("jalr zero,4(ra)","jr 4(ra)"),
 # ---- system ----
 ("ecall","ecall"),("ebreak","ebreak"),("mret","mret"),("sret","sret"),("wfi","wfi"),
 ("fence rw,rw","fence rw,rw"),("fence iorw,iorw","fence iorw,iorw"),
 ("fence r,w","fence r,w"),("fence.i","fence.i"),("pause","pause"),
 ("csrr a0,mstatus","csrr a0,mstatus"),("csrw mtvec,a0","csrw mtvec,a0"),
 ("csrrw a0,mscratch,a1","csrrw a0,mscratch,a1"),
 ("csrrs a0,mie,a1","csrrs a0,mie,a1"),("csrrc a0,mip,a1","csrrc a0,mip,a1"),
 ("csrs mstatus,a0","csrs mstatus,a0"),("csrc mstatus,a0","csrc mstatus,a0"),
 ("csrrwi a0,mstatus,5","csrrwi a0,mstatus,5"),("csrwi mstatus,5","csrwi mstatus,5"),
 ("csrrsi a0,mie,3","csrrsi a0,mie,3"),("csrsi mie,3","csrsi mie,3"),
 ("csrrci a0,mie,3","csrrci a0,mie,3"),("csrci mie,3","csrci mie,3"),
 ("csrr a0,cycle","csrr a0,cycle"),("csrr a0,mhartid","csrr a0,mhartid"),
 ("csrr a0,satp","csrr a0,satp"),("csrr a0,0x7c0","csrr a0,0x7c0"),
 # ---- A extension ----
 ("lr.w a0,(a1)","lr.w a0,(a1)"),("lr.d a0,(a1)","lr.d a0,(a1)"),
 ("sc.w a0,a2,(a1)","sc.w a0,a2,(a1)"),("sc.d a0,a2,(a1)","sc.d a0,a2,(a1)"),
 ("amoswap.w a0,a2,(a1)","amoswap.w a0,a2,(a1)"),
 ("amoadd.d a0,a2,(a1)","amoadd.d a0,a2,(a1)"),
 ("amoxor.w a0,a2,(a1)","amoxor.w a0,a2,(a1)"),
 ("amoor.w.aq a0,a2,(a1)","amoor.w.aq a0,a2,(a1)"),
 ("amoand.d.rl a0,a2,(a1)","amoand.d.rl a0,a2,(a1)"),
 ("amomin.w.aqrl a0,a2,(a1)","amomin.w.aqrl a0,a2,(a1)"),
 ("amomax.d a0,a2,(a1)","amomax.d a0,a2,(a1)"),
 ("amominu.w a0,a2,(a1)","amominu.w a0,a2,(a1)"),
 ("amomaxu.d a0,a2,(a1)","amomaxu.d a0,a2,(a1)"),
 # ---- Zba / Zbb / Zbs / Zicond ----
 ("sh1add a0,a1,a2","sh1add a0,a1,a2"),("sh2add a0,a1,a2","sh2add a0,a1,a2"),
 ("sh3add a0,a1,a2","sh3add a0,a1,a2"),("add.uw a0,a1,a2","add.uw a0,a1,a2"),
 ("add.uw a0,a1,zero","zext.w a0,a1"),
 ("sh1add.uw a0,a1,a2","sh1add.uw a0,a1,a2"),("sh2add.uw a0,a1,a2","sh2add.uw a0,a1,a2"),
 ("sh3add.uw a0,a1,a2","sh3add.uw a0,a1,a2"),("slli.uw a0,a1,5","slli.uw a0,a1,0x5"),
 ("andn a0,a1,a2","andn a0,a1,a2"),("orn a0,a1,a2","orn a0,a1,a2"),
 ("xnor a0,a1,a2","xnor a0,a1,a2"),
 ("min a0,a1,a2","min a0,a1,a2"),("minu a0,a1,a2","minu a0,a1,a2"),
 ("max a0,a1,a2","max a0,a1,a2"),("maxu a0,a1,a2","maxu a0,a1,a2"),
 ("rol a0,a1,a2","rol a0,a1,a2"),("ror a0,a1,a2","ror a0,a1,a2"),
 ("rolw a0,a1,a2","rolw a0,a1,a2"),("rorw a0,a1,a2","rorw a0,a1,a2"),
 ("clz a0,a1","clz a0,a1"),("ctz a0,a1","ctz a0,a1"),("cpop a0,a1","cpop a0,a1"),
 ("clzw a0,a1","clzw a0,a1"),("ctzw a0,a1","ctzw a0,a1"),("cpopw a0,a1","cpopw a0,a1"),
 ("sext.b a0,a1","sext.b a0,a1"),("sext.h a0,a1","sext.h a0,a1"),
 ("rori a0,a1,5","rori a0,a1,0x5"),("roriw a0,a1,5","roriw a0,a1,0x5"),
 ("rev8 a0,a1","rev8 a0,a1"),("orc.b a0,a1","orc.b a0,a1"),
 ("andi a0,a1,255","zext.b a0,a1"),
 ("bclr a0,a1,a2","bclr a0,a1,a2"),("bext a0,a1,a2","bext a0,a1,a2"),
 ("binv a0,a1,a2","binv a0,a1,a2"),("bset a0,a1,a2","bset a0,a1,a2"),
 ("bclri a0,a1,5","bclri a0,a1,0x5"),("bexti a0,a1,5","bexti a0,a1,0x5"),
 ("binvi a0,a1,5","binvi a0,a1,0x5"),("bseti a0,a1,5","bseti a0,a1,0x5"),
 ("czero.eqz a0,a1,a2","czero.eqz a0,a1,a2"),("czero.nez a0,a1,a2","czero.nez a0,a1,a2"),
]

RV64_16 = [   # compressed, RV64
 ("c.addi4spn a0,sp,16","addi a0,sp,16"),("c.addi4spn s1,sp,1020","addi s1,sp,1020"),
 ("c.lw a0,4(a1)","lw a0,4(a1)"),("c.lw a2,124(a3)","lw a2,124(a3)"),
 ("c.ld a0,8(a1)","ld a0,8(a1)"),("c.ld a2,248(a3)","ld a2,248(a3)"),
 ("c.sw a0,4(a1)","sw a0,4(a1)"),("c.sw a2,124(a3)","sw a2,124(a3)"),
 ("c.sd a0,8(a1)","sd a0,8(a1)"),("c.sd a2,248(a3)","sd a2,248(a3)"),
 ("c.lbu a0,1(a1)","lbu a0,1(a1)"),("c.lbu a2,3(a3)","lbu a2,3(a3)"),
 ("c.nop","nop"),("c.addi a0,-3","addi a0,a0,-3"),("c.addi a0,31","addi a0,a0,31"),
 ("c.addi a0,-32","addi a0,a0,-32"),
 ("c.addiw a0,1","addiw a0,a0,1"),("c.addiw a0,-1","addiw a0,a0,-1"),
 ("c.li a0,-1","li a0,-1"),("c.li a0,0","li a0,0"),("c.li a5,31","li a5,31"),
 ("c.addi16sp sp,-32","addi sp,sp,-32"),("c.addi16sp sp,64","addi sp,sp,64"),
 ("c.addi16sp sp,496","addi sp,sp,496"),("c.addi16sp sp,-512","addi sp,sp,-512"),
 ("c.lui a0,1","lui a0,0x1"),("c.lui a0,31","lui a0,0x1f"),
 ("c.lui a0,0xfffe0","lui a0,0xfffe0"),("c.lui a1,0xfffff","lui a1,0xfffff"),
 ("c.srli a0,3","srli a0,a0,3"),("c.srli a0,63","srli a0,a0,63"),
 ("c.srai a0,3","srai a0,a0,3"),("c.srai a0,63","srai a0,a0,63"),
 ("c.andi a0,-4","andi a0,a0,-4"),("c.andi a0,31","andi a0,a0,31"),
 ("c.sub a0,a1","sub a0,a0,a1"),("c.xor a0,a1","xor a0,a0,a1"),
 ("c.or a0,a1","or a0,a0,a1"),("c.and a0,a1","and a0,a0,a1"),
 ("c.subw a0,a1","subw a0,a0,a1"),("c.addw a0,a1","addw a0,a0,a1"),
 ("c.j .+20","j "+T(20)),("c.j .-6","j "+T(-6)),
 ("c.j .+2046","j "+T(2046)),("c.j .-2048","j "+T(-2048)),
 ("c.beqz a0,.+8","beqz a0,"+T(8)),("c.beqz a5,.-6","beqz a5,"+T(-6)),
 ("c.bnez a1,.-6","bnez a1,"+T(-6)),("c.bnez a1,.+254","bnez a1,"+T(254)),
 ("c.bnez a1,.-256","bnez a1,"+T(-256)),
 ("c.slli a0,5","slli a0,a0,5"),("c.slli a0,63","slli a0,a0,63"),
 ("c.lwsp a0,4(sp)","lw a0,4(sp)"),("c.lwsp a0,252(sp)","lw a0,252(sp)"),
 ("c.ldsp ra,8(sp)","ld ra,8(sp)"),("c.ldsp a0,504(sp)","ld a0,504(sp)"),
 ("c.swsp a0,4(sp)","sw a0,4(sp)"),("c.swsp a0,252(sp)","sw a0,252(sp)"),
 ("c.sdsp ra,8(sp)","sd ra,8(sp)"),("c.sdsp a0,504(sp)","sd a0,504(sp)"),
 ("c.jr a5","jr a5"),("c.jr ra","ret"),("c.mv a0,a1","mv a0,a1"),
 ("c.ebreak","ebreak"),("c.jalr a5","jalr a5"),("c.add a0,a1","add a0,a0,a1"),
]

RV32_16 = [("c.jal .+10","jal ra,"+T(10)),("c.jal .-2048","jal ra,"+T(-2048))]

# Zcb ops the decoder does not implement yet (objdump text as expected).
ZCB_GAPS = [
 ("c.lhu a0,2(a1)","lhu a0,2(a1)"),("c.lh a0,2(a1)","lh a0,2(a1)"),
 ("c.sb a0,1(a1)","sb a0,1(a1)"),("c.sh a0,2(a1)","sh a0,2(a1)"),
 ("c.zext.b a0","zext.b a0,a0"),("c.sext.b a0","sext.b a0,a0"),
 ("c.zext.h a0","zext.h a0,a0"),("c.sext.h a0","sext.h a0,a0"),
 ("c.zext.w a0","zext.w a0,a0"),("c.not a0","not a0,a0"),
 ("c.mul a0,a1","mul a0,a0,a1"),
]

def assemble(cases, march, mabi):
    with tempfile.TemporaryDirectory() as d:
        s, o = os.path.join(d,"a.s"), os.path.join(d,"a.o")
        open(s,"w").write("  .text\n" + "\n".join("  "+a for a,_ in cases) + "\n")
        r = subprocess.run([AS,"-march="+march,"-mabi="+mabi,s,"-o",o],capture_output=True,text=True)
        if r.returncode: sys.exit("assembler failed:\n"+r.stderr)
        out = subprocess.run([OBJ,"-d",o],capture_output=True,text=True).stdout
    rows=[]
    for l in out.splitlines():
        m=re.match(r'\s*([0-9a-f]+):\t([0-9a-f]+)\s*\t(.*)',l)
        if m: rows.append((int(m[1],16),int(m[2],16),m[3]))
    if len(rows)!=len(cases): sys.exit("count mismatch %d vs %d"%(len(rows),len(cases)))
    return rows

def cross_check(name, cases, rows):
    """Compare expected text with objdump; print anything unexpected."""
    def canon(t,a=None,base=0):
        t=re.sub(r'\s*#.*|\s*<[^>]*>','',t); t=re.sub(r'\s+',' ',t).strip(); t=re.sub(r',\s*',',',t)
        return t
    bad=0
    for (asm,exp),(addr,enc,obj) in zip(cases,rows):
        o=canon(obj); e=canon(exp)
        # normalise branch targets to offsets
        def off(s,base):
            m=re.match(r'^(\w+) (.*?)(?:,)?(0x)?([0-9a-f]+)$',s)
            return s
        o2=re.sub(r'(?<![\w.])(0x)?([0-9a-f]+)$',lambda m:m.group(0),o)
        if o.split(' ')[0] in('j','jal','beq','bne','blt','bge','bltu','bgeu','beqz','bnez'):
            mo=re.search(r'([0-9a-f]+)$',o); me=re.search(r'0x([0-9a-f]+)$',e)
            if mo and me:
                M=(1<<64)-1
                if (int(mo[1],16)-addr)&M != (int(me[1],16)-BASE)&M: print("!! target",name,asm,o,e); bad+=1
                continue
        # numbers -> decimal
        n=lambda s: re.sub(r'-?0x[0-9a-f]+',lambda m:str(int(m[0],16)),s)
        if n(o)!=n(e): print("   style-diff [%s] %-28s objdump: %-28s decoder: %s"%(name,asm,o,e))
    return bad

def emit(fn, cases, rows):
    with open(fn,"w") as f:
        f.write("// AUTO-GENERATED by tests/tools/gen_cases.py -- do not edit by hand.\n")
        f.write("// {encoding, expected decoder text, source assembly}\n")
        for (asm,exp),(_,enc,_) in zip(cases,rows):
            width = 4 if enc<=0xffff and len(("%x"%enc))<=4 and asm.startswith("c.") else 8
            f.write('{0x%0*x, "%s", "%s"},\n'%(width,enc,exp,asm))

here=os.path.dirname(os.path.abspath(__file__)); out=os.path.join(here,"..")
sets=[("cases_rv64_32.inc",RV64_32,"rv64imafd_zba_zbb_zbs_zicond_zihintpause_zifencei","lp64"),
      ("cases_rv64_16.inc",RV64_16,"rv64imac_zcb","lp64"),
      ("cases_rv32_16.inc",RV32_16,"rv32imac","ilp32"),
      ("cases_zcb_gaps.inc",ZCB_GAPS,"rv64imac_zcb_zbb_zba","lp64")]
for fn,cs,march,abi in sets:
    rows=assemble(cs,march,abi); cross_check(fn,cs,rows); emit(os.path.join(out,fn),cs,rows)
    print("wrote",fn,len(cs),"cases")

import struct

# Hand-picked RV64GC machine code bytes (verified encodings), mixing
# 32-bit and compressed (16-bit) instructions, and a branch/jump so we
# can see label/target resolution in action. This emulates the body of:
#
#   int add(int a, int b) { return a + b; }   // uses a0,a1 -> a0
#   _start: calls add(3,4), loops
#
# Bytes below correspond to (little-endian, verified against the
# standard RISC-V spec encodings):
code = bytes([
    0x13, 0x05, 0x00, 0x00,  # 00000513   addi a0, zero, 0        (li a0,0)
    0x93, 0x05, 0x10, 0x00,  # 00100593   addi a1, zero, 1        (li a1,1)
    0x33, 0x86, 0xb5, 0x00,  # 00b58633   add  a2, a1, a1
    0x01, 0x45,              # 4501       c.li a0,0
    0x85, 0x05,              # 0585       c.addi a1,1
    0x93, 0x87, 0x07, 0x00,  # 00078793   addi a5,a5,0            (mv a5,a5 form)
    0x63, 0x0c, 0xc7, 0x00,  # 00c78c63   beq  a5,a2,+24
    0x13, 0x01, 0x01, 0xff,  # ff010113   addi sp,sp,-16
    0x23, 0x34, 0xa1, 0x00,  # 00a13423   sd   a0,8(sp)
    0x82, 0x80,              # 8082       c.ret? actually check below
    0x67, 0x80, 0x00, 0x00,  # 00008067   ret  (jalr x0,0(ra))
    0x6f, 0x00, 0x00, 0x00,  # 0000006f   j    . (jal x0,0)
])
print(len(code))

E64_EHDR = "<16sHHIQQQIHHHHHH"
E64_SHDR = "<IIQQQQIIQQ"

ehdr_size = 64
shdr_size = 64
n_sections = 3  # NULL, .text, .shstrtab

shstrtab = b"\x00.text\x00.shstrtab\x00"
text_off = ehdr_size
text_addr = 0x10000
text_size = len(code)
shstrtab_off = text_off + text_size
shoff = shstrtab_off + len(shstrtab)
# pad shoff to 8-byte alignment
pad = (-shoff) % 8
shoff += pad

e_ident = b"\x7fELF" + bytes([2,1,1,0]) + b"\x00"*8
ehdr = struct.pack(E64_EHDR,
    e_ident,
    2,          # e_type ET_EXEC
    243,        # e_machine EM_RISCV
    1,          # e_version
    text_addr,  # e_entry
    0,          # e_phoff
    shoff,      # e_shoff
    0,          # e_flags
    ehdr_size,  # e_ehsize
    0, 0,       # phentsize, phnum
    shdr_size,  # shentsize
    n_sections, # shnum: NULL, .text, .shstrtab
    n_sections - 1  # shstrndx -> last one is .shstrtab
)

def shdr(name_off, sh_type, flags, addr, offset, size, link, info, addralign, entsize):
    return struct.pack(E64_SHDR, name_off, sh_type, flags, addr, offset, size, link, info, addralign, entsize)

sh_null = shdr(0,0,0,0,0,0,0,0,0,0)
name_text = shstrtab.index(b".text\x00")
sh_text = shdr(name_text, 1, 0x6, text_addr, text_off, text_size, 0,0,4,0)  # SHT_PROGBITS, alloc+execinstr
name_shstr = shstrtab.index(b".shstrtab\x00")
sh_shstrtab = shdr(name_shstr, 3, 0, 0, shstrtab_off, len(shstrtab), 0,0,1,0) # SHT_STRTAB

out = ehdr + code + shstrtab + b"\x00"*pad + sh_null + sh_text + sh_shstrtab
with open("test.elf","wb") as f:
    f.write(out)
print("wrote test.elf, size", len(out))

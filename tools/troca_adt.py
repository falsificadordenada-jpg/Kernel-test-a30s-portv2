#!/usr/bin/env python3
"""Troca a ADT embutida no shim de uma imagem M4vN (boot.img Android, shim em 0x800).
Uso: troca_adt.py entrada.img adt.bin saida.img
A ADT fica no shim+0x140 (max 0x670 bytes); o shim copia N palavras de 8 bytes e grava o
tamanho em boot_args+0x68. Aqui ajustamos 'mov x6,#N' (shim+0x120) e 'mov w3,#len' (shim+0x110)."""
import struct, sys
SH = 0x800
inp, adtf, outp = sys.argv[1:4]
d = bytearray(open(inp, 'rb').read()); adt = open(adtf, 'rb').read()
adt += b'\0' * (-len(adt) % 8)
MAXL = 0x7b0 - 0x140   # depois disso ha codigo do shim
assert len(adt) <= MAXL, 'ADT grande demais (%d > %d)' % (len(adt), MAXL)
# confere as instrucoes esperadas
assert struct.unpack_from('<I', d, SH+0x110)[0] & 0xffe0001f == 0x52800003, 'mov w3 inesperado'
assert struct.unpack_from('<I', d, SH+0x120)[0] & 0xffe0001f == 0xd2800006, 'mov x6 inesperado'
d[SH+0x140:SH+0x7b0] = b'\0' * MAXL
d[SH+0x140:SH+0x140+len(adt)] = adt
struct.pack_into('<I', d, SH+0x110, 0x52800003 | (len(adt) << 5))
struct.pack_into('<I', d, SH+0x120, 0xd2800006 | ((len(adt)//8) << 5))
open(outp, 'wb').write(d)
print('ADT trocada: %d bytes, %d palavras' % (len(adt), len(adt)//8))

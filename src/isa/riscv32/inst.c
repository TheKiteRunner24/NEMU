#include "local-include/reg.h"
#include <cpu/cpu.h>
#include <cpu/ifetch.h>
#include <cpu/decode.h>

#define R(i) (cpu.gpr[i]._32)
#define Mr(addr, len)       vaddr_read(s, addr, len, MMU_DYNAMIC)
#define Mw(addr, len, data) vaddr_write(s, addr, len, data, MMU_DYNAMIC)

enum {
  TYPE_R, TYPE_I, TYPE_J,
  TYPE_U, TYPE_S, TYPE_B,
  TYPE_N, // none
};

static void decode_operand(Decode *s, word_t *src1, word_t *src2, word_t *imm, int type) {
  uint32_t i = s->isa.instr.val;
  int rs1 = BITS(i, 19, 15);
  int rs2 = BITS(i, 24, 20);
  switch (type) {
    case TYPE_I: *imm = SEXT(BITS(i, 31, 20), 12);
                 *src1 = R(rs1); *src2 = *imm;
                 break;
    case TYPE_U: *imm = BITS(i, 31, 12) << 12; break;
    case TYPE_J: *imm = (SEXT(BITS(i, 31, 31), 1) << 20) | (BITS(i, 19, 12) << 12) |
                   (BITS(i, 20, 20) << 11) | (BITS(i, 30, 21) << 1);
                 break;
    case TYPE_S: *imm = (SEXT(BITS(i, 31, 25), 7) << 5) | BITS(i, 11, 7); goto R;
    case TYPE_B:
      *imm = (SEXT(BITS(i, 31, 31), 1) << 12) | (BITS(i, 7, 7) << 11) |
        (BITS(i, 30, 25) << 5) | (BITS(i, 11, 8) << 1);
      // fall through
    case TYPE_R: R: *src1 = R(rs1); *src2 = R(rs2); break;
  }
}

int isa_new_fetch_decode(Decode *s) {
  s->isa.instr.val = instr_fetch(&s->snpc, 4);
  s->dnpc = s->snpc;
  int rd  = BITS(s->isa.instr.val, 11, 7);
  word_t imm = 0, src1 = 0, src2 = 0;

#define INSTPAT_INST(s) ((s)->isa.instr.val)
#define INSTPAT_MATCH(s, name, type, ... /* body */ ) { \
  decode_operand(s, &src1, &src2, &imm, concat(TYPE_, type)); \
  __VA_ARGS__ ; \
}

  INSTPAT_START();
  INSTPAT("??????? ????? ????? ??? ????? 01101 11", lui      , U, R(rd) = imm);
  INSTPAT("??????? ????? ????? ??? ????? 00101 11", auipc    , U, R(rd) = imm + s->pc);
  INSTPAT("??????? ????? ????? ??? ????? 11011 11", jal      , J, s->dnpc = s->pc + imm, R(rd) = s->snpc);
  INSTPAT("??????? ????? ????? ??? ????? 11001 11", jalr     , I, s->dnpc = src1 + src2, R(rd) = s->snpc);
  INSTPAT("??????? ????? ????? 000 ????? 11000 11", beq      , B, if (src1 == src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 001 ????? 11000 11", bne      , B, if (src1 != src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 100 ????? 11000 11", blt      , B, if ((sword_t)src1 <  (sword_t)src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 101 ????? 11000 11", bge      , B, if ((sword_t)src1 >= (sword_t)src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 110 ????? 11000 11", bltu     , B, if (src1 <  src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 111 ????? 11000 11", bgeu     , B, if (src1 >= src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 000 ????? 00000 11", lb       , I, R(rd) = SEXT(Mr(src1 + src2, 1), 8));
  INSTPAT("??????? ????? ????? 001 ????? 00000 11", lh       , I, R(rd) = SEXT(Mr(src1 + src2, 2), 16));
  INSTPAT("??????? ????? ????? 010 ????? 00000 11", lw       , I, R(rd) = Mr(src1 + src2, 4));
  INSTPAT("??????? ????? ????? 100 ????? 00000 11", lb       , I, R(rd) = Mr(src1 + src2, 1));
  INSTPAT("??????? ????? ????? 101 ????? 00000 11", lh       , I, R(rd) = Mr(src1 + src2, 2));
  INSTPAT("??????? ????? ????? 000 ????? 01000 11", sb       , S, Mw(src1 + imm, 1, src2));
  INSTPAT("??????? ????? ????? 001 ????? 01000 11", sh       , S, Mw(src1 + imm, 2, src2));
  INSTPAT("??????? ????? ????? 010 ????? 01000 11", sw       , S, Mw(src1 + imm, 4, src2));
  INSTPAT("??????? ????? ????? 000 ????? 00100 11", addi     , I, R(rd) = src1 + src2);
  INSTPAT("??????? ????? ????? 010 ????? 00100 11", slti     , I, R(rd) = (sword_t)src1 < (sword_t)src2);
  INSTPAT("??????? ????? ????? 011 ????? 00100 11", sltiu    , I, R(rd) = src1 <  src2);
  INSTPAT("??????? ????? ????? 100 ????? 00100 11", xori     , I, R(rd) = src1 ^  src2);
  INSTPAT("??????? ????? ????? 110 ????? 00100 11", ori      , I, R(rd) = src1 |  src2);
  INSTPAT("??????? ????? ????? 111 ????? 00100 11", andi     , I, R(rd) = src1 &  src2);
  INSTPAT("0000000 ????? ????? 001 ????? 00100 11", slli     , I, R(rd) = src1 << src2);
  INSTPAT("0000000 ????? ????? 101 ????? 00100 11", srli     , I, R(rd) = src1 >> src2);
  INSTPAT("0100000 ????? ????? 101 ????? 00100 11", srai     , I, R(rd) = (sword_t)src1 >> (src2 & 0x1f));
  INSTPAT("0000000 ????? ????? 000 ????? 01100 11", add      , R, R(rd) = src1 +  src2);
  INSTPAT("0100000 ????? ????? 000 ????? 01100 11", sub      , R, R(rd) = src1 -  src2);
  INSTPAT("0000000 ????? ????? 001 ????? 01100 11", sll      , R, R(rd) = src1 << src2);
  INSTPAT("0000000 ????? ????? 010 ????? 01100 11", slt      , R, R(rd) = (sword_t)src1 <  (sword_t)src2);
  INSTPAT("0000000 ????? ????? 011 ????? 01100 11", sltu     , R, R(rd) = src1 <  src2);
  INSTPAT("0000000 ????? ????? 100 ????? 01100 11", xor      , R, R(rd) = src1 ^  src2);
  INSTPAT("0000000 ????? ????? 101 ????? 01100 11", slr      , R, R(rd) = src1 >> src2);
  INSTPAT("0100000 ????? ????? 101 ????? 01100 11", sra      , R, R(rd) = (sword_t)src1 >> src2);
  INSTPAT("0000000 ????? ????? 110 ????? 01100 11", or       , R, R(rd) = src1 |  src2);
  INSTPAT("0000000 ????? ????? 111 ????? 01100 11", and      , R, R(rd) = src1 &  src2);

  INSTPAT("0000001 ????? ????? 000 ????? 01100 11", mul      , R, R(rd) = src1 *  src2);
  INSTPAT("0000001 ????? ????? 001 ????? 01100 11", mulh     , R, R(rd) = ((int64_t)(sword_t)src1 *  (int64_t)(sword_t)src2) >> 32);
  INSTPAT("0000001 ????? ????? 010 ????? 01100 11", mulhsu   , R, R(rd) = src1 *  src2);
  INSTPAT("0000001 ????? ????? 011 ????? 01100 11", mulhu    , R, R(rd) = ((uint64_t)src1 *  (uint64_t)src2) >> 32);
  INSTPAT("0000001 ????? ????? 100 ????? 01100 11", div      , R, R(rd) = (sword_t)src1 /  (sword_t)src2);
  INSTPAT("0000001 ????? ????? 101 ????? 01100 11", divu     , R, R(rd) = src1 /  src2);
  INSTPAT("0000001 ????? ????? 110 ????? 01100 11", rem      , R, R(rd) = (sword_t)src1 %  (sword_t)src2);
  INSTPAT("0000001 ????? ????? 111 ????? 01100 11", remu     , R, R(rd) = src1 %  src2);

  INSTPAT("??????? ????? ????? ??? ????? 11010 11", nemu_trap, N, NEMUTRAP(s->pc, R(10))); // R(10) is $a0
  INSTPAT("??????? ????? ????? ??? ????? ????? ??", inv      , N, INV(s->pc));
  INSTPAT_END();

  R(0) = 0; // reset $zero to 0

  return 0;
}

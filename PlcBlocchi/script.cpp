#include "config.h"
#if HAS_SCRIPT
#include "script.h"

bool runScript(Block& b, int32_t* out, Block* ext, int32_t* extOut) {
  const bool wide = b.type == BT_CSCRIPT;                  // 8 ingressi / 4 uscite
  const uint8_t nIn = wide ? 8 : 4, firstLocal = wide ? 12 : 6;
  const uint8_t* c = Engine::code() + b.k[0];
  const uint16_t len = b.k[1];
  int32_t* vars = Engine::scriptVars + b.k[2];
  const uint8_t nv = b.k[3];
  int32_t st[16];
  uint8_t sp = 0;
  uint16_t pc = 0, budget = SCRIPT_BUDGET;

#define PUSH(v) do { if (sp >= 16) return true; st[sp++] = (v); } while (0)
#define POP(x)  do { if (!sp) return true; x = st[--sp]; } while (0)

  while (pc < len) {
    if (!budget--) return false;
    uint8_t op = c[pc++];
    int32_t a, x;
    switch (op) {
      case OP_END: return true;
      case OP_PUSH:
        if (pc + 4 > len) return true;
        x = (int32_t)((uint32_t)c[pc] | ((uint32_t)c[pc + 1] << 8) | ((uint32_t)c[pc + 2] << 16) | ((uint32_t)c[pc + 3] << 24));
        pc += 4; PUSH(x); break;
      case OP_PUSHB: if (pc >= len) return true; PUSH((int8_t)c[pc++]); break;
      case OP_LOAD: {
        if (pc >= len) return true;
        uint8_t v = c[pc++];
        if (v < 4) x = Engine::input(b, v);
        else if (v < nIn) x = ext ? Engine::input(*ext, v - 4) : 0;
        else if (v < nIn + 2) x = out[v - nIn];
        else if (v < firstLocal) x = extOut ? extOut[v - nIn - 2] : 0;
        else x = (uint8_t)(v - firstLocal) < nv ? vars[v - firstLocal] : 0;
        PUSH(x); break;
      }
      case OP_STORE: {
        if (pc >= len) return true;
        uint8_t v = c[pc++];
        POP(x);
        if (v >= nIn && v < nIn + 2) out[v - nIn] = x;
        else if (v >= nIn + 2 && v < firstLocal) { if (extOut) extOut[v - nIn - 2] = x; }
        else if (v >= firstLocal && (uint8_t)(v - firstLocal) < nv) vars[v - firstLocal] = x;
        break;
      }
      case OP_ADD: POP(x); POP(a); PUSH(a + x); break;
      case OP_SUB: POP(x); POP(a); PUSH(a - x); break;
      case OP_MUL: POP(x); POP(a); PUSH(a * x); break;
      case OP_DIV: POP(x); POP(a); PUSH(x ? a / x : 0); break;
      case OP_MOD: POP(x); POP(a); PUSH(x ? a % x : 0); break;
      case OP_NEG: POP(x); PUSH(-x); break;
      case OP_EQ:  POP(x); POP(a); PUSH(a == x); break;
      case OP_NE:  POP(x); POP(a); PUSH(a != x); break;
      case OP_LT:  POP(x); POP(a); PUSH(a < x); break;
      case OP_LE:  POP(x); POP(a); PUSH(a <= x); break;
      case OP_GT:  POP(x); POP(a); PUSH(a > x); break;
      case OP_GE:  POP(x); POP(a); PUSH(a >= x); break;
      case OP_AND: POP(x); POP(a); PUSH(a && x); break;
      case OP_OR:  POP(x); POP(a); PUSH(a || x); break;
      case OP_XOR: POP(x); POP(a); PUSH((a != 0) != (x != 0)); break;
      case OP_NOT: POP(x); PUSH(!x); break;
      case OP_JMP:
        if (pc + 2 > len) return true;
        pc = c[pc] | (c[pc + 1] << 8); break;
      case OP_JZ: {
        if (pc + 2 > len) return true;
        uint16_t t = c[pc] | (c[pc + 1] << 8);
        pc += 2; POP(x);
        if (!x) pc = t;
        break;
      }
      case OP_ABS: POP(x); PUSH(x < 0 ? -x : x); break;
      case OP_MIN: POP(x); POP(a); PUSH(a < x ? a : x); break;
      case OP_MAX: POP(x); POP(a); PUSH(a > x ? a : x); break;
      case OP_LIMIT: { int32_t mx, v, mn; POP(mx); POP(v); POP(mn); PUSH(v < mn ? mn : v > mx ? mx : v); break; }
      case OP_POP: POP(x); break;
      case OP_MGETB: {
        if (pc >= len) return true;
        uint8_t i = c[pc++];
        PUSH(i < NUM_MBITS ? (Engine::mbits[i >> 3] >> (i & 7)) & 1 : 0); break;
      }
      case OP_MGETW: { if (pc >= len) return true; uint8_t i = c[pc++]; PUSH(i < NUM_MWORDS ? Engine::mwords[i] : 0); break; }
      case OP_MSETB: {
        if (pc >= len) return true;
        uint8_t i = c[pc++];
        POP(x);
        if (i < NUM_MBITS) { if (x) Engine::mbits[i >> 3] |= 1 << (i & 7); else Engine::mbits[i >> 3] &= ~(1 << (i & 7)); }
        break;
      }
      case OP_MSETW: { if (pc >= len) return true; uint8_t i = c[pc++]; POP(x); if (i < NUM_MWORDS) Engine::mwords[i] = x; break; }
      case OP_MILLIS: PUSH((int32_t)(millis() & 0x7FFFFFFF)); break;
      case OP_BAND: POP(x); POP(a); PUSH(a & x); break;
      case OP_BOR:  POP(x); POP(a); PUSH(a | x); break;
      case OP_BXOR: POP(x); POP(a); PUSH(a ^ x); break;
      case OP_BNOT: POP(x); PUSH(~x); break;
      case OP_SHL:  POP(x); POP(a); PUSH(x >= 0 && x < 32 ? (int32_t)((uint32_t)a << x) : 0); break;
      case OP_SHR:  POP(x); POP(a); PUSH(x >= 0 && x < 32 ? a >> x : (a < 0 ? -1 : 0)); break;
      case OP_DUP:  POP(x); PUSH(x); PUSH(x); break;
      default: return true;             // codice non valido: si ferma senza danni
    }
  }
  return true;
#undef PUSH
#undef POP
}
#endif  // HAS_SCRIPT

#pragma once
#include "engine.h"

// Macchina virtuale per il blocco "Script ST". Il testo (Structured Text, come SCL
// di Siemens) viene compilato nel browser in un codice a pila molto compatto;
// qui viene eseguito a ogni ciclo del PLC con un tetto di istruzioni
// (SCRIPT_BUDGET): un ciclo infinito non blocca il PLC.
//
// Indirizzi delle variabili:
//  - Script ST (BT_SCRIPT): 0..3 = IN1..IN4 (sola lettura), 4..5 = OUT1..OUT2, 6.. = locali
//  - Script C (BT_CSCRIPT, con l'eventuale blocco BT_CEXT che lo segue):
//    0..7 = IN1..IN8, 8..11 = OUT1..OUT4, 12.. = locali
// Le locali conservano il valore da un ciclo all'altro.
enum : uint8_t {
  OP_END = 0, OP_PUSH = 1, OP_PUSHB = 2, OP_LOAD = 3, OP_STORE = 4,
  OP_ADD = 5, OP_SUB = 6, OP_MUL = 7, OP_DIV = 8, OP_MOD = 9, OP_NEG = 10,
  OP_EQ = 11, OP_NE = 12, OP_LT = 13, OP_LE = 14, OP_GT = 15, OP_GE = 16,
  OP_AND = 17, OP_OR = 18, OP_NOT = 19, OP_XOR = 20,
  OP_JMP = 21, OP_JZ = 22,
  OP_ABS = 23, OP_MIN = 24, OP_MAX = 25, OP_LIMIT = 26, OP_POP = 27,
  OP_MGETB = 28, OP_MGETW = 29, OP_MSETB = 30, OP_MSETW = 31, OP_MILLIS = 32,
  OP_BAND = 33, OP_BOR = 34, OP_BXOR = 35, OP_BNOT = 36, OP_SHL = 37, OP_SHR = 38, OP_DUP = 39,
};

// false = superato il tetto di istruzioni in questo ciclo
// ext / extOut: il blocco BT_CEXT che segue uno script C (nullptr se non c'e')
bool runScript(Block& b, int32_t* out, Block* ext = nullptr, int32_t* extOut = nullptr);

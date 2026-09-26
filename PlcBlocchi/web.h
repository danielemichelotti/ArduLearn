#pragma once
#include "config.h"

void webBegin();
void webTick();
void webNetUp();   // (Wi-Fi) la rete e' cambiata: riapre il server web    // gestisce al massimo una richiesta per chiamata

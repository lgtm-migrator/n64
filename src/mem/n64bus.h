#ifndef N64_N64BUS_H
#define N64_N64BUS_H

#include <util.h>
#include "../system/n64system.h"

bool tlb_probe(word vaddr, word* paddr, int* entry_number, cp0_t* cp0);
word vatopa(word address, cp0_t* cp0);

void n64_write_dword(n64_system_t* system, word address, dword value);
dword n64_read_dword(n64_system_t* system, word address);

void n64_write_word(n64_system_t* system, word address, word value);
word n64_read_word(n64_system_t* system, word address);

void n64_write_half(n64_system_t* system, word address, half value);
half n64_read_half(n64_system_t* system, word address);

void n64_write_byte(n64_system_t* system, word address, byte value);
byte n64_read_byte(n64_system_t* system, word address);

#endif //N64_N64BUS_H

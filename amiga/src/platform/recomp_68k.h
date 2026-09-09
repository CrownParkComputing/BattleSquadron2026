#ifndef AMIGA_RECOMP_68K_H
#define AMIGA_RECOMP_68K_H

#include <stdint.h>

enum {
    RECOMP_68K_CCR_C = 0x01,
    RECOMP_68K_CCR_V = 0x02,
    RECOMP_68K_CCR_Z = 0x04,
    RECOMP_68K_CCR_N = 0x08,
    RECOMP_68K_CCR_X = 0x10,
    RECOMP_68K_CCR_MASK = 0x1f
};

typedef struct {
    uint32_t d[8];
    uint32_t a[8];
    uint32_t pc;
    uint16_t sr;
} Recomp68kContext;

static inline void recomp_68k_set_dword(uint32_t *reg, uint16_t value)
{
    *reg = (*reg & UINT32_C(0xffff0000)) | value;
}

static inline void recomp_68k_set_dbyte(uint32_t *reg, uint8_t value)
{
    *reg = (*reg & UINT32_C(0xffffff00)) | value;
}

static inline void recomp_68k_set_ccr(Recomp68kContext *cpu, uint8_t ccr)
{
    cpu->sr = (uint16_t)((cpu->sr & ~RECOMP_68K_CCR_MASK) |
                         (ccr & RECOMP_68K_CCR_MASK));
}

static inline uint8_t recomp_68k_ccr(const Recomp68kContext *cpu)
{
    return (uint8_t)(cpu->sr & RECOMP_68K_CCR_MASK);
}

#endif

#ifndef MEM_TIER_H
#define MEM_TIER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MEM_TIER_1 = 1, // 300MB+ Donor (Title Takeover)
    MEM_TIER_2 = 2, // 55-68MB (Applet Mode)
    MEM_TIER_3 = 3  // 24-28MB (Minimal)
} MemTier;

#define MEM_TIER_1_FULL MEM_TIER_1

typedef struct {
    int tier;
    uint64_t free_ram_bytes;
} MemTierInfo;

MemTier mem_detect_tier(void);
uint64_t mem_get_available(void);
MemTierInfo mem_tier_detect(void);

#ifdef __cplusplus
}
#endif

#endif // MEM_TIER_H

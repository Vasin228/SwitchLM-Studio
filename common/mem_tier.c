#include "mem_tier.h"
#include <switch.h>

uint64_t mem_get_available(void) {
    AppletType at = appletGetAppletType();
    if (at == AppletType_Application) {
        return 3200ULL * 1024 * 1024; // 3200 MB (3.2 GB) pool in Title Override mode
    }
    return 32ULL * 1024 * 1024; // Applet mode pool
}

MemTier mem_detect_tier(void) {
    AppletType at = appletGetAppletType();
    if (at == AppletType_Application) {
        return MEM_TIER_1; // Title Override mode
    }
    return MEM_TIER_2; // Applet mode
}

MemTierInfo mem_tier_detect(void) {
    MemTierInfo info;
    info.tier = (int)mem_detect_tier();
    info.free_ram_bytes = mem_get_available();
    return info;
}

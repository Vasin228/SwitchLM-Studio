#pragma once
#include <switch.h>
#include <cstddef>
#include <cstdint>

class UnsafeMem {
public:
    // Expand memory by +40MB from SystemUnsafe pool
    static int expand();
    // Revert memory expansion
    static int contract();

    static void* getDonorBuffer();
    static size_t getDonorSize();
    static bool isExpanded();

    // Application donor memory bridge (via TransferMemory)
    static int mapApplicationDonor(Handle tmem_handle, size_t size);
    static void releaseApplicationDonor();

    // Memory watchdog: checks if application needs its memory back
    static bool checkMemoryPressure();

private:
    static void* s_unsafe_buf;
    static size_t s_unsafe_size;
    static VirtmemReservation* s_unsafe_res;
    static bool s_is_physical_mapped;

    static void* s_donor_buf;
    static size_t s_donor_size;
    static Handle s_donor_tmem;
    static bool s_expanded;
};

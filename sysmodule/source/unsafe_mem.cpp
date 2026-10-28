#include "unsafe_mem.hpp"
#include <switch.h>
#include <cstring>
#include <cstdlib>

void* UnsafeMem::s_unsafe_buf = nullptr;
size_t UnsafeMem::s_unsafe_size = 0;
VirtmemReservation* UnsafeMem::s_unsafe_res = nullptr;
bool UnsafeMem::s_is_physical_mapped = false;

void* UnsafeMem::s_donor_buf = nullptr;
size_t UnsafeMem::s_donor_size = 0;
Handle UnsafeMem::s_donor_tmem = 0;
bool UnsafeMem::s_expanded = false;

int UnsafeMem::expand() {
    if (s_expanded) return 0;

    size_t size = 40ULL * 1024 * 1024; // +40 MB

    // 1. Attempt Atmosphère privileged svcSetUnsafeLimit + svcMapPhysicalMemoryUnsafe
    Result rc = svcSetUnsafeLimit(size);
    if (R_SUCCEEDED(rc)) {
        virtmemLock();
        void* addr = virtmemFindAslr(size, 0x1000);
        VirtmemReservation* res = nullptr;
        if (addr) {
            res = virtmemAddReservation(addr, size);
        }
        virtmemUnlock();

        if (addr && res) {
            rc = svcMapPhysicalMemoryUnsafe(addr, size);
            if (R_SUCCEEDED(rc)) {
                s_unsafe_buf = addr;
                s_unsafe_size = size;
                s_unsafe_res = res;
                s_is_physical_mapped = true;
                s_expanded = true;
                return 0;
            } else {
                virtmemLock();
                virtmemRemoveReservation(res);
                virtmemUnlock();
            }
        }
    }

    // 2. Fallback: aligned heap buffer (4096-byte aligned for Tegra X1 DMA)
    void* heap_buf = aligned_alloc(4096, size);
    if (heap_buf) {
        memset(heap_buf, 0, size);
        s_unsafe_buf = heap_buf;
        s_unsafe_size = size;
        s_is_physical_mapped = false;
        s_expanded = true;
        return 0;
    }

    return -1;
}

int UnsafeMem::contract() {
    if (!s_expanded) return 0;

    if (s_is_physical_mapped && s_unsafe_buf) {
        svcUnmapPhysicalMemoryUnsafe(s_unsafe_buf, s_unsafe_size);
        if (s_unsafe_res) {
            virtmemLock();
            virtmemRemoveReservation(s_unsafe_res);
            virtmemUnlock();
            s_unsafe_res = nullptr;
        }
        svcSetUnsafeLimit(0);
    } else if (s_unsafe_buf) {
        free(s_unsafe_buf);
    }

    s_unsafe_buf = nullptr;
    s_unsafe_size = 0;
    s_is_physical_mapped = false;
    s_expanded = false;
    return 0;
}

void* UnsafeMem::getDonorBuffer() {
    if (s_donor_buf) return s_donor_buf;
    return s_unsafe_buf;
}

size_t UnsafeMem::getDonorSize() {
    if (s_donor_size > 0) return s_donor_size;
    return s_unsafe_size;
}

bool UnsafeMem::isExpanded() {
    return s_expanded;
}

int UnsafeMem::mapApplicationDonor(Handle tmem_handle, size_t size) {
    if (s_donor_buf) releaseApplicationDonor();

    virtmemLock();
    void* addr = virtmemFindAslr(size, 0x1000);
    VirtmemReservation* res = nullptr;
    if (addr) {
        res = virtmemAddReservation(addr, size);
    }
    virtmemUnlock();

    if (!addr || !res) return -1;

    Result rc = svcMapTransferMemory(tmem_handle, addr, size, Perm_Rw);
    if (R_FAILED(rc)) {
        virtmemLock();
        virtmemRemoveReservation(res);
        virtmemUnlock();
        return -2;
    }

    s_donor_buf = addr;
    s_donor_size = size;
    s_donor_tmem = tmem_handle;
    return 0;
}

void UnsafeMem::releaseApplicationDonor() {
    if (s_donor_buf && s_donor_tmem) {
        svcUnmapTransferMemory(s_donor_tmem, s_donor_buf, s_donor_size);
        s_donor_buf = nullptr;
        s_donor_size = 0;
        svcCloseHandle(s_donor_tmem);
        s_donor_tmem = 0;
    }
}

bool UnsafeMem::checkMemoryPressure() {
    u64 total = 0, used = 0;
    if (R_SUCCEEDED(svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)) &&
        R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0))) {
        if (total > used) {
            u64 free_mem = total - used;
            // If free memory falls below 8 MB, trigger emergency release
            return (free_mem < 8ULL * 1024 * 1024);
        }
    }
    return false;
}

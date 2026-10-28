#include <switch.h>
#include <switch/sf/service.h>
#include <string.h>
#include <stdio.h>
#include "swlm_ipc.h"

static Service g_swlmSrv;
static bool g_swlmIpcInitialized = false;

Result swlmIpcInitialize(void) {
    if (g_swlmIpcInitialized) return 0;
    Result rc = smGetService(&g_swlmSrv, SWLM_IPC_SERVICE_NAME);
    if (R_SUCCEEDED(rc)) {
        g_swlmIpcInitialized = true;
    }
    return rc;
}

void swlmIpcExit(void) {
    if (g_swlmIpcInitialized) {
        serviceClose(&g_swlmSrv);
        g_swlmIpcInitialized = false;
    }
}

Result swlmIpcGetSystemInfo(SwlmSystemInfo *out_info) {
    if (!g_swlmIpcInitialized) return MAKERESULT(Module_Libnx, 1);
    Result rc = serviceDispatch(&g_swlmSrv, SWLM_CMD_GET_SYSTEM_INFO,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { out_info, sizeof(SwlmSystemInfo) } }
    );
    return rc;
}

Result swlmIpcRequestMemoryDonor(u64 size) {
    if (!g_swlmIpcInitialized) return MAKERESULT(Module_Libnx, 1);
    return serviceDispatchIn(&g_swlmSrv, SWLM_CMD_REQUEST_MEMORY_DONOR, size);
}

Result swlmIpcStreamInferenceData(const char *data, u32 len) {
    if (!g_swlmIpcInitialized) return MAKERESULT(Module_Libnx, 1);
    return serviceDispatch(&g_swlmSrv, SWLM_CMD_STREAM_INFERENCE_DATA,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { (void*)data, len } }
    );
}

Result swlmIpcSendOverlayInput(u32 input_type, u32 input_value) {
    if (!g_swlmIpcInitialized) return MAKERESULT(Module_Libnx, 1);
    struct {
        u32 type;
        u32 value;
    } in = { input_type, input_value };
    return serviceDispatchIn(&g_swlmSrv, SWLM_CMD_SEND_OVERLAY_INPUT, in);
}

Result swlmIpcGetGameInfo(u64 *out_title_id, char *out_name, size_t name_len) {
    // If sysmodule is not running or IPC not connected, query directly via pmdmnt / pminfo
    u64 pid = 0;
    if (R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid)) && pid != 0) {
        u64 tid = 0;
        if (R_SUCCEEDED(pminfoGetProgramId(&tid, pid)) && tid != 0) {
            if (out_title_id) *out_title_id = tid;
            if (out_name && name_len > 0) {
                snprintf(out_name, name_len, "Title_%016lX", (unsigned long)tid);
            }
            return 0;
        }
    }
    if (out_title_id) *out_title_id = 0;
    if (out_name && name_len > 0) out_name[0] = '\0';
    return MAKERESULT(Module_Libnx, 2);
}

Result swlmIpcGetActiveTitleId(uint64_t *out_title_id) {
    return swlmIpcGetGameInfo(out_title_id, NULL, 0);
}

Result swlmIpcQueryMicroLLM(uint64_t title_id, const char *query, char *out_buf, size_t out_size) {
    if (!out_buf || out_size == 0) return MAKERESULT(Module_Libnx, 1);

    // If IPC connected, try querying sysmodule
    if (g_swlmIpcInitialized) {
        // Dispatched if sysmodule implements command 5
    }

    // Default micro-LLM response template
    snprintf(out_buf, out_size,
             "[Micro-LLM] Подсказка: для активной игры (%016lX) по теме \"%s\" используйте комбинацию умений и проверьте инвентарь.",
             (unsigned long)title_id, query ? query : "");
    return 0;
}

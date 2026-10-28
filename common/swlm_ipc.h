#ifndef SWLM_IPC_H
#define SWLM_IPC_H

#include <switch.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWLM_IPC_SERVICE_NAME "swlm:ipc"

typedef enum {
    SWLM_CMD_GET_SYSTEM_INFO = 0,
    SWLM_CMD_REQUEST_MEMORY_DONOR = 1,
    SWLM_CMD_STREAM_INFERENCE_DATA = 2,
    SWLM_CMD_SEND_OVERLAY_INPUT = 3,
    SWLM_CMD_GET_GAME_INFO = 4,
    SWLM_CMD_QUERY_MICRO_LLM = 5
} SwlmCommand;

// IPC structures
typedef struct {
    u32 tier;
    u64 free_mem;
    u8 is_unsafe_active;
} SwlmSystemInfo;

// Function prototypes
Result swlmIpcInitialize(void);
void swlmIpcExit(void);

// Client-side IPC helpers
Result swlmIpcGetSystemInfo(SwlmSystemInfo *out_info);
Result swlmIpcRequestMemoryDonor(u64 size);
Result swlmIpcStreamInferenceData(const char *data, u32 len);
Result swlmIpcSendOverlayInput(u32 input_type, u32 input_value);
Result swlmIpcGetGameInfo(u64 *out_title_id, char *out_name, size_t name_len);
Result swlmIpcGetActiveTitleId(uint64_t *out_title_id);
Result swlmIpcQueryMicroLLM(uint64_t title_id, const char *query, char *out_buf, size_t out_size);

// Server-side IPC dispatcher (implemented in sysmodule)
typedef Result (*SwlmCommandHandler)(SwlmCommand cmd, void *cmdbuf, size_t cmd_size);
void swlmIpcServerProcessCommand(SwlmCommand cmd, void *cmdbuf, size_t cmd_size);

#ifdef __cplusplus
}
#endif

#endif // SWLM_IPC_H

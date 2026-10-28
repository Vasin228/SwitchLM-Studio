#include "ipc_server.hpp"
#include <switch.h>
#include <string.h>
#include <vector>
extern "C" {
#include "swlm_ipc.h"
#include "mem_tier.h"
}

IPCServer::IPCServer() : m_running(false), m_port_handle(0), m_detector(nullptr), m_llm(nullptr) {}

IPCServer::~IPCServer() {
    stop();
}

bool IPCServer::init(GameDetector *detector, MicroLLM *llm) {
    m_detector = detector;
    m_llm = llm;

    SmServiceName name = smEncodeName(SWLM_IPC_SERVICE_NAME);
    Result rc = smRegisterService(&m_port_handle, name, false, 4);
    if (R_FAILED(rc)) {
        return false;
    }

    m_running = true;
    return true;
}

void IPCServer::stop() {
    m_running = false;
    if (m_port_handle != 0) {
        svcCloseHandle(m_port_handle);
        m_port_handle = 0;
    }
}

void IPCServer::handleSession(Handle session) {
    // Process single IPC session request
    u32 *tls = (u32*)armGetTls();

    // Check for CMIF request (SFCI = 0x49434653)
    // CMIF format: tls[0..3] HIPC header, then Data Header
    // Search for SFCI magic
    int magic_idx = -1;
    for (int i = 0; i < 16; i++) {
        if (tls[i] == 0x49434653) {
            magic_idx = i;
            break;
        }
    }

    if (magic_idx >= 0) {
        u32 cmd_id = tls[magic_idx + 2];
        u32 *out = &tls[magic_idx];

        // Format CMIF reply (SFCO = 0x4F434653)
        out[0] = 0x4F434653;
        out[1] = 0;
        out[2] = 0; // Result = Success
        out[3] = 0;

        switch (cmd_id) {
            case SWLM_CMD_GET_SYSTEM_INFO: {
                SwlmSystemInfo info;
                info.tier = (u32)mem_detect_tier();
                info.free_mem = mem_get_available();
                info.is_unsafe_active = UnsafeMem::isExpanded() ? 1 : 0;
                memcpy(&out[4], &info, sizeof(info));
                break;
            }
            case SWLM_CMD_REQUEST_MEMORY_DONOR: {
                UnsafeMem::expand();
                out[4] = 0; // OK
                break;
            }
            case SWLM_CMD_GET_GAME_INFO: {
                u64 tid = m_detector ? m_detector->getActiveTitleID() : 0;
                char name[128] = {0};
                if (m_detector) {
                    m_detector->getActiveTitleName(name, sizeof(name));
                }
                memcpy(&out[4], &tid, sizeof(tid));
                memcpy(&out[6], name, sizeof(name));
                break;
            }
            case SWLM_CMD_SEND_OVERLAY_INPUT: {
                u32 itype = tls[magic_idx + 4];
                u32 ival = tls[magic_idx + 5];
                if (m_llm) m_llm->onOverlayInput(itype, ival);
                out[4] = 0;
                break;
            }
            default:
                break;
        }
    }
}

void IPCServer::run() {
    std::vector<Handle> handles;
    handles.push_back(m_port_handle);

    while (m_running) {
        s32 handle_idx = -1;
        Result rc = svcWaitSynchronization(&handle_idx, handles.data(), handles.size(), 500000000ULL); // 500ms timeout

        if (rc == 0xEA01) {
            // Timeout: continue event loop
            continue;
        }

        if (R_SUCCEEDED(rc) && handle_idx >= 0) {
            if (handle_idx == 0) {
                // Incoming connection on port -> accept session
                Handle new_session = 0;
                if (R_SUCCEEDED(svcAcceptSession(&new_session, m_port_handle))) {
                    if (handles.size() < 5) {
                        handles.push_back(new_session);
                    } else {
                        svcCloseHandle(new_session);
                    }
                }
            } else {
                // Incoming request on an active session
                Handle sess = handles[handle_idx];
                s32 dummy_idx = -1;
                rc = svcReplyAndReceive(&dummy_idx, &sess, 1, 0, 0);
                if (R_SUCCEEDED(rc)) {
                    handleSession(sess);
                    svcReplyAndReceive(&dummy_idx, &sess, 0, sess, 0);
                } else {
                    // Session closed or disconnected
                    svcCloseHandle(sess);
                    handles.erase(handles.begin() + handle_idx);
                }
            }
        }
    }

    // Cleanup sessions
    for (size_t i = 1; i < handles.size(); i++) {
        svcCloseHandle(handles[i]);
    }
}

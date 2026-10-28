#include "model_manager.hpp"
#include <switch.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

ModelManager::ModelManager() {}
ModelManager::~ModelManager() {}

std::string ModelManager::detectQuantization(const std::string &filename) {
    std::string upper = filename;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (upper.find("Q4_K_M") != std::string::npos) return "Q4_K_M";
    if (upper.find("Q4_K_S") != std::string::npos) return "Q4_K_S";
    if (upper.find("Q5_K_M") != std::string::npos) return "Q5_K_M";
    if (upper.find("Q3_K_M") != std::string::npos) return "Q3_K_M";
    if (upper.find("Q3_K_S") != std::string::npos) return "Q3_K_S";
    if (upper.find("Q2_K")   != std::string::npos) return "Q2_K";
    if (upper.find("Q8_0")   != std::string::npos) return "Q8_0";
    if (upper.find("Q4_0")   != std::string::npos) return "Q4_0";
    if (upper.find("Q6_K")   != std::string::npos) return "Q6_K";
    return "Q4_0";
}

bool ModelManager::isAppletCompatible(uint64_t size_bytes) {
    return size_bytes < 380ULL * 1024 * 1024;
}

bool ModelManager::isOverlayMicro(uint64_t size_bytes) {
    return size_bytes <= 68ULL * 1024 * 1024;
}

void ModelManager::scanDirectory(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN) continue;
        std::string name = entry->d_name;
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find(".gguf") == std::string::npos) continue;
        std::string full_path = std::string(dir_path) + "/" + name;
        
        bool already_added = false;
        for (const auto& existing : m_models) {
            if (existing.path == full_path) {
                already_added = true;
                break;
            }
        }
        if (already_added) continue;

        struct stat st;
        if (stat(full_path.c_str(), &st) != 0) continue;
        uint64_t size = (uint64_t)st.st_size;

        // Verify valid GGUF header and ensure file has neural weights (tensor_count > 0)
        FILE* fp = fopen(full_path.c_str(), "rb");
        if (fp) {
            char magic[4] = {0};
            uint32_t version = 0;
            uint64_t tensor_count = 0;
            if (fread(magic, 1, 4, fp) == 4 && memcmp(magic, "GGUF", 4) == 0) {
                fread(&version, 4, 1, fp);
                fread(&tensor_count, 8, 1, fp);
            }
            fclose(fp);
            if (tensor_count == 0) {
                // Ignore empty metadata stubs without tensor weights
                continue;
            }
        } else {
            continue;
        }
        DiscoveredModel m;
        m.path = full_path;
        m.name = name;
        m.size_bytes = size;
        m.quantization = detectQuantization(name);
        m.applet_ok = isAppletCompatible(size);
        m.overlay_micro = isOverlayMicro(size);
        m.loaded = false;
        m_models.push_back(m);
    }
    closedir(dir);
}

void ModelManager::scanModels() {
    m_models.clear();
    scanDirectory("sdmc:/switch/SwitchLM/models");
    scanDirectory("sdmc:/switch/SwitchLM/micro");
    
    // Sort: Qwen model is always first (#1), followed by other models
    std::sort(m_models.begin(), m_models.end(), [](const DiscoveredModel &a, const DiscoveredModel &b) {
        bool a_qwen = (a.name.find("Qwen") != std::string::npos || a.name.find("qwen") != std::string::npos);
        bool b_qwen = (b.name.find("Qwen") != std::string::npos || b.name.find("qwen") != std::string::npos);
        if (a_qwen != b_qwen) return a_qwen;
        return a.size_bytes > b.size_bytes;
    });
}

const std::vector<DiscoveredModel>& ModelManager::getModels() const {
    return m_models;
}

const DiscoveredModel* ModelManager::getModel(size_t index) const {
    if (index < m_models.size()) return &m_models[index];
    return nullptr;
}

size_t ModelManager::getModelCount() const {
    return m_models.size();
}

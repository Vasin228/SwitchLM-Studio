#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct DiscoveredModel {
    std::string path;
    std::string name;
    uint64_t size_bytes;
    std::string quantization;
    bool applet_ok;
    bool overlay_micro;
    bool loaded;
};

class ModelManager {
public:
    ModelManager();
    ~ModelManager();

    void scanModels();
    const std::vector<DiscoveredModel>& getModels() const;
    const DiscoveredModel* getModel(size_t index) const;
    size_t getModelCount() const;

private:
    std::vector<DiscoveredModel> m_models;

    void scanDirectory(const char *dir_path);
    std::string detectQuantization(const std::string &filename);
    bool isAppletCompatible(uint64_t size_bytes);
    bool isOverlayMicro(uint64_t size_bytes);
};
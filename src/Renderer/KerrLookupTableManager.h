#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include "Scene.h"
#include "Shader.h"

class KerrLookupTableManager {
public:
    KerrLookupTableManager();
    ~KerrLookupTableManager();

    void initialize(int lutResolution = 64, float maxDistance = 100.0f);
    uint32_t getLookupTable(const BlackHole& blackHole);
    bool needsRegeneration(const BlackHole& blackHole);
    void regenerateLookupTable(const BlackHole& blackHole);
    void cleanup();

    int getLutResolution() const { return m_lutResolution; }
    float getMaxDistance() const { return m_maxDistance; }
    uint32_t getCurrentLookupTable() const { return m_currentLookupTable; }

    void setLutResolution(int resolution);
    void setMaxDistance(float distance);

    bool isGenerating() const { return m_isGenerating; }
    void forceRegenerateAll();
    int getGenerationProgress() const { return m_generationProgress; }

private:
    struct LookupTableEntry {
        uint32_t textureId = 0;
        BlackHole blackHole;
        bool isGenerated = false;
        double lastUsed = 0.0;
    };

    void generateLookupTable(const BlackHole& blackHole, uint32_t textureId);
    void createTexture3D(uint32_t& textureId);
    void deleteTexture3D(uint32_t textureId);

    std::unique_ptr<Shader> m_kerrLutShader;

    std::unordered_map<BlackHole, LookupTableEntry, BlackHoleHash> m_lookupCache;
    uint32_t m_currentLookupTable;
    BlackHole m_currentBlackHole;

    int m_lutResolution;
    float m_maxDistance;
    static const int MAX_CACHE_SIZE = 8;

    bool m_initialized;
    bool m_isGenerating = false;
    int m_generationProgress = 0; // 0-100

    void bindUniforms(const BlackHole& blackHole);
    double getCurrentTime() const;
    void evictOldestEntry();
};

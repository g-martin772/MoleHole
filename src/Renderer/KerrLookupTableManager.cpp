#include "KerrLookupTableManager.h"

#include <glad/gl.h>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cmath>

KerrLookupTableManager::KerrLookupTableManager()
    : m_currentLookupTable(0), m_lutResolution(64),
      m_maxDistance(100.0f), m_initialized(false) {
}

KerrLookupTableManager::~KerrLookupTableManager() {
    cleanup();
}

void KerrLookupTableManager::initialize(int lutResolution, float maxDistance) {
    m_lutResolution = lutResolution;
    m_maxDistance = maxDistance;

    m_kerrLutShader = std::make_unique<Shader>("../shaders/kerr_lut_generator.comp", true);
    m_initialized = true;
}

void KerrLookupTableManager::createTexture3D(GLuint& textureId) {
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_3D, textureId);

    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F,
                 m_lutResolution, m_lutResolution, m_lutResolution,
                 0, GL_RGBA, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_3D, 0);
}

void KerrLookupTableManager::deleteTexture3D(GLuint textureId) {
    if (textureId != 0) {
        glDeleteTextures(1, &textureId);
    }
}

GLuint KerrLookupTableManager::getLookupTable(const BlackHole& blackHole) {
    if (!m_initialized) {
        std::cerr << "KerrLookupTableManager not initialized" << std::endl;
        return 0;
    }

    auto it = m_lookupCache.find(blackHole);
    if (it != m_lookupCache.end()) {
        it->second.lastUsed = getCurrentTime();
        m_currentLookupTable = it->second.textureId;
        m_currentBlackHole = blackHole;
        return m_currentLookupTable;
    }

    GLuint newTextureId = 0;
    createTexture3D(newTextureId);

    if (m_lookupCache.size() >= MAX_CACHE_SIZE) {
        evictOldestEntry();
    }

    LookupTableEntry entry;
    entry.textureId = newTextureId;
    entry.blackHole = blackHole;
    entry.isGenerated = false;
    entry.lastUsed = getCurrentTime();

    m_lookupCache[blackHole] = entry;

    generateLookupTable(blackHole, newTextureId);
    m_lookupCache[blackHole].isGenerated = true;

    m_currentLookupTable = newTextureId;
    m_currentBlackHole = blackHole;

    return newTextureId;
}

void KerrLookupTableManager::generateLookupTable(const BlackHole& blackHole, GLuint textureId) {
    if (!m_kerrLutShader) {
        std::cerr << "Kerr LUT shader not available" << std::endl;
        return;
    }

    m_isGenerating = true;
    m_generationProgress = 0;

    glBindImageTexture(0, textureId, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    m_kerrLutShader->Bind();

    bindUniforms(blackHole);

    int workGroupSize = 8; // matches local_size in shader
    int numWorkGroups = (m_lutResolution + workGroupSize - 1) / workGroupSize;

    m_kerrLutShader->Dispatch(numWorkGroups, numWorkGroups, numWorkGroups);

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    m_generationProgress = 100;
    m_isGenerating = false;

    m_kerrLutShader->Unbind();
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
}

void KerrLookupTableManager::bindUniforms(const BlackHole& blackHole) {
    // Use existing Shader utility methods instead of manual uniform binding
    m_kerrLutShader->SetVec3("u_blackHolePos", blackHole.position);
    m_kerrLutShader->SetFloat("u_blackHoleMass", blackHole.mass);
    m_kerrLutShader->SetFloat("u_blackHoleSpin", blackHole.spin);
    m_kerrLutShader->SetVec3("u_blackHoleSpinAxis", blackHole.spinAxis);
    m_kerrLutShader->SetFloat("u_maxDistance", m_maxDistance);
    m_kerrLutShader->SetInt("u_lutResolution", m_lutResolution);
}

bool KerrLookupTableManager::needsRegeneration(const BlackHole& blackHole) {
    auto it = m_lookupCache.find(blackHole);
    return it == m_lookupCache.end() || !it->second.isGenerated;
}

void KerrLookupTableManager::regenerateLookupTable(const BlackHole& blackHole) {
    auto it = m_lookupCache.find(blackHole);
    if (it != m_lookupCache.end()) {
        generateLookupTable(blackHole, it->second.textureId);
        it->second.isGenerated = true;
        it->second.lastUsed = getCurrentTime();
    } else {
        getLookupTable(blackHole);
    }
}

void KerrLookupTableManager::cleanup() {
    for (auto& pair : m_lookupCache) {
        deleteTexture3D(pair.second.textureId);
    }
    m_lookupCache.clear();
    m_currentLookupTable = 0;
}

void KerrLookupTableManager::setLutResolution(int resolution) {
    if (resolution != m_lutResolution) {
        m_lutResolution = resolution;
        cleanup();
    }
}

void KerrLookupTableManager::setMaxDistance(float distance) {
    if (distance != m_maxDistance) {
        m_maxDistance = distance;
        cleanup();
    }
}

void KerrLookupTableManager::forceRegenerateAll() {
    for (auto& pair : m_lookupCache) {
        pair.second.isGenerated = false;
        generateLookupTable(pair.second.blackHole, pair.second.textureId);
        pair.second.isGenerated = true;
        pair.second.lastUsed = getCurrentTime();
    }
}

double KerrLookupTableManager::getCurrentTime() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() / 1000.0;
}

void KerrLookupTableManager::evictOldestEntry() {
    if (m_lookupCache.empty()) return;

    auto oldest = m_lookupCache.begin();
    for (auto it = m_lookupCache.begin(); it != m_lookupCache.end(); ++it) {
        if (it->second.lastUsed < oldest->second.lastUsed) {
            oldest = it;
        }
    }

    deleteTexture3D(oldest->second.textureId);
    m_lookupCache.erase(oldest);
}

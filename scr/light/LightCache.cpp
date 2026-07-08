#include "LightCache.h"
#include "../chunk/ChunkManager.h"
#include <iostream>

LightCacheManager::~LightCacheManager() {
    shutdown();
}

void LightCacheManager::initGL() {
    if (m_initialized) return;
    m_initialized = true;
    std::cout << "[LightCache] GL initialized" << std::endl;
}

void LightCacheManager::shutdown() {
    if (m_lightSSBO)      { glDeleteBuffers(1, &m_lightSSBO);      m_lightSSBO = 0; }
    if (m_sectionMapSSBO) { glDeleteBuffers(1, &m_sectionMapSSBO); m_sectionMapSSBO = 0; }
    m_lightSSBOSize = 0;
    m_sectionMapSSBOSize = 0;
    m_initialized = false;
}

SectionLightCache* LightCacheManager::getOrCreate(uint64_t sectionKey) {
    return &m_caches[sectionKey];
}

const SectionLightCache* LightCacheManager::get(uint64_t sectionKey) const {
    auto it = m_caches.find(sectionKey);
    return (it != m_caches.end()) ? &it->second : nullptr;
}

SectionLightCache* LightCacheManager::getMutable(uint64_t sectionKey) {
    auto it = m_caches.find(sectionKey);
    return (it != m_caches.end()) ? &it->second : nullptr;
}

void LightCacheManager::remove(uint64_t sectionKey) {
    m_caches.erase(sectionKey);
}

void LightCacheManager::clear() {
    m_caches.clear();
    if (m_lightSSBO) {
        glDeleteBuffers(1, &m_lightSSBO);
        m_lightSSBO = 0;
        m_lightSSBOSize = 0;
    }
    if (m_sectionMapSSBO) {
        glDeleteBuffers(1, &m_sectionMapSSBO);
        m_sectionMapSSBO = 0;
        m_sectionMapSSBOSize = 0;
    }
}

void LightCacheManager::rebuildSectionOffsets(const glm::ivec3& camSecMin,
                                               const glm::ivec3& camSecMax) {
    m_cachedCamSecMin = camSecMin;
    m_cachedCamSecMax = camSecMax;

    glm::ivec3 range = camSecMax - camSecMin + 1;
    int totalSlots = range.x * range.y * range.z;

    // Allocate section map: each slot stores offset into lightData SSBO (or -1)
    std::vector<int32_t> sectionMap(totalSlots, -1);
    std::vector<uint8_t> lightData;

    for (auto& [key, cache] : m_caches) {
        if (!cache.hasAnyLight()) continue;

        // Decode SectionKey (from ChunkManager::makeSectionKey):
        //   ux = chunkX & 0xFFFFFF, uy = sectionY & 0xFF, uz = chunkZ & 0xFFFFFF
        //   key = (ux << 32) | (uz << 8) | uy
        int chunkX = int(int32_t((key >> 32) & 0xFFFFFFu));
        int chunkZ = int(int32_t((key >> 8)  & 0xFFFFFFu));
        int sectionY = int(key & 0xFFu);

        // Sign extension for negative chunk coords
        if (chunkX & 0x800000) chunkX |= ~0xFFFFFF;
        if (chunkZ & 0x800000) chunkZ |= ~0xFFFFFF;

        int sectionX = chunkX;
        int sectionZ = chunkZ;

        // Compute position in lookup table
        glm::ivec3 rel = glm::ivec3(sectionX, sectionY, sectionZ) - camSecMin;

        if (rel.x < 0 || rel.x >= range.x ||
            rel.y < 0 || rel.y >= range.y ||
            rel.z < 0 || rel.z >= range.z)
            continue;

        int mapIdx = rel.x + rel.y * range.x + rel.z * range.x * range.y;

        // Append light data
        int offset = (int)lightData.size() / (int)sizeof(uint32_t);
        const uint32_t* src = cache.rawData();
        lightData.insert(lightData.end(),
                          reinterpret_cast<const uint8_t*>(src),
                          reinterpret_cast<const uint8_t*>(src + SectionLightCache::CELLS));

        sectionMap[mapIdx] = offset;
        cache.dirty = false;
    }

    // Upload section map SSBO
    GLsizeiptr mapBytes = (GLsizeiptr)sectionMap.size() * sizeof(int32_t);
    if (!m_sectionMapSSBO || m_sectionMapSSBOSize < mapBytes) {
        if (!m_sectionMapSSBO) glGenBuffers(1, &m_sectionMapSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_sectionMapSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, mapBytes, sectionMap.data(), GL_DYNAMIC_DRAW);
        m_sectionMapSSBOSize = mapBytes;
    } else {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_sectionMapSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, mapBytes, sectionMap.data());
    }

    // Upload light data SSBO
    GLsizeiptr dataBytes = (GLsizeiptr)lightData.size();
    if (dataBytes == 0) {
        if (!m_lightSSBO) {
            glGenBuffers(1, &m_lightSSBO);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_lightSSBO);
            int zero = 0;
            glBufferData(GL_SHADER_STORAGE_BUFFER, 4, &zero, GL_DYNAMIC_DRAW);
            m_lightSSBOSize = 4;
        }
    } else if (!m_lightSSBO || m_lightSSBOSize < dataBytes) {
        if (!m_lightSSBO) glGenBuffers(1, &m_lightSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_lightSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, dataBytes, lightData.data(), GL_DYNAMIC_DRAW);
        m_lightSSBOSize = dataBytes;
    } else {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_lightSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, dataBytes, lightData.data());
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void LightCacheManager::uploadToGPU(const glm::ivec3& cameraSecMin,
                                     const glm::ivec3& cameraSecMax) {
    if (!m_initialized) return;

    bool needsRebuild = (cameraSecMin != m_cachedCamSecMin ||
                         cameraSecMax != m_cachedCamSecMax);

    if (!needsRebuild) {
        for (const auto& [key, cache] : m_caches) {
            if (cache.dirty) { needsRebuild = true; break; }
        }
    }

    if (needsRebuild) {
        rebuildSectionOffsets(cameraSecMin, cameraSecMax);
    }
}

void LightCacheManager::bindSSBOs() const {
    if (m_lightSSBO) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_lightSSBO);
    }
    if (m_sectionMapSSBO) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_sectionMapSSBO);
    }
}

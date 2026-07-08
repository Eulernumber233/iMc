#include "LightSource.h"
#include <algorithm>

void LightSourceRegistry::addLight(const glm::ivec3& pos, const LightSource& src) {
    m_lights[posKey(pos)] = src;
}

void LightSourceRegistry::removeLight(const glm::ivec3& pos) {
    m_lights.erase(posKey(pos));
}

bool LightSourceRegistry::hasLight(const glm::ivec3& pos) const {
    return m_lights.find(posKey(pos)) != m_lights.end();
}

void LightSourceRegistry::removeLightsInChunk(int chunkX, int chunkZ) {
    int minX = chunkX * 16;
    int maxX = minX + 15;
    int minZ = chunkZ * 16;
    int maxZ = minZ + 15;
    for (auto it = m_lights.begin(); it != m_lights.end(); ) {
        const auto& pos = it->second.pos;
        if (pos.x >= minX && pos.x <= maxX &&
            pos.z >= minZ && pos.z <= maxZ) {
            it = m_lights.erase(it);
        } else {
            ++it;
        }
    }
}

void LightSourceRegistry::onBlockChanged(const glm::ivec3& pos,
                                          BlockState oldState, BlockState newState) {
    BlockType oldType = oldState.type();
    BlockType newType = newState.type();

    if (isLightBlock(oldType)) {
        removeLight(pos);
    }
    if (isLightBlock(newType)) {
        LightSource src = getBlockLightDef(newType, pos);
        if (src.intensity > 0.0f) {
            addLight(pos, src);
        }
    }
}

LightSource LightSourceRegistry::makeTorchLight(const glm::ivec3& torchAirPos,
                                                  BlockType torchType) {
    (void)torchType;
    // Torch light source: warm yellow, radius 14 cells
    return LightSource(torchAirPos,
                        glm::vec3(0.98f, 0.85f, 0.35f),
                        0.8f,
                        14.0f,
                        LightType::Point);
}

std::vector<LightSource> LightSourceRegistry::queryAffecting(
    const glm::ivec3& regionMin, const glm::ivec3& regionMax) const
{
    std::vector<LightSource> result;
    for (const auto& [key, src] : m_lights) {
        if (src.pos.x + (int)src.radius >= regionMin.x &&
            src.pos.x - (int)src.radius <= regionMax.x &&
            src.pos.y + (int)src.radius >= regionMin.y &&
            src.pos.y - (int)src.radius <= regionMax.y &&
            src.pos.z + (int)src.radius >= regionMin.z &&
            src.pos.z - (int)src.radius <= regionMax.z)
        {
            result.push_back(src);
        }
    }
    return result;
}

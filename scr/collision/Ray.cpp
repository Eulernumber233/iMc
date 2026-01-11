// Ray.cpp 修复版
#include "Ray.h"
#include "../chunk/ChunkManager.h"
#include "../chunk/Chunk.h"
#include <iostream>
#include <limits>
#include <cmath>

Ray::Ray(const glm::vec3& origin, const glm::vec3& direction)
    : m_origin(origin), m_direction(glm::normalize(direction))
{
    // 预计算反方向，提高性能
    m_invDirection = glm::vec3(
        1.0f / (std::abs(m_direction.x) > 0.0001f ? m_direction.x : 0.0001f),
        1.0f / (std::abs(m_direction.y) > 0.0001f ? m_direction.y : 0.0001f),
        1.0f / (std::abs(m_direction.z) > 0.0001f ? m_direction.z : 0.0001f)
    );
}

// 世界坐标转区块局部坐标
glm::ivec3 Ray::worldToLocal(const glm::ivec3& worldPos) {
    return glm::ivec3(
        ((worldPos.x % Chunk::WIDTH) + Chunk::WIDTH) % Chunk::WIDTH,
        worldPos.y,
        ((worldPos.z % Chunk::DEPTH) + Chunk::DEPTH) % Chunk::DEPTH
    );
}

Ray::HitResult Ray::cast(ChunkManager* chunkManager, float maxDistance) {
    HitResult result;
    if (!chunkManager) {
        return result;
    }

    // 使用改进的DDA算法进行射线检测
    if (ddaCast(chunkManager, result, maxDistance)) {
        result.hit = true;
    }

    return result;
}

bool Ray::ddaCast(ChunkManager* chunkManager, HitResult& result, float maxDistance) {
    // 1. 重要：对起点进行微小偏移，避免精度问题
    glm::vec3 origin = m_origin;

    // 2. 确定当前方块坐标（向下取整）
    glm::ivec3 currentBlock(
        static_cast<int>(std::floor(origin.x)),
        static_cast<int>(std::floor(origin.y)),
        static_cast<int>(std::floor(origin.z))
    );

    // 3. 确定步进方向
    glm::ivec3 step(
        m_direction.x > 0 ? 1 : -1,
        m_direction.y > 0 ? 1 : -1,
        m_direction.z > 0 ? 1 : -1
    );

    // 4. 计算到下一个边界的距离
    glm::vec3 tMax;
    glm::vec3 tDelta = glm::abs(m_invDirection);  // 移动一个方块的距离

    // 计算初始tMax
    for (int i = 0; i < 3; ++i) {
        if (m_direction[i] != 0) {
            // 计算到当前方块边界或下一个方块边界的距离
            float boundary = m_direction[i] > 0 ?
                currentBlock[i] + 1.0f :  // 正方向：下一个方块边界
                currentBlock[i];          // 负方向：当前方块边界

            tMax[i] = (boundary - origin[i]) * m_invDirection[i];
        }
        else {
            tMax[i] = std::numeric_limits<float>::max();
        }
    }

    // 5. 步进循环
    glm::vec3 normal(0.0f);
    float distance = 0.0f;
    int maxSteps = static_cast<int>(maxDistance * 3);

    for (int stepCount = 0; stepCount < maxSteps; ++stepCount) {
        // 找到最小的tMax（下一个遇到的边界）
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            // X方向最先遇到边界
            currentBlock.x += step.x;
            distance = tMax.x;
            tMax.x += tDelta.x;
            normal = glm::vec3(-step.x, 0.0f, 0.0f);  // 法线指向射线来的方向
        }
        else if (tMax.y < tMax.z) {
            // Y方向最先遇到边界
            currentBlock.y += step.y;
            distance = tMax.y;
            tMax.y += tDelta.y;
            normal = glm::vec3(0.0f, -step.y, 0.0f);
        }
        else {
            // Z方向最先遇到边界
            currentBlock.z += step.z;
            distance = tMax.z;
            tMax.z += tDelta.z;
            normal = glm::vec3(0.0f, 0.0f, -step.z);
        }

        // 检查是否超出最大距离
        if (distance > maxDistance) {
            break;
        }

        // 6. 检查当前方块
        // 获取区块位置
        glm::ivec2 chunkPos(
            static_cast<int>(std::floor(static_cast<float>(currentBlock.x) / Chunk::WIDTH)),
            static_cast<int>(std::floor(static_cast<float>(currentBlock.z) / Chunk::DEPTH))
        );

        Chunk* chunk = chunkManager->getChunk(chunkPos);
        if (!chunk) {
            // 区块未加载，跳过
            continue;
        }

        // 获取局部坐标
        glm::ivec3 localPos = worldToLocal(currentBlock);

        // 获取方块类型
        BlockType blockType = chunk->getBlock(localPos.x, localPos.y, localPos.z);

        // 跳过空气方块
        if (blockType == BLOCK_AIR) {
            continue;
        }

        // 7. 计算精确的命中点
        glm::vec3 hitPoint = m_origin + m_direction * distance;

        // 8. 确定被击中的面
        BlockFace face = BlockFace::FRONT;
        if (normal.x > 0.5f) face = BlockFace::LEFT;      // +X normal = 击中LEFT面
        else if (normal.x < -0.5f) face = BlockFace::RIGHT; // -X normal = 击中RIGHT面
        else if (normal.y > 0.5f) face = BlockFace::DOWN;   // +Y normal = 击中DOWN面
        else if (normal.y < -0.5f) face = BlockFace::UP;    // -Y normal = 击中UP面
        else if (normal.z > 0.5f) face = BlockFace::BACK;   // +Z normal = 击中BACK面
        else if (normal.z < -0.5f) face = BlockFace::FRONT; // -Z normal = 击中FRONT面

        // 9. 填充结果
        result.blockPos = currentBlock;
        result.adjacentPos = currentBlock + glm::ivec3(normal);  // 相邻位置用于放置
        result.hitPoint = hitPoint;
        result.normal = normal;
        result.face = face;
        result.distance = distance;
        result.blockType = blockType;

        return true;
    }

    return false;
}
#include "Chunk.h"
#include "../generate/TerrainGenerator.h"
#include <iostream>
#include <algorithm>
#include <cstring>

Chunk::Chunk(const glm::ivec2& position,ChunkManager* chunkManeger)
    : m_position(position)
    , m_isLoaded(false)
    , m_isVisible(false)
	, m_chunkManager(chunkManeger)
{
    m_minPos.x = m_position.x * WIDTH;
    m_minPos.y = 0;  // Y轴从0开始
    m_minPos.z = m_position.y * DEPTH;

    m_maxPos.x = m_minPos.x + WIDTH;
    m_maxPos.y = HEIGHT;
    m_maxPos.z = m_minPos.z + DEPTH;
    // 初始化方块数组
    m_blocks.resize(VOLUME, BLOCK_AIR);

}

Chunk::~Chunk() {
    unload();
}

void Chunk::load() {
    if (m_isLoaded) return;

    // 生成地形
    generateTerrain();

    // 连接邻居区块
    connectNeighborBlock();

    // 计算可见性并生成实例化数据
    calculateVisibility();

    m_isLoaded = true;

    // 输出调试信息
    //print_m_instanceData();
}

void Chunk::unload() {
    if (!m_isLoaded) return;

    // 清空实例化数据
    for (auto& pair : m_instanceData) {
        pair.second.clear();
        pair.second.shrink_to_fit();
    }

    // 清空方块数据
    m_blocks.clear();
    m_blocks.shrink_to_fit();

    m_isLoaded = false;
    m_isVisible = false;
}

void Chunk::is_boundary_face_visible(Chunk* self, BlockFace face)
{
    if (NeighborChunk[face] == nullptr) {
        NeighborChunk[face] = self;
    }
    else {
        std::cout << "NeightChunk is not nullptr\n";
        return;
    }

    // 对之前未处理的边界面进行可见性判断
    switch (face) {
    case RIGHT:
        for (int i = 0; i < DEPTH; i++) {
            for (int j = 0; j < HEIGHT; j++) {
                BlockType block = getBlock(WIDTH - 1, j, i);
                if (block == BLOCK_AIR) continue;
                if (self->getBlock(0, j, i) == BLOCK_AIR) {
                    addFaceToInstanceData(WIDTH - 1, j, i, face, block);
                }
                else {
					//std::cout << "RIGHT face not visible at (" << WIDTH - 1 << ", " << j << ", " << i << ")\n";
                }
            }
        }
        break;
    case LEFT:
        for (int i = 0; i < DEPTH; i++) {
            for (int j = 0; j < HEIGHT; j++) {
                BlockType block = getBlock(0, j, i);
                if (block == BLOCK_AIR) continue;
                if (self->getBlock(WIDTH - 1, j, i) == BLOCK_AIR) {
                    addFaceToInstanceData(0, j, i, face, block);
                }
            }
        }
        break;
    case FRONT:
        for (int i = 0; i < WIDTH; i++) {
            for (int j = 0; j < HEIGHT; j++) {
                BlockType block = getBlock(i, j, DEPTH-1);
                if (block == BLOCK_AIR) continue;
                if (self->getBlock(i, j, 0) == BLOCK_AIR) {
                    addFaceToInstanceData(i, j, DEPTH - 1, face, block);
                }
            }
        }
        break;
    case BACK:
        for (int i = 0; i < WIDTH; i++) {
            for (int j = 0; j < HEIGHT; j++) {
                BlockType block = getBlock(i, j, 0);
                if (block == BLOCK_AIR) continue;
                if (self->getBlock(i, j, DEPTH-1) == BLOCK_AIR) {
                    addFaceToInstanceData(i, j, 0, face, block);
                }
            }
        }
        break;
    }
}

glm::ivec3 Chunk::getWorldPos(int localX, int localY, int localZ) const {
    return glm::ivec3(
        m_position.x * WIDTH + localX,
        localY,  // Y轴是连续的
        m_position.y * DEPTH + localZ
    );
}

glm::vec3 Chunk::getCenter() const {
    return glm::vec3(
        m_position.x * WIDTH + WIDTH / 2.0f,
        HEIGHT / 2.0f,
        m_position.y * DEPTH + DEPTH / 2.0f
    );
}

BlockType Chunk::getBlock(int x, int y, int z) const {
    // 检查边界
    if (x < 0 || x >= WIDTH ||
        y < 0 || y >= HEIGHT ||
        z < 0 || z >= DEPTH) {
        return BLOCK_AIR;
    }

    // 计算索引
    int index = (y * DEPTH + z) * WIDTH + x;
    return m_blocks[index];
}

void Chunk::setBlock(int x, int y, int z, BlockType block)
{
    // 检查边界
    if (x < 0 || x >= WIDTH ||
        y < 0 || y >= HEIGHT ||
        z < 0 || z >= DEPTH) {
        return;
    }
    m_blocks[(y * DEPTH + z)* WIDTH + x] = block;
}

void Chunk::generateTerrain() {
    // 使用地形生成器填充方块
    TerrainGenerator generator;

    // 设置参数
    auto& params = generator.getParams();
    params.seed = 123312312345;


    // 生成地形
    generator.fillChunk(this, m_position);
    //TerrainGenerator::fillChunk(this, m_position);
}

void Chunk::calculateVisibility() {
    // 清空现有实例化数据
    for (auto& pair : m_instanceData) {
        pair.second.clear();
    }
    BlockFace Faces[] = { BlockFace::RIGHT, BlockFace::LEFT, BlockFace::FRONT, BlockFace::BACK, BlockFace::UP, BlockFace::DOWN };

    // 遍历所有方块
    for (int z = 0; z < DEPTH; ++z) {
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                BlockType block = getBlock(x, y, z);

                // 跳过空气方块
                if (block == BLOCK_AIR) continue;

                // 获取方块属性
                BlockProperties props = GetBlockProperties(block);

                // 如果是透明方块，渲染所有面（简化处理）
                if (props.isTransparent) {
                    for (int face = 0; face < 6; ++face) {
                        addFaceToInstanceData(x, y, z, Faces[face], block);
                    }
                }
                // 如果是固体方块，只渲染可见面
                else if (props.isSolid) {
                    for (int face = 0; face < 6; ++face) {
                        if (isFaceVisible(x, y, z, Faces[face])) {
                            addFaceToInstanceData(x, y, z, Faces[face], block);
                        }
                    }
                }
            }
        }
    }
}

bool Chunk::isFaceVisible(int x, int y, int z, BlockFace face) const {
    // 计算相邻方块的世界坐标
    int nx = x, ny = y, nz = z;

    switch (face) {
    case RIGHT: nx++; break; // +X
    case LEFT: nx--; break; // -X
    case UP: ny++; break; // +Y
    case DOWN: ny--; break; // -Y
    case FRONT: nz++; break; // +Z
    case BACK: nz--; break; // -Z
    }

    if (ny > HEIGHT || ny < 0) {
        return true;
    }
    BlockType neighbor;
    //BlockProperties neighborProps = GetBlockProperties(neighbor);
    //return neighborProps.isTransparent;
    if (nx < 0 || nx>15 || nz < 0 || nz>15) {
        Chunk* neighbor_chunk = getNeighborChunk(nx, nz);

        if (neighbor_chunk == nullptr) {
            return false;// 相邻未加载的区块直接设为不可见面
        }
        neighbor = neighbor_chunk->getBlock((nx + WIDTH) % WIDTH, ny, (nz + DEPTH) % DEPTH);
    }
    else {
        neighbor = getBlock(nx, ny, nz);
    }
    return neighbor == BLOCK_AIR;
}

void Chunk::addFaceToInstanceData(int x, int y, int z, BlockFace face, BlockType blockType) {
    glm::mat4 matrix = createFaceMatrix(x, y, z, face);
    m_instanceData[{blockType,face}].push_back(matrix);
	//m_PosToBlockFace[{(uint8_t)x, (uint8_t)y, (uint8_t)z, (uint8_t)face}]++; // TODO 用于以后增删方块查询
}

glm::mat4 Chunk::createFaceMatrix(int x, int y, int z, BlockFace face) const {
    // 基础变换：平移到方块位置
    glm::mat4 matrix = glm::translate(glm::mat4(1.0f),
        glm::vec3(x + m_position.x * WIDTH+0.5, y+0.5, z + m_position.y * DEPTH+0.5));

    // 根据面方向应用旋转
    switch (face) {
    case RIGHT: // +X (右)
        matrix = glm::translate(matrix, glm::vec3(0.5f, 0.0f, 0.0f));
        matrix = glm::rotate(matrix, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        break;
    case LEFT: // -X (左)
        matrix = glm::translate(matrix, glm::vec3(-0.5f, 0.0f, 0.0f));// -0.5
        matrix = glm::rotate(matrix, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
       
        break;
    case UP: // +Y (上)
        matrix = glm::translate(matrix, glm::vec3(0.0f, 0.5f, 0.0f));
        matrix = glm::rotate(matrix, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        
        break;
    case DOWN: // -Y (下)
        matrix = glm::translate(matrix, glm::vec3(0.0f, -0.5f, 0.0f));// -0.5
        matrix = glm::rotate(matrix, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        
        break;
    case FRONT: // +Z (前)
        // 默认朝向就是+Z
        matrix = glm::translate(matrix, glm::vec3(0.0f, 0.0f, 0.5f));
        break;
    case BACK: // -Z (后)
        matrix = glm::translate(matrix, glm::vec3(0.0f, 0.0f, -0.5f));// -0.5
        matrix = glm::rotate(matrix, glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));       
        break;
    }

    return matrix;
}

void Chunk::connectNeighborBlock()
{
    NeighborChunk[0] = m_chunkManager->getChunk(m_position.x + 1, m_position.y);
    NeighborChunk[1] = m_chunkManager->getChunk(m_position.x - 1, m_position.y);
    NeighborChunk[2] = m_chunkManager->getChunk(m_position.x, m_position.y + 1);
    NeighborChunk[3] = m_chunkManager->getChunk(m_position.x, m_position.y - 1);

    // 此面是邻居的视角
    BlockFace neighborFaces[4] = { BlockFace::LEFT, BlockFace::RIGHT, BlockFace::BACK, BlockFace::FRONT };
    for (int i = 0; i < 4;i++) {
        if (NeighborChunk[i] == nullptr)continue;
        NeighborChunk[i]->is_boundary_face_visible(this, neighborFaces[i]);
    }
} 

Chunk* Chunk::getNeighborChunk(int x, int z) const
{
    if (x < 0) {
        return NeighborChunk[1];
    }
    else if (x >= WIDTH) {
        return  NeighborChunk[0];
    }
    else if (z < 0) {
        return  NeighborChunk[3];
    }
    else if(z >= DEPTH) {
        return  NeighborChunk[2];
    }
    return  nullptr;
}



int Chunk::getTotalInstances() const {
    int total = 0;
    for (const auto& pair : m_instanceData) {
        total += pair.second.size();
    }
    return total;
}

void Chunk::printDebugInfo() const {
    std::cout << "=== Chunk Debug Info ===" << std::endl;
    std::cout << "Position: (" << m_position.x << ", " << m_position.y << ")" << std::endl;
    std::cout << "Loaded: " << (m_isLoaded ? "Yes" : "No") << std::endl;
    std::cout << "Visible: " << (m_isVisible ? "Yes" : "No") << std::endl;
    std::cout << "Total Instances: " << getTotalInstances() << std::endl;
    std::cout << "========================" << std::endl;
}

void Chunk::print_m_instanceData() const
{
    std::cout << "Chunk (" << m_position.x << ", " << m_position.y
        << ") loaded. Total instances: " << getTotalInstances() << std::endl;
    for (auto data : m_instanceData) {
		std::cout << "BlockType " << static_cast<int>(data.first.type)<< "BlockFace " 
            << static_cast<int>(data.first.face_id) << " has " << data.second.size() << " instances." << std::endl;
    }
}

bool Chunk::is_can_render(std::shared_ptr<Camera> camera)
{
    if (!camera) return true;

    // 简单的缓存机制：如果相机参数未变，使用缓存结果
    static int frameCount = 0;
    frameCount++;

    // 检查缓存是否有效
    bool cameraMoved = (glm::distance(m_cachedCameraPos, camera->Position) > 1.0f);
    bool fovChanged = (std::abs(m_cachedCameraFOV - camera->FOV) > 0.1f);

    // 如果相机参数变化或超过10帧，重新计算
    if (cameraMoved || fovChanged || (frameCount - m_lastVisibilityFrame > 10)) {
        m_cachedCameraPos = camera->Position;
        m_cachedCameraFOV = camera->FOV;
        m_lastVisibilityFrame = frameCount;

        // 获取视锥体平面
        auto planes = camera->GetFrustumPlanes();       

        // 检查AABB是否在视锥体内
        if (!isAABBInFrustum(m_minPos, m_maxPos, planes)) {
            m_cachedVisibility = false;
            return false;
        }

        // 距离剔除
        glm::vec3 chunkCenter = getCenter();
        float distance = glm::distance(chunkCenter, camera->Position);
        const float MAX_RENDER_DISTANCE = 300.0f;

        m_cachedVisibility = (distance <= MAX_RENDER_DISTANCE);
    }
    return m_cachedVisibility;
}

// 判断AABB是否在视锥体内（使用优化的方法）
bool Chunk::isAABBInFrustum(const glm::vec3& min, const glm::vec3& max,
    const std::array<glm::vec4, 6>& planes) const {
    // 对每个平面进行测试
    for (const auto& plane : planes) {
        // 找到AABB在平面法线方向上的最远点（负方向最远点）
        glm::vec3 p = min;

        if (plane.x >= 0) p.x = max.x;
        if (plane.y >= 0) p.y = max.y;
        if (plane.z >= 0) p.z = max.z;

        // 如果最远点在平面背面，则整个AABB都在平面外
        if (glm::dot(glm::vec3(plane), p) + plane.w < 0) {
            return false;
        }
    }
    return true;
}
#pragma once
#include "../core.h"
#include "BlockType.h"
#include "ChunkManager.h"
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <memory>
#include "../Camera.h"
// 区块类 - 管理16x64x16区域的方块
class Chunk {
public:
    // 使用常量
    static constexpr int WIDTH = ChunkConstants::CHUNK_WIDTH;
    static constexpr int HEIGHT = ChunkConstants::CHUNK_HEIGHT;
    static constexpr int DEPTH = ChunkConstants::CHUNK_DEPTH;
    static constexpr int VOLUME = ChunkConstants::CHUNK_VOLUME;

    // 构造函数/析构函数
    Chunk(const glm::ivec2& position, ChunkManager* chunkManager);  // position是区块的XZ坐标（Y轴是连续的）
    ~Chunk();

    // 加载区块：生成地形并计算可见性
    void load();
    void unload();
    void is_boundary_face_visible(Chunk* self, BlockFace face);


    // 获取区块状态
    bool isLoaded() const { return m_isLoaded; }
    bool isVisible() const { return m_isVisible; }
    void setVisible(bool visible) { m_isVisible = visible; }

    // 获取区块位置
    glm::ivec2 getPosition() const { return m_position; }
    glm::ivec3 getWorldPos(int localX, int localY, int localZ) const;
    glm::vec3 getCenter() const;

    // 获取方块
    BlockType getBlock(int x, int y, int z) const;

    // 设置方块
    void setBlock(int x, int y, int z, BlockType block);

    // 获取实例化数据
    const std::unordered_map<BlockFaceType, std::vector<glm::mat4>>& getInstanceData() const {
        return m_instanceData;
    }

    // 获取渲染统计
    int getTotalInstances() const;
    void printDebugInfo() const;
    void print_m_instanceData()const;

    // 摄像机视锥剔除
	bool is_can_render(std::shared_ptr<Camera> camera);
    // 判断点是否在平面正面（可见）
    bool isPointInFrontOfPlane(const glm::vec3& point, const glm::vec4& plane) {
        return glm::dot(glm::vec3(plane), point) + plane.w > 0;
    }
    bool isAABBInFrustum(const glm::vec3& min, const glm::vec3& max,
        const std::array<glm::vec4, 6>& planes) const;

private:
	ChunkManager* m_chunkManager;  // 指向区块管理器的指针
    // 方块数据
    std::vector<BlockType> m_blocks;

    // 区块位置（只有XZ坐标，Y轴是连续的）
    glm::ivec2 m_position;  // 区块坐标，不是世界坐标
    glm::vec3 m_minPos;
    glm::vec3 m_maxPos;

    // 实例化数据：每种方块类型对应一个矩阵列表
    std::unordered_map<BlockFaceType, std::vector<glm::mat4>> m_instanceData;
    //std::unordered_map<BlockFaceKey, int> m_PosToBlockFace;

    // 状态标志
    bool m_isLoaded;
    bool m_isVisible;
    std::vector<bool>m_boundary_face_isLoaded = { false }; // 标记边界面是否进行了可见性判断 0:+X  1:-X  2:+Z  3:-Z

	// 可见性缓存
    mutable bool m_cachedVisibility = true;
    mutable int m_lastVisibilityFrame = -1;
    glm::vec3 m_cachedCameraPos;
    float m_cachedCameraFOV;

    // 私有方法
    void generateTerrain();
    void calculateVisibility();

    // 可见性检查
    bool isFaceVisible(int x, int y, int z, BlockFace face) const;
    void addFaceToInstanceData(int x, int y, int z, BlockFace face, BlockType blockType);

    // 生成面矩阵
    glm::mat4 createFaceMatrix(int x, int y, int z, BlockFace face) const;

    // 连接相邻区块区块
    void connectNeighborBlock();

    // 获取相邻区块
    Chunk* getNeighborChunk(int nx,int nz)const ;

    std::vector<Chunk*> NeighborChunk = std::vector<Chunk*>(4, nullptr);// 0:+X  1:-X  2:+Z  3:-Z

    friend class ChunkManager;  // 允许ChunkManager访问私有方法
};
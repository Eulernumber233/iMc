#pragma once
#ifndef _CORE_H_
#define _CORE_H_



#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/types.h>
#include "stb_image.h"

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <unordered_map>
#include <memory>

// 注意：ChunkConstants 已迁移到 scr/chunk/ChunkDimensions.h，
// 减少 chunk 几何参数变更时的编译影响面。
// 真正用到 chunk 尺寸的文件请显式 #include "../chunk/ChunkDimensions.h"。

namespace RenderConstants {
    constexpr int MAX_INSTANCES = 1000000;
    constexpr float BLOCK_SIZE = 1.0f;
}

namespace WorldConstants {
    constexpr unsigned int WORLD_SEED = 114514;
    // 渲染半径：默认值。运行时由 config（启动参数 / config 文件）覆盖
    constexpr unsigned int RENDER_RADIUS = 8;
}


#endif
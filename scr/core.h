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

namespace RenderConstants {
    constexpr int MAX_INSTANCES = 20; // 弃用
    constexpr float BLOCK_SIZE = 1.0f;// 弃用
}

namespace WorldConstants {
    constexpr unsigned int WORLD_SEED = 114514;
    // 渲染半径：默认值。运行时由 config（启动参数 / config 文件）覆盖
    constexpr unsigned int RENDER_RADIUS = 8;
}

#endif
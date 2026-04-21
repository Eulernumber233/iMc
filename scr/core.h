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

namespace ChunkConstants {
    constexpr int CHUNK_WIDTH = 16;
    constexpr int CHUNK_HEIGHT = 64;
    constexpr int CHUNK_DEPTH = 16;
    constexpr int CHUNK_VOLUME = CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH;
}

namespace RenderConstants {
    constexpr int MAX_INSTANCES = 1000000;
    constexpr float BLOCK_SIZE = 1.0f;
}

namespace WorldConstants {
    constexpr unsigned int WORLD_SEED = 114514;
}


#endif
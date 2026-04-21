#pragma once

#include "core.h"
struct Vertex {
    glm::vec3 Position;
    glm::ivec1 faceIndex;
    glm::vec2 TexCoords;
    glm::vec3 Normal;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    int count;
    Vertex() {

    }
    Vertex(const glm::vec3& pos, unsigned int faceIndex_, const glm::vec3& tex, const glm::vec3& nor)
        : Position(pos), faceIndex(faceIndex_), TexCoords(tex), Normal(nor) {
    }
    Vertex(float x, float y, float z, int index, float u, float v, float nx, float ny, float nz) {
        Position = glm::vec3(x, y, z);
        faceIndex = glm::ivec1(index);
        TexCoords = glm::vec2(u, v);
        Normal = glm::vec3(nx, ny, nz);
    }

    Vertex(float x, float y, float z, float index, float u, float v, float nx, float ny, float nz) {
        Position = glm::vec3(x, y, z);
        faceIndex = glm::ivec1((int)index);
        TexCoords = glm::vec2(u, v);
        Normal = glm::vec3(nx, ny, nz);
    }
};


const std::vector<Vertex> vertices = {
    { -0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f },
    {  0.5f, -0.5f,  0.5f, 0.0f, 1.0f, 1.0f,  0.0f, 0.0f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f },
    { -0.5f,  0.5f,  0.5f, 0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f },

    {  0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 1.0f,  0.0f, 0.0f, -1.0f },
    { -0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f,  0.0f, 0.0f, -1.0f },
    { -0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 0.0f,  0.0f, 0.0f, -1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f },

    { -0.5f, -0.5f, -0.5f, 2.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f },
    { -0.5f, -0.5f,  0.5f, 2.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f },
    { -0.5f,  0.5f,  0.5f, 2.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f },
    { -0.5f,  0.5f, -0.5f, 2.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f },

    {  0.5f, -0.5f,  0.5f, 3.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f },
    {  0.5f, -0.5f, -0.5f, 3.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f },
    {  0.5f,  0.5f, -0.5f, 3.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f },
    {  0.5f,  0.5f,  0.5f, 3.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },

    { -0.5f,  0.5f,  0.5f, 4.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f },
    {  0.5f,  0.5f,  0.5f, 4.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f },
    {  0.5f,  0.5f, -0.5f, 4.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f },
    { -0.5f,  0.5f, -0.5f, 4.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f },

    { -0.5f, -0.5f, -0.5f, 5.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f },
    {  0.5f, -0.5f, -0.5f, 5.0f, 1.0f, 1.0f, 0.0f, -1.0f, 0.0f },
    {  0.5f, -0.5f,  0.5f, 5.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f },
    { -0.5f, -0.5f,  0.5f, 5.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f }
};

const std::vector<unsigned int> indices = {
    0, 1, 2,
    0, 2, 3,

    4, 5, 6,
    4, 6, 7,

    8, 9, 10,
    8, 10, 11,

    12, 13, 14,
    12, 14, 15,

    16, 17, 18,
    16, 18, 19,

    20, 21, 22,
    20, 22, 23
};

//const std::vector<glm::vec3> birch_logs_pos_vec = {
//    {1.0 ,0.0 ,1.0},
//    {3.0 ,0.0 ,1.0},
//    {5.0 ,0.0 ,1.0},
//    {7.0 ,0.0 ,1.0},
//    {1.0 ,0.0 ,3.0},
//    {3.0 ,0.0 ,3.0},
//    {5.0 ,0.0 ,3.0},
//    {7.0 ,0.0 ,3.0},
//    {1.0 ,0.0 ,5.0},
//    {3.0 ,0.0 ,5.0},
//    {5.0 ,0.0 ,5.0},
//    {7.0 ,0.0 ,5.0},
//    {1.0 ,0.0 ,7.0},
//    {3.0 ,0.0 ,7.0},
//    {5.0 ,0.0 ,7.0},
//    {7.0 ,0.0 ,7.0},
//    {2.0 ,0.0 ,2.0},
//    {4.0 ,0.0 ,2.0},
//    {6.0 ,0.0 ,2.0},
//    {8.0 ,0.0 ,2.0},
//    {2.0 ,0.0 ,4.0},
//    {4.0 ,0.0 ,4.0},
//    {6.0 ,0.0 ,4.0},
//    {8.0 ,0.0 ,4.0},
//    {2.0 ,0.0 ,6.0},
//    {4.0 ,0.0 ,6.0},
//    {6.0 ,0.0 ,6.0},
//    {8.0 ,0.0 ,6.0},
//    {2.0 ,0.0 ,8.0},
//    {4.0 ,0.0 ,8.0},
//    {6.0 ,0.0 ,8.0},
//    {8.0 ,0.0 ,8.0},
//};

const std::vector<glm::vec3> grass_block_logs_pos_vec = {
    {1.0 ,-1.0 ,1.0},
    {3.0 ,-1.0 ,1.0},
    {5.0 ,-1.0 ,1.0},
    {7.0 ,-1.0 ,1.0},
    {1.0 ,-1.0 ,3.0},
    {3.0 ,-1.0 ,3.0},
    {5.0 ,-1.0 ,3.0},
    {7.0 ,-1.0 ,3.0},
    {1.0 ,-1.0 ,5.0},
    {3.0 ,-1.0 ,5.0},
    {5.0 ,-1.0 ,5.0},
    {7.0 ,-1.0 ,5.0},
    {1.0 ,-1.0 ,7.0},
    {3.0 ,-1.0 ,7.0},
    {5.0 ,-1.0 ,7.0},
    {7.0 ,-1.0 ,7.0},
};

const float skyboxVertices[] = {
    // positions          
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

    -1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f,

    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f
};

const std::vector<std::string> faces
{
    "resources/textures/skybox/right.png",
    "resources/textures/skybox/left.png",
    "resources/textures/skybox/top.png",
    "resources/textures/skybox/bottom.png",
    "resources/textures/skybox/front.png",
    "resources/textures/skybox/back.png",
};


const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 900;

const unsigned int SHADOW_WIDTH = 4096;
const unsigned int SHADOW_HEIGHT = 4096;

const float MAX_SHADOW_DISTANCE = 300.0f;
#version 430 core


const int FACEFRONT = 2;
const int FACEBACK = 3;
const int FACERIGHT = 0;
const int FACELeft = 1;
const int FACEUP = 4;
const int FACEDOWN = 5;

const int BLOCK_AIR = 0;
const int BLOCK_STONE = 1;
const int BLOCK_DIRT = 2;
const int BLOCK_GRASS = 3;
const int BLOCK_WATER = 4;
const int BLOCK_SAND = 5;
const int BLOCK_WOOD = 6;
const int BLOCK_LEAVES = 7;

// 输入
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

//out vec4 FragColor;
// G-Buffer输出
layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedo;
layout(location = 3) out vec4 gProperties;

uniform sampler2D diffuse; 
uniform int BlockType; // 方块类型（0-255）
uniform int BlockFace; 
uniform vec3 textureParam; // 贴图变换参数

// 草方块颜色
//uniform vec3 u_GrassDarkColor;   // 深绿色：比如vec3(0.2, 0.6, 0.15)
//uniform vec3 u_GrassMidColor;    // 中绿色：比如vec3(0.35, 0.75, 0.25)
//uniform vec3 u_GrassLightColor;  // 浅绿色：比如vec3(0.45, 0.8, 0.35)

void main() {
    // 位置 + 深度
    gPosition = vec4(vWorldPos, gl_FragCoord.z);
    
    // 法线
    gNormal = vec4(normalize(vNormal), 0.0);
    
    // 反照率颜色
    gAlbedo = texture(diffuse, vTexCoord);
    if(BlockType == BLOCK_GRASS && BlockFace ==  FACEUP){
        gAlbedo = vec4(gAlbedo.rgb*textureParam/255.0, gAlbedo.a);
    }

    // 属性：方块类型（0-255）、自发光强度、粗糙度、金属度
    gProperties = vec4(
        float(BlockType) / 255.0,  // 方块类型（归一化到0-1）
        0.0,                        // 自发光强度（大部分方块不发光）
        0.8,                        // 粗糙度（大部分方块是粗糙的）
        0.0                         // 金属度（方块不是金属）
    );
    //FragColor = gAlbedo;
    //FragColor = vec4(0.5,0.1,0.3,1.0);
}
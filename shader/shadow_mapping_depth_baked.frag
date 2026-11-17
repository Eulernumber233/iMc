#version 460 core

// 烘焙路径的阴影深度片段着色器。
// 烘焙网格在 CPU 侧已经排除了 BLOCK_ERRER 占位面，这里无需 discard，
// 深度完全由硬件写入深度附件。
void main() {
}

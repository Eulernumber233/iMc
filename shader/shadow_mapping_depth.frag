#version 330 core

// 阶段 2：阴影贴图改为普通深度纹理附件，深度由硬件写入，frag 几乎为空。
// 仅丢弃 BLOCK_ERRER 占位面（复用/释放的面 slot 留在原地，不参与阴影）。
flat in int vBlockType;

const int BLOCK_ERRER = 255;

void main() {
    if (vBlockType == BLOCK_ERRER) discard;
    // 深度由硬件写入深度附件，无颜色输出。
}

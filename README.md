# iMc - OpenGL Minecraft-inspired Project

一个基于OpenGL的Minecraft风格图形应用程序，包含地形生成、区块管理、物理碰撞和渲染系统。

## 项目特性

- 基于OpenGL 3.3核心模式的现代图形渲染
- 动态地形生成和区块管理
- 实时物理碰撞检测
- 多线程任务系统
- 用户界面系统
- 延迟渲染和SSAO效果

## 构建指南

本项目使用CMake作为构建系统。详细构建步骤请参考 [CMake构建指南](README_CMAKE.md)。

### 快速开始

1. 克隆仓库：
   ```bash
   git clone <仓库地址>
   cd iMc
   ```

2. 使用CMake生成Visual Studio 2022解决方案：
   ```bash
   mkdir build
   cd build
   cmake -G "Visual Studio 17 2022" ..
   ```

3. 构建项目：
   ```bash
   cmake --build . --config Debug
   ```

4. 运行程序：
   - 可执行文件位于 `build/Debug/iMc.exe`

### 详细构建指南

请参考 [CMake构建指南](README_CMAKE.md) 获取详细的构建步骤和依赖项管理信息。

### 依赖项

- GLFW 3.x
- GLEW 2.x
- OpenGL 3.3+
- GLM
- Assimp
- jsoncpp

推荐使用 [vcpkg](https://github.com/Microsoft/vcpkg) 管理依赖项。

## 项目结构

```
iMc/
├── CMakeLists.txt          # CMake构建配置
├── README.md              # 项目说明（本文档）
├── README_CMAKE.md       # 详细构建指南
├── scr/                  # 源代码
│   ├── chunk/           # 区块管理
│   ├── collision/       # 碰撞检测
│   ├── generate/        # 地形生成
│   ├── mode/           # 模型加载
│   ├── physics/        # 物理系统
│   ├── player/         # 玩家控制
│   ├── render/         # 渲染系统
│   ├── task/          # 任务系统
│   └── UI/            # 用户界面
├── shader/             # 着色器文件
└── assert/             # 资源文件（纹理等）
```

## 开发环境

- **操作系统**: Windows 10/11
- **编译器**: Visual Studio 2022 (MSVC v143)
- **C++标准**: C++17
- **图形API**: OpenGL 3.3+

## 许可证

[在此添加许可证信息]

## 贡献

欢迎提交Issue和Pull Request。请确保代码符合项目编码规范并通过构建测试。
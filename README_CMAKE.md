# iMc 项目构建指南

本文档提供使用 CMake 构建 iMc 项目的详细步骤。项目是一个基于 OpenGL 的图形应用程序，使用了 GLFW、GLEW、GLM、Assimp 等库。

## 环境要求

- **操作系统**: Windows 10/11（64位）
- **编译器**: Visual Studio 2022（MSVC v143）
- **CMake**: 版本 3.15 或更高
- **Git**: 用于克隆仓库

## 快速开始

### 1. 克隆仓库
```bash
git clone <仓库地址>
cd iMc
```

### 2. 配置CMake

#### 方法一：使用CMake GUI
1. 打开CMake GUI
2. 设置源代码路径为 `iMc` 目录
3. 设置构建路径（例如 `iMc/build`）
4. 点击 "Configure"，选择 "Visual Studio 17 2022" 作为生成器
5. 点击 "Generate"

#### 方法二：使用命令行
```bash
# 清理旧的构建文件（如果存在）
if exist build rmdir /s /q build

# 创建构建目录
mkdir build
cd build

# 生成 Visual Studio 2022 解决方案
cmake -G "Visual Studio 17 2022" ..
```

### 3. 构建项目

#### 在 Visual Studio 中构建
1. 打开生成的 `iMc.sln` 文件
2. 选择配置（Debug 或 Release）和平台（x64）
3. 点击 "生成" -> "生成解决方案"

#### 使用命令行构建
```bash
# 在 build 目录中
cmake --build . --config Debug
# 或
cmake --build . --config Release
```

### 4. 运行程序
构建完成后，可执行文件将位于：
- `build/Debug/iMc.exe`（Debug 配置）
- `build/Release/iMc.exe`（Release 配置）

资源文件（着色器和纹理）会自动复制到可执行文件所在目录。

## 依赖项管理

项目依赖以下库：
- **GLFW3**: 窗口和输入管理
- **GLEW**: OpenGL 扩展加载
- **OpenGL**: 图形 API
- **GLM**: 数学库
- **Assimp**: 模型加载
- **jsoncpp**: JSON 解析（用于纹理配置）

### 安装依赖项

#### 选项一：使用 CMake FetchContent（自动下载）
如果未找到系统安装的依赖库，CMake 可以自动下载并构建它们（默认启用）。只需正常运行 CMake 配置即可：

```bash
cmake -G "Visual Studio 17 2022" ..
```

CMake 会自动下载以下库的源代码并构建：
- GLFW 3.3.8
- GLEW 2.2.0
- GLM 0.9.9.8
- Assimp 5.3.1
- jsoncpp 1.9.5

注意：首次构建需要下载依赖库，可能需要较长时间。如果需要禁用此功能，可以设置 `-DUSE_FETCHCONTENT=OFF`。

#### 选项二：使用 vcpkg（推荐）
1. 安装 vcpkg（如果尚未安装）:
   ```bash
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   ```

2. 安装所需库:
   ```bash
   vcpkg install glfw3 glew glm assimp jsoncpp
   ```

3. 在 CMake 配置时指定 vcpkg 工具链:
   ```bash
   cmake -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=[vcpkg根目录]/scripts/buildsystems/vcpkg.cmake ..
   ```

#### 选项三：手动安装
1. 下载并安装以下库：
   - [GLFW](https://www.glfw.org/download.html)
   - [GLEW](http://glew.sourceforge.net/)
   - [GLM](https://github.com/g-truc/glm)
   - [Assimp](https://github.com/assimp/assimp)
   - [jsoncpp](https://github.com/open-source-parsers/jsoncpp)

2. 将库文件和头文件放置在系统路径中，或通过 CMake 变量指定路径。

## 项目结构

```
iMc/
├── CMakeLists.txt          # CMake 构建配置
├── README_CMAKE.md        # 构建指南（本文档）
├── scr/                   # 源代码
│   ├── chunk/            # 区块相关
│   ├── collision/        # 碰撞检测
│   ├── generate/         # 地形生成
│   ├── mode/             # 模型加载
│   ├── physics/          # 物理系统
│   ├── player/           # 玩家控制
│   ├── render/           # 渲染系统
│   ├── task/             # 任务系统
│   ├── UI/               # 用户界面
│   └── ...               # 其他源文件
├── shader/               # 着色器文件
│   ├── *.vert           # 顶点着色器
│   └── *.frag           # 片段着色器
└── assert/               # 资源文件
    └── textures/         # 纹理图片
```

## 构建配置

### 编译器设置
- **C++ 标准**: C++17
- **编译器**: MSVC (Visual Studio 2022)
- **平台**: x64

### 预处理器定义
- **Debug 模式**: `WIN32`, `_DEBUG`, `_CONSOLE`
- **Release 模式**: `WIN32`, `NDEBUG`, `_CONSOLE`

## 常见问题

### 1. CMake 找不到依赖库
- 确保已安装所有依赖项
- 如果使用 vcpkg，请正确设置 `CMAKE_TOOLCHAIN_FILE`
- 可以手动设置库路径：
  ```bash
  cmake -G "Visual Studio 17 2022" -DGLFW3_ROOT="C:/path/to/glfw" ..
  ```

### 2. 链接错误
- 检查库文件路径是否正确
- 确保库版本与编译器兼容
- Debug 和 Release 配置需要使用对应版本的库

### 3. 运行时缺少 DLL
- 确保 `glew32.dll`、`glfw3.dll`、`assimp-vc143-mt(d).dll` 等文件在可执行文件目录中
- 这些文件在构建时会从 `bin/debug/` 或 `bin/release/` 目录自动复制

### 4. 资源文件未找到
- 资源文件会在构建后自动复制到输出目录
- 如果手动运行程序，请确保在可执行文件同一目录下存在 `shader/` 和 `assert/` 文件夹

## 高级配置

### 自定义构建选项
可以在 CMake 配置时设置以下选项：
```bash
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Debug -DUSE_VCPKG=ON ..
```

### 生成其他构建系统
CMake 支持多种生成器：
```bash
# Visual Studio 2019
cmake -G "Visual Studio 16 2019" ..

# Ninja（更快的构建）
cmake -G "Ninja" ..
```

## 贡献指南
1. 确保代码符合 C++17 标准
2. 使用一致的代码风格
3. 添加新功能时更新 CMake 配置
4. 测试 Debug 和 Release 构建

## 许可证
[在此添加项目许可证信息]

---

如有问题，请参考项目原始文档或联系维护者。
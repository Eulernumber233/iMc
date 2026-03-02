cmake_minimum_required(VERSION 3.15)
project(iMc VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置Windows平台相关选项
if(WIN32)
    add_definitions(-DWIN32 -D_WIN32)
    if(MSVC)
        # 禁用某些警告
        add_compile_options(/W4 /WX)
        # 设置调试和发布配置
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# 根据配置设置预处理器定义
set(DEBUG_DEFINITIONS _DEBUG _CONSOLE)
set(RELEASE_DEFINITIONS NDEBUG _CONSOLE)

# 查找依赖包
find_package(OpenGL REQUIRED)

# 选项：使用FetchContent下载缺失的依赖
option(USE_FETCHCONTENT "Use FetchContent to download missing dependencies" ON)

# 包含FetchContent模块
include(FetchContent)

# GLFW
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND AND USE_FETCHCONTENT)
    message(STATUS "GLFW3 not found, downloading via FetchContent")
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.8
    )
    FetchContent_MakeAvailable(glfw)
    set(glfw3_FOUND TRUE)
endif()
if(NOT glfw3_FOUND)
    message(FATAL_ERROR "GLFW3 not found and USE_FETCHCONTENT is OFF")
endif()

# GLEW
find_package(GLEW QUIET)
if(NOT GLEW_FOUND AND USE_FETCHCONTENT)
    message(STATUS "GLEW not found, downloading via FetchContent")
    FetchContent_Declare(
        glew
        GIT_REPOSITORY https://github.com/Perlmint/glew-cmake.git
        GIT_TAG glew-cmake-2.2.0
    )
    FetchContent_MakeAvailable(glew)
    set(GLEW_FOUND TRUE)
endif()
if(NOT GLEW_FOUND)
    message(FATAL_ERROR "GLEW not found and USE_FETCHCONTENT is OFF")
endif()

# Assimp
find_package(assimp QUIET)
if(NOT assimp_FOUND AND USE_FETCHCONTENT)
    message(STATUS "Assimp not found, downloading via FetchContent")
    FetchContent_Declare(
        assimp
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG v5.3.1
    )
    FetchContent_MakeAvailable(assimp)
    set(assimp_FOUND TRUE)
endif()
if(NOT assimp_FOUND)
    message(FATAL_ERROR "Assimp not found and USE_FETCHCONTENT is OFF")
endif()

# GLM (header-only)
find_package(glm QUIET)
if(NOT glm_FOUND AND USE_FETCHCONTENT)
    message(STATUS "GLM not found, downloading via FetchContent")
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 0.9.9.8
    )
    FetchContent_MakeAvailable(glm)
    set(glm_FOUND TRUE)
endif()
if(NOT glm_FOUND)
    message(FATAL_ERROR "GLM not found and USE_FETCHCONTENT is OFF")
endif()

# jsoncpp
find_package(jsoncpp QUIET)
if(NOT jsoncpp_FOUND AND USE_FETCHCONTENT)
    message(STATUS "jsoncpp not found, downloading via FetchContent")
    FetchContent_Declare(
        jsoncpp
        GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
        GIT_TAG 1.9.5
    )
    FetchContent_MakeAvailable(jsoncpp)
    set(jsoncpp_FOUND TRUE)
endif()
if(NOT jsoncpp_FOUND)
    message(WARNING "jsoncpp not found, building without json support")
    set(JSONCPP_FOUND FALSE)
else()
    set(JSONCPP_FOUND TRUE)
endif()

# 收集所有源文件
file(GLOB_RECURSE SOURCES "scr/*.cpp")
# 排除不需要的文件（如果有）
# list(FILTER SOURCES EXCLUDE REGEX ".*_test\\.cpp$")

# 收集所有头文件目录
file(GLOB_RECURSE HEADERS "scr/*.h")
if(HEADERS)
    list(REMOVE_DUPLICATES HEADERS)
endif()

# 创建可执行文件
add_executable(iMc ${SOURCES} ${HEADERS})

# 设置目标属性
target_compile_features(iMc PRIVATE cxx_std_17)

# 包含目录
target_include_directories(iMc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/scr
    ${CMAKE_CURRENT_SOURCE_DIR}/scr/chunk
    ${CMAKE_CURRENT_SOURCE_DIR}/scr/collision
    ${CMAKE_CURRENT_SOURCE_DIR}/scr/generate
    ${CMAKE_CURRENT_SOURCE_DIR}/scr/mode
    ${CMAKE_CURRENT_SOURCE_DIR}/scr/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/scr/player
    ${CMAKE_CURRENT_SOURCE_DIR}/scr/render
    ${CMAKE_CURRENT_SOURCE_DIR}/scr/task
    ${CMAKE_CURRENT_SOURCE_DIR}/scr/UI
)

# 链接库
target_link_libraries(iMc PRIVATE
    OpenGL::GL
    glfw
    GLEW::GLEW
    assimp::assimp
    glm::glm
)

if(JSONCPP_FOUND)
    target_link_libraries(iMc PRIVATE jsoncpp)
endif()

# Windows特定库
if(WIN32)
    target_link_libraries(iMc PRIVATE opengl32)
endif()

# 复制资源文件（着色器和纹理）
# 创建自定义目标来复制资源
add_custom_target(copy_resources ALL
    COMMENT "Copying resource files to output directory"
)

# 复制着色器文件
file(GLOB_RECURSE SHADER_FILES "shader/*")
foreach(shader_file ${SHADER_FILES})
    # 计算相对路径
    file(RELATIVE_PATH rel_path "${CMAKE_CURRENT_SOURCE_DIR}/shader" "${shader_file}")
    get_filename_component(rel_dir "${rel_path}" DIRECTORY)
    add_custom_command(TARGET copy_resources POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:iMc>/shader/${rel_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${shader_file}"
        "$<TARGET_FILE_DIR:iMc>/shader/${rel_path}"
    )
endforeach()

# 复制纹理资源文件
file(GLOB_RECURSE ASSET_FILES "assert/*")
foreach(asset_file ${ASSET_FILES})
    # 计算相对路径
    file(RELATIVE_PATH rel_path "${CMAKE_CURRENT_SOURCE_DIR}/assert" "${asset_file}")
    get_filename_component(rel_dir "${rel_path}" DIRECTORY)
    add_custom_command(TARGET copy_resources POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:iMc>/assert/${rel_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${asset_file}"
        "$<TARGET_FILE_DIR:iMc>/assert/${rel_path}"
    )
endforeach()

# 复制debug版本的配置文件
add_custom_command(TARGET copy_resources POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/PropertySheet_debug.props"
    "$<TARGET_FILE_DIR:iMc>/PropertySheet_debug.props"
)

# 设置调试和发布的预处理器定义
target_compile_definitions(iMc PRIVATE $<$<CONFIG:Debug>:${DEBUG_DEFINITIONS}>)
target_compile_definitions(iMc PRIVATE $<$<NOT:$<CONFIG:Debug>>:${RELEASE_DEFINITIONS}>)

# 安装目标（可选）
install(TARGETS iMc RUNTIME DESTINATION bin)
install(DIRECTORY shader DESTINATION .)
install(DIRECTORY assert DESTINATION .)
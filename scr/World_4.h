#pragma once
#include "World.h"
#include "generate/TerrainGenerator.h"
#include "render/RenderSystem.h"
#include "chunk/ChunkManager.h"
#include <thread>
class World_4 :public World
{
public:
    World_4(std::shared_ptr<Camera> camera_, GLFWwindow* window_, unsigned int seed = 42)
        :World(camera_, window_),_seed(seed) {    

    }

private:
    unsigned int _seed = 42;

    // 交互相关成员
    std::unique_ptr<BlockOutlineRenderer> m_outlineRenderer;
    Ray::HitResult m_lastHitResult;
    bool m_hasSelectedBlock = false;
    glm::ivec3 m_selectedBlockPos;
    // 交互距离
    float m_interactionDistance = 8.0f;

public:

    int run() {
        TextureMgr::GetInstance();
        BlockFaceType::init_type_map();

		camera->Position = glm::vec3(0.0f, 60.0f, 0.0f);
        // 创建渲染系统
        RenderSystem renderSystem(SCR_WIDTH, SCR_HEIGHT);
        if (!renderSystem.initialize()) {
            std::cerr << "Failed to initialize render system" << std::endl;
            return -1;
        }

        // 设置光照参数
        renderSystem.setLightDirection(glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)));
        renderSystem.setLightColor(glm::vec3(1.0f, 0.95f, 0.9f));
        renderSystem.setLightIntensity(1.0f);
        renderSystem.setAmbientColor(glm::vec3(0.1f, 0.1f, 0.1f));

        // 创建区块管理器
        ChunkManager chunkManager;
        chunkManager.initialize(4,camera->Position); // 渲染半径8个区块
		chunkManager.printStats();

        // 设置地形生成器种子
        //TerrainGenerator::setSeed(12345);

        std::thread  updateThread([&chunkManager, this]() {
            while (!glfwWindowShouldClose(window)) {
                //chunkManager.update(camera->Position);
                std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 每50ms更新一次
            }
			});
		//updateThread.join();

        while (!glfwWindowShouldClose(window))
        {
            static float deltaTime = 0.0f;
            static float lastFrame = 0.0f;
            float currentFrame = static_cast<float>(glfwGetTime());
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;
            showFPS(window);
            readKey_toMove(window, deltaTime);

            chunkManager.update(camera);// TODO

            // 创建视图和投影矩阵
            glm::mat4 view = camera->GetViewMatrix();
            glm::mat4 projection = glm::perspective(glm::radians(45.0f),
                (float)SCR_WIDTH / (float)SCR_HEIGHT,
                0.1f, 1000.0f);

            // 更新选中方块
            updateBlockSelection(chunkManager, renderSystem);

            // 渲染帧
            renderSystem.render(chunkManager, view, projection, camera);



            glfwSwapBuffers(window);
            glfwPollEvents();

        }
        updateThread.detach();

        glfwTerminate();
        return 0;
    }
    // 更新方块选择
    void updateBlockSelection(ChunkManager& chunkManager, RenderSystem& renderSystem) {
        // 从屏幕中心发射射线
        std::shared_ptr<Ray> ray = camera->GetViewRay();
        // 进行射线检测
        Ray::HitResult hit = ray->cast(&chunkManager, m_interactionDistance);
		
        static glm::ivec3 lastSelectedBlockPos = glm::ivec3(-99999);
        if (hit.hit) {
            m_hasSelectedBlock = true;
            m_selectedBlockPos = hit.blockPos;
            m_lastHitResult = hit;

            // 通知渲染系统渲染边框
            renderSystem.setSelectedBlock(m_selectedBlockPos);
            
            if (lastSelectedBlockPos != m_selectedBlockPos) {
				lastSelectedBlockPos = m_selectedBlockPos;
                std::cout << "hit block at: " << hit.blockPos.x << ", " 
                    << hit.blockPos.y << ", " << hit.blockPos.z << std::endl;
            }
        }
        else {
            m_hasSelectedBlock = false;
            renderSystem.clearSelectedBlock();
        }
    }
};


#pragma once
#include "../core.h"
#include "../Shader.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <functional>

// UI组件基类
class UIComponent {
public:
    UIComponent(const std::string& id);
    virtual ~UIComponent();

    // 通用属性
    std::string id;
    glm::vec2 position = glm::vec2(0.0f);
    glm::vec2 size = glm::vec2(100.0f, 50.0f);
    glm::vec4 color = glm::vec4(1.0f);
    float alpha = 1.0f;
    bool visible = true;
    bool interactive = false;
    int zIndex = 0; // 渲染顺序

    // 变换
    glm::mat4 transform = glm::mat4(1.0f);
    float rotation = 0.0f;

    // 锚点 (0-1)
    glm::vec2 anchor = glm::vec2(0.0f);

    // 更新和渲染
    virtual void update(float deltaTime) {}
    virtual void render(const glm::mat4& projection) = 0;

    // 交互 
    virtual bool containsPoint(const glm::vec2& point) const;
    virtual void onClick() {}
    virtual void onHoverEnter() {}
    virtual void onHoverLeave() {}

    // 变换辅助函数
    void setPosition(float x, float y);
    void setSize(float width, float height);
    void setColor(float r, float g, float b, float a = 1.0f);
    void setRotation(float degrees);

protected:
    glm::mat4 calculateTransform() const;
    bool isHovered = false;
};

// 矩形组件
class UIRect : public UIComponent {
public:
    UIRect(const std::string& id);
    ~UIRect();

    // 矩形特定属性
    float borderRadius = 0.0f;
    bool filled = true;
    float borderWidth = 1.0f;
    glm::vec4 borderColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    void render(const glm::mat4& projection) override;
    
    GLuint VAO, VBO, EBO;

    static void initGeometry();
    static GLuint s_VAO, s_VBO, s_EBO;
    static bool s_geometryInitialized;
};

// 图片组件
class UIImage : public UIComponent {
public:
    UIImage(const std::string& id);
    ~UIImage();

    // 图片特定属性
    GLuint textureID = 0;
    glm::vec4 textureRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f); // u, v, width, height
    bool preserveAspectRatio = false;

    void render(const glm::mat4& projection) override;
    bool containsPoint(const glm::vec2& point) const override;

    bool loadTextureByFilePath(const std::string& filepath);
    bool loadTextureByGLid(GLuint texID);
	bool loadTextureByTextureName(const std::string& texture_name);

private:
    GLuint VAO, VBO, EBO;
    static void initGeometry();
    static GLuint s_VAO, s_VBO, s_EBO;
    static bool s_geometryInitialized;
};

// 文本组件
class UIText : public UIComponent {
public:
    UIText(const std::string& id);
    ~UIText();

    // 文本特定属性
    std::string text;
    std::string fontName = "default";
    float fontSize = 16.0f;
    glm::vec4 textColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    //TextAlignment alignment = TextAlignment::LEFT;

    void setText(const std::string& text);
    void render(const glm::mat4& projection) override;

    // 字体管理
    struct FontInfo {
        GLuint textureID;
        std::unordered_map<char, glm::vec4> charUVs;
        glm::ivec2 charSize;
    };

    static bool loadFont(const std::string& name, const std::string& texturePath,
        const std::string& configPath);

private:
    GLuint VAO, VBO, EBO;
    static void initGeometry();
    static GLuint s_VAO, s_VBO, s_EBO;
    static bool s_geometryInitialized;
    static std::unordered_map<std::string, FontInfo> s_fonts;

    void updateTexture();
    GLuint m_textTexture = 0;
    glm::vec2 m_textureSize = glm::vec2(0.0f);
};

// 按钮组件
class UIButton : public UIComponent {
public:
    UIButton(const std::string& id);
    ~UIButton();

    // 按钮特定属性
    std::string label;
    glm::vec4 normalColor = glm::vec4(0.2f, 0.2f, 0.8f, 1.0f);
    glm::vec4 hoverColor = glm::vec4(0.3f, 0.3f, 0.9f, 1.0f);
    glm::vec4 pressedColor = glm::vec4(0.1f, 0.1f, 0.7f, 1.0f);
    float borderRadius = 5.0f;

    // 事件回调
    std::function<void()> onClickCallback;

    void setLabel(const std::string& label);
    void render(const glm::mat4& projection) override;
    void update(float deltaTime) override;
    void onClick() override;
    void onHoverEnter() override;
    void onHoverLeave() override;

private:
    UIRect* m_background;
    UIText* m_label;
    bool m_isPressed = false;
};

// 容器组件（用于分组）
class UIContainer : public UIComponent {
public:
    UIContainer(const std::string& id);
    ~UIContainer();

    void addComponent(std::shared_ptr<UIComponent> component);
    void removeComponent(const std::string& id);
    std::shared_ptr<UIComponent> getComponent(const std::string& id);

    void render(const glm::mat4& projection) override;
    void update(float deltaTime) override;

    // 遍历组件
    template<typename T>
    std::vector<std::shared_ptr<T>> getComponentsOfType() {
        std::vector<std::shared_ptr<T>> result;
        for (const auto& comp : m_children) {
            if (auto typed = std::dynamic_pointer_cast<T>(comp)) {
                result.push_back(typed);
            }
        }
        return result;
    }

private:
    std::vector<std::shared_ptr<UIComponent>> m_children;
};

// UI管理器
class UIManager {
public:
    static UIManager& getInstance();

    // 初始化/清理
    void initialize(int screenWidth, int screenHeight);
    void shutdown();

    // 组件管理
    void addComponent(std::shared_ptr<UIComponent> component);
    void removeComponent(const std::string& id);
    std::shared_ptr<UIComponent> getComponent(const std::string& id);

    // 获取着色器
    Shader& getUIShader() { return m_uiShader; }

    // 更新和渲染
    void update(float deltaTime);
    void render();

    // 交互处理
    void handleMouseClick(float x, float y);
    void handleMouseMove(float x, float y);

    // 屏幕尺寸变化
    void onScreenResize(int width, int height);

    // 投影矩阵
    glm::mat4 getProjectionMatrix() const;

    // 工具函数
    glm::vec2 screenToUIPoint(float screenX, float screenY) const;

private:
    UIManager() = default;
    ~UIManager();

    int m_screenWidth = 0;
    int m_screenHeight = 0;

    // 渲染着色器
    Shader m_uiShader{
        { { GL_VERTEX_SHADER, "shader/ui.vert" },
          { GL_FRAGMENT_SHADER, "shader/ui.frag" } }
    };

    // 组件存储
    std::unordered_map<std::string, std::shared_ptr<UIComponent>> m_components;
    std::vector<std::shared_ptr<UIComponent>> m_sortedComponents;

    // 交互状态
    std::shared_ptr<UIComponent> m_hoveredComponent = nullptr;
    std::shared_ptr<UIComponent> m_pressedComponent = nullptr;

    // 私有方法
    void sortComponentsByZIndex();
    bool checkComponentInteraction(const glm::vec2& point,
        std::shared_ptr<UIComponent>& result);
};
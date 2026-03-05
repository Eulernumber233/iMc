#include "UIManager.h"
#include "UIHotbar.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include "../TextureMgr.h"

// UIComponent ʵ��
UIComponent::UIComponent(const std::string& id) : id(id) {}

UIComponent::~UIComponent() {}

bool UIComponent::containsPoint(const glm::vec2& point) const {
    glm::vec2 minPos = position - size * anchor;
    glm::vec2 maxPos = minPos + size;
    return point.x >= minPos.x && point.x <= maxPos.x &&
        point.y >= minPos.y && point.y <= maxPos.y;
}

void UIComponent::setPosition(float x, float y) {
    position = glm::vec2(x, y);
}

void UIComponent::setSize(float width, float height) {
    size = glm::vec2(width, height);
}

void UIComponent::setColor(float r, float g, float b, float a) {
    color = glm::vec4(r, g, b, a);
}

void UIComponent::setRotation(float degrees) {
    rotation = degrees;
    transform = glm::rotate(glm::mat4(1.0f), glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::mat4 UIComponent::calculateTransform() const {
    glm::mat4 result = glm::mat4(1.0f);

    // ƽ��
    glm::vec2 finalPos = position - size * anchor;
    result = glm::translate(result, glm::vec3(finalPos, 0.0f));

    // ��ת
    if (rotation != 0.0f) {
        result = glm::rotate(result, glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
    }

    // ����
    result = glm::scale(result, glm::vec3(size, 1.0f));

    return result;
}

// UIRect ��̬��Ա��ʼ��
GLuint UIRect::s_VAO = 0;
GLuint UIRect::s_VBO = 0;
GLuint UIRect::s_EBO = 0;
bool UIRect::s_geometryInitialized = false;

UIRect::UIRect(const std::string& id) : UIComponent(id) {
    if (!s_geometryInitialized) {
        initGeometry();
    }
    VAO = s_VAO;
    VBO = s_VBO;
    EBO = s_EBO;
}

UIRect::~UIRect() {}

void UIRect::initGeometry() {
    float vertices[] = {
        // 位置        // 纹理坐标（Y翻转，因为纹理加载时不翻转）
        0.0f, 1.0f,   0.0f, 0.0f,  // 左上
        0.0f, 0.0f,   0.0f, 1.0f,  // 左下
        1.0f, 0.0f,   1.0f, 1.0f,  // 右下
        1.0f, 1.0f,   1.0f, 0.0f   // 右上
    };

    unsigned int indices[] = {
        0, 1, 2,
        0, 2, 3
    };

    glGenVertexArrays(1, &s_VAO);
    glGenBuffers(1, &s_VBO);
    glGenBuffers(1, &s_EBO);

    glBindVertexArray(s_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, s_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // λ������
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // ������������
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
        (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    s_geometryInitialized = true;
}

void UIRect::render(const glm::mat4& projection) {
    if (!visible) return;

    // ����任����
    glm::mat4 model = calculateTransform();

    // ���û��
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST); // UI渲染禁用深度测试

    // ʹ����ɫ��
    auto& uiManager = UIManager::getInstance();
    Shader& shader = uiManager.getUIShader(); // ��Ҫ�����������

    shader.use();
    shader.setMat4("uProjection", projection);
    shader.setMat4("uTransform", model);
    shader.setVec4("uColor", color);
    shader.setFloat("uAlpha", alpha);
    shader.setInt("uHasTexture", 0);
    shader.setInt("uIsRounded", borderRadius > 0.0f ? 1 : 0);

    if (borderRadius > 0.0f) {
        shader.setVec2("uSize", size);
        shader.setFloat("uRadius", borderRadius);
    }

    // ��Ⱦ
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    // �ָ��������
    glEnable(GL_DEPTH_TEST); // 恢复深度测试
    glDisable(GL_BLEND);
}

// UIImage ��̬��Ա��ʼ��
GLuint UIImage::s_VAO = 0;
GLuint UIImage::s_VBO = 0;
GLuint UIImage::s_EBO = 0;
bool UIImage::s_geometryInitialized = false;

UIImage::UIImage(const std::string& id) : UIComponent(id) {
    if (!s_geometryInitialized) {
        initGeometry();
    }
    VAO = s_VAO;
    VBO = s_VBO;
    EBO = s_EBO;
}

UIImage::~UIImage() {
    // ע�⣺������TextureMgr���������ﲻɾ��
}

void UIImage::initGeometry() {
    // ʹ����UIRect��ͬ�ļ�����
    UIRect::initGeometry();
    s_VAO = UIRect::s_VAO;
    s_VBO = UIRect::s_VBO;
    s_EBO = UIRect::s_EBO;
    s_geometryInitialized = true;
}

void UIImage::render(const glm::mat4& projection) {
    if (!visible || textureID == 0) return;

    // ����任����
    glm::mat4 model = calculateTransform();

    // ���û��
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST); // UI渲染禁用深度测试

    // ʹ����ɫ��
    auto& uiManager = UIManager::getInstance();
    Shader& shader = uiManager.getUIShader();

    shader.use();
    shader.setMat4("uProjection", projection);
    shader.setMat4("uTransform", model);
    shader.setVec4("uColor", color);
    shader.setFloat("uAlpha", alpha);
    shader.setInt("uHasTexture", 1);
    shader.setInt("uIsText", 0);

    // ������
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // ��Ⱦ
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    // �ָ��������
    glEnable(GL_DEPTH_TEST); // 恢复深度测试
    glDisable(GL_BLEND);
}

bool UIImage::containsPoint(const glm::vec2& point) const {
    // ����ͼƬ��ֻ����Ƿ��ھ����ڣ�������͸������
    return UIComponent::containsPoint(point);
}

// auto textureMgr = TextureMgr::GetInstance();
bool UIImage::loadTextureByFilePath(const std::string& filepath) {
    //textureID = textureMgr->loadTexture2D(filepath);
    //return textureID != 0;
    return false; // ��ʵ��
}

bool UIImage::loadTextureByGLid(GLuint ID)
{
    textureID = ID;
    return textureID != 0;
}

bool UIImage::loadTextureByTextureName(const std::string& texturename) {
    textureID = TextureMgr::GetInstance()->GetTexture2D(texturename);
    return textureID != 0;
}

// UIText ʵ��
std::unordered_map<std::string, UIText::FontInfo> UIText::s_fonts;
GLuint UIText::s_VAO = 0;
GLuint UIText::s_VBO = 0;
GLuint UIText::s_EBO = 0;
bool UIText::s_geometryInitialized = false;

UIText::UIText(const std::string& id) : UIComponent(id) {
    if (!s_geometryInitialized) {
        initGeometry();
    }
    VAO = s_VAO;
    VBO = s_VBO;
    EBO = s_EBO;
}

UIText::~UIText() {
    if (m_textTexture != 0) {
        glDeleteTextures(1, &m_textTexture);
    }
}

void UIText::initGeometry() {
    // ʹ����UIRect��ͬ�ļ�����
    UIRect::initGeometry();
    s_VAO = UIRect::s_VAO;
    s_VBO = UIRect::s_VBO;
    s_EBO = UIRect::s_EBO;
    s_geometryInitialized = true;
}

void UIText::setText(const std::string& text) {
    this->text = text;
    updateTexture();
}

void UIText::render(const glm::mat4& projection) {
    if (!visible || text.empty() || m_textTexture == 0) return;

    // ����任����
    glm::mat4 model = calculateTransform();

    // ���û��
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST); // UI渲染禁用深度测试

    // ʹ����ɫ��
    auto& uiManager = UIManager::getInstance();
    Shader& shader = uiManager.getUIShader();

    shader.use();
    shader.setMat4("uProjection", projection);
    shader.setMat4("uTransform", model);
    shader.setVec4("uColor", color);
    shader.setFloat("uAlpha", alpha);
    shader.setInt("uHasTexture", 1);
    shader.setInt("uIsText", 1);
    shader.setVec4("uTextColor", textColor);

    // ������
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textTexture);

    // ��Ⱦ
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    // �ָ��������
    glEnable(GL_DEPTH_TEST); // 恢复深度测试
    glDisable(GL_BLEND);
}

void UIText::updateTexture() {
    // �����ʵ�֣�����һ���򵥵��ı�����
    // ��ʵ����Ŀ�У�Ӧ��ʹ��FreeType��

    if (m_textTexture != 0) {
        glDeleteTextures(1, &m_textTexture);
    }

    // ��������
    glGenTextures(1, &m_textTexture);
    glBindTexture(GL_TEXTURE_2D, m_textTexture);

    // ��ʾ��������һ��64x64�İ�ɫ����
    const int texSize = 64;
    std::vector<unsigned char> pixels(texSize * texSize * 4, 255);

    // �򵥻�����ĸ������ֻ��ʾ����ʵ��Ӧ��ʹ�����壩
    for (int i = 0; i < text.length() && i < 8; i++) {
        int x = i * 8;
        // �򵥻����ַ�����
        for (int y = 20; y < 44; y++) {
            int idx = (y * texSize + x + 2) * 4;
            pixels[idx] = 0;       // R
            pixels[idx + 1] = 0;   // G
            pixels[idx + 2] = 0;   // B
            pixels[idx + 3] = 255; // A
        }
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texSize, texSize, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    m_textureSize = glm::vec2(texSize, texSize);
    size = glm::vec2(texSize, texSize); // �Զ�������С
}

bool UIText::loadFont(const std::string& name, const std::string& texturePath,
    const std::string& configPath) {
    // ������������������
    // �����ʵ��
    return true;
}

// UIButton ʵ��
UIButton::UIButton(const std::string& id) : UIComponent(id) {
    m_background = new UIRect(id + "_bg");
    m_label = new UIText(id + "_label");

    m_background->color = normalColor;
    m_background->borderRadius = borderRadius;
    interactive = true;
}

UIButton::~UIButton() {
    delete m_background;
    delete m_label;
}

void UIButton::setLabel(const std::string& label) {
    this->label = label;
    m_label->setText(label);

    // ������ǩλ�þ���
    glm::vec2 labelSize = m_label->size;
    m_label->position = glm::vec2(
        (size.x - labelSize.x) * 0.5f,
        (size.y - labelSize.y) * 0.5f
    );
}

void UIButton::render(const glm::mat4& projection) {
    if (!visible) return;

    // ��Ⱦ����
    m_background->position = position;
    m_background->size = size;
    m_background->visible = visible;
    m_background->render(projection);

    // ��Ⱦ��ǩ
    if (!label.empty()) {
        m_label->position = position + glm::vec2(
            (size.x - m_label->size.x) * 0.5f,
            (size.y - m_label->size.y) * 0.5f
        );
        m_label->visible = visible;
        m_label->render(projection);
    }
}

void UIButton::update(float deltaTime) {
    // ���°�ť״̬
    if (isHovered && !m_isPressed) {
        m_background->color = hoverColor;
    }
    else if (m_isPressed) {
        m_background->color = pressedColor;
    }
    else {
        m_background->color = normalColor;
    }
}

void UIButton::onClick() {
    m_isPressed = true;
    if (onClickCallback) {
        onClickCallback();
    }
}

void UIButton::onHoverEnter() {
    isHovered = true;
}

void UIButton::onHoverLeave() {
    isHovered = false;
    m_isPressed = false;
}

// UIContainer ʵ��
UIContainer::UIContainer(const std::string& id) : UIComponent(id) {}

UIContainer::~UIContainer() {}

void UIContainer::addComponent(std::shared_ptr<UIComponent> component) {
    m_children.push_back(component);
}

void UIContainer::removeComponent(const std::string& id) {
    m_children.erase(
        std::remove_if(m_children.begin(), m_children.end(),
            [&id](const std::shared_ptr<UIComponent>& comp) {
                return comp->id == id;
            }),
        m_children.end()
    );
}

std::shared_ptr<UIComponent> UIContainer::getComponent(const std::string& id) {
    for (const auto& comp : m_children) {
        if (comp->id == id) {
            return comp;
        }
    }
    return nullptr;
}

void UIContainer::render(const glm::mat4& projection) {
    if (!visible) return;

    // ��Ⱦ���������
    for (const auto& comp : m_children) {
        if (comp->visible) {
            // 应用容器的变换（考虑锚点，但不应用缩放）
            glm::vec2 finalPos = position - size * anchor;
            glm::mat4 containerTransform = glm::translate(glm::mat4(1.0f),
                glm::vec3(finalPos, 0.0f));

            // 组合变换矩阵
            glm::mat4 combinedProjection = projection * containerTransform;

            comp->render(combinedProjection);
        }
    }
}

void UIContainer::update(float deltaTime) {
    if (!visible) return;

    for (const auto& comp : m_children) {
        comp->update(deltaTime);
    }
}

// UIManager ʵ��
UIManager& UIManager::getInstance() {
    static UIManager instance;
    return instance;
}

UIManager::~UIManager() {
    shutdown();
}

void UIManager::initialize(int screenWidth, int screenHeight) {
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // ��ʼ��UI��ɫ��
    m_uiShader.setInt("uTexture", 0);

    std::cout << "UIManager initialized successfully!" << std::endl;
}

void UIManager::shutdown() {
    m_components.clear();
    m_sortedComponents.clear();
}

void UIManager::addComponent(std::shared_ptr<UIComponent> component) {
    m_components[component->id] = component;
    sortComponentsByZIndex();
}

void UIManager::removeComponent(const std::string& id) {
    m_components.erase(id);
    sortComponentsByZIndex();
}

std::shared_ptr<UIComponent> UIManager::getComponent(const std::string& id) {
    auto it = m_components.find(id);
    if (it != m_components.end()) {
        return it->second;
    }
    return nullptr;
}

void UIManager::update(float deltaTime) {
    for (const auto& pair : m_components) {
        if (pair.second->visible) {
            pair.second->update(deltaTime);
        }
    }
}

void UIManager::render() {
    if (m_sortedComponents.empty()) return;

    // ��������ͶӰ
    glm::mat4 projection = glm::ortho(0.0f, (float)m_screenWidth,
        0.0f, (float)m_screenHeight,
        -1.0f, 1.0f);

    // ��Ⱦ�����������zIndex����
    for (const auto& component : m_sortedComponents) {
        if (component->visible) {
            component->render(projection);
        }
    }
}

void UIManager::handleMouseClick(float x, float y) {
    glm::vec2 uiPoint = screenToUIPoint(x, y);

    // �����zIndex��ʼ���
    std::shared_ptr<UIComponent> clickedComponent = nullptr;
    for (auto it = m_sortedComponents.rbegin(); it != m_sortedComponents.rend(); ++it) {
        auto component = *it;
        if (component->visible && component->interactive &&
            component->containsPoint(uiPoint)) {
            clickedComponent = component;
            break;
        }
    }

    if (clickedComponent) {
        clickedComponent->onClick();
        m_pressedComponent = clickedComponent;
    }
}

void UIManager::handleMouseMove(float x, float y) {
    glm::vec2 uiPoint = screenToUIPoint(x, y);

    // �����ͣ
    std::shared_ptr<UIComponent> hoveredComponent = nullptr;
    for (auto it = m_sortedComponents.rbegin(); it != m_sortedComponents.rend(); ++it) {
        auto component = *it;
        if (component->visible && component->interactive &&
            component->containsPoint(uiPoint)) {
            hoveredComponent = component;
            break;
        }
    }

    // ������ͣ״̬�仯
    if (hoveredComponent != m_hoveredComponent) {
        if (m_hoveredComponent) {
            m_hoveredComponent->onHoverLeave();
        }
        if (hoveredComponent) {
            hoveredComponent->onHoverEnter();
        }
        m_hoveredComponent = hoveredComponent;
    }
}

void UIManager::handleMouseScroll(float xoffset, float yoffset) {
    // 查找所有UIHotbar组件并传递滚轮事件
    for (const auto& pair : m_components) {
        auto component = pair.second;
        // 尝试动态转换为UIHotbar
        if (auto hotbar = std::dynamic_pointer_cast<UIHotbar>(component)) {
            hotbar->scroll(yoffset);
        }
        // 如果是容器，还需要递归查找子组件？UIHotbar继承自UIContainer，但UIContainer的子组件不在m_components中
        // 由于UIHotbar本身是UIContainer，它已经直接添加到m_components中，所以上述转换应该能捕获到
    }
}

void UIManager::onScreenResize(int width, int height) {
    m_screenWidth = width;
    m_screenHeight = height;
}

glm::mat4 UIManager::getProjectionMatrix() const {
    return glm::ortho(0.0f, (float)m_screenWidth,
        0.0f, (float)m_screenHeight,
        -1.0f, 1.0f);
}

glm::vec2 UIManager::screenToUIPoint(float screenX, float screenY) const {
    // OpenGL����ԭ�������½ǣ���Ļ����ԭ�������Ͻ�
    float uiX = screenX;
    float uiY = m_screenHeight - screenY; // ��תY��
    return glm::vec2(uiX, uiY);
}

void UIManager::sortComponentsByZIndex() {
    m_sortedComponents.clear();
    for (const auto& pair : m_components) {
        m_sortedComponents.push_back(pair.second);
    }

    std::sort(m_sortedComponents.begin(), m_sortedComponents.end(),
        [](const std::shared_ptr<UIComponent>& a,
            const std::shared_ptr<UIComponent>& b) {
                return a->zIndex < b->zIndex;
        });
}

bool UIManager::checkComponentInteraction(const glm::vec2& point,
    std::shared_ptr<UIComponent>& result) {
    for (auto it = m_sortedComponents.rbegin(); it != m_sortedComponents.rend(); ++it) {
        auto component = *it;
        if (component->visible && component->interactive &&
            component->containsPoint(point)) {
            result = component;
            return true;
        }
    }
    return false;
}
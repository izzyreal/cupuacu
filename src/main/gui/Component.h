#pragma once

#include <SDL3/SDL.h>

#include <vector>
#include <memory>
#include <string>

#include "../CupuacuState.h"

class Component {
private:
    std::string componentName;
    bool dirty = false;
    uint16_t xPos = 0, yPos = 0;
    uint16_t width = 0, height = 0;
    Component *parent = nullptr;
    std::vector<std::unique_ptr<Component>> children;

    void setParent(Component *parentToUse);

protected:
    CupuacuState *state;
    const std::vector<std::unique_ptr<Component>>& getChildren() const;
    void removeAllChildren();
    const bool isMouseOver() const;
    Component* getParent() const;

public:
    Component(CupuacuState *stateToUse, const std::string componentNameToUse);

    template <typename T>
    T* addChildAndSetDirty(std::unique_ptr<T> &childToAdd)
    {
        children.push_back(std::move(childToAdd));
        children.back()->setParent(this);
        children.back()->setDirty();
        return dynamic_cast<T*>(children.back().get());
    }

    template <typename T, typename... Args>
    T* emplaceChildAndSetDirty(Args&&... args)
    {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        children.push_back(std::move(child));
        children.back()->setParent(this);
        children.back()->setDirty();
        return static_cast<T*>(children.back().get());
    }

    const std::string getComponentName() const;

    void setBounds(const uint16_t xPosToUse, const uint16_t yPosToUse, const uint16_t widthToUse, const uint16_t heightToUse);
    void setSize(const uint16_t widthToUse, const uint16_t heightToUse);
    void setYPos(const uint16_t yPosToUse);
    void setDirty();
    void setDirtyRecursive();
    bool isDirtyRecursive();
    void draw(SDL_Renderer* renderer);

    virtual void onDraw(SDL_Renderer* renderer) {}
    virtual void mouseLeave() {}
    virtual void mouseEnter() {}
    virtual bool mouseLeftButtonDown(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) { return false; }
    virtual bool mouseLeftButtonUp(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) { return false; }
    virtual bool mouseMove(const int32_t mouseX, const int32_t mouseY, const float mouseRelY, const bool leftButtonIsDown) { return false; }
    virtual void timerCallback() { for (auto &c : children) { c->timerCallback(); }}
    virtual void resized() {}

    bool handleEvent(const SDL_Event& e);
    uint16_t getWidth() const;
    uint16_t getHeight() const;
    uint16_t getXPos() const;
    uint16_t getYPos() const;
    const std::pair<int, int> getAbsolutePosition();
    const bool constainsAbsoluteCoordinate(const int x, const int y);
    Component* findComponentAt(const int x, const int y);
};

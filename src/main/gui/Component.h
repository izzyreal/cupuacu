#pragma once

#include "MouseEvent.h"

#include "Helpers.h"
#include "Colors.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

namespace cupuacu::gui {

struct State;

class Component {
private:
    bool visible = false;
    bool parentClippingEnabled = true;
    bool interceptMouseEnabled = true;
    bool dirty = false;
    std::string componentName;
    int32_t xPos = 0, yPos = 0;
    int32_t width = 0, height = 0;
    Component *parent = nullptr;
    std::vector<std::unique_ptr<Component>> children;

    void setParent(Component *parentToUse);

protected:
    cupuacu::State *state;
    void removeAllChildren();
    Component* getParent() const;

public:
    Component(cupuacu::State*, const std::string componentName);
    void setVisible(bool shouldBeVisible);
    bool isVisible() const { return visible; }
    const std::vector<std::unique_ptr<Component>>& getChildren() const;

    int32_t getCenterX()
    {
        const auto bounds = getBounds();
        return bounds.x + std::round(bounds.w / 2.f);
    }

    static bool isComponentOrChildOf(Component*, Component*);

    bool hasChild(Component *component)
    {
        for (auto &c : children)
        {
            if (c.get() == component) return true;
        }
        return false;
    }

    void removeChild(Component*);

    void setInterceptMouseEnabled(const bool shouldInterceptMouse);

    const bool isMouseOver() const;

    SDL_Rect getBounds()
    {
        return { xPos, yPos, width, height };
    }

    SDL_Rect getLocalBounds()
    {
        return { 0, 0, width, height };
    }

    SDL_FRect getLocalBoundsF()
    {
        return { 0.0f, 0.0f, (float)width, (float)height };
    }

    SDL_Rect getAbsoluteBounds()
    {
        auto rect = getLocalBounds();
        const auto [absX, absY] = getAbsolutePosition();
        rect.x = absX;
        rect.y = absY;
        return rect;
    }

    void disableParentClipping() { parentClippingEnabled = false; }
    bool isParentClippingEnabled() { return parentClippingEnabled; }

    template <typename T>
    void removeChildrenOfType()
    {
        children.erase(
            std::remove_if(children.begin(), children.end(),
                [](const std::unique_ptr<Component>& child) {
                    return dynamic_cast<T*>(child.get()) != nullptr;
                }),
            children.end()
        );

        setDirty();
    }

    template <typename T>
    T* addChild(std::unique_ptr<T> &childToAdd)
    {
        children.push_back(std::move(childToAdd));
        children.back()->setParent(this);
        children.back()->setVisible(true);
        children.back()->setDirty();
        return dynamic_cast<T*>(children.back().get());
    }

    template <typename T, typename... Args>
    T* emplaceChild(Args&&... args)
    {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        children.push_back(std::move(child));
        children.back()->setParent(this);
        children.back()->setVisible(true);
        children.back()->setDirty();
        return static_cast<T*>(children.back().get());
    }

    const std::string getComponentName() const;

    void sendToBack();
    void bringToFront();
    void setBounds(const SDL_Rect);
    void setBounds(int32_t xPosToUse, int32_t yPosToUse, int32_t widthToUse, int32_t heightToUse);
    void setSize(int32_t widthToUse, int32_t heightToUse);
    void setYPos(int32_t yPosToUse);
    void setDirty();
    void draw(SDL_Renderer* renderer);

    virtual void onDraw(SDL_Renderer* renderer) {}
    virtual void mouseLeave() {}
    virtual void mouseEnter() {}
    virtual bool mouseDown(const MouseEvent&) { return false; }
    virtual bool mouseUp(const MouseEvent&) { return false; }
    virtual bool mouseMove(const MouseEvent&) { return false; }
    virtual void timerCallback() {}
    virtual void resized() {}

    bool handleMouseEvent(const MouseEvent&);
    int32_t getWidth() const;
    int32_t getHeight() const;
    int32_t getXPos() const;
    int32_t getYPos() const;
    std::pair<int, int> getAbsolutePosition();
    bool containsAbsoluteCoordinate(int x, int y);
    Component* findComponentAt(int x, int y);

    void timerCallbackRecursive()
    {
        timerCallback();
        for (auto &c : children)
        {
            c->timerCallbackRecursive();
        }
    }

    void printTree(int depth = 0) const
    {
        for (int i = 0; i < depth; ++i)
        {
            printf("  ");
        }

        printf("%s (%dx%d at %d,%d)\n",
               componentName.c_str(),
               width, height, xPos, yPos);

        for (const auto& child : children)
        {
            child->printTree(depth + 1);
        }
    }

    bool isDirty() const { return dirty; }
};
}

#include "Component.h"
#include "../CupuacuState.h"
#include "MenuBar.h"
#include <ranges>
#include <cmath>

#define DEBUG_DRAW 0
#if DEBUG_DRAW
#include <cstdlib>
#endif

Component::Component(CupuacuState *stateToUse, const std::string componentNameToUse) :
    state(stateToUse), componentName(componentNameToUse)
{
}

void Component::setVisible(bool shouldBeVisible)
{
    if (visible != shouldBeVisible)
    {
        visible = shouldBeVisible;
        SDL_Rect r = getAbsoluteBounds();
        if (r.w > 0 && r.h > 0) {
            state->dirtyRects.push_back(r);
        }
    }
}

void Component::setInterceptMouseEnabled(const bool shouldInterceptMouse)
{
    interceptMouseEnabled = shouldInterceptMouse;
}

void Component::setParent(Component *parentToUse)
{
    parent = parentToUse;
}

const std::vector<std::unique_ptr<Component>>& Component::getChildren() const
{
    return children;
}

void Component::removeChild(Component *child)
{
    auto oldBounds = child->getAbsoluteBounds();
    for (auto it = children.begin(); it != children.end(); ++it)
    {
        if (it->get() == child)
        {
            children.erase(it);
            state->dirtyRects.push_back(oldBounds);
            setDirty();
            break;
        }
    }
}

void Component::sendToBack()
{
    if (parent == nullptr)
        return;

    auto& parentChildren = parent->children;
    auto thisIter = std::find_if(parentChildren.begin(), parentChildren.end(),
        [this](const std::unique_ptr<Component>& child) { return child.get() == this; });

    if (thisIter != parentChildren.end() && thisIter != parentChildren.begin())
    {
        auto ptr = std::move(*thisIter);
        parentChildren.erase(thisIter);
        parentChildren.insert(parentChildren.begin(), std::move(ptr));
        parent->setDirty();
    }
}

void Component::bringToFront()
{
    if (parent == nullptr)
        return;

    auto& parentChildren = parent->children;
    auto thisIter = std::find_if(
        parentChildren.begin(), parentChildren.end(),
        [this](const std::unique_ptr<Component>& child) { return child.get() == this; });

    if (thisIter != parentChildren.end() && thisIter != parentChildren.end() - 1)
    {
        auto tmp = std::move(*thisIter);
        thisIter = parentChildren.erase(thisIter);
        parentChildren.push_back(std::move(tmp));
        parent->setDirty();
    }
}

void Component::removeAllChildren()
{
    for (auto &c : children)
    {
        if (state->componentUnderMouse == c.get())
            state->componentUnderMouse = nullptr;
        if (state->capturingComponent == c.get())
            state->capturingComponent = nullptr;
    }
    children.clear();
    setDirty();
}

const bool Component::isMouseOver() const
{
    return state->componentUnderMouse == this;
}

Component* Component::getParent() const
{
    return parent;
}

const std::string Component::getComponentName() const
{
    return componentName;
}

void Component::setBounds(const SDL_Rect b)
{
    setBounds(b.x, b.y, b.w, b.h);
}

void Component::setBounds(int32_t xPosToUse, int32_t yPosToUse, int32_t widthToUse, int32_t heightToUse)
{
    if (xPosToUse == xPos && yPosToUse == yPos && widthToUse == width && heightToUse == height)
    {
        return;
    }
    setDirty();
    xPos = xPosToUse;
    yPos = yPosToUse;
    width = widthToUse;
    height = heightToUse;
    setDirty();
    resized();
}

void Component::setSize(int32_t widthToUse, int32_t heightToUse)
{
    if (widthToUse != width || heightToUse != height)
    {
        setDirty();
    }
    width = widthToUse;
    height = heightToUse;
    setDirty();
    resized();
}

void Component::setYPos(int32_t yPosToUse)
{
    if (yPosToUse != yPos)
    {
        setDirty();
    }
    yPos = yPosToUse;
    setDirty();
    resized();
}

void Component::setDirty()
{
    if (!visible)
        return;
    dirty = true;
    SDL_Rect r = getAbsoluteBounds();
    if (r.w > 0 && r.h > 0) {
        state->dirtyRects.push_back(r);
    }
    for (auto &c : children) {
        if (c->isVisible()) {
            c->setDirty();
        }
    }
}

void Component::draw(SDL_Renderer* renderer)
{
    if (!visible)
        return;

    SDL_Rect absRect = getAbsoluteBounds();
    bool intersects = true;

    if (parentClippingEnabled)
    {
        intersects = false;
        SDL_Rect parentRect;
        if (parent) {
            parentRect = parent->getAbsoluteBounds();
        } else {
            SDL_GetRenderViewport(renderer, &parentRect);
        }
        SDL_Rect intersection;
        if (SDL_GetRectIntersection(&absRect, &parentRect, &intersection)) {
            for (const auto& dr : state->dirtyRects) {
                if (SDL_HasRectIntersection(&intersection, &dr)) {
                    intersects = true;
                    break;
                }
            }
        }
    } else {
        for (const auto& dr : state->dirtyRects) {
            if (SDL_HasRectIntersection(&absRect, &dr)) {
                intersects = true;
                break;
            }
        }
    }

    if (!intersects)
    {
#if DEBUG_DRAW
        printf("Not drawing %s because it doesn't intersect\n", componentName.c_str());
#endif
        return;
    }

    SDL_Rect viewPortRect;
    SDL_GetRenderViewport(renderer, &viewPortRect);
    SDL_Rect parentViewPortRect = viewPortRect;

    viewPortRect.x = absRect.x;
    viewPortRect.y = absRect.y;
    viewPortRect.w = absRect.w;
    viewPortRect.h = absRect.h;

    if (parentClippingEnabled)
    {
        SDL_Rect viewPortRectToUse;
        if (SDL_GetRectIntersection(&parentViewPortRect, &viewPortRect, &viewPortRectToUse))

        {
            SDL_SetRenderViewport(renderer, &viewPortRectToUse);
        }
        else
        {
#if DEBUG_DRAW
        printf("Not drawing %s because it doesn't intersect\n", componentName.c_str());
#endif
            return;
        }
    }
    else
    {
        SDL_SetRenderViewport(renderer, &viewPortRect);
    }

    if (dirty)
    {
        onDraw(renderer);
#if DEBUG_DRAW
        printf("drawing %s\n", componentName.c_str());
        Uint8 r = rand() % 256;
        Uint8 g = rand() % 256;
        Uint8 b = rand() % 256;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, r, g, b, 128);
        auto localBounds = getLocalBoundsF();
        SDL_RenderFillRect(renderer, &localBounds);
#endif
    }
#if DEBUG_DRAW
    else
    {
        printf("Not drawing %s because it's not dirty\n", componentName.c_str());
    }
#endif

    for (auto& c : children)
    {
        c->draw(renderer);
    }

    SDL_SetRenderViewport(renderer, &parentViewPortRect);
    dirty = false;
}

bool Component::handleMouseEvent(const MouseEvent &mouseEvent)
{
    if (!visible)
        return false;

    float localXf = mouseEvent.mouseXf - xPos;
    float localYf = mouseEvent.mouseYf - yPos;
    int localXi = static_cast<int>(std::floor(localXf));
    int localYi = static_cast<int>(std::floor(localYf));

    MouseEvent localMouseEvent = withNewCoordinates(mouseEvent, localXi, localYi, localXf, localYf);

    for (auto& c : std::views::reverse(children))
    {
        if (c->handleMouseEvent(localMouseEvent))
            return true;
    }

    Component *capturingComponent = state->capturingComponent;

    if (localXi < 0 || localXi >= width || localYi < 0 || localYi >= height)
    {
        if (mouseEvent.type == MOVE)
        {
            if (this == capturingComponent)
            {
                if (mouseMove(localMouseEvent))
                    return true;
            }
        }
        else if (mouseEvent.type == UP && mouseEvent.buttonState.left)
        {
            if (this == capturingComponent)
            {
                if (mouseUp(localMouseEvent))
                {
                    state->capturingComponent = nullptr;
                    return true;
                }
            }
        }
    }
    else
    {
        if (mouseEvent.type == MOVE)
        {
            if (mouseMove(localMouseEvent))
                return true;
        }
        else if (mouseEvent.type == DOWN && mouseEvent.buttonState.left)
        {
            if (mouseDown(localMouseEvent))
            {
                state->capturingComponent = this;
                if (componentName.substr(0, 4) != "Menu")
                {
                    state->menuBar->hideSubMenus();
                }
                return true;
            }
        }
        else if (mouseEvent.type == UP && mouseEvent.buttonState.left)
        {
            if (mouseUp(localMouseEvent))
            {
                state->capturingComponent = nullptr;
                return true;
            }
        }
    }
    return false;
}

int32_t Component::getWidth() const { return width; }
int32_t Component::getHeight() const { return height; }
int32_t Component::getXPos() const { return xPos; }
int32_t Component::getYPos() const { return yPos; }

std::pair<int, int> Component::getAbsolutePosition()
{
    int resultX = getXPos(), resultY = getYPos();
    Component *p = getParent();
    while (p != nullptr)
    {
        resultX += p->getXPos();
        resultY += p->getYPos();
        p = p->getParent();
    }
    return {resultX, resultY};
}

bool Component::containsAbsoluteCoordinate(int x, int y)
{
    if (!visible)
        return false;
    auto [absX, absY] = getAbsolutePosition();
    SDL_Rect rect{ absX, absY, width, height };

    if (parentClippingEnabled) {
        Component* p = parent;
        while (p != nullptr) {
            if (p->parentClippingEnabled) {
                auto [px, py] = p->getAbsolutePosition();
                SDL_Rect parentRect{ px, py, p->getWidth(), p->getHeight() };
                SDL_Rect intersection;
                if (!SDL_GetRectIntersection(&rect, &parentRect, &intersection)) {
                    return false;
                }
                rect = intersection;
            }
            p = p->getParent();
        }
    }

    const SDL_Point pt{x, y};
    return SDL_PointInRect(&pt, &rect);
}

Component* Component::findComponentAt(int x, int y)
{
    if (!visible)
        return nullptr;

    for (auto &c : std::views::reverse(children))
    {
        if (auto found = c->findComponentAt(x, y); found != nullptr)
            return found;
    }

    if (interceptMouseEnabled && containsAbsoluteCoordinate(x, y))
        return this;

    return nullptr;
}

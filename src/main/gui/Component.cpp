#include "Component.h"

#include "MenuBar.h"
#include "../CupuacuState.h"

#include <ranges>

#define DEBUG_DRAW 0

#if DEBUG_DRAW
#include <cstdlib>
#endif

Component::Component(CupuacuState *stateToUse, const std::string componentNameToUse) :
    state(stateToUse), componentName(componentNameToUse)
{
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
            break;
        }
    }
    state->dirtyRects.push_back(oldBounds);
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
    {
        return;
    }

    auto& parentChildren = parent->children;

    auto thisIter = std::find_if(parentChildren.begin(), parentChildren.end(),
        [this](const std::unique_ptr<Component>& child) { return child.get() == this; });

    if (thisIter != parentChildren.end() && thisIter != parentChildren.end() - 1)
    {
        parentChildren.push_back(std::move(*thisIter));
        parentChildren.erase(thisIter);
        parent->setDirty();
    }
}

void Component::removeAllChildren()
{
    for (auto &c : children)
    {
        if (state->componentUnderMouse == c.get())
        {
            state->componentUnderMouse = nullptr;
        }

        if (state->capturingComponent == c.get())
        {
            state->capturingComponent = nullptr;
        }
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

void Component::setBounds(const uint16_t xPosToUse, const uint16_t yPosToUse, const uint16_t widthToUse, const uint16_t heightToUse)
{
    if (xPosToUse != xPos || yPosToUse != yPos || widthToUse != width || heightToUse != height)
    {
        setDirty();  // Mark old bounds
    }
    xPos = xPosToUse;
    yPos = yPosToUse;
    width = widthToUse;
    height = heightToUse;
    setDirty();
    resized();
}

void Component::setSize(const uint16_t widthToUse, const uint16_t heightToUse)
{
    if (widthToUse != width || heightToUse != height)
    {
        setDirty();  // Mark old bounds
    }
    width = widthToUse;
    height = heightToUse;
    setDirty();
    resized();
}

void Component::setYPos(const uint16_t yPosToUse)
{
    if (yPosToUse != yPos)
    {
        setDirty();  // Mark old bounds
    }
    yPos = yPosToUse;
    setDirty();
    resized();
}

void Component::setDirty()
{
    state->dirtyRects.push_back(getAbsoluteBounds());
    
    for (auto &c : children)
    {
        c->setDirty();
    }
}

void Component::draw(SDL_Renderer* renderer)
{
#if DEBUG_DRAW
    if (!state->dirtyRects.empty() && componentName == "RootComponent")
    {
        printf("======\n");
        printf("==== componentUnderMouse: %s\n", (state->componentUnderMouse == nullptr ? "<none>" : state->componentUnderMouse->getComponentName().c_str()));
    }
#endif
    SDL_FRect absFRect = getAbsoluteBounds();
    SDL_Rect absRect = FRectToRect(absFRect);

    bool intersects = false;

    for (const auto& drF : state->dirtyRects)
    {
        SDL_Rect dr = FRectToRect(drF);
        if (SDL_HasRectIntersection(&absRect, &dr))
        {
            intersects = true;
            setDirty();
            break;
        }
    }

    if (!intersects)
    {
        return;
    }

    SDL_Rect viewPortRect;
    SDL_GetRenderViewport(renderer, &viewPortRect);
    SDL_Rect parentViewPortRect = viewPortRect;

    viewPortRect.x = static_cast<int>(absFRect.x);
    viewPortRect.y = static_cast<int>(absFRect.y);
    viewPortRect.w = static_cast<int>(absFRect.w);
    viewPortRect.h = static_cast<int>(absFRect.h);

    if (parentClippingEnabled)
    {
        SDL_Rect viewPortRectToUse;
        if (SDL_GetRectIntersection(&parentViewPortRect, &viewPortRect, &viewPortRectToUse))
            SDL_SetRenderViewport(renderer, &viewPortRectToUse);
    }
    else
    {
        SDL_SetRenderViewport(renderer, &viewPortRect);
    }

    onDraw(renderer);

#if DEBUG_DRAW
    printf("drawing %s\n", componentName.c_str());
    Uint8 r = rand() % 256;
    Uint8 g = rand() % 256;
    Uint8 b = rand() % 256;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, r, g, b, 128);
    auto localBounds = getLocalBounds();
    SDL_RenderFillRect(renderer, &localBounds);
#endif

    // Draw all children if this component is drawn, ensuring they appear over the parent's drawing
    for (auto& c : children)
    {
        c->draw(renderer);
    }

    SDL_SetRenderViewport(renderer, &parentViewPortRect);
}

bool Component::handleMouseEvent(const MouseEvent &mouseEvent)
{
    float localXf = mouseEvent.mouseXf - xPos;
    float localYf = mouseEvent.mouseYf - yPos;
    int localXi   = static_cast<int>(std::floor(localXf));
    int localYi   = static_cast<int>(std::floor(localYf));

    MouseEvent localMouseEvent = withNewCoordinates(mouseEvent, localXi, localYi, localXf, localYf);

    for (auto& c : std::views::reverse(children))
    {
        if (c->handleMouseEvent(localMouseEvent))
        {
            return true;
        }
    }

    Component *capturingComponent = state->capturingComponent;

    if (localXi < 0 || localXi >= width || localYi < 0 || localYi >= height)
    {
        if (mouseEvent.type == MOVE)
        {
            if (this == capturingComponent)
            {
                if (mouseMove(localMouseEvent))
                {
                    return true;
                }
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
            {
                return true;
            }
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

uint16_t Component::getWidth() const
{
    return width;
}

uint16_t Component::getHeight() const
{
    return height;
}

uint16_t Component::getXPos() const
{
    return xPos;
}

uint16_t Component::getYPos() const
{
    return yPos;
}

std::pair<int, int> Component::getAbsolutePosition()
{
    int resultX = getXPos(), resultY = getYPos();
    Component *parent = getParent();
    while (parent != nullptr)
    {
        resultX += parent->getXPos();
        resultY += parent->getYPos();
        parent = parent->getParent();
    }
    return {resultX, resultY};
}

bool Component::containsAbsoluteCoordinate(const int x, const int y)
{
    auto [absX, absY] = getAbsolutePosition();
    SDL_Rect rect { absX, absY, (int)getWidth(), (int)getHeight() };

    if (parentClippingEnabled)
    {
        Component* p = parent;
        while (p != nullptr)
        {
            auto [px, py] = p->getAbsolutePosition();
            SDL_Rect parentRect { px, py, (int)p->getWidth(), (int)p->getHeight() };

            SDL_Rect intersection;
            if (!SDL_GetRectIntersection(&rect, &parentRect, &intersection))
            {
                return false;
            }
            rect = intersection;

            p = p->getParent();
        }
    }

    const SDL_Point pointToUse{x, y};

    return SDL_PointInRect(&pointToUse, &rect);
}

Component* Component::findComponentAt(const int x, const int y)
{
    for (auto &c : std::views::reverse(children))
    {
        if (auto foundComponent = c->findComponentAt(x, y); foundComponent != nullptr)
        {
            return foundComponent;
        }
    }

    if (interceptMouseEnabled && containsAbsoluteCoordinate(x, y))
    {
        return this;
    }

    return nullptr;
}

#include "Component.hpp"
#include "../State.hpp"
#include "MenuBar.hpp"
#include "Window.hpp"
#include <ranges>
#include <cmath>

#define DEBUG_DRAW 0
#if DEBUG_DRAW
#include <cstdlib>
#endif

using namespace cupuacu::gui;

Component::Component(State *stateToUse, const std::string &componentNameToUse)
    : state(stateToUse), componentName(componentNameToUse)
{
}

void Component::setVisible(const bool shouldBeVisible)
{
    if (visible != shouldBeVisible)
    {
        visible = shouldBeVisible;
        const SDL_Rect r = getAbsoluteBounds();
        if (window && r.w > 0 && r.h > 0)
        {
            window->getDirtyRects().push_back(r);
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
    if (parent)
    {
        setWindow(parent->window);
    }
}

const std::vector<std::unique_ptr<Component>> &Component::getChildren() const
{
    return children;
}

void Component::removeChild(Component *child)
{
    auto it = std::find_if(children.begin(), children.end(),
                           [child](const std::unique_ptr<Component> &c)
                           {
                               return c.get() == child;
                           });
    if (it == children.end())
    {
        return;
    }

    clearWindowPointersForSubtree(it->get());
    const auto oldBounds = it->get()->getAbsoluteBounds();
    for (; it != children.end(); ++it)
    {
        if (it->get() == child)
        {
            children.erase(it);
            if (window)
            {
                window->getDirtyRects().push_back(oldBounds);
            }
            setDirty();
            break;
        }
    }
}

void Component::sendToBack() const
{
    if (parent == nullptr)
    {
        return;
    }

    auto &parentChildren = parent->children;
    const auto thisIter =
        std::find_if(parentChildren.begin(), parentChildren.end(),
                     [this](const std::unique_ptr<Component> &child)
                     {
                         return child.get() == this;
                     });

    if (thisIter != parentChildren.end() && thisIter != parentChildren.begin())
    {
        auto ptr = std::move(*thisIter);
        parentChildren.erase(thisIter);
        parentChildren.insert(parentChildren.begin(), std::move(ptr));
        parent->setDirty();
    }
}

void Component::bringToFront() const
{
    if (parent == nullptr)
    {
        return;
    }

    auto &parentChildren = parent->children;
    auto thisIter = std::find_if(parentChildren.begin(), parentChildren.end(),
                                 [this](const std::unique_ptr<Component> &child)
                                 {
                                     return child.get() == this;
                                 });

    if (thisIter != parentChildren.end() &&
        thisIter != parentChildren.end() - 1)
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
        clearWindowPointersForSubtree(c.get());
    }
    children.clear();
    setDirty();
}

const bool Component::isMouseOver() const
{
    return window && window->getComponentUnderMouse() == this;
}

Component *Component::getParent() const
{
    return parent;
}

void Component::clearWindowPointersForSubtree(Component *subtreeRoot) const
{
    if (!window || !subtreeRoot)
    {
        return;
    }

    Component *underMouse = window->getComponentUnderMouse();
    if (underMouse && isComponentOrChildOf(underMouse, subtreeRoot))
    {
        window->setComponentUnderMouse(nullptr);
        underMouse->mouseLeave();
    }

    Component *capturing = window->getCapturingComponent();
    if (capturing && isComponentOrChildOf(capturing, subtreeRoot))
    {
        window->setCapturingComponent(nullptr);
    }

    if (auto *menuBar = window->getMenuBar();
        menuBar && isComponentOrChildOf(menuBar, subtreeRoot))
    {
        window->setMenuBar(nullptr);
    }
}

void Component::setWindow(Window *windowToUse)
{
    if (window == windowToUse)
    {
        return;
    }

    window = windowToUse;
    for (const auto &c : children)
    {
        c->setWindow(windowToUse);
    }
}

const std::string Component::getComponentName() const
{
    return componentName;
}

void Component::setBounds(const SDL_Rect b)
{
    setBounds(b.x, b.y, b.w, b.h);
}

void Component::setBounds(const int32_t xPosToUse, const int32_t yPosToUse,
                          const int32_t widthToUse, const int32_t heightToUse)
{
    if (xPosToUse == xPos && yPosToUse == yPos && widthToUse == width &&
        heightToUse == height)
    {
        return;
    }

    const auto oldBounds = getLocalBounds();
    const SDL_Rect newBounds = {xPosToUse, yPosToUse, widthToUse, heightToUse};

    xPos = xPosToUse;
    yPos = yPosToUse;
    width = widthToUse;
    height = heightToUse;

    setDirty();

    SDL_Rect unionBounds;
    SDL_GetRectUnion(&oldBounds, &newBounds, &unionBounds);
    const bool occludesOldBounds = SDL_RectsEqual(&unionBounds, &newBounds);

    if (!occludesOldBounds && parent != nullptr)
    {
        parent->setDirty();
    }

    resized();
}

void Component::setSize(const int32_t widthToUse, const int32_t heightToUse)
{
    setBounds(xPos, yPos, widthToUse, heightToUse);
}

void Component::setYPos(const int32_t yPosToUse)
{
    setBounds(xPos, yPosToUse, width, height);
}

void Component::markDirtySelf()
{
    if (!visible)
    {
        return;
    }
    dirty = true;
}

void Component::setDirty()
{
    if (!visible)
    {
        return;
    }

    const SDL_Rect r = getAbsoluteBounds();
    if (window && r.w > 0 && r.h > 0)
    {
        window->getDirtyRects().push_back(r);
    }

    Component *repaintRoot = this;
    while (repaintRoot && !repaintRoot->isOpaque())
    {
        repaintRoot = repaintRoot->parent;
    }

    if (!repaintRoot)
    {
        repaintRoot = this;
    }

    repaintRoot->markDirtySelf();
}

void Component::draw(SDL_Renderer *renderer)
{
    SDL_Rect viewport{};
    SDL_GetRenderViewport(renderer, &viewport);
    draw(renderer, viewport);
}

void Component::draw(SDL_Renderer *renderer, const SDL_Rect &invalidRect)
{
    draw(renderer, invalidRect, false);
}

void Component::draw(SDL_Renderer *renderer, const SDL_Rect &invalidRect,
                     const bool ancestorRepainted)
{
    if (!visible)
    {
        return;
    }
    if (!window)
    {
        return;
    }

    const SDL_Rect absRect = getAbsoluteBounds();

    SDL_Rect parentClipRect;
    SDL_GetRenderViewport(renderer, &parentClipRect);

    SDL_Rect selfClipRect;
    if (!SDL_GetRectIntersection(&parentClipRect, &absRect, &selfClipRect))
    {
        selfClipRect = {0, 0, 0, 0};
    }

    SDL_Rect childClipRect = parentClipRect;
    if (parentClippingEnabled &&
        !SDL_GetRectIntersection(&parentClipRect, &absRect, &childClipRect))
    {
#if DEBUG_DRAW
        printf("Not drawing %s because child clip is empty\n",
               componentName.c_str());
#endif
        return;
    }

    SDL_Rect childInvalidRect;
    if (!SDL_GetRectIntersection(&childClipRect, &invalidRect, &childInvalidRect))
    {
#if DEBUG_DRAW
        printf("Not drawing %s because subtree doesn't intersect invalid rect\n",
               componentName.c_str());
#endif
        return;
    }

    const bool shouldDrawSelf = dirty || ancestorRepainted;
    SDL_Rect selfDrawRect;
    if (shouldDrawSelf &&
        SDL_GetRectIntersection(&selfClipRect, &invalidRect, &selfDrawRect))
    {
        SDL_SetRenderViewport(renderer, &selfDrawRect);

        SDL_Rect localInvalidRect = selfDrawRect;
        localInvalidRect.x -= absRect.x;
        localInvalidRect.y -= absRect.y;
        onDraw(renderer, localInvalidRect);
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
    else if (shouldDrawSelf)
    {
        printf("Not drawing %s because self draw rect is empty\n",
               componentName.c_str());
    }
#endif

    SDL_SetRenderViewport(renderer, &childClipRect);
    for (const auto &c : children)
    {
        c->draw(renderer, childInvalidRect, shouldDrawSelf);
    }

    SDL_SetRenderViewport(renderer, &parentClipRect);
}

void Component::clearDirtyRecursive()
{
    dirty = false;
    for (const auto &c : children)
    {
        c->clearDirtyRecursive();
    }
}

bool Component::handleMouseEvent(const MouseEvent &mouseEvent)
{
    if (!visible)
    {
        return false;
    }

    const float localXf = mouseEvent.mouseXf - xPos;
    const float localYf = mouseEvent.mouseYf - yPos;
    const int localXi = static_cast<int>(std::floor(localXf));
    const int localYi = static_cast<int>(std::floor(localYf));

    const MouseEvent localMouseEvent =
        withNewCoordinates(mouseEvent, localXi, localYi, localXf, localYf);

    for (const auto &c : std::views::reverse(children))
    {
        if (c->handleMouseEvent(localMouseEvent))
        {
            return true;
        }
    }

    const Component *capturingComponent =
        window ? window->getCapturingComponent() : nullptr;

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
                    if (window)
                    {
                        window->setCapturingComponent(nullptr);
                    }
                    return true;
                }
            }
        }
        else if (mouseEvent.type == WHEEL)
        {
            // Wheel events are only handled by components under the cursor.
        }
    }
    else
    {
        if (window && window->getMenuBar() &&
            (window->getMenuBar()->hasMenuOpen() ||
             window->getMenuBar()->shouldOpenSubMenuOnMouseOver()))
        {
            if (!isComponentOrChildOf(this, window->getMenuBar()))
            {
                if (mouseEvent.type == DOWN)
                {
                    window->getMenuBar()->setOpenSubMenuOnMouseOver(false);
                    window->getMenuBar()->hideSubMenus();
                    return true;
                }

                return false;
            }
        }

        if (mouseEvent.type == MOVE)
        {
            if (mouseMove(localMouseEvent))
            {
                return true;
            }
        }
        else if (mouseEvent.type == WHEEL)
        {
            if (mouseWheel(localMouseEvent))
            {
                return true;
            }
        }
        else if (mouseEvent.type == DOWN && mouseEvent.buttonState.left)
        {
            if (mouseDown(localMouseEvent))
            {
                if (window && shouldCaptureMouse())
                {
                    window->setCapturingComponent(this);
                }
                return true;
            }
        }
        else if (mouseEvent.type == UP && mouseEvent.buttonState.left)
        {
            if (mouseUp(localMouseEvent))
            {
                if (window)
                {
                    window->setCapturingComponent(nullptr);
                }
                return true;
            }
        }
    }
    return false;
}

int32_t Component::getWidth() const
{
    return width;
}
int32_t Component::getHeight() const
{
    return height;
}
int32_t Component::getXPos() const
{
    return xPos;
}
int32_t Component::getYPos() const
{
    return yPos;
}

std::pair<int, int> Component::getAbsolutePosition() const
{
    int resultX = getXPos(), resultY = getYPos();
    const Component *p = getParent();
    while (p != nullptr)
    {
        resultX += p->getXPos();
        resultY += p->getYPos();
        p = p->getParent();
    }
    return {resultX, resultY};
}

bool Component::containsAbsoluteCoordinate(const int x, const int y)
{
    if (!visible)
    {
        return false;
    }
    auto [absX, absY] = getAbsolutePosition();
    SDL_Rect rect{absX, absY, width, height};

    if (parentClippingEnabled)
    {
        Component *p = parent;
        while (p != nullptr)
        {
            if (p->parentClippingEnabled)
            {
                auto [px, py] = p->getAbsolutePosition();
                SDL_Rect parentRect{px, py, p->getWidth(), p->getHeight()};
                SDL_Rect intersection;
                if (!SDL_GetRectIntersection(&rect, &parentRect, &intersection))
                {
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

Component *Component::findComponentAt(const int x, const int y)
{
    if (!visible)
    {
        return nullptr;
    }

    for (const auto &c : std::views::reverse(children))
    {
        if (const auto found = c->findComponentAt(x, y); found != nullptr)
        {
            return found;
        }
    }

    if (interceptMouseEnabled && containsAbsoluteCoordinate(x, y))
    {
        return this;
    }

    return nullptr;
}

bool Component::isComponentOrChildOf(Component *c1, Component *c2)
{
    if (c1 == c2)
    {
        return true;
    }

    for (auto &child : c2->children)
    {
        if (isComponentOrChildOf(c1, child.get()))
        {
            return true;
        }
    }

    return false;
}

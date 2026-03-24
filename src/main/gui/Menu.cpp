#include "gui/Menu.hpp"

#include "gui/MenuBar.hpp"
#include "gui/MenuLayoutPlanning.hpp"
#include "gui/MenuPlanning.hpp"
#include "gui/Label.hpp"
#include "gui/Window.hpp"
#include "gui/Helpers.hpp"
#include "gui/Colors.hpp"

#include "gui/text.hpp"

#include "gui/RoundedRect.hpp"
#include "gui/UiScale.hpp"

using namespace cupuacu::gui;

namespace
{
    bool isMenuDebugOverlayEnabled()
    {
        static const bool enabled = []
        {
            const char *value = SDL_getenv("CUPUACU_DEBUG_MENU_BOUNDS");
            return value && value[0] != '\0' && value[0] != '0';
        }();
        return enabled;
    }

    void drawMenuPopupShell(SDL_Renderer *renderer, const SDL_FRect &rect,
                            const float radius, const uint8_t pixelScale,
                            const SDL_Color &fill, const SDL_Color &border)
    {
        const SDL_FRect snappedRect =
            snapRoundedRectToPixelGrid(rect, pixelScale);
        const float snappedRadius =
            snapRoundedRectRadiusToPixelGrid(radius, pixelScale);

        drawRoundedRectPixelPerfect(renderer, snappedRect, snappedRadius, fill,
                                    pixelScale);

        const int x = toRoundedRectGridUnits(snappedRect.x, pixelScale);
        const int y = toRoundedRectGridUnits(snappedRect.y, pixelScale);
        const int width = std::max(
            0, toRoundedRectGridUnits(snappedRect.w, pixelScale));
        const int height = std::max(
            0, toRoundedRectGridUnits(snappedRect.h, pixelScale));
        const int radiusUnits = std::clamp(
            toRoundedRectGridUnits(snappedRadius, pixelScale), 0,
            std::min(width, height) / 2);

        if (width <= 0 || height <= 0)
        {
            return;
        }

        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b,
                               border.a);

        for (int row = 0; row < height; ++row)
        {
            int left = 0;
            int spanWidth = 0;
            if (!getRoundedRectRowSpan(x, width, height, radiusUnits, true, true,
                                       row, left, spanWidth))
            {
                continue;
            }

            const int canvasY = y + row;
            if (row == 0 || row == height - 1)
            {
                fillRoundedRectGridSegment(renderer, left, spanWidth, canvasY,
                                           pixelScale);
                continue;
            }

            fillRoundedRectGridSegment(renderer, left, 1, canvasY, pixelScale);
            fillRoundedRectGridSegment(renderer, left + spanWidth - 1, 1,
                                       canvasY, pixelScale);
        }
    }

    class MenuPopupShell : public Component
    {
    public:
        explicit MenuPopupShell(cupuacu::State *stateToUse)
            : Component(stateToUse, "MenuPopupShell")
        {
            disableParentClipping();
            setInterceptMouseEnabled(false);
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            const uint8_t pixelScale = state ? state->pixelScale : 1;
            const SDL_FRect rect =
                snapRoundedRectToPixelGrid(getLocalBoundsF(), pixelScale);
            const float radius = snapRoundedRectRadiusToPixelGrid(
                scaleUiF(state, 14.0f), pixelScale);
            constexpr SDL_Color fill = {50, 50, 50, 255};
            constexpr SDL_Color outline = {180, 180, 180, 255};
            drawMenuPopupShell(renderer, rect, radius, pixelScale, fill,
                               outline);

            if (isMenuDebugOverlayEnabled())
            {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                Helpers::fillRect(renderer, rect, SDL_Color{255, 0, 255, 70});
                drawMenuPopupShell(renderer, rect, radius, pixelScale,
                                   SDL_Color{0, 0, 0, 0},
                                   SDL_Color{255, 255, 255, 220});
            }
        }
    };

    void requestFullWindowRedraw(cupuacu::gui::Menu *menu)
    {
        if (!menu)
        {
            return;
        }

        if (auto *window = menu->getWindow())
        {
            if (auto *root = window->getRootComponent())
            {
                root->setDirty();
                return;
            }
        }

        menu->setDirty();
    }
}

Menu::Menu(State *state, const std::string &menuNameToUse,
           const std::function<void()> &actionToUse)
    : Component(state, "Menu for " + menuNameToUse), menuName(menuNameToUse),
      action(actionToUse)
{
    disableParentClipping();
    label = emplaceChild<Label>(state);
    label->setInterceptMouseEnabled(false);
    label->setFontSize(state->menuFontSize);
    popupShell = emplaceChild<MenuPopupShell>(state);
    popupShell->setVisible(false);
}

Menu::Menu(State *state,
           const std::function<std::string()> &menuNameGetterToUse,
           const std::function<void()> &actionToUse)
    : Component(state, "Menu"), menuName(""), action(actionToUse),
      menuNameGetter(menuNameGetterToUse)
{
    disableParentClipping();
    label = emplaceChild<Label>(state);
    label->setInterceptMouseEnabled(false);
    label->setFontSize(state->menuFontSize);
    popupShell = emplaceChild<MenuPopupShell>(state);
    popupShell->setVisible(false);
}

void Menu::setIsAvailable(const std::function<bool()> &isAvailableToUse)
{
    isAvailable = isAvailableToUse;
}

std::string Menu::getMenuName() const
{
    if (menuName.empty())
    {
        return menuNameGetter();
    }
    return menuName;
}

bool Menu::isFirstLevel() const
{
    return dynamic_cast<MenuBar *>(getParent()) != nullptr;
}

bool Menu::shouldShowAsSubMenuItem() const
{
    return !getMenuName().empty();
}

bool Menu::isEffectivelyAvailable() const
{
    if (!isAvailable())
    {
        return false;
    }

    const auto *parentMenu = dynamic_cast<const Menu *>(getParent());
    if (!parentMenu)
    {
        return true;
    }

    return parentMenu->isEffectivelyAvailable();
}

void Menu::resized()
{
    label->setBounds(0, 0, getWidth(), getHeight());

    if (isFirstLevel())
    {
        label->setCenterHorizontally(true);
    }
    else
    {
        label->setMargin(32);
    }
}

void Menu::showSubMenus()
{
    if (subMenus.empty())
    {
        return;
    }

    const int nestedHorizontalOverlap = scaleUi(state, 10.0f);
    const bool firstLevel = isFirstLevel();
    const int itemHeight = menuItemHeight(state);
    const int subMenuHorizontalMargin = scaleUi(state, 64.0f);
    std::vector<int> textWidths;
    std::vector<bool> shouldShow;
    textWidths.reserve(subMenus.size());
    shouldShow.reserve(subMenus.size());
    for (const auto &subMenu : subMenus)
    {
        const bool show = subMenu->shouldShowAsSubMenuItem();
        shouldShow.push_back(show);
        if (!show)
        {
            textWidths.push_back(0);
            continue;
        }

        const auto subMenuName = subMenu->getMenuName();
        auto [tw, th] = measureText(
            subMenuName, scaleFontPointSize(state, state->menuFontSize));
        textWidths.push_back(tw);
    }

    const auto layoutPlan = planMenuSubMenuLayout(
        firstLevel, getWidth(), getHeight(), itemHeight,
        nestedHorizontalOverlap, subMenuHorizontalMargin, textWidths,
        shouldShow);

    int shellX = 0;
    int shellY = 0;
    int shellWidth = 0;
    int shellHeight = 0;
    bool hasVisibleShell = false;

    for (std::size_t i = 0; i < subMenus.size(); ++i)
    {
        auto *subMenu = subMenus[i];
        const auto &item = layoutPlan[i];
        subMenu->setVisible(item.visible);
        if (!item.visible)
        {
            continue;
        }
        subMenu->setBounds(item.x, item.y, item.width, item.height);

        if (!hasVisibleShell)
        {
            shellX = item.x;
            shellY = item.y;
            shellWidth = item.width;
            shellHeight = item.height;
            hasVisibleShell = true;
        }
        else
        {
            const int shellRight = std::max(shellX + shellWidth,
                                            item.x + item.width);
            const int shellBottom = std::max(shellY + shellHeight,
                                             item.y + item.height);
            shellX = std::min(shellX, item.x);
            shellY = std::min(shellY, item.y);
            shellWidth = shellRight - shellX;
            shellHeight = shellBottom - shellY;
        }
    }

    if (popupShell)
    {
        popupShell->setVisible(hasVisibleShell);
        if (hasVisibleShell)
        {
            const int borderInset = std::max(1, scaleUi(state, 1.0f));
            popupShell->setBounds(shellX - borderInset, shellY - borderInset,
                                  shellWidth + borderInset * 2,
                                  shellHeight + borderInset * 2);
        }
    }

    if (!firstLevel)
    {
        bringToFront();
    }

    currentlyOpen = true;
    requestFullWindowRedraw(this);
}

void Menu::hideSubMenus()
{
    if (popupShell)
    {
        popupShell->setVisible(false);
    }

    for (const auto &subMenu : subMenus)
    {
        subMenu->hideSubMenus();
        subMenu->setVisible(false);
    }

    currentlyOpen = false;
    requestFullWindowRedraw(this);
}

void Menu::onDraw(SDL_Renderer *renderer)
{
    const auto radius = snapRoundedRectRadiusToPixelGrid(
        scaleUiF(state, 14.0f), state ? state->pixelScale : 1);
    const auto rect = snapRoundedRectToPixelGrid(
        getLocalBoundsF(), state ? state->pixelScale : 1);
    const bool available = isEffectivelyAvailable();

    label->setOpacity(available ? 255 : 128);

    label->setText(getMenuName());

    if (isFirstLevel())
    {
        constexpr SDL_Color bg = Colors::background;
        Helpers::setRenderDrawColor(renderer, bg);

        SDL_RenderFillRect(renderer, &rect);

        if (currentlyOpen)
        {
            constexpr SDL_Color col1 = {70, 70, 70, 255};
            drawRoundedRectBordered(renderer, rect, radius, Colors::border,
                                    col1, state ? state->pixelScale : 1);
        }

        return;
    }

    const auto parentMenu = dynamic_cast<Menu *>(getParent());
    Menu *firstVisibleSibling = nullptr;
    Menu *lastVisibleSibling = nullptr;
    for (auto *sibling : parentMenu->subMenus)
    {
        if (!sibling->shouldShowAsSubMenuItem() || !sibling->isVisible())
        {
            continue;
        }
        if (!firstVisibleSibling)
        {
            firstVisibleSibling = sibling;
        }
        lastVisibleSibling = sibling;
    }
    const bool isFirst = firstVisibleSibling == this;
    const bool isLast = lastVisibleSibling == this;

    auto rectShrunk = rect;

    const float shrink = scaleUiF(state, 6.0f);
    rectShrunk.x += shrink;
    rectShrunk.y += shrink;
    rectShrunk.w -= shrink * 2;
    rectShrunk.h -= shrink * 2;
    rectShrunk = snapRoundedRectToPixelGrid(
        rectShrunk, state ? state->pixelScale : 1);

    if (isMouseOver() && available)
    {
        constexpr SDL_Color col2 = {60, 60, 200, 255};
        if (isFirst && isLast)
        {
            drawRoundedRectPixelPerfect(renderer, rectShrunk, radius, col2,
                                        state ? state->pixelScale : 1);
        }
        else if (isFirst)
        {
            drawTopRoundedRectPixelPerfect(renderer, rectShrunk, radius, col2,
                                           state ? state->pixelScale : 1);
        }
        else if (isLast)
        {
            drawBottomRoundedRectPixelPerfect(
                renderer, rectShrunk, radius, col2,
                state ? state->pixelScale : 1);
        }
        else
        {
            SDL_RenderFillRect(renderer, &rectShrunk);
        }
    }

    if (isMenuDebugOverlayEnabled())
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        const SDL_Color debugColor =
            isFirst ? SDL_Color{255, 80, 80, 140}
                    : (isLast ? SDL_Color{80, 120, 255, 140}
                              : SDL_Color{80, 255, 120, 140});
        Helpers::fillRect(renderer, rect, debugColor);
        if (isMouseOver() && available)
        {
            Helpers::fillRect(renderer, rectShrunk,
                              SDL_Color{255, 255, 0, 120});
        }
    }
}

bool Menu::mouseDown(const MouseEvent &e)
{
    const auto plan =
        planMenuMouseDown(subMenus.empty() == false, currentlyOpen,
                          isEffectivelyAvailable());
    if (const auto window = getWindow(); window && window->getMenuBar())
    {
        if (plan.hideMenuBarSubMenus)
        {
            window->getMenuBar()->hideSubMenus();
            if (const auto root = window->getRootComponent())
            {
                root->setDirty();
            }
        }
    }
    if (plan.hideSelfSubMenus)
    {
        hideSubMenus();
    }
    if (plan.showSelfSubMenus)
    {
        showSubMenus();
    }
    if (plan.invokeAction && action)
    {
        action();
    }
    return true;
}

bool Menu::mouseUp(const MouseEvent &e)
{
    return true;
}

void Menu::mouseLeave()
{
    setDirty();

    const auto window = getWindow();
    if (!window || !window->getMenuBar())
    {
        return;
    }

    auto *componentUnderMouse = window->getComponentUnderMouse();
    if (dynamic_cast<Menu *>(componentUnderMouse) == nullptr)
    {
        const bool componentUnderMouseIsInMenuBarTree =
            componentUnderMouse &&
            Component::isComponentOrChildOf(componentUnderMouse,
                                            window->getMenuBar());
        const auto plan = planMenuMouseLeave(
            componentUnderMouseIsInMenuBarTree, window->getMenuBar()->hasMenuOpen());
        if (plan.hideMenuBarSubMenus)
        {
            window->getMenuBar()->hideSubMenus();
        }
        if (plan.enableOpenOnHover)
        {
            window->getMenuBar()->setOpenSubMenuOnMouseOver(true);
        }
    }
}

void Menu::mouseEnter()
{
    const auto window = getWindow();
    if (!window || !window->getMenuBar())
    {
        return;
    }

    if (!isFirstLevel())
    {
        if (const auto *parentMenu = dynamic_cast<Menu *>(getParent()))
        {
            for (auto *siblingMenu : parentMenu->subMenus)
            {
                if (siblingMenu != this)
                {
                    siblingMenu->hideSubMenus();
                }
            }
        }

        if (!subMenus.empty() && isEffectivelyAvailable())
        {
            showSubMenus();
        }
        setDirty();
        return;
    }

    const auto plan = planMenuMouseEnter(
        !subMenus.empty(),
        window->getMenuBar()->getOpenMenu() != nullptr &&
            window->getMenuBar()->getOpenMenu() != this,
        window->getMenuBar()->shouldOpenSubMenuOnMouseOver());
    if (plan.hideMenuBarSubMenus)
    {
        window->getMenuBar()->hideSubMenus();
    }
    if (plan.showSelfSubMenus)
    {
        showSubMenus();
    }

    setDirty();
}

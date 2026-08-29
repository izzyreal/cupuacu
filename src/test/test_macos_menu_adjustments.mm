#import <Cocoa/Cocoa.h>

#include <catch2/catch_test_macros.hpp>

#include "TestPaths.hpp"
#include "TestSdlTtfGuard.hpp"
#include "gui/AboutWindow.hpp"
#include "platform/macos/MenuAdjustments.hpp"

#include <SDL3/SDL.h>

#include <string>

namespace
{
    void installStandardApplicationMenu()
    {
        [NSApplication sharedApplication];

        NSMenu *mainMenu = [[NSMenu alloc] init];
        NSMenu *applicationMenu = [[NSMenu alloc] init];
        [applicationMenu
            addItemWithTitle:@"About SDL Application"
                      action:@selector(orderFrontStandardAboutPanel:)
               keyEquivalent:@""];

        NSMenuItem *applicationMenuItem =
            [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
        [applicationMenuItem setSubmenu:applicationMenu];
        [mainMenu addItem:applicationMenuItem];
        [NSApp setMainMenu:mainMenu];

        [applicationMenuItem release];
        [applicationMenu release];
        [mainMenu release];
    }

    NSMenuItem *findApplicationAboutItem()
    {
        NSMenu *mainMenu = [NSApp mainMenu];
        if (mainMenu == nil || [mainMenu numberOfItems] == 0)
        {
            return nil;
        }

        NSMenu *applicationMenu = [[mainMenu itemAtIndex:0] submenu];
        for (NSMenuItem *item in [applicationMenu itemArray])
        {
            if ([[item title] isEqualToString:@"About Cupuacu"])
            {
                return item;
            }
        }
        return nil;
    }
} // namespace

TEST_CASE("macOS application About item opens the bespoke singleton window",
          "[gui][macos]")
{
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0)
    {
        bool initialized = SDL_Init(SDL_INIT_VIDEO);
        const std::string nativeDriverError = SDL_GetError();
        if (!initialized)
        {
            REQUIRE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
            REQUIRE(SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software"));
            initialized = SDL_Init(SDL_INIT_VIDEO);
        }
        INFO("Native driver error: " << nativeDriverError);
        INFO("Fallback driver error: " << SDL_GetError());
        REQUIRE(initialized);
    }
    cupuacu::test::ensureSdlTtfInitialized();
    cupuacu::test::StateWithTestPaths state{};

    [NSApplication sharedApplication];
    NSMenu *originalMainMenu = [[NSApp mainMenu] retain];
    installStandardApplicationMenu();
    cupuacu::platform::macos::configureApplicationMenu(&state);
    NSMenuItem *aboutItem = findApplicationAboutItem();
    REQUIRE(aboutItem != nil);
    REQUIRE([aboutItem action] == NSSelectorFromString(@"showAboutWindow:"));
    REQUIRE([aboutItem action] != @selector(orderFrontStandardAboutPanel:));

    REQUIRE([NSApp sendAction:[aboutItem action]
                           to:[aboutItem target]
                         from:aboutItem]);
    REQUIRE(state.aboutWindow != nullptr);
    REQUIRE(state.aboutWindow->isOpen());

    auto *firstAboutWindow = state.aboutWindow.get();
    REQUIRE([NSApp sendAction:[aboutItem action]
                           to:[aboutItem target]
                         from:aboutItem]);
    REQUIRE(state.aboutWindow.get() == firstAboutWindow);

    state.aboutWindow.reset();
    cupuacu::platform::macos::clearApplicationMenuState();
    [NSApp setMainMenu:originalMainMenu];
    [originalMainMenu release];
}

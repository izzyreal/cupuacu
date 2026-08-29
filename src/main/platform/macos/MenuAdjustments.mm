#import <Cocoa/Cocoa.h>

#include "MenuAdjustments.hpp"
#include "gui/AboutWindow.hpp"

namespace
{
    cupuacu::State *applicationState = nullptr;
}

@interface CupuacuApplicationMenuTarget : NSObject
- (void)showAboutWindow:(id)sender;
@end

@implementation CupuacuApplicationMenuTarget
- (void)showAboutWindow:(id)sender
{
    (void)sender;
    cupuacu::gui::showAboutWindow(applicationState);
}
@end

namespace
{
    CupuacuApplicationMenuTarget *applicationMenuTarget()
    {
        static CupuacuApplicationMenuTarget *target =
            [[CupuacuApplicationMenuTarget alloc] init];
        return target;
    }

    void redirectAboutMenuItem()
    {
        NSMenu *mainMenu = [NSApp mainMenu];
        if (mainMenu == nil || [mainMenu numberOfItems] == 0)
        {
            return;
        }

        NSMenu *applicationMenu = [[mainMenu itemAtIndex:0] submenu];
        if (applicationMenu == nil)
        {
            return;
        }

        const SEL standardAboutAction =
            @selector(orderFrontStandardAboutPanel:);
        const SEL cupuacuAboutAction = @selector(showAboutWindow:);
        for (NSMenuItem *item in [applicationMenu itemArray])
        {
            if ([item action] == standardAboutAction ||
                [item action] == cupuacuAboutAction)
            {
                [item setTitle:@"About Cupuacu"];
                [item setTarget:applicationMenuTarget()];
                [item setAction:cupuacuAboutAction];
                return;
            }
        }
    }

    void clearWindowCloseShortcut()
    {
        NSMenu *windowsMenu = [NSApp windowsMenu];
        if (windowsMenu == nil)
        {
            return;
        }

        for (NSMenuItem *item in [windowsMenu itemArray])
        {
            if ([item action] == @selector(performClose:))
            {
                [item setKeyEquivalent:@""];
                [item setKeyEquivalentModifierMask:0];
                return;
            }
        }
    }
} // namespace

namespace cupuacu::platform::macos
{
    void configureApplicationMenu(State *state)
    {
        @autoreleasepool
        {
            applicationState = state;
            redirectAboutMenuItem();
            clearWindowCloseShortcut();
        }
    }

    void clearApplicationMenuState()
    {
        applicationState = nullptr;
    }
} // namespace cupuacu::platform::macos

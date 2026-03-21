#import <Cocoa/Cocoa.h>

#include "MenuAdjustments.hpp"

namespace cupuacu::platform::macos
{
    void clearWindowCloseShortcut()
    {
        @autoreleasepool
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
    }
}

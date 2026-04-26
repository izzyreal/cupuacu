#include "PlatformSaveFileDialog.hpp"

#import <Cocoa/Cocoa.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>

namespace
{
    void reactivateAfterDialog()
    {
        for (NSRunningApplication *app in
             [NSRunningApplication runningApplicationsWithBundleIdentifier:
                                       @"com.apple.dock"])
        {
            [app activateWithOptions:0];
            break;
        }
        [NSApp activateIgnoringOtherApps:YES];
    }

    NSWindow *nsWindowFor(SDL_Window *window)
    {
        if (!window)
        {
            return nil;
        }

        return (__bridge NSWindow *)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    }
} // namespace

bool cupuacu::actions::showPlatformSaveFileDialog(
    SDL_DialogFileCallback callback, void *userdata, SDL_Window *parentWindow,
    const std::string &defaultLocation)
{
    if (!callback)
    {
        return false;
    }

    NSSavePanel *panel = [NSSavePanel savePanel];
    const auto path = std::filesystem::path(defaultLocation);
    const auto directory = path.has_parent_path() ? path.parent_path() : path;
    const auto filename = path.has_filename() ? path.filename().string()
                                              : std::string{};

    if (!directory.empty())
    {
        NSString *directoryString =
            [NSString stringWithUTF8String:directory.string().c_str()];
        [panel setDirectoryURL:[NSURL fileURLWithPath:directoryString]];
    }
    if (!filename.empty())
    {
        [panel setNameFieldStringValue:
                   [NSString stringWithUTF8String:filename.c_str()]];
    }

    auto completion = ^(NSInteger result) {
      if (result == NSModalResponseOK)
      {
          const std::string selectedPath =
              [[panel.URL path] UTF8String] ? [[panel.URL path] UTF8String] : "";
          const char *files[2] = {selectedPath.c_str(), nullptr};
          callback(userdata, files, -1);
      }
      else if (result == NSModalResponseCancel)
      {
          const char *files[1] = {nullptr};
          callback(userdata, files, -1);
      }
      reactivateAfterDialog();
    };

    if (NSWindow *parent = nsWindowFor(parentWindow))
    {
        [panel beginSheetModalForWindow:parent completionHandler:completion];
    }
    else
    {
        [panel beginWithCompletionHandler:completion];
    }
    return true;
}

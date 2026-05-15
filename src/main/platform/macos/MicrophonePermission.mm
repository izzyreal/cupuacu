#include "MicrophonePermission.hpp"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

namespace cupuacu::platform::macos
{
    namespace
    {
        bool &microphoneAccessOverrideEnabled()
        {
            static bool enabled = false;
            return enabled;
        }

        bool &microphoneAccessOverrideValue()
        {
            static bool granted = false;
            return granted;
        }
    } // namespace

    bool ensureMicrophoneAccess()
    {
        if (microphoneAccessOverrideEnabled())
        {
            return microphoneAccessOverrideValue();
        }

        const AVAuthorizationStatus status =
            [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
        switch (status)
        {
            case AVAuthorizationStatusAuthorized:
                return true;
            case AVAuthorizationStatusDenied:
            case AVAuthorizationStatusRestricted:
                return false;
            case AVAuthorizationStatusNotDetermined:
                break;
        }

        __block bool granted = false;
        __block bool finished = false;
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL allowed)
                                 {
                                     granted = allowed;
                                     finished = true;
                                 }];

        if ([NSThread isMainThread])
        {
            while (!finished)
            {
                @autoreleasepool
                {
                    [[NSRunLoop currentRunLoop]
                        runMode:NSDefaultRunLoopMode
                     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
                }
            }
        }
        else
        {
            while (!finished)
            {
                [NSThread sleepForTimeInterval:0.01];
            }
        }

        return granted;
    }

    void setMicrophoneAccessOverrideForTesting(const bool granted)
    {
        microphoneAccessOverrideValue() = granted;
        microphoneAccessOverrideEnabled() = true;
    }

    void resetMicrophoneAccessOverrideForTesting()
    {
        microphoneAccessOverrideEnabled() = false;
    }
} // namespace cupuacu::platform::macos

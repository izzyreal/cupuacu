#include "MicrophonePermission.hpp"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

namespace cupuacu::platform::macos
{
    bool ensureMicrophoneAccess()
    {
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
} // namespace cupuacu::platform::macos

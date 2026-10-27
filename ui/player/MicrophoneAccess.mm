#include "MicrophoneAccess.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#import <AVFoundation/AVFoundation.h>

MicrophoneAccess microphoneAccess()
{
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
    case AVAuthorizationStatusAuthorized:
        return MicrophoneAccess::Granted;
    case AVAuthorizationStatusNotDetermined:
        return MicrophoneAccess::Undetermined;
    case AVAuthorizationStatusDenied:
    case AVAuthorizationStatusRestricted:
        break;
    }
    return MicrophoneAccess::Denied;
}

void requestMicrophoneAccess(QObject* context, std::function<void(bool granted)> done)
{
    QPointer<QObject> guard(context);
    auto callback = std::make_shared<std::function<void(bool)>>(std::move(done));
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL granted) {
                                 // On an arbitrary queue: hop to the main thread, where
                                 // the guard can be checked safely.
                                 QMetaObject::invokeMethod(
                                     QCoreApplication::instance(), [guard, callback, granted] {
                                         if (guard)
                                             (*callback)(granted);
                                     },
                                     Qt::QueuedConnection);
                             }];
}

/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/microphonepermission_mac.mm
  *
  *  macOS implementation: AVFoundation directly, NOT Qt's QMicrophonePermission.
  *
  *  WHY NOT QT. QCoreApplication::requestPermission(QMicrophonePermission{}) ends in
  *  QDarwinPermissionPlugin::requestPermission(), which first looks NSMicrophoneUsageDescription up
  *  in NSBundle.mainBundle.infoDictionary and answers Denied when it is not there -- without ever
  *  asking the system, so no dialog is shown and the system is left with no answer at all. That
  *  dictionary is the Info.plist of a bundle; it does not see the plist that a plain executable
  *  carries in its __TEXT,__info_plist section, which is what the system itself reads. So every
  *  build of this application that is not a .app bundle could never obtain the microphone through
  *  Qt, however the application was started and whatever the user answered.
  *
  *  QCoreApplication::checkPermission() has no such check -- it returns the AVFoundation status as
  *  it is -- which is why an application that was allowed the microphone by other means (the
  *  microphone test of the audio settings opens the device through CoreAudio) worked after a
  *  restart while the chat page kept refusing.
  *
  *  Qt's own FIXME in that file says the same thing: what needs the usage description is the
  *  responsible process, not NSBundle.mainBundle.
  */

#import <AVFoundation/AVFoundation.h>

#include <QDesktopServices>
#include <QString>
#include <QUrl>

#include <hatnuise/microphonepermission.h>

HATN_UISE_NAMESPACE_BEGIN

namespace {

/**
 * @brief Call `done` with `granted` on the GUI thread.
 *
 * AVFoundation runs its completion handler on a queue of its own. The main dispatch queue is drained
 * by the Qt event loop on the GUI thread, so a block posted to it lands where the callers of this
 * file expect to be. Once that loop is gone the block simply never runs, which is what should happen
 * to an answer that arrives while the application is shutting down.
 *
 * `done` is copied into the block on purpose: copying an Objective-C block copies the std::function
 * with it, so nothing here outlives the block.
 */
void postToGui(std::function<void(bool)> done, bool granted)
{
    if (!done)
    {
        return;
    }

    dispatch_async(dispatch_get_main_queue(),^{
        done(granted);
    });
}

} // anonymous namespace

//---------------------------------------------------------------

bool microphonePermissionGranted()
{
    return [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]==AVAuthorizationStatusAuthorized;
}

//---------------------------------------------------------------

void requestMicrophonePermission(std::function<void(bool)> done)
{
    const auto status=[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (status==AVAuthorizationStatusAuthorized)
    {
        postToGui(std::move(done),true);
        return;
    }
    if (status==AVAuthorizationStatusRestricted)
    {
        // Not the user's choice but a policy of the device (managed device, screen time): there is no
        // dialog for it, and the settings of the user cannot change it either.
        postToGui(std::move(done),false);
        return;
    }

    // Both "no answer yet" and "denied" are asked for. The first shows the system dialog. The second
    // returns at once and silently, but it returns what the SYSTEM says rather than the status this
    // process has cached, and asking refreshes that cache -- which is how a permission that the user
    // has just given in the system settings is seen without restarting the application.
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted)
    {
        postToGui(done,granted==YES);
    }];
}

//---------------------------------------------------------------

bool canOpenMicrophonePrivacySettings() noexcept
{
    return true;
}

//---------------------------------------------------------------

void openMicrophonePrivacySettings()
{
    // Privacy & Security -> Microphone. The same scheme the notification settings are opened with,
    // see whitemdesktop/src/osnotificationsettings_mac.mm.
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"))
    );
}

//---------------------------------------------------------------

HATN_UISE_NAMESPACE_END

/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/microphonepermission.h
  *
  *  Whether the operating system lets this application open a microphone, and how to ask for it.
  *
  */

/****************************************************************************/

#ifndef HATNUISEMICROPHONEPERMISSION_H
#define HATNUISEMICROPHONEPERMISSION_H

#include <functional>

#include <hatnuise/hatnuise.h>

HATN_UISE_NAMESPACE_BEGIN

/**
 * @brief Whether the user has allowed the application to use the microphone. Never asks, never blocks.
 *
 * Where the platform has no such permission this is true.
 *
 * On macOS the answer comes from a status this process has cached: a permission that was changed
 * elsewhere while the application runs is seen only after requestMicrophonePermission(), which goes
 * back to the system for it.
 */
HATN_UISE_EXPORT bool microphonePermissionGranted();

/**
 * @brief Ask the system for the microphone and tell the outcome.
 *
 * `done` is called on the GUI thread, at most once, and never from inside this call: even an answer
 * that is already known is posted. That is deliberate — a caller may be tearing down the widget it
 * asked from, and a callback that lands in the middle of that is a trap.
 *
 * On macOS the system dialog appears here, and only while the system has no answer yet; afterwards
 * the answer comes back without one. The system is asked even when the cached status says denied,
 * because that is what refreshes it: a permission granted in System Settings, or through the
 * microphone test of the audio settings, is then seen without restarting the application.
 *
 * The application must carry NSMicrophoneUsageDescription, either in the Info.plist of its bundle or
 * in the __TEXT,__info_plist section of the executable, or the system ends the process here.
 */
HATN_UISE_EXPORT void requestMicrophonePermission(std::function<void(bool)> done);

//! Whether openMicrophonePrivacySettings() leads anywhere on this platform.
HATN_UISE_EXPORT bool canOpenMicrophonePrivacySettings() noexcept;

/**
 * @brief Open the page of the system settings where the user can allow the microphone.
 *
 * Does nothing where canOpenMicrophonePrivacySettings() is false.
 */
HATN_UISE_EXPORT void openMicrophonePrivacySettings();

HATN_UISE_NAMESPACE_END

#endif // HATNUISEMICROPHONEPERMISSION_H

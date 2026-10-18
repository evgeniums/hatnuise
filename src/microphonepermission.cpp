/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/microphonepermission.cpp
  *
  *  Implementation for every platform but macOS, which has one of its own in
  *  microphonepermission_mac.mm and does not compile this file.
  *
  *  Here the permission goes through Qt. Windows and Linux have no permission backend in Qt at all,
  *  so it answers Granted and nothing is ever asked; the code below is what keeps the answer right
  *  should Qt grow one.
  *
  *  WINDOWS. Qt's "always granted" is wrong there: Settings > Privacy > Microphone can switch the
  *  microphone off for the whole machine, for the user, or for desktop applications alone, and a
  *  desktop application that is refused gets no error from the audio stack worth the name -- the
  *  device either fails to open or delivers digital silence. A desktop application has no API to ask
  *  (there is no dialog for it, the user has to flip the switch), but the switches are readable: they
  *  are the "consent store" of the capability access manager, plus the group policy. windowsMicrophoneAllowed()
  *  below reads them; a value that is absent means nobody ever denied anything, that is allowed.
  */

#include <initializer_list>
#include <utility>

#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <QCoreApplication>
#include <QDesktopServices>
#include <QString>
#include <QTimer>
#include <QUrl>

#if QT_VERSION >= QT_VERSION_CHECK(6,5,0)
#include <QPermissions>
#endif

#include <hatnuise/microphonepermission.h>

HATN_UISE_NAMESPACE_BEGIN

namespace {

//! Call `done` with `granted` on the GUI thread, never from inside the caller. See the header.
void postToGui(std::function<void(bool)> done, bool granted)
{
    if (!done)
    {
        return;
    }
    auto* app=QCoreApplication::instance();
    if (app==nullptr)
    {
        return;
    }

    QTimer::singleShot(0,app,
        [done=std::move(done),granted]()
        {
            done(granted);
        }
    );
}

#ifdef Q_OS_WIN

//! Whether the REG_SZ value `name` under `subKey` of `root` exists and equals "Deny".
bool consentValueIsDeny(HKEY root, const wchar_t* subKey, const wchar_t* name)
{
    wchar_t buf[32]={};
    DWORD size=sizeof(buf)-sizeof(wchar_t); // keep room for the terminator RegGetValueW may not add
    const LONG rc=RegGetValueW(root,subKey,name,RRF_RT_REG_SZ|RRF_SUBKEY_WOW64_64KEY,nullptr,buf,&size);
    return rc==ERROR_SUCCESS && lstrcmpiW(buf,L"Deny")==0;
}

/**
 * @brief Whether Windows lets desktop applications use the microphone.
 *
 * Denied when any of these says so:
 *  - "Microphone access" (the machine-wide and the per-user switch),
 *  - "Let desktop apps access your microphone" (the NonPackaged key, the one that matters to a
 *    plain Win32 process like this one),
 *  - the group policy "Let Windows apps access the microphone" set to "Force Deny".
 */
bool windowsMicrophoneAllowed()
{
    static const wchar_t* const kConsent=
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone";
    static const wchar_t* const kConsentDesktop=
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone\\NonPackaged";
    static const wchar_t* const kPolicy=L"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy";

    for (HKEY root : {HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER})
    {
        if (consentValueIsDeny(root,kConsent,L"Value") || consentValueIsDeny(root,kConsentDesktop,L"Value"))
        {
            return false;
        }
    }

    DWORD policy=0;
    DWORD size=sizeof(policy);
    const LONG rc=RegGetValueW(HKEY_LOCAL_MACHINE,kPolicy,L"LetAppsAccessMicrophone",
                               RRF_RT_REG_DWORD|RRF_SUBKEY_WOW64_64KEY,nullptr,&policy,&size);
    if (rc==ERROR_SUCCESS && policy==2)
    {
        return false;
    }

    return true;
}

#endif // Q_OS_WIN

} // anonymous namespace

//---------------------------------------------------------------

bool microphonePermissionGranted()
{
#ifdef Q_OS_WIN
    if (!windowsMicrophoneAllowed())
    {
        return false;
    }
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6,5,0)
    auto* app=QCoreApplication::instance();
    if (app==nullptr)
    {
        return false;
    }
    return app->checkPermission(QMicrophonePermission{})==Qt::PermissionStatus::Granted;
#else
    // Nothing to ask before Qt 6.5, and no platform here asks anyway.
    return true;
#endif
}

//---------------------------------------------------------------

void requestMicrophonePermission(std::function<void(bool)> done)
{
#ifdef Q_OS_WIN
    if (!windowsMicrophoneAllowed())
    {
        // Nothing to ask: there is no dialog for a desktop application, only the switch in Settings.
        postToGui(std::move(done),false);
        return;
    }
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6,5,0)
    auto* app=QCoreApplication::instance();
    if (app==nullptr)
    {
        return;
    }

    QMicrophonePermission permission;
    switch (app->checkPermission(permission))
    {
        case Qt::PermissionStatus::Granted:
            postToGui(std::move(done),true);
            return;

        case Qt::PermissionStatus::Denied:
            // Asking again does nothing here: only the settings of the system can change it.
            postToGui(std::move(done),false);
            return;

        case Qt::PermissionStatus::Undetermined:
            break;
    }

    app->requestPermission(permission,app,
        [done](const QPermission& result)
        {
            // Qt may answer from inside requestPermission(), so post even this one.
            postToGui(done,result.status()==Qt::PermissionStatus::Granted);
        }
    );
#else
    postToGui(std::move(done),true);
#endif
}

//---------------------------------------------------------------

bool canOpenMicrophonePrivacySettings() noexcept
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

//---------------------------------------------------------------

void openMicrophonePrivacySettings()
{
#ifdef Q_OS_WIN
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:privacy-microphone")));
#endif
}

//---------------------------------------------------------------

HATN_UISE_NAMESPACE_END

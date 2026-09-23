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
  */

#include <utility>

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

} // anonymous namespace

//---------------------------------------------------------------

bool microphonePermissionGranted()
{
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

/*
    Copyright (c) 2024 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    {{LICENSE}}
*/

/****************************************************************************/
/*
    
*/
/** @file hatnuise/desktoperror.h
  */

/****************************************************************************/

#ifndef HATNUISEDESKTOPERROR_H
#define HATNUISEDESKTOPERROR_H

#include <hatn/common/error.h>

#include <hatnuise/hatnuise.h>

#include <QString>

HATN_UISE_NAMESPACE_BEGIN

struct desktopErrorT
{
    QString operator()(const HATN_COMMON::Error& ec, const QString& message={}) const
    {
        auto msg=QString::fromStdString(ec.message());

        if (message.isEmpty())
        {
            return msg;
        }

        QString format{"%1: %2"};
        msg=format.arg(message,msg);
        return msg;
    }
};
constexpr desktopErrorT desktopError{};

HATN_UISE_NAMESPACE_END

#endif // HATNUISEDESKTOPERROR_H

/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/uisehatnbridge.сpp
  *
  */

#include <QPointer>
#include <QApplication>

#include <hatnuise/uisehatnbridge.h>

HATN_UISE_NAMESPACE_BEGIN

//---------------------------------------------------------------

UiseHatnBridge::UiseHatnBridge(HATN_CLIENTAPP_NAMESPACE::Dispatcher* dispatcher) : m_dispatcher(dispatcher)
{
    moveToThread(qApp->thread());
}

//---------------------------------------------------------------

void UiseHatnBridge::exec(
        const std::string& service,
        const std::string& method,
        HATN_CLIENTAPP_NAMESPACE::Request request,
        HATN_CLIENTAPP_NAMESPACE::Callback callback,
        QObject* callbackContext
    )
{
    QPointer<QObject> ctx=callbackContext;
    if (ctx.isNull())
    {
        ctx=this;
    }

    auto cb=[callback=std::move(callback),ctx](const HATN_NAMESPACE::Error& ec, HATN_CLIENTAPP_NAMESPACE::Response response)
    {
        if (!ctx)
        {
            return;
        }

        auto handler=[ec,response=std::move(response),callback=std::move(callback)]()
        {
            callback(ec,std::move(response));
        };

        QMetaObject::invokeMethod(ctx,std::move(handler),Qt::QueuedConnection);
    };

    m_dispatcher->exec(
        service,
        method,
        std::move(request),
        std::move(cb)
    );
}

//---------------------------------------------------------------

HATN_UISE_NAMESPACE_END

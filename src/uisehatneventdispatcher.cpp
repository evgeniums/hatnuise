/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/uisehatneventdispatcher.сpp
  *
  */

#include <QPointer>
#include <QApplication>

#include <hatn/app//appenv.h>

#include <hatnuise/uisehatneventdispatcher.h>

HATN_UISE_NAMESPACE_BEGIN

//---------------------------------------------------------------

UiseHatnEventDispatcher::UiseHatnEventDispatcher(HATN_CLIENTAPP_NAMESPACE::EventDispatcher* dispatcher) : m_dispatcher(dispatcher)
{
    moveToThread(qApp->thread());
}

//---------------------------------------------------------------

size_t UiseHatnEventDispatcher::subscribe(
        HATN_CLIENTAPP_NAMESPACE::EventHandler handler,
        HATN_CLIENTAPP_NAMESPACE::EventKey key,
        QObject* handlerContext
    )
{
    QPointer<QObject> hCtx=handlerContext;
    if (hCtx.isNull())
    {
        hCtx=this;
    }

    auto proxyHandler=[handler=std::move(handler),hCtx](HATN_COMMON_NAMESPACE::SharedPtr<HATN_APP_NAMESPACE::AppEnv> env,
                                                            HATN_COMMON_NAMESPACE::SharedPtr<HATN_CLIENTAPP_NAMESPACE::Context> ctx,
                                                            std::shared_ptr<HATN_CLIENTAPP_NAMESPACE::Event> event)
    {
        if (!hCtx)
        {
            return;
        }

        auto queuedHandler=[handler=std::move(handler),env=std::move(env),ctx=std::move(ctx),event=std::move(event)]()
        {
            handler(std::move(env),std::move(ctx),std::move(event));
        };

        QMetaObject::invokeMethod(hCtx,std::move(queuedHandler),Qt::QueuedConnection);
    };

    auto id=m_dispatcher->subscribe(
        std::move(proxyHandler),
        std::move(key)
    );

    return id;
}

//---------------------------------------------------------------

void UiseHatnEventDispatcher::unsubscribe(size_t id)
{
    m_dispatcher->unsubscribe(id);
}

//---------------------------------------------------------------

HATN_UISE_NAMESPACE_END

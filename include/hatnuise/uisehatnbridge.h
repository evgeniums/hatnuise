/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/uisehatnbridge.h
  *
  */

/****************************************************************************/

#ifndef HATNUISEHATNBRIDGE_H
#define HATNUISEHATNBRIDGE_H

#include <QObject>
#include <QPointer>

#include <boost/hana.hpp>

#include <hatn/clientapp/clientbridge.h>
#include <hatnuise/hatnuise.h>

HATN_UISE_NAMESPACE_BEGIN

class HATN_UISE_EXPORT UiseHatnBridge : public QObject
{
    Q_OBJECT

    public:

        UiseHatnBridge(HATN_CLIENTAPP_NAMESPACE::Dispatcher* dispatcher=nullptr);

        void exec(
            const std::string& service,
            const std::string& method,
            HATN_CLIENTAPP_NAMESPACE::Request request,
            HATN_CLIENTAPP_NAMESPACE::Callback callback,
            QObject* callbackContext=nullptr
        );

        HATN_CLIENTAPP_NAMESPACE::Dispatcher* dispatcher() const noexcept
        {
            return m_dispatcher;
        }

        void setDispatcher(HATN_CLIENTAPP_NAMESPACE::Dispatcher* dispatcher)
        {
            m_dispatcher=dispatcher;
        }

        template <typename CallbackT>
        auto guiCallback(
                CallbackT callback,
                QObject* callbackContext=nullptr
            )
        {
            QPointer<QObject> ctx=callbackContext;
            if (ctx.isNull())
            {
                ctx=this;
            }

            auto cb=[callback=std::move(callback),ctx](auto&& ...args)
            {
                if (!ctx)
                {
                    return;
                }

                auto ts=boost::hana::make_tuple(std::forward<decltype(args)>(args)...);
                auto handler=[ts{std::move(ts)},callback=std::move(callback)]()
                {
                    hana::unpack(std::move(ts),
                                 [callback{std::move(callback)}](auto&& ...args) mutable
                                 {
                                     callback(std::forward<decltype(args)>(args)...);
                                 }
                    );
                };

                QMetaObject::invokeMethod(ctx,std::move(handler),Qt::QueuedConnection);
            };

            return cb;
        }

    private:

        HATN_CLIENTAPP_NAMESPACE::Dispatcher* m_dispatcher;
};

HATN_UISE_NAMESPACE_END

#endif // HATNUISEHATNBRIDGE_H

/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/uisehatneventdispatcher.h
  *
  */

/****************************************************************************/

#ifndef HATNUISEHATNEVENTDISPATCHER_H
#define HATNUISEHATNEVENTDISPATCHER_H

#include <QObject>

#include <hatn/clientapp/eventdispatcher.h>
#include <hatnuise/hatnuise.h>

HATN_UISE_NAMESPACE_BEGIN

class HATN_UISE_EXPORT UiseHatnEventDispatcher : public QObject
{
    Q_OBJECT

    public:

        UiseHatnEventDispatcher(HATN_CLIENTAPP_NAMESPACE::EventDispatcher* dispatcher=nullptr);

        HATN_CLIENTAPP_NAMESPACE::EventDispatcher* dispatcher() const noexcept
        {
            return m_dispatcher;
        }

        void setDispatcher(HATN_CLIENTAPP_NAMESPACE::EventDispatcher* dispatcher)
        {
            m_dispatcher=dispatcher;
        }

        size_t subscribe(
            HATN_CLIENTAPP_NAMESPACE::EventHandler handler,
            HATN_CLIENTAPP_NAMESPACE::EventKey key,
            QObject* handlerContext=nullptr
        );

        void unsubscribe(
            size_t id
        );

    private:

        HATN_CLIENTAPP_NAMESPACE::EventDispatcher* m_dispatcher;
};

HATN_UISE_NAMESPACE_END

#endif // HATNUISEHATNEVENTDISPATCHER_H

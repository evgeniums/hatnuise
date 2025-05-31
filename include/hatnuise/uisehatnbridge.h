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
            HATN_CLIENTAPP_NAMESPACE::Callback callback
        );

        HATN_CLIENTAPP_NAMESPACE::Dispatcher* dispatcher() const noexcept
        {
            return m_dispatcher;
        }

        void setDispatcher(HATN_CLIENTAPP_NAMESPACE::Dispatcher* dispatcher)
        {
            m_dispatcher=dispatcher;
        }

    private:

        HATN_CLIENTAPP_NAMESPACE::Dispatcher* m_dispatcher;
};

HATN_UISE_NAMESPACE_END

#endif // HATNUISEHATNBRIDGE_H

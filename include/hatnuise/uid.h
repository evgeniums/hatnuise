/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/uid.h
  *
  */

/****************************************************************************/

#ifndef HATNUISEUID_H
#define HATNUISEUID_H

#include <QMetaType>

#include <hatn/clientserver/models/oid.h>
#include <hatn/clientserver/cacheoptions.h>

#include <hatnuise/hatnuise.h>

Q_DECLARE_METATYPE(HATN_CLIENT_SERVER_NAMESPACE::Uid);
Q_DECLARE_METATYPE(HATN_CLIENT_SERVER_NAMESPACE::CacheOptions);

HATN_UISE_NAMESPACE_BEGIN

HATN_UISE_NAMESPACE_END

#endif // HATNUISEUID_H

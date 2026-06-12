/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/datetime.h
  *
  */

/****************************************************************************/

#ifndef HATNUISEDATETIME_H
#define HATNUISEDATETIME_H

#include <QDateTime>

#include <hatn/common/datetime.h>
#include <hatnuise/hatnuise.h>

HATN_UISE_NAMESPACE_BEGIN

struct dtToLocalQDtT
{
    QDateTime operator()(const HATN_COMMON_NAMESPACE::DateTime& dt) const
    {
        if (dt.isNull())
        {
            return QDateTime{};
        }
        auto utcDt=QDateTime::fromSecsSinceEpoch(dt.toEpoch());
        auto localDt=utcDt.toLocalTime();
        return localDt;
    }
};
constexpr dtToLocalQDtT dtToLocalQDt{};

HATN_UISE_NAMESPACE_END

#endif // HATNUISEDATETIME_H

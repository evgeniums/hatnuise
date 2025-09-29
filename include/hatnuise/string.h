/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/string.h
  *
  */

/****************************************************************************/

#ifndef HATNUISESTRING_H
#define HATNUISESTRING_H

#include <QString>

#include <hatnuise/hatnuise.h>

HATN_UISE_NAMESPACE_BEGIN

struct stdToQStringT
{
    QString operator()(const std::string& str) const
    {
        return QString::fromUtf8(str.data(),str.size());
    }
};
constexpr stdToQStringT stdToQString{};

struct qStringToStdT
{
    std::string operator()(const QString str) const
    {
        return str.toStdString();
    }
};
constexpr qStringToStdT qStringToStd{};

HATN_UISE_NAMESPACE_END

#endif // HATNUISESTRING_H

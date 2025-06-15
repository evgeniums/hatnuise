/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/objectpanel.сpp
  *
  */

#include <uise/desktop/editablelabel.hpp>

#include <hatnuise/objectpanel.h>

HATN_UISE_NAMESPACE_BEGIN

//---------------------------------------------------------------

ObjectPanelHelper::ObjectPanelHelper() : m_factory(defaultObjectPanelFieldFactory())
{
}

//---------------------------------------------------------------

std::shared_ptr<ObjectPanelFieldFactory> defaultObjectPanelFieldFactory()
{
    auto factory=std::make_shared<ObjectPanelFieldFactory>();

    auto makeInt=[](int id)
    {
        return ObjectPanelField(id, new UISE_DESKTOP_NAMESPACE::EditableLabelInt());
    };

    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::Int8,
        makeInt
    );
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::Int16,
        makeInt
        );
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::Int32,
        makeInt
        );
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::Int64,
        makeInt
        );
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::UInt8,
        makeInt
        );
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::UInt16,
        makeInt
        );
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::UInt32,
        makeInt
        );
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::UInt64,
        makeInt
    );

    auto makeDouble=[](int id)
    {
        return ObjectPanelField(id, new UISE_DESKTOP_NAMESPACE::EditableLabelDouble());
    };

    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::Float,
        makeDouble
    );
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::Double,
        makeDouble
    );

    auto makeString=[](int id)
    {
        return ObjectPanelField(id, new UISE_DESKTOP_NAMESPACE::EditableLabelText());
    };
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::String,
        makeString
    );

    auto makeDate=[](int id)
    {
        return ObjectPanelField(id, new UISE_DESKTOP_NAMESPACE::EditableLabelDate());
    };
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::Date,
        makeDate
    );

    auto makeDateTime=[](int id)
    {
        return ObjectPanelField(id, new UISE_DESKTOP_NAMESPACE::EditableLabelDateTime());
    };
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::DateTime,
        makeDateTime
    );

    auto makeTime=[](int id)
    {
        return ObjectPanelField(id, new UISE_DESKTOP_NAMESPACE::EditableLabelTime());
    };
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::Time,
        makeTime
    );

    auto makeOid=[](int id)
    {
        return ObjectPanelField(id, new UISE_DESKTOP_NAMESPACE::EditableLabelText());
    };
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::ObjectId,
        makeOid
    );

    return factory;
}

//---------------------------------------------------------------

HATN_UISE_NAMESPACE_END

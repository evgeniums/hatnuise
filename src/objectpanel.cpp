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

#include <hatn/common/meta/enumint.h>
#include <uise/desktop/editablelabel.hpp>

#include <hatnuise/objectpanel.h>

HATN_UISE_NAMESPACE_BEGIN

UISE_DESKTOP_USING
HATN_USING

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

    //! @todo Implement Int64, UInt32, UInt64

    factory->registerType(
        EditableLabelType::Int,
        makeInt
    );
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
        HATN_DATAUNIT_NAMESPACE::ValueType::UInt8,
        makeInt
        );
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::UInt16,
        makeInt
        );

    auto makeDouble=[](int id)
    {
        return ObjectPanelField(id, new UISE_DESKTOP_NAMESPACE::EditableLabelDouble());
    };

    factory->registerType(
        EditableLabelType::Double,
        makeDouble
    );
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
    auto makeTextEdit=[](int id)
    {
        return ObjectPanelField(id, new UISE_DESKTOP_NAMESPACE::EditableLabelTextEdit());
    };
    factory->registerType(
        EditableLabelType::Text,
        makeString
    );
    factory->registerType(
        EditableLabelType::TextEdit,
        makeTextEdit
    );
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::String,
        makeString
    );

    auto makeDate=[](int id)
    {
        return ObjectPanelField(id, new UISE_DESKTOP_NAMESPACE::EditableLabelDate());
    };
    factory->registerType(
        EditableLabelType::Date,
        makeDate
    );
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::Date,
        makeDate
    );

    auto makeDateTime=[](int id)
    {
        return ObjectPanelField(id, new UISE_DESKTOP_NAMESPACE::EditableLabelDateTime());
    };
    factory->registerType(
        EditableLabelType::DateTime,
        makeDateTime
    );
    factory->registerType(
        HATN_DATAUNIT_NAMESPACE::ValueType::DateTime,
        makeDateTime
    );

    auto makeTime=[](int id)
    {
        return ObjectPanelField(id, new UISE_DESKTOP_NAMESPACE::EditableLabelTime());
    };
    factory->registerType(
        EditableLabelType::Time,
        makeTime
    );
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

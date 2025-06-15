/*
    Copyright (c) 2020 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

*/

/****************************************************************************/
/*

*/
/** @file hatnuise/objectpanel.h
  *
  */

/****************************************************************************/

#ifndef HATNUISEOBJECTPANEL_H
#define HATNUISEOBJECTPANEL_H

#include <map>
#include <memory>

#include <QPointer>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QTimeZone>

#include <hatn/dataunit/tags.h>
#include <hatn/db/update.h>

#include <uise/desktop/valuewidget.hpp>
#include <uise/desktop/editablepanel.hpp>

#include <hatnuise/hatnuise.h>

HATN_UISE_NAMESPACE_BEGIN

HDU_TAG_DECLARE(FieldConfig)

struct ObjectPanelField
{
    using Widget=UISE_DESKTOP_NAMESPACE::AbstractValueWidget;

    int id;
    QPointer<Widget> widget;
    int idInPanel;

    ObjectPanelField(
        int fieldId=-1,
        Widget* widget=nullptr
    ) : id(fieldId),
        widget(widget),
        idInPanel(-1)
    {}
};

class ObjectPanelFieldFactory
{
    public:

        using Field=ObjectPanelField;
        using Builder=std::function<Field (int id)>;

        Field makePanelField(int type, int id) const
        {
            auto b=builder(type);
            if (!b)
            {
                return Field{};
            }
            return b(id);
        }

        template <typename T>
        Field makePanelField(T type, int id) const
        {
            return makePanelField(static_cast<int>(type),id);
        }

        Builder builder(int type) const
        {
            auto it=m_builders.find(type);
            if (it!=m_builders.end())
            {
                return it->second;
            }
            return Builder{};
        }

        void registerType(int type, Builder builder)
        {
            m_builders.emplace(type,std::move(builder));
        }

        template <typename T>
        void registerType(T type, Builder builder)
        {
            registerType(static_cast<int>(type),std::move(builder));
        }

        void merge(const ObjectPanelFieldFactory& other)
        {
            for (const auto& it: other.m_builders)
            {
                registerType(it.first,it.second);
            }
        }

    private:

        std::map<int,Builder> m_builders;
};


class AbstractObjectPanel : public QObject
{
    Q_OBJECT

    public:

        using Field=ObjectPanelField;
        using Widget=typename Field::Widget;

        void addField(Field field)
        {
            auto id=field.id;
            auto it=m_fields.emplace(id,std::move(field));
            doAddField(it.first->second);
        }

        Field* field(int id)
        {
            auto it=m_fields.find(id);
            if (it!=m_fields.end())
            {
                return &it->second;
            }
            return nullptr;
        }

        const Field* field(int id) const
        {
            auto it=m_fields.find(id);
            if (it!=m_fields.end())
            {
                return &it->second;
            }
            return nullptr;
        }

    protected:

        virtual void doAddField(Field& field) =0;

        std::map<int,Field> m_fields;
};

//! @todo Support bool and repeated fields

class ObjectPanel : public AbstractObjectPanel
{
    Q_OBJECT

    public:

        ObjectPanel(UISE_DESKTOP_NAMESPACE::AbstractEditablePanel* panel) : m_panel(panel)
        {
            connect(
                panel,
                SIGNAL(destroyed()),
                this,
                SLOT(deleteLater())
                );
        }

        UISE_DESKTOP_NAMESPACE::AbstractEditablePanel* panel() const
        {
            return m_panel;
        }

    protected:

        virtual void doAddField(Field& field) override
        {
            auto idInPanel=m_panel->addValueWidget(field.widget);
            field.idInPanel=idInPanel;
        }

    private:

        UISE_DESKTOP_NAMESPACE::AbstractEditablePanel* m_panel;
};

class HATN_UISE_EXPORT ObjectPanelHelper
{
    public:

        using Field=ObjectPanelField;
        using Widget=typename Field::Widget;

        ObjectPanelHelper();

        void setFactory(std::shared_ptr<ObjectPanelFieldFactory> factory)
        {
            m_factory=std::move(factory);
        }

        std::shared_ptr<ObjectPanelFieldFactory> factory() const
        {
            return m_factory;
        }

        void mergeFactory(std::shared_ptr<ObjectPanelFieldFactory> factory)
        {
            if (!m_factory)
            {
                m_factory=std::move(factory);
            }
            else
            {
                m_factory->merge(*factory);
            }
        }

        template <typename ObjectField>
        static auto toQtType(const ObjectField& objField)
        {
            if constexpr (HATN_DATAUNIT_NAMESPACE::types::IsSignedInt<ObjectField::typeId>.value)
            {
                return static_cast<int64_t>(objField.value());
            }
            else if constexpr (HATN_DATAUNIT_NAMESPACE::types::IsUnsignedInt<ObjectField::typeId>.value)
            {
                return static_cast<uint64_t>(objField.value());
            }
            else if constexpr (HATN_DATAUNIT_NAMESPACE::types::IsString<ObjectField::typeId>.value)
            {
                return QString::fromStdString(objField.value());
            }
            else if constexpr (HATN_DATAUNIT_NAMESPACE::types::IsDouble<ObjectField::typeId>.value)
            {
                return static_cast<double>(objField.value());
            }
            else if constexpr (ObjectField::typeId==HATN_DATAUNIT_NAMESPACE::ValueType::DateTime)
            {
                QDateTime dt;
                const auto& fdt=objField.value();
                QTimeZone tz(fdt.tzSecs());
                dt.setMSecsSinceEpoch(fdt.toEpochMs());
                return dt;
            }
            else if constexpr (ObjectField::typeId==HATN_DATAUNIT_NAMESPACE::ValueType::Time)
            {
                const auto& ftm=objField.value();
                QTime tm{ftm.hour(),ftm.minute(),ftm.second(),ftm.millisecond()};
                return tm;
            }
            else if constexpr (ObjectField::typeId==HATN_DATAUNIT_NAMESPACE::ValueType::Date)
            {
                const auto& fdt=objField.value();
                QDate dt{fdt.year(),fdt.month(),fdt.day()};
                return dt;
            }
            else if constexpr (ObjectField::typeId==HATN_DATAUNIT_NAMESPACE::ValueType::ObjectId)
            {
                return objField.value().toString();
            }
            else
            {
                return 0;
            }
        }

        template <typename ObjectField>
        static auto toHatnType(const ObjectField& objField, const QVariant& value)
        {
            if constexpr (HATN_DATAUNIT_NAMESPACE::types::IsInt64<ObjectField::typeId>.value)
            {
                return value.toLongLong();
            }
            else if constexpr (HATN_DATAUNIT_NAMESPACE::types::IsSignedInt<ObjectField::typeId>.value)
            {
                return static_cast<typename ObjectField::type>(value.toInt());
            }
            else if constexpr (HATN_DATAUNIT_NAMESPACE::types::IsUInt64<ObjectField::typeId>.value)
            {
                return value.toULongLong();
            }
            else if constexpr (HATN_DATAUNIT_NAMESPACE::types::IsUnsignedInt<ObjectField::typeId>.value)
            {
                return static_cast<typename ObjectField::type>(value.toUInt());
            }
            else if constexpr (HATN_DATAUNIT_NAMESPACE::types::IsString<ObjectField::typeId>.value)
            {
                return value.toString().toStdString();
            }
            else if constexpr (HATN_DATAUNIT_NAMESPACE::types::IsDouble<ObjectField::typeId>.value)
            {
                return value.toDouble();
            }
            else if constexpr (ObjectField::typeId==HATN_DATAUNIT_NAMESPACE::ValueType::DateTime)
            {
                auto v=value.toDateTime();
                auto dt=HATN_COMMON_NAMESPACE::DateTime::fromEpochMs(v.toMSecsSinceEpoch(),v.timeZone().fixedSecondsAheadOfUtc());
                return dt;
            }
            else if constexpr (ObjectField::typeId==HATN_DATAUNIT_NAMESPACE::ValueType::Time)
            {
                auto v=value.toTime();
                HATN_COMMON_NAMESPACE::Time tm{v.hour(),v.minute(),v.second(),v.msec()};
                return tm;
            }
            else if constexpr (ObjectField::typeId==HATN_DATAUNIT_NAMESPACE::ValueType::Date)
            {
                auto v=value.toDate();
                QDate dt{v.year(),v.month(),v.day()};
                return dt;
            }
            else if constexpr (std::is_same_v<typename ObjectField::type,HATN_DATAUNIT_NAMESPACE::ObjectId>)
            {
                auto v=value.toString();
                auto oid=HATN_DATAUNIT_NAMESPACE::ObjectId::fromString(v.toStdString());
                if (!oid)
                {
                    return oid.takeValue();
                }
                return HATN_DATAUNIT_NAMESPACE::ObjectId{};
            }
            else
            {
                return typename ObjectField::type{};
            }
        }

        template <typename ObjectField>
        Field makeField(const ObjectField& objField) const
        {
            return m_factory->makePanelField(objField.valueTypeId(),objField.fieldId());
        }

        template <typename ObjectField>
        void setField(Field& field, const ObjectField& objField) const
        {
            if (!field.widget)
            {
                return;
            }
            field.widget->setVariantValue(toQtType(objField));
        }

        template <typename ObjectField>
        void getField(const Field& field, ObjectField& objField) const
        {
            if (!field.widget)
            {
                return;
            }
            objField.set(toHatnType(objField,field.widget->variantValue()));
        }

        template <typename UnitTags>
        void constructPanel(const UnitTags& unitTags, AbstractObjectPanel* panel) const
        {
            hana::for_each(
                unitTags,
                [this,panel](const auto& x)
                {
                    const auto& objField=hana::first(x);
                    const auto& fieldTags=hana::second(x);

                    auto field=makeField(objField);
                    if (!field.widget)
                    {
                        return;
                    }

                    field.widget->setConfig(HDU_EXTRACT_FIELD_TAG(fieldTags,FieldConfig));
                    panel->addField(std::move(field));
                }
            );
        }

        template <typename UnitT>
        void loadPanel(const UnitT* unit, AbstractObjectPanel* panel) const
        {
            unit->iterateConst(
                [panel,this](const auto& field)
                {
                    auto panelField=panel->field(field.id());
                    if (panelField!=nullptr)
                    {
                        setField(panelField->widget,field);
                    }
                }
            );
        }

        template <typename UnitT>
        void savePanel(UnitT* unit, const AbstractObjectPanel* panel) const
        {
            unit->iterate(
                [panel,this](const auto& field)
                {
                    auto panelField=panel->field(field.id());
                    if (panelField!=nullptr)
                    {
                        getField(panelField->widget,field);
                    }
                }
            );
        }

        template <typename UnitT>
        HATN_DB_NAMESPACE::update::Request getUpdateRequest(const UnitT* unit, const AbstractObjectPanel* panel) const
        {
            //! @todo critical: Implement getUpdateRequest()
            return HATN_DB_NAMESPACE::update::Request{};
        }

    private:

        std::shared_ptr<ObjectPanelFieldFactory> m_factory;
};

std::shared_ptr<ObjectPanelFieldFactory> HATN_UISE_EXPORT defaultObjectPanelFieldFactory();

HATN_UISE_NAMESPACE_END

#endif // HATNUISEOBJECTPANEL_H

/*
    Copyright (c) 2024 - current, Evgeny Sidorov (decfile.com), All rights reserved.

    {{LICENSE}}
*/

/****************************************************************************/
/*

*/
/** @file hatnuise/demo/objectpanel-demo/main.сpp
  *
  */

#include <QFrame>
#include <QPushButton>
#include <QTextEdit>
#include <QScrollArea>
#include <QApplication>
#include <QMainWindow>

#include <uise/desktop/style.hpp>
#include <uise/desktop/widgetfactory.hpp>
#include <uise/desktop/utils/layout.hpp>
#include <uise/desktop/editablepanel.hpp>
#include <uise/desktop/editablepanels.hpp>

#include <hatn/common/meta/enumint.h>
#include <hatn/dataunit/syntax.h>
#include <hatn/dataunit/ipp/syntax.ipp>
#include <hatn/dataunit/ipp/objectid.ipp>

#include <hatnuise/objectpanel.h>

HATN_COMMON_USING
HATN_DATAUNIT_USING
UISE_DESKTOP_USING
HATN_UISE_USING

HDU_UNIT(u1,
    HDU_FIELD(f1,TYPE_INT8,1,false,-99)
    HDU_FIELD(f2,TYPE_INT16,2,false,-9999)
    HDU_FIELD(f3,TYPE_INT32,3,false,-99999)
    HDU_FIELD(f5,TYPE_UINT8,5,false,101)
    HDU_FIELD(f6,TYPE_UINT16,6,false,1001)
    HDU_FIELD(f9,TYPE_STRING,9,false,"Hello world")
    HDU_FIELD(f10,HDU_TYPE_FIXED_STRING(32),10,false, "Hi")
    HDU_FIELD(f11,TYPE_FLOAT,11, false, 100.51f)
    HDU_FIELD(f12,TYPE_DOUBLE,12, false, 1000.12l)
    HDU_FIELD(f13,TYPE_OBJECT_ID,13)
    HDU_FIELD(f14,TYPE_DATETIME,14)
    HDU_FIELD(f15,TYPE_DATE,15)
    HDU_FIELD(f16,TYPE_TIME,16)
)

HDU_UNIT(u2,
    HDU_FIELD(f1,TYPE_INT8,1)
    HDU_FIELD(f2,TYPE_INT16,2)
    HDU_FIELD(f3,TYPE_INT32,3)
    HDU_FIELD(f5,TYPE_UINT8,5)
    HDU_FIELD(f6,TYPE_UINT16,6)
    HDU_FIELD(f9,TYPE_STRING,9)
    HDU_FIELD(f10,HDU_TYPE_FIXED_STRING(32),10)
    HDU_FIELD(f11,TYPE_FLOAT,11)
    HDU_FIELD(f12,TYPE_DOUBLE,12)
    HDU_FIELD(f13,TYPE_OBJECT_ID,13)
    HDU_FIELD(f14,TYPE_DATETIME,14)
    HDU_FIELD(f15,TYPE_DATE,15)
    HDU_FIELD(f16,TYPE_TIME,16)
)

template <typename T>
void checkEqual(const T& l, const T& r, std::string name)
{
    if (l!=r)
    {
        std::cerr << fmt::format("Values \"{}\" are not equal: sample={}, result={}",name,l,r) << std::endl;
    }
}

void checkEqual(const double& l, const double& r, std::string name)
{
    if (std::fabs(l - r) > std::numeric_limits<double>::epsilon())
    {
        std::cerr << fmt::format("Values \"{}\" are not equal: sample={}, result={}",name,l,r) << std::endl;
    }
}

void checkEqual(const float& l, const float& r, std::string name)
{
    if (std::fabs(l - r) > std::numeric_limits<float>::epsilon())
    {
        std::cerr << fmt::format("Values \"{}\" are not equal: sample={}, result={}",name,l,r) << std::endl;
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc,argv);

    Style::instance().applyStyleSheet();

    QMainWindow w;
    auto scArea=new QScrollArea();
    auto mainFrame=new QFrame();
    scArea->setWidget(mainFrame);
    scArea->setWidgetResizable(true);

    auto l = Layout::vertical(mainFrame);

    auto panels=Style::instance().widgetFactory()->makeWidget<AbstractEditablePanels>(mainFrame);
    l->addWidget(panels);

    auto panel1=Style::instance().widgetFactory()->makeWidget<AbstractEditablePanel>(panels);
    panels->addPanel(panel1,0,Qt::AlignTop);
    panel1->setTitle("Panel for u1");
    auto p1=new ObjectPanel();
    p1->setPanel(panel1);

    ObjectPanelHelper panelHelper;

    ValueWidgetConfig cfg1{
        {EnumInt(ValueWidgetProperty::Label),QObject::tr("Field1")},
        {EnumInt(ValueWidgetProperty::Comment),"This is field of TYPE_INT8"},
        {EnumInt(ValueWidgetProperty::Min),-100},
        {EnumInt(ValueWidgetProperty::Max),100}
    };
    ValueWidgetConfig cfg2{
        {EnumInt(ValueWidgetProperty::Label),QObject::tr("Field2")},
            {EnumInt(ValueWidgetProperty::Comment),"This is field of TYPE_INT16"},
            {EnumInt(ValueWidgetProperty::Min),std::numeric_limits<int16_t>::min()},
            {EnumInt(ValueWidgetProperty::Max),std::numeric_limits<int16_t>::max()},
            {EnumInt(ValueWidgetProperty::InGroup),false}
    };
    ValueWidgetConfig cfg3{
        {EnumInt(ValueWidgetProperty::Label),QObject::tr("Field3")},
        {EnumInt(ValueWidgetProperty::Comment),"This is field of TYPE_INT32"},
        {EnumInt(ValueWidgetProperty::Min),std::numeric_limits<int32_t>::min()},
        {EnumInt(ValueWidgetProperty::Max),std::numeric_limits<int32_t>::max()},
        {EnumInt(ValueWidgetProperty::InGroup),false},
        {EnumInt(ValueWidgetProperty::Editable),false}
    };
    ValueWidgetConfig cfg5{
        {EnumInt(ValueWidgetProperty::Label),QObject::tr("Field5")},
        {EnumInt(ValueWidgetProperty::Comment),"This is field of TYPE_UINT8"},
        {EnumInt(ValueWidgetProperty::Min),std::numeric_limits<uint8_t>::min()},
        {EnumInt(ValueWidgetProperty::Max),std::numeric_limits<uint8_t>::max()}
    };
    ValueWidgetConfig cfg6{
        {EnumInt(ValueWidgetProperty::Label),QObject::tr("Field6")},
        {EnumInt(ValueWidgetProperty::Comment),"This is field of TYPE_UINT16"},
        {EnumInt(ValueWidgetProperty::Min),std::numeric_limits<uint16_t>::min()},
        {EnumInt(ValueWidgetProperty::Max),std::numeric_limits<uint16_t>::max()},
        {EnumInt(ValueWidgetProperty::InGroup),false}
    };
    ValueWidgetConfig cfg9{
        {EnumInt(ValueWidgetProperty::Label),QObject::tr("Field9")},
        {EnumInt(ValueWidgetProperty::Comment),"This is field of TYPE_STRING"}
    };
    ValueWidgetConfig cfg10{
        {EnumInt(ValueWidgetProperty::Label),QObject::tr("Field10")},
        {EnumInt(ValueWidgetProperty::Comment),"This is field of HDU_TYPE_FIXED_STRING(32)"}
    };
    ValueWidgetConfig cfg11{
        {EnumInt(ValueWidgetProperty::Label),QObject::tr("Field11")},
        {EnumInt(ValueWidgetProperty::Comment),"This is field of TYPE_FLOAT"},
        {EnumInt(ValueWidgetProperty::Min),-10000.0},
        {EnumInt(ValueWidgetProperty::Max),10000.0}
    };
    ValueWidgetConfig cfg12{
        {EnumInt(ValueWidgetProperty::Label),QObject::tr("Field12")},
        {EnumInt(ValueWidgetProperty::Comment),"This is field of TYPE_DOUBLE"},
        {EnumInt(ValueWidgetProperty::Min),-10000000.0},
        {EnumInt(ValueWidgetProperty::Max),10000000.0}
    };
    ValueWidgetConfig cfg13{
        {EnumInt(ValueWidgetProperty::Label),QObject::tr("Field13")},
        {EnumInt(ValueWidgetProperty::Comment),"This is field of TYPE_OBJECT_ID"},
        {EnumInt(ValueWidgetProperty::InGroup),false},
        {EnumInt(ValueWidgetProperty::Editable),false}
    };
    ValueWidgetConfig cfg14{
        {EnumInt(ValueWidgetProperty::Label),QObject::tr("Field14")},
        {EnumInt(ValueWidgetProperty::Comment),"This is field of TYPE_DATE_TIME"}
    };
    ValueWidgetConfig cfg15{
        {EnumInt(ValueWidgetProperty::Label),QObject::tr("Field15")},
        {EnumInt(ValueWidgetProperty::Comment),"This is field of TYPE_DATE in UTC"}
    };
    ValueWidgetConfig cfg16{
        {EnumInt(ValueWidgetProperty::Label),QObject::tr("Field16")},
        {EnumInt(ValueWidgetProperty::Comment),"This is field of TYPE_TIME in UTC"}
    };
    auto tags1=unit_field_tags(
            field_tags(u1::f1,HDU_TAG(FieldConfig,cfg1)),
            field_tags(u1::f2,HDU_TAG(FieldConfig,cfg2)),
            field_tags(u1::f3,HDU_TAG(FieldConfig,cfg3)),
            field_tags(u1::f5,HDU_TAG(FieldConfig,cfg5)),
            field_tags(u1::f6,HDU_TAG(FieldConfig,cfg6)),
            field_tags(u1::f9,HDU_TAG(FieldConfig,cfg9)),
            field_tags(u1::f10,HDU_TAG(FieldConfig,cfg10)),
            field_tags(u1::f11,HDU_TAG(FieldConfig,cfg11)),
            field_tags(u1::f12,HDU_TAG(FieldConfig,cfg12)),
            field_tags(u1::f13,HDU_TAG(FieldConfig,cfg13)),
            field_tags(u1::f14,HDU_TAG(FieldConfig,cfg14)),
            field_tags(u1::f15,HDU_TAG(FieldConfig,cfg15)),
            field_tags(u1::f16,HDU_TAG(FieldConfig,cfg16))
        );
    panelHelper.constructPanel(tags1,p1);

    u1::type obj1;
    obj1.setFieldValue(u1::f13,ObjectId::generateId());
    obj1.setFieldValue(u1::f14,HATN_COMMON_NAMESPACE::DateTime::currentUtc());
    obj1.setFieldValue(u1::f15,Date::currentUtc());
    obj1.setFieldValue(u1::f16,Time::currentUtc());
    panelHelper.loadPanel(&obj1,p1);

    u2::type obj2;
    panelHelper.savePanel(&obj2,p1);

    checkEqual(obj1.fieldValue(u1::f1),obj2.fieldValue(u2::f1),u2::f1.name());
    checkEqual(obj1.fieldValue(u1::f2),obj2.fieldValue(u2::f2),u2::f2.name());
    checkEqual(obj1.fieldValue(u1::f3),obj2.fieldValue(u2::f3),u2::f3.name());
    checkEqual(obj1.fieldValue(u1::f5),obj2.fieldValue(u2::f5),u2::f5.name());
    checkEqual(obj1.fieldValue(u1::f6),obj2.fieldValue(u2::f6),u2::f6.name());
    checkEqual(obj1.fieldValue(u1::f9),obj2.fieldValue(u2::f9),u2::f9.name());
    checkEqual(obj1.fieldValue(u1::f10),obj2.fieldValue(u2::f10),u2::f10.name());
    checkEqual(obj1.fieldValue(u1::f11),obj2.fieldValue(u2::f11),u2::f11.name());
    checkEqual(obj1.fieldValue(u1::f12),obj2.fieldValue(u2::f12),u2::f12.name());
    checkEqual(obj1.fieldValue(u1::f13),obj2.fieldValue(u2::f13),u2::f13.name());
    checkEqual(obj1.fieldValue(u1::f14).toUtc(),obj2.fieldValue(u2::f14).toUtc(),u2::f14.name());
    checkEqual(obj1.fieldValue(u1::f15),obj2.fieldValue(u2::f15),u2::f15.name());
    checkEqual(obj1.fieldValue(u1::f16),obj2.fieldValue(u2::f16),u2::f16.name());

    w.setCentralWidget(scArea);
    w.resize(1000,600);
    w.setWindowTitle("Object Panel Demo");
    w.show();
    return app.exec();
}

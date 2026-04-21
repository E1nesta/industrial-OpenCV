// 领域实体：DefectItem 定义单个 AOI 缺陷明细。
// 该对象用于承载缺陷框、面积与缺陷类型等结构化信息。
#pragma once

#include <QMetaType>
#include <QRect>
#include <QString>

struct DefectItem
{
    // 缺陷框坐标。
    QRect boundingRect;
    // 缺陷面积。
    double area = 0.0;
    // 缺陷类别。
    QString category = QStringLiteral("blob_defect");
    // 缺陷描述。
    QString description;
};

Q_DECLARE_METATYPE(DefectItem)

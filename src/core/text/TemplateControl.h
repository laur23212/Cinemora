#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace drift {

// Dafin: one quick control a title template exposes in the inspector — a widget bound to one
// property of the clip. `bind` uses the paths documented by templateBindingCatalog()
// (TextTemplate.h); only those paths reach the renderer, which the validator enforces.
struct TemplateControl
{
    QString id;          // unique in the template
    QString label;
    QString type;        // text | font | color | scalar | bool | enum
    QString bind;
    QString group;       // optional section label; empty = the main list
    QString unit;        // shown after a scalar: "px", "s", "%", "ms", "°"
    QString description; // tooltip
    double min = 0.0;
    double max = 1.0;
    double step = 0.0;   // 0 = continuous
    QStringList options;      // enum values
    QStringList optionLabels; // optional, same length as options

    bool operator==(const TemplateControl &o) const
    {
        return id == o.id && label == o.label && type == o.type && bind == o.bind && group == o.group
               && unit == o.unit && description == o.description && min == o.min && max == o.max
               && step == o.step && options == o.options && optionLabels == o.optionLabels;
    }
};

QJsonObject templateControlToJson(const TemplateControl &control);
TemplateControl templateControlFromJson(const QJsonObject &object);

} // namespace drift

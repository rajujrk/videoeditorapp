#include "EffectInfo.h"

#include <QStringList>

QVariantMap describeEffect(const tve::LoadedEffect& effect)
{
    return describeEffect(effect.manifest);
}

QVariantMap describeEffect(const tve::EffectManifest& m)
{
    QVariantList params;
    for (const tve::EffectParamSpec& p : m.params) {
        QVariantList defaults;
        for (float v : p.defaultValue)
            defaults << double(v);
        QStringList values;
        for (const std::string& v : p.enumValues)
            values << QString::fromStdString(v);
        params << QVariantMap{
            {QStringLiteral("id"), QString::fromStdString(p.id)},
            {QStringLiteral("title"), QString::fromStdString(p.title.at("en"))},
            {QStringLiteral("type"), QString::fromLatin1(tve::toString(p.type).data())},
            {QStringLiteral("min"), p.min},
            {QStringLiteral("max"), p.max},
            {QStringLiteral("default"), p.type == tve::ParamType::Color ? QVariant(defaults) : defaults.value(0)},
            {QStringLiteral("values"), values},
            // Scalar parameters the manifest allows to animate.
            {QStringLiteral("keyframable"), p.keyframable
                                               && (p.type == tve::ParamType::Float || p.type == tve::ParamType::Int)},
        };
    }
    QStringList categories;
    for (const std::string& c : m.categories)
        categories << QString::fromStdString(c);
    return QVariantMap{
        {QStringLiteral("id"), QString::fromStdString(m.id)},
        {QStringLiteral("title"), QString::fromStdString(m.displayTitle())},
        {QStringLiteral("categories"), categories},
        {QStringLiteral("canvas"), m.space == tve::EffectSpace::Canvas},
        {QStringLiteral("params"), params},
    };
}

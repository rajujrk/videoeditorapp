#include "ShaderEffects.h"

#include "gpu/EffectLibrary.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <rhi/qshaderbaker.h>

#include <memory>

UserEffectsNotifier* UserEffectsNotifier::instance()
{
    static UserEffectsNotifier* notifier = new UserEffectsNotifier;
    return notifier;
}

namespace shadereffects {

namespace {

// The app's helpers, as every built-in effect has them.
const char* kPrelude = R"(const float TVE_PI = 3.14159265359;

float tveLuma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec2 tveAspect() { return vec2(tve_texelSize.y / tve_texelSize.x, 1.0); }
float tveHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float tveNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(tveHash(i), tveHash(i + vec2(1.0, 0.0)), f.x),
               mix(tveHash(i + vec2(0.0, 1.0)), tveHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
vec4 tveSample(vec2 uv)
{
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return vec4(0.0);
    return texture(tve_source, uv);
}
)";

const QRegularExpression& validId()
{
    static const QRegularExpression re(QStringLiteral("^[a-z][a-z0-9_]{0,31}$"));
    return re;
}

QVariantList paramsOf(const Spec& spec)
{
    return spec.value(QStringLiteral("params")).toList();
}

QString slug(const QString& title)
{
    QString s = title.toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    s = s.mid(0, 24);
    while (s.startsWith(QLatin1Char('_')))
        s.remove(0, 1);
    while (s.endsWith(QLatin1Char('_')))
        s.chop(1);
    return s.isEmpty() ? QStringLiteral("effect") : s;
}

QString folderOf(const QString& id)
{
    return QDir(folder()).filePath(id);
}

// Writes the manifest, the shader (source and compiled) and the spec into `dir`.
bool writeFolder(const QString& dir, const Spec& spec, const QString& id, const QShader& shader, QString* error)
{
    if (!QDir().mkpath(dir)) {
        *error = QObject::tr("Could not make %1").arg(dir);
        return false;
    }
    const auto write = [&](const QString& name, const QByteArray& bytes) {
        QSaveFile file(QDir(dir).filePath(name));
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    };
    Spec saved = spec;
    saved.insert(QStringLiteral("id"), id);
    if (!write(QStringLiteral("manifest.json"), manifest(spec, id)) || !write(QStringLiteral("effect.frag"), wrap(spec).toUtf8())
        || !write(QStringLiteral("effect.frag.qsb"), shader.serialized())
        || !write(QStringLiteral("source.json"), QJsonDocument(QJsonObject::fromVariantMap(saved)).toJson())) {
        *error = QObject::tr("Could not save the effect in %1").arg(dir);
        return false;
    }
    return true;
}

} // namespace

QString folder()
{
    const QString overridden = qEnvironmentVariable("TVE_MY_SHADERS_DIR");
    return overridden.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("My Effects"))
                                : overridden;
}

QString wrap(const Spec& spec, int* linesBefore)
{
    QString block = QStringLiteral("    vec2 tve_texelSize;\n    float tve_time;\n    float tve_flipY;\n");
    for (const QVariant& v : paramsOf(spec)) {
        const QVariantMap p = v.toMap();
        const QString type = p.value(QStringLiteral("type")).toString() == QLatin1String("color") ? QStringLiteral("vec3")
                                                                                                  : QStringLiteral("float");
        block += QStringLiteral("    %1 %2;\n").arg(type, p.value(QStringLiteral("id")).toString());
    }
    QString head = QStringLiteral("#version 440\n\nlayout(location = 0) in vec2 v_uv;\nlayout(location = 0) out vec4 fragColor;\n\n"
                                  "layout(std140, binding = 0) uniform EffectParams {\n%1};\n\n"
                                  "layout(binding = 1) uniform sampler2D tve_source;\n\n%2\n#line 1\n")
                       .arg(block, QString::fromUtf8(kPrelude));
    if (linesBefore)
        *linesBefore = int(head.count(QLatin1Char('\n')));
    return head + spec.value(QStringLiteral("code")).toString()
         + QStringLiteral("\n\nvoid main()\n{\n    fragColor = effect(v_uv);\n}\n");
}

std::optional<QShader> compile(const Spec& spec, QString* error)
{
    // The parameters: names a shader can use, not the app's own, not twice.
    QStringList seen;
    for (const QVariant& v : paramsOf(spec)) {
        const QString id = v.toMap().value(QStringLiteral("id")).toString();
        if (!validId().match(id).hasMatch() || id.startsWith(QLatin1String("tve"))) {
            *error = QObject::tr("“%1” cannot name a setting: use small letters, digits and _, starting with a letter").arg(id);
            return std::nullopt;
        }
        if (seen.contains(id)) {
            *error = QObject::tr("Two settings are called “%1”").arg(id);
            return std::nullopt;
        }
        seen << id;
    }
    if (!spec.value(QStringLiteral("code")).toString().contains(QRegularExpression(QStringLiteral("vec4\\s+effect\\s*\\(")))) {
        *error = QObject::tr("Write a function vec4 effect(vec2 uv) that returns the colour at uv");
        return std::nullopt;
    }
    QShaderBaker baker;
    baker.setSourceString(wrap(spec).toUtf8(), QShader::FragmentStage);
    baker.setGeneratedShaders({{QShader::SpirvShader, QShaderVersion(100)}, {QShader::MslShader, QShaderVersion(12)}});
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    QShader shader = baker.bake();
    if (!shader.isValid()) {
        // "ERROR: :23: …" lines: the line numbers are the user's already (#line 1 before the code).
        QString message = baker.errorMessage().trimmed();
        message.replace(QRegularExpression(QStringLiteral("ERROR: [^:]*:(\\d+):")), QObject::tr("Line \\1:"));
        message.remove(QRegularExpression(QStringLiteral("\\n?ERROR: \\d+ compilation errors\\..*")));
        *error = message.isEmpty() ? QObject::tr("The shader could not be compiled") : message;
        return std::nullopt;
    }
    // Checked as the library checks every effect (its parameters and the shader agree).
    try {
        tve::EffectLibrary::fromShaders(manifest(spec, QStringLiteral("user.check")).toStdString(), {shader});
    } catch (const std::exception& e) {
        *error = QString::fromUtf8(e.what());
        return std::nullopt;
    }
    return shader;
}

QByteArray manifest(const Spec& spec, const QString& id)
{
    QJsonArray params;
    for (const QVariant& v : paramsOf(spec)) {
        const QVariantMap p = v.toMap();
        const bool colour = p.value(QStringLiteral("type")).toString() == QLatin1String("color");
        QJsonObject param{{QStringLiteral("id"), p.value(QStringLiteral("id")).toString()},
                          {QStringLiteral("type"), colour ? QStringLiteral("color") : QStringLiteral("float")},
                          {QStringLiteral("title"), QJsonObject{{QStringLiteral("en"), p.value(QStringLiteral("title")).toString()}}}};
        if (colour) {
            QJsonArray rgb;
            for (const QVariant& c : p.value(QStringLiteral("default")).toList())
                rgb.append(c.toDouble());
            while (rgb.size() < 3)
                rgb.append(1.0);
            param.insert(QStringLiteral("default"), rgb);
        } else {
            const double lo = p.value(QStringLiteral("min"), 0.0).toDouble(), hi = p.value(QStringLiteral("max"), 1.0).toDouble();
            param.insert(QStringLiteral("min"), std::min(lo, hi));
            param.insert(QStringLiteral("max"), std::max(lo, hi));
            param.insert(QStringLiteral("default"), std::clamp(p.value(QStringLiteral("default"), lo).toDouble(), std::min(lo, hi), std::max(lo, hi)));
            param.insert(QStringLiteral("keyframable"), true);
        }
        params.append(param);
    }
    const QString title = spec.value(QStringLiteral("title")).toString().trimmed();
    const QJsonObject json{{QStringLiteral("id"), id},
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("kind"), QStringLiteral("video-effect")},
                           {QStringLiteral("category"), QJsonArray{QStringLiteral("mine")}},
                           {QStringLiteral("title"), QJsonObject{{QStringLiteral("en"), title.isEmpty() ? QStringLiteral("My effect") : title}}},
                           {QStringLiteral("params"), params},
                           {QStringLiteral("passes"), QJsonArray{QJsonObject{{QStringLiteral("fragment"), QStringLiteral("effect.frag")}}}},
                           {QStringLiteral("license"), QStringLiteral("TVE-Original")}};
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

QString preview(const Spec& spec, QString* error)
{
    const auto shader = compile(spec, error);
    if (!shader)
        return {};
    // Each preview its own id, so the previews and effect chains see the newest.
    static int drafts = 0;
    const QString id = QStringLiteral("user.draft_%1").arg(++drafts);
    static QTemporaryDir scratch;
    const QString dir = QDir(scratch.path()).filePath(id);
    if (!writeFolder(dir, spec, id, *shader, error))
        return {};
    if (const std::string why = tve::EffectLibrary::addUserEffect(dir); !why.empty()) {
        *error = QString::fromStdString(why);
        return {};
    }
    return id;
}

QString save(const Spec& spec, QString* error)
{
    const auto shader = compile(spec, error);
    if (!shader)
        return {};
    QString id = spec.value(QStringLiteral("id")).toString();
    if (!id.startsWith(QLatin1String("user.")) || id.startsWith(QLatin1String("user.draft_"))) {
        const QString base = QStringLiteral("user.") + slug(spec.value(QStringLiteral("title")).toString());
        id = base;
        for (int n = 2; QDir(folderOf(id)).exists(); ++n)
            id = base + QStringLiteral("_%1").arg(n);
    }
    if (!writeFolder(folderOf(id), spec, id, *shader, error))
        return {};
    if (const std::string why = tve::EffectLibrary::addUserEffect(folderOf(id)); !why.empty()) {
        *error = QString::fromStdString(why);
        return {};
    }
    return id;
}

Spec specOf(const QString& id)
{
    QFile file(QDir(folderOf(id)).filePath(QStringLiteral("source.json")));
    if (!id.startsWith(QLatin1String("user.")) || !file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

bool remove(const QString& id)
{
    if (!id.startsWith(QLatin1String("user.")) || id.contains(QLatin1Char('/')) || id.contains(QLatin1String("..")))
        return false;
    QDir dir(folderOf(id));
    if (!dir.exists())
        return false;
    tve::EffectLibrary::removeUserEffect(id.toStdString());
    // (Only this effect's own folder, inside your effects folder.)
    return dir.removeRecursively();
}

void loadAll()
{
    const QDir root(folder());
    for (const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (const std::string why = tve::EffectLibrary::addUserEffect(root.filePath(name)); !why.empty())
            qWarning("Your effect %s: %s", qPrintable(name), why.c_str());
    }
}

QVariantList list()
{
    QVariantList out;
    for (const tve::LoadedEffect* e : tve::EffectLibrary::userEffects()) {
        const QString id = QString::fromStdString(e->manifest.id);
        if (id.startsWith(QLatin1String("user.draft_")))
            continue;
        out << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("title"), QString::fromStdString(e->manifest.displayTitle())}};
    }
    return out;
}

bool exportTo(const QString& id, const QString& path, QString* error)
{
    Spec spec = specOf(id);
    if (spec.isEmpty()) {
        *error = QObject::tr("No such effect of yours");
        return false;
    }
    spec.remove(QStringLiteral("id"));
    spec.insert(QStringLiteral("format"), QStringLiteral("tve-shader-effect"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(QJsonObject::fromVariantMap(spec)).toJson()) < 0 || !file.commit()) {
        *error = QObject::tr("Could not write %1").arg(path);
        return false;
    }
    return true;
}

QString importFrom(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
        *error = QObject::tr("Cannot read %1").arg(path);
        return {};
    }
    Spec spec = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    if (spec.value(QStringLiteral("format")).toString() != QLatin1String("tve-shader-effect")) {
        *error = QObject::tr("%1 is not an effect made in the shader editor").arg(QFileInfo(path).fileName());
        return {};
    }
    spec.remove(QStringLiteral("format"));
    spec.remove(QStringLiteral("id"));
    return save(spec, error);
}

} // namespace shadereffects

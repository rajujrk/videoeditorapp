#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <rhi/qshader.h>

#include <optional>

// Your own GPU effects, written in the shader editor: a function `vec4 effect(vec2 uv)` in GLSL
// (it may read `tve_source`, the clip's picture, and the app's helpers: tveLuma, tveNoise,
// tveSample…) with sliders and colours of your own as its parameters. Compiled in the app
// (Qt's shader baker, to Metal), checked like the built-in effects, kept in your effects
// folder (TVE_MY_SHADERS_DIR; else "My Effects" in the app's data) and added to the effect
// library as "user.<name>" effects in the "mine" category.
// Says when your own effects change (saved, removed), so effect lists are read again.
class UserEffectsNotifier : public QObject {
    Q_OBJECT
public:
    static UserEffectsNotifier* instance();
signals:
    void changed();
};

namespace shadereffects {

// An effect as the editor has it: {id ("" for a new one), title, code, params: [{id, title,
// type "float"/"color", min, max, default (a number, or [r, g, b])}]}.
using Spec = QVariantMap;

QString folder();
// The whole fragment shader for a spec, and how many lines come before the user's code.
QString wrap(const Spec& spec, int* linesBefore = nullptr);
// Compiles a spec; on failure `error` says what is wrong, with line numbers in the user's code.
std::optional<QShader> compile(const Spec& spec, QString* error);
// The manifest for a spec (its id given).
QByteArray manifest(const Spec& spec, const QString& id);
// Compiles a spec and puts it in the library under a draft id for a preview; the id, or "" (error).
QString preview(const Spec& spec, QString* error);
// Compiles and saves a spec (a new id from its title when it has none), adding it to the
// library; the id, or "" (error).
QString save(const Spec& spec, QString* error);
// A saved effect's spec, or empty.
Spec specOf(const QString& id);
bool remove(const QString& id);
// Every saved effect, added to the library (at start).
void loadAll();
// Saved effects: [{id, title}].
QVariantList list();
// One effect as a file to share (its spec as JSON), and one read back (saved; its id).
bool exportTo(const QString& id, const QString& path, QString* error);
QString importFrom(const QString& path, QString* error);

} // namespace shadereffects

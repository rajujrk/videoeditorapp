#pragma once

#include "model/Project.h"

#include <QObject>
#include <QQuickAsyncImageProvider>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

// Templates: projects with replaceable media slots, editable texts and music, made in any of
// six shapes. The built-in ones are original recipes (effects/templates/*/template.json:
// slots and texts timed in beats of their music — one of our own pieces — with a
// transition between slots); the person's own are `.tvet` folders (template.json with the
// project, its media in media/) saved from a project. Placeholders are pictures drawn here.
// Folders: <app data>/Templates (TVE_TEMPLATE_DIR) for the person's, <app data>/Template
// cache (TVE_TEMPLATE_CACHE_DIR) for placeholders and music, and template-favorites.json
// (TVE_TEMPLATE_FAVORITES_FILE).
class TemplateLibrary : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The project's templates")
    // [{id, name, category, description, slots, texts, seconds, shape, builtIn, favorite}]
    Q_PROPERTY(QVariantList templates READ templates NOTIFY changed)
    // [{id, label}]
    Q_PROPERTY(QVariantList categories READ categories CONSTANT)
    // [{id ("16:9"…), width, height}]
    Q_PROPERTY(QVariantList shapes READ shapes CONSTANT)

public:
    explicit TemplateLibrary(QObject* parent = nullptr);

    // (Listed the first time they are asked for.)
    QVariantList templates() const;
    QVariantList categories() const;
    QVariantList shapes() const;

    Q_INVOKABLE void toggleFavorite(const QString& id);
    // Deletes one of the person's own templates.
    Q_INVOKABLE bool remove(const QString& id);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString lastError() const { return m_error; }

    // A new project from a template in a shape ("" for the template's own).
    static std::optional<tve::Project> build(const QString& id, const QString& shape, QString* error, bool withMusic = true);
    // The template's project for previews (its own shape, no music), built once.
    static std::shared_ptr<const tve::Project> previewProject(const QString& id);

    // Saves a project as one of the person's templates: `slotClips` become placeholders,
    // `texts` titles stay editable, `music` (or kNoId) is its music; everything else it uses
    // is copied in. Its id, or empty (with `error`).
    QString save(const tve::Project& project, const QString& name, const QString& category, const std::vector<tve::Id>& slotClips,
                 const std::vector<tve::Id>& texts, tve::Id music, QString* error);

    // One of our own pieces of music (a built-in sound id) rendered into the cache: its file
    // and tempo (beats per minute); empty (with `error`) if there is no such piece.
    static QString builtInMusic(const QString& soundId, double* tempo, QString* error);
    static QString userDirectory();
    static QString cacheDirectory();

signals:
    void changed();

private:
    void list();

    QVariantList m_list;
    bool m_listed = false;
    QStringList m_favorites;
    QString m_error;
};

// Template previews: image://tvetemplate/<url-encoded id>@<seconds>@<height>.
class TemplatePreviewProvider : public QQuickAsyncImageProvider {
public:
    static constexpr const char* kId = "tvetemplate";

    TemplatePreviewProvider();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

private:
    QThreadPool m_pool;
};

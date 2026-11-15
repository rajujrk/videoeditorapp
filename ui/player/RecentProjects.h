#pragma once

#include "model/Project.h"

#include <QObject>
#include <QThreadPool>
#include <QVariantList>

#include <memory>

// The projects opened or saved lately, for the Home screen: [{path, name, lastOpened (ms
// since epoch), width, height, duration (seconds)}], newest first, each file once. Kept in
// <app data>/recent-projects.json ({"version": 1, "projects": [...]}), or
// TVE_RECENT_PROJECTS_FILE; saved at once. Each has a cover (a frame a third of the way in),
// rendered off the main thread into "Recent Covers" next to that file. Changes start from the
// file as it is (another controller, window or copy of the app may have changed it).
class RecentProjects : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaximum = 100;

    explicit RecentProjects(QObject* parent = nullptr);
    ~RecentProjects() override;

    QString filePath() const { return m_file; }
    // The list as QML shows it: each entry also has `missing` (the file isn't there now),
    // `folder` (where it is) and `cover` (a file URL, "" until there is one).
    QVariantList list() const;
    // Opened or saved now: to the top of the list. A cover is rendered when the project has
    // something to show and the one there is older than the file (always when `freshCover`).
    void note(const QString& path, const tve::Project& project, bool freshCover);
    bool remove(const QString& path);
    // Reads the file again (another window or copy of the app may have changed it).
    void reload();

signals:
    void changed();

private:
    QString coverPath(const QString& path) const;
    void renderCover(const QString& path, std::shared_ptr<const tve::Project> project);
    void save() const;

    QString m_file;
    QVariantList m_entries; // as stored (no missing/folder/cover)
    QThreadPool m_pool;     // covers, one at a time
};

#pragma once

#include <QAbstractListModel>
#include <QVariantList>

// The timeline's tracks (top to bottom) as a list model with one "track" role holding the
// track's map. When an edit leaves the set of tracks unchanged, rows are updated in place
// rather than reset, so the delegates showing them (track headers and their buttons) are
// kept instead of rebuilt after every edit.
class TrackListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { TrackRole = Qt::UserRole + 1 };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_tracks.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{TrackRole, "track"}}; }

    void update(const QVariantList& tracks);

private:
    QVariantList m_tracks;
};

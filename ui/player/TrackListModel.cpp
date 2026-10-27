#include "TrackListModel.h"

#include <QVariantMap>

QVariant TrackListModel::data(const QModelIndex& index, int role) const
{
    if (role != TrackRole || !index.isValid() || index.row() >= m_tracks.size())
        return {};
    return m_tracks.at(index.row());
}

void TrackListModel::update(const QVariantList& tracks)
{
    auto ids = [](const QVariantList& list) {
        QVariantList result;
        for (const QVariant& track : list)
            result << track.toMap().value(QStringLiteral("id"));
        return result;
    };
    if (ids(tracks) != ids(m_tracks)) {
        beginResetModel();
        m_tracks = tracks;
        endResetModel();
        return;
    }
    for (qsizetype row = 0; row < tracks.size(); ++row) {
        if (tracks.at(row) == m_tracks.at(row))
            continue;
        m_tracks[row] = tracks.at(row);
        emit dataChanged(index(int(row)), index(int(row)), {TrackRole});
    }
}

#pragma once

#include "analysis/Highlights.h"
#include "model/People.h"
#include "model/Project.h"
#include "model/Template.h"

#include <QString>
#include <QVariantList>

#include <optional>
#include <vector>

// Edits made for you. An instant video or a highlight montage: the best moments of the
// videos and the pictures, as many as fit the length asked for, in order, each cut to a
// whole number of beats of the style's music (one of our own pieces), with the style's
// transition and look, cropped to fill the shape, and an opening title if there is one.
// Smart shorts: moments of one long video laid end to end in a tall frame, each marked as a
// range to export on its own, following the subject. Both are projects with template parts
// (the clips as filled slots), so the storyboard can change what is in them.

struct AutoEditStyle {
    QString id;
    QString name;
    QString description;
    QString music;      // a built-in sound id
    double cutBeats;    // how long a cut of video lasts, at most (beats)
    double photoBeats;  // how long a picture stays (beats)
    QString transition; // a built-in transition id
    double transitionBeats;
    QString look;       // a built-in look id, or empty
    QVariantMap title;  // the opening title's style (textFrom keys and x, y)
};
const std::vector<AutoEditStyle>& autoEditStyles();
const AutoEditStyle* findAutoEditStyle(const QString& id);
QVariantList autoEditStyleList();

struct AutoEditSource {
    tve::MediaItem media;
    std::vector<tve::Moment> moments; // best first (videos); empty for pictures
};

std::optional<tve::Project> buildAutoEdit(const std::vector<AutoEditSource>& sources, const AutoEditStyle& style,
                                          const tve::TemplateShape& shape, double seconds, const QString& title,
                                          const QString& name, QString* error);

struct ShortPlan {
    tve::Moment moment;
    QString title;
};
// `subject` (may be empty) is where the subject is through the media, to follow it.
std::optional<tve::Project> buildShorts(const tve::MediaItem& media, const std::vector<ShortPlan>& shorts,
                                        const std::vector<tve::SubjectSample>& subject, QString* error);

// The position keyframes and placement that follow a subject through a clip in a canvas of
// another shape (what Auto reframe does); false when there is no subject to follow.
bool followSubject(tve::Clip& clip, const tve::MediaItem& media, const std::vector<tve::SubjectSample>& subject,
                   int canvasWidth, int canvasHeight, double smoothing);

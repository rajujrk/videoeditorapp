#pragma once

#include "model/ColorGrade.h"

#include <QImage>
#include <QQuickImageProvider>

#include <string>
#include <vector>

// The built-in looks (filters): :/effects/looks/looks.json, generated with their LUTs by
// tools/make_looks.py.
struct LookInfo {
    std::string id;
    std::string title;
    std::string category;
    tve::ColorGrade grade;
};

const std::vector<LookInfo>& builtInLooks();
const LookInfo* findLook(const std::string& id);

// A sample scene (sky, sun, skin, foliage, water, greys) graded by `grade`, on the CPU with
// the same maths as the GPU pass: what a look's preview shows.
QImage gradedSample(const tve::ColorGrade& grade, QSize size);

// Library previews: image://tvelook/<look id> is the sample scene with that look.
class LookPreviewProvider : public QQuickImageProvider {
public:
    static constexpr const char* kId = "tvelook";
    static constexpr int kWidth = 192;
    static constexpr int kHeight = 108;

    LookPreviewProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

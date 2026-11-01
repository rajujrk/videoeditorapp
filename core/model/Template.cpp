#include "model/Template.h"

#include <algorithm>
#include <cmath>

namespace tve {

const std::vector<TemplateShape>& templateShapes()
{
    static const std::vector<TemplateShape> shapes{
        {"16:9", 1920, 1080}, {"9:16", 1080, 1920}, {"1:1", 1080, 1080},
        {"4:3", 1440, 1080},  {"3:4", 1080, 1440},  {"21:9", 2560, 1080},
    };
    return shapes;
}

std::optional<TemplateShape> templateShape(const std::string& id)
{
    for (const TemplateShape& shape : templateShapes())
        if (shape.id == id)
            return shape;
    return std::nullopt;
}

const TemplateShape& nearestShape(std::int32_t width, std::int32_t height)
{
    const double ratio = double(std::max(width, 1)) / double(std::max(height, 1));
    const TemplateShape* best = &templateShapes().front();
    for (const TemplateShape& shape : templateShapes())
        if (std::abs(std::log(double(shape.width) / shape.height / ratio))
            < std::abs(std::log(double(best->width) / best->height / ratio)))
            best = &shape;
    return *best;
}

ClipCrop fillCrop(std::int32_t mediaWidth, std::int32_t mediaHeight, std::int32_t canvasWidth, std::int32_t canvasHeight)
{
    ClipCrop crop;
    if (mediaWidth <= 0 || mediaHeight <= 0 || canvasWidth <= 0 || canvasHeight <= 0)
        return crop;
    const double media = double(mediaWidth) / mediaHeight;
    const double canvas = double(canvasWidth) / canvasHeight;
    if (media > canvas) {
        const double cut = (1.0 - canvas / media) / 2.0; // too wide: trim the sides
        crop.left = crop.right = cut;
    } else if (media < canvas) {
        const double cut = (1.0 - media / canvas) / 2.0; // too tall: trim top and bottom
        crop.top = crop.bottom = cut;
    }
    return crop;
}

double fillSpeed(Ticks available, Ticks length)
{
    if (length <= 0 || available >= length)
        return 1.0;
    return std::max(0.25, double(std::max<Ticks>(available, 0)) / double(length));
}

} // namespace tve

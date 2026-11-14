import QtQuick
import TechieVideoEditor.Theme

// Drawn over the player (never exported): thirds, centre lines, the title- and action-safe
// areas, where a vertical social app's own buttons and captions cover the picture, and rulers
// in canvas pixels along the top and left edges. Clicks pass through.
Item {
    id: guides
    required property var project
    property bool thirds: false
    property bool centre: false
    property bool safeAreas: false
    property bool rulers: false
    // A vertical social app's layout ("" none, or a profiles id): what its own top bar,
    // captions and buttons cover is shaded, and what stays clear outlined.
    property string social: ""
    readonly property bool any: thirds || centre || safeAreas || rulers || social !== ""
    visible: any

    // Where each kind of vertical (9:16) app puts its own things over the picture, as fractions
    // of the canvas: {x, y, width, height, label}. Typical layouts, not any one app's.
    readonly property var profiles: [
        { id: "feed", title: qsTr("Short-video feed"),
          zones: [{ x: 0, y: 0, width: 1, height: 0.1, label: qsTr("Top bar") },
                  { x: 0, y: 0.78, width: 1, height: 0.22, label: qsTr("Caption, name and sound") },
                  { x: 0.84, y: 0.4, width: 0.16, height: 0.38, label: qsTr("Buttons") }],
          clear: { x: 0.04, y: 0.1, width: 0.78, height: 0.68 } },
        { id: "stories", title: qsTr("Stories"),
          zones: [{ x: 0, y: 0, width: 1, height: 0.14, label: qsTr("Progress and name") },
                  { x: 0, y: 0.82, width: 1, height: 0.18, label: qsTr("Reply bar") }],
          clear: { x: 0.04, y: 0.14, width: 0.92, height: 0.68 } },
        { id: "live", title: qsTr("Live stream"),
          zones: [{ x: 0, y: 0, width: 1, height: 0.12, label: qsTr("Name and viewers") },
                  { x: 0, y: 0.62, width: 0.72, height: 0.38, label: qsTr("Comments") },
                  { x: 0.72, y: 0.82, width: 0.28, height: 0.18, label: qsTr("Gifts and likes") }],
          clear: { x: 0.04, y: 0.12, width: 0.92, height: 0.5 } }
    ]
    readonly property var socialProfile: profiles.find((p) => p.id === social) || null
    // Where titles and faces stay clear of all of it (canvas fractions; null without a profile).
    readonly property var socialClear: socialProfile ? socialProfile.clear : null

    // The canvas inside the item, fitted keeping its aspect ratio.
    readonly property real canvasScale: Math.min(width / project.canvasWidth, height / project.canvasHeight)
    readonly property rect canvasRect: Qt.rect((width - project.canvasWidth * canvasScale) / 2,
                                               (height - project.canvasHeight * canvasScale) / 2,
                                               project.canvasWidth * canvasScale, project.canvasHeight * canvasScale)
    // Canvas pixels between labelled ruler marks: the first step at least ~80 points apart.
    readonly property int rulerStep: {
        for (const step of [10, 20, 50, 100, 200, 250, 500, 1000, 2000])
            if (step * canvasScale >= 80)
                return step
        return 5000
    }

    onCanvasRectChanged: paper.requestPaint()
    onThirdsChanged: paper.requestPaint()
    onCentreChanged: paper.requestPaint()
    onSafeAreasChanged: paper.requestPaint()
    onRulersChanged: paper.requestPaint()
    onSocialChanged: paper.requestPaint()

    Canvas {
        id: paper
        objectName: "guidesCanvas"
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const r = guides.canvasRect
            const line = (x1, y1, x2, y2) => { ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke() }
            // Each guide on a dark line first, so it shows on any picture.
            const stroke = (draw, colour, dash) => {
                for (const pass of [{ c: "#66000000", w: 2.5 }, { c: colour, w: 1 }]) {
                    ctx.strokeStyle = pass.c
                    ctx.lineWidth = pass.w
                    ctx.setLineDash(dash || [])
                    draw()
                }
                ctx.setLineDash([])
            }
            if (guides.thirds)
                stroke(() => {
                    for (const f of [1 / 3, 2 / 3]) {
                        line(r.x + r.width * f, r.y, r.x + r.width * f, r.y + r.height)
                        line(r.x, r.y + r.height * f, r.x + r.width, r.y + r.height * f)
                    }
                }, "#E6FFFFFF")
            if (guides.centre)
                stroke(() => {
                    line(r.x + r.width / 2, r.y, r.x + r.width / 2, r.y + r.height)
                    line(r.x, r.y + r.height / 2, r.x + r.width, r.y + r.height / 2)
                }, String(Theme.color.warning), [6, 4])
            if (guides.safeAreas) {
                // Action safe (90 %) and title safe (80 %).
                for (const area of [{ f: 0.9, dash: [] }, { f: 0.8, dash: [4, 4] }]) {
                    const w = r.width * area.f, h = r.height * area.f
                    stroke(() => ctx.strokeRect(r.x + (r.width - w) / 2, r.y + (r.height - h) / 2, w, h),
                           String(Theme.color.accentText), area.dash)
                }
            }
            if (guides.socialProfile) {
                // What the app covers, shaded and named; what stays clear, outlined.
                ctx.font = "600 11px sans-serif"
                for (const z of guides.socialProfile.zones) {
                    const zx = r.x + z.x * r.width, zy = r.y + z.y * r.height
                    const zw = z.width * r.width, zh = z.height * r.height
                    ctx.fillStyle = "#73000000"
                    ctx.fillRect(zx, zy, zw, zh)
                    ctx.save()
                    ctx.beginPath()
                    ctx.rect(zx, zy, zw, zh)
                    ctx.clip()
                    ctx.strokeStyle = "#33FFFFFF"
                    ctx.lineWidth = 1
                    for (let d = -zh; d < zw; d += 10)
                        line(zx + d, zy + zh, zx + d + zh, zy)
                    ctx.restore()
                    ctx.fillStyle = "#F2FFFFFF"
                    const label = z.label
                    const tw = ctx.measureText(label).width
                    if (tw + 8 <= zw && zh >= 16)
                        ctx.fillText(label, zx + (zw - tw) / 2, zy + zh / 2 + 4)
                }
                const c = guides.socialClear
                stroke(() => ctx.strokeRect(r.x + c.x * r.width, r.y + c.y * r.height, c.width * r.width, c.height * r.height),
                       String(Theme.color.success), [6, 3])
            }
            if (guides.rulers) {
                const band = 18
                ctx.fillStyle = "#B3000000"
                ctx.fillRect(r.x, r.y, r.width, band)
                ctx.fillRect(r.x, r.y + band, band, r.height - band)
                ctx.strokeStyle = "#CCFFFFFF"
                ctx.fillStyle = "#E6FFFFFF"
                ctx.lineWidth = 1
                ctx.font = "10px sans-serif"
                const step = guides.rulerStep, s = guides.canvasScale
                for (let px = 0; px <= guides.project.canvasWidth; px += step / 5) {
                    const x = r.x + px * s
                    const major = Math.round(px) % step === 0
                    line(x, r.y + band - (major ? band * 0.6 : band * 0.25), x, r.y + band)
                    if (major && px > 0)
                        ctx.fillText(String(Math.round(px)), x + 2, r.y + 10)
                }
                for (let px = 0; px <= guides.project.canvasHeight; px += step / 5) {
                    const y = r.y + px * s
                    const major = Math.round(px) % step === 0
                    if (y < r.y + band)
                        continue
                    line(r.x + band - (major ? band * 0.6 : band * 0.25), y, r.x + band, y)
                    if (major) {
                        ctx.save()
                        ctx.translate(r.x + 10, y - 2)
                        ctx.rotate(-Math.PI / 2)
                        ctx.fillText(String(Math.round(px)), 0, 0)
                        ctx.restore()
                    }
                }
            }
        }
    }
}

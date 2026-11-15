// Draws the app icon — the logo's timeline (three clips) and red playhead on a blue rounded
// square, sized and inset like macOS app icons — at every size an .iconset needs, then packs
// it into app/AppIcon.icns with iconutil.
//
//   swift tools/make_icon.swift
import AppKit
import Foundation

let root = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent().deletingLastPathComponent()
let iconset = FileManager.default.temporaryDirectory.appendingPathComponent("AppIcon.iconset")
try? FileManager.default.removeItem(at: iconset)
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)

func colour(_ hex: UInt32, _ alpha: CGFloat = 1) -> CGColor {
    CGColor(red: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
            blue: CGFloat(hex & 0xFF) / 255, alpha: alpha)
}

func draw(_ pixels: Int) -> Data {
    let space = CGColorSpace(name: CGColorSpace.sRGB)!
    let ctx = CGContext(data: nil, width: pixels, height: pixels, bitsPerComponent: 8, bytesPerRow: 0, space: space,
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    let s = CGFloat(pixels) / 1024
    ctx.scaleBy(x: s, y: s)
    // Drawn top-down like the SVG: flip.
    ctx.translateBy(x: 0, y: 1024)
    ctx.scaleBy(x: 1, y: -1)

    // The tile: 824 × 824 inset 100, with the soft shadow macOS icons have.
    let tile = CGRect(x: 100, y: 100, width: 824, height: 824)
    let shape = CGPath(roundedRect: tile, cornerWidth: 185, cornerHeight: 185, transform: nil)
    ctx.saveGState()
    ctx.setShadow(offset: CGSize(width: 0, height: 10), blur: 28, color: colour(0x000000, 0.28))
    ctx.addPath(shape)
    ctx.setFillColor(colour(0x356AE6))
    ctx.fillPath()
    ctx.restoreGState()
    ctx.saveGState()
    ctx.addPath(shape)
    ctx.clip()
    let gradient = CGGradient(colorsSpace: space, colors: [colour(0x5B8CF5), colour(0x2A55CF)] as CFArray,
                              locations: [0, 1])!
    ctx.drawLinearGradient(gradient, start: CGPoint(x: 512, y: 100), end: CGPoint(x: 512, y: 924), options: [])
    ctx.restoreGState()

    // The logo's 32-unit drawing, scaled onto the tile.
    let unit: CGFloat = 824 / 32
    func rect(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat, _ r: CGFloat, _ fill: CGColor) {
        let box = CGRect(x: 100 + x * unit, y: 100 + y * unit, width: w * unit, height: h * unit)
        ctx.addPath(CGPath(roundedRect: box, cornerWidth: r * unit, cornerHeight: r * unit, transform: nil))
        ctx.setFillColor(fill)
        ctx.fillPath()
    }
    rect(6, 8.5, 13, 4, 1.5, colour(0xFFFFFF))
    rect(10, 14, 16, 4, 1.5, colour(0xFFFFFF, 0.78))
    rect(6, 19.5, 9, 4, 1.5, colour(0xFFFFFF, 0.56))
    rect(21, 5, 2, 22, 1, colour(0xFF5A5F))

    let image = ctx.makeImage()!
    return NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:])!
}

for points in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let name = scale == 1 ? "icon_\(points)x\(points).png" : "icon_\(points)x\(points)@2x.png"
        try draw(points * scale).write(to: iconset.appendingPathComponent(name))
    }
}
let out = root.appendingPathComponent("app/AppIcon.icns")
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["-c", "icns", iconset.path, "-o", out.path]
try iconutil.run()
iconutil.waitUntilExit()
try draw(1024).write(to: FileManager.default.temporaryDirectory.appendingPathComponent("AppIcon-1024.png"))
print(iconutil.terminationStatus == 0 ? "wrote \(out.path)" : "iconutil failed")
exit(iconutil.terminationStatus)

// The README's proof board: the app's own screenshots (docs/images, taken by
// tools/make_screenshots.sh) — a whole project on the timeline, then three close-ups —
// on the app's dark canvas, written to assets/readme/showcase.webp (needs cwebp).
//
//   swift assets/readme/source/make_showcase.swift
import AppKit
import CoreText
import Foundation

let root = URL(fileURLWithPath: CommandLine.arguments[0])
    .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
let images = root.appendingPathComponent("docs/images")

func colour(_ hex: UInt32, _ alpha: CGFloat = 1) -> CGColor {
    CGColor(red: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
            blue: CGFloat(hex & 0xFF) / 255, alpha: alpha)
}

func load(_ name: String) -> CGImage {
    let url = images.appendingPathComponent(name)
    guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
          let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
        fatalError("can't read \(url.path)")
    }
    return image
}

// The board, at twice the README's 1200 so it stays sharp on Retina screens.
let width = 2400
let margin: CGFloat = 48, gap: CGFloat = 40
let main = load("template-mode-dark.png")
let mainWidth = CGFloat(width) - 2 * margin
let mainHeight = (mainWidth * CGFloat(main.height) / CGFloat(main.width)).rounded()
let tileWidth = ((mainWidth - 2 * gap) / 3).rounded(.down)
let tileHeight = (tileWidth / 1.6).rounded()
let labelSize: CGFloat = 40
let height = Int(margin + mainHeight + gap + tileHeight + 34 + labelSize * 1.6 + margin)

// Close-ups: (screenshot, crop in its pixels, label).
let closeUps: [(String, CGRect, String)] = [
    ("color-dark.png", CGRect(x: 1330, y: 80, width: 1600, height: 1000), "Colour wheels, curves and scopes"),
    ("transcript-dark.png", CGRect(x: 150, y: 90, width: 1660, height: 1037), "Captions and transcript, on your Mac"),
    ("track-plane-dark.png", CGRect(x: 1100, y: 90, width: 1840, height: 1150), "Plane tracking, titles pinned on"),
]

let space = CGColorSpace(name: CGColorSpace.sRGB)!
let ctx = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0, space: space,
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
// Top-down coordinates, as the layout above is written.
ctx.translateBy(x: 0, y: CGFloat(height))
ctx.scaleBy(x: 1, y: -1)

let board = CGRect(x: 0, y: 0, width: width, height: height)
ctx.addPath(CGPath(roundedRect: board, cornerWidth: 48, cornerHeight: 48, transform: nil))
ctx.setFillColor(colour(0x0F1115))
ctx.fillPath()

func place(_ image: CGImage, crop: CGRect?, in frame: CGRect) {
    let picture = crop.flatMap { image.cropping(to: $0) } ?? image
    let shape = CGPath(roundedRect: frame, cornerWidth: 18, cornerHeight: 18, transform: nil)
    ctx.saveGState()
    ctx.addPath(shape)
    ctx.clip()
    // Images draw bottom-up: flip within the frame.
    ctx.translateBy(x: frame.minX, y: frame.maxY)
    ctx.scaleBy(x: 1, y: -1)
    ctx.interpolationQuality = .high
    ctx.draw(picture, in: CGRect(x: 0, y: 0, width: frame.width, height: frame.height))
    ctx.restoreGState()
    ctx.addPath(shape)
    ctx.setStrokeColor(colour(0x2E3440))
    ctx.setLineWidth(2)
    ctx.strokePath()
}

func label(_ text: String, x: CGFloat, baseline: CGFloat) {
    let font = CTFontCreateUIFontForLanguage(.system, labelSize, nil)!
    let bold = CTFontCreateCopyWithSymbolicTraits(font, labelSize, nil, .traitBold, .traitBold) ?? font
    let attributes: [NSAttributedString.Key: Any] = [
        NSAttributedString.Key(kCTFontAttributeName as String): bold,
        NSAttributedString.Key(kCTForegroundColorAttributeName as String): colour(0xE8EAEE),
    ]
    let line = CTLineCreateWithAttributedString(NSAttributedString(string: text, attributes: attributes))
    ctx.saveGState()
    ctx.textMatrix = CGAffineTransform(scaleX: 1, y: -1) // text upright in the flipped context
    ctx.textPosition = CGPoint(x: x, y: baseline)
    CTLineDraw(line, ctx)
    ctx.restoreGState()
    // Too long for its tile?
    let bounds = CTLineGetTypographicBounds(line, nil, nil, nil)
    if CGFloat(bounds) > tileWidth { print("label too long: \(text)") }
}

place(main, crop: nil, in: CGRect(x: margin, y: margin, width: mainWidth, height: mainHeight))
let rowTop = margin + mainHeight + gap
for (index, (name, crop, text)) in closeUps.enumerated() {
    let x = margin + CGFloat(index) * (tileWidth + gap)
    place(load(name), crop: crop, in: CGRect(x: x, y: rowTop, width: tileWidth, height: tileHeight))
    // The playhead's red, as a small marker before each label.
    ctx.setFillColor(colour(0xFF5A5F))
    ctx.fill(CGRect(x: x, y: rowTop + tileHeight + 34 + 6, width: 6, height: labelSize - 4))
    label(text, x: x + 20, baseline: rowTop + tileHeight + 34 + labelSize - 4)
}

// Written as PNG, then as WebP (with cwebp) for the README: a tenth of the size.
let image = ctx.makeImage()!
let png = FileManager.default.temporaryDirectory.appendingPathComponent("showcase.png")
let out = root.appendingPathComponent("assets/readme/showcase.webp")
try NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:])!.write(to: png)
let cwebp = Process()
cwebp.executableURL = URL(fileURLWithPath: "/usr/bin/env")
cwebp.arguments = ["cwebp", "-quiet", "-q", "88", "-alpha_q", "100", "-m", "6", png.path, "-o", out.path]
try cwebp.run()
cwebp.waitUntilExit()
guard cwebp.terminationStatus == 0 else { fatalError("cwebp failed (brew install webp)") }
print("wrote \(out.path) (\(width) × \(height))")

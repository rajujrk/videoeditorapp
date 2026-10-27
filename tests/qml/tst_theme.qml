import QtQuick
import QtTest
import TechieVideoEditor.Theme

// Every text/background token pair must meet WCAG 2.1 AA in both themes:
// 4.5:1 for text, 3:1 for non-text UI such as the playhead.
TestCase {
    name: "ThemeContrast"

    function linear(channel) {
        return channel <= 0.03928 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4)
    }
    function luminance(c) {
        return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b)
    }
    function contrast(a, b) {
        const la = luminance(a), lb = luminance(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }

    function test_contrast_data() {
        const pairs = [
            ["textPrimary", "canvas", 4.5],
            ["textPrimary", "surface", 4.5],
            ["textPrimary", "surfaceRaised", 4.5],
            ["textPrimary", "surfaceHover", 4.5],
            ["textSecondary", "canvas", 4.5],
            ["textSecondary", "surface", 4.5],
            ["textSecondary", "surfaceRaised", 4.5],
            ["textSecondary", "surfaceHover", 4.5],
            ["accentText", "surface", 4.5],
            ["accentText", "surfaceRaised", 4.5],
            ["accentText", "accentSubtle", 4.5],
            ["textOnAccent", "accent", 4.5],
            ["textOnAccent", "accentHover", 4.5],
            ["textOnAccent", "accentPressed", 4.5],
            ["textPrimary", "accentSubtle", 4.5],
            ["playhead", "surfaceSunken", 3.0],
            ["playhead", "surface", 3.0],
        ]
        const rows = []
        for (const dark of [true, false]) {
            for (const [fg, bg, min] of pairs)
                rows.push({ tag: (dark ? "dark " : "light ") + fg + " on " + bg, dark: dark, fg: fg, bg: bg, min: min })
        }
        return rows
    }

    function test_contrast(data) {
        Theme.dark = data.dark
        const ratio = contrast(Theme.color[data.fg], Theme.color[data.bg])
        verify(ratio >= data.min, data.tag + " is " + ratio.toFixed(2) + ":1, needs " + data.min + ":1")
    }

    function cleanupTestCase() {
        Theme.dark = true
    }
}

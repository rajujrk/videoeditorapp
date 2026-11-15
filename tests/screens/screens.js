.pragma library
// Shared by the screens suite (tests/screens/tst_*.qml, run by tools/make_screenshots.sh).

// The palette the app window sets (app/Main.qml), on the test window, so stock controls,
// popups and dialogs look in the pictures as they do in the app. Call it after a theme change.
function palette(window, Theme) {
    const p = window.palette
    const c = Theme.color
    p.window = c.surface; p.windowText = c.textPrimary
    p.base = c.surfaceRaised; p.alternateBase = c.surfaceHover
    p.text = c.textPrimary; p.button = c.surfaceRaised; p.buttonText = c.textPrimary
    p.brightText = c.textOnAccent; p.highlight = c.accent; p.highlightedText = c.textOnAccent
    p.light = c.surfaceHover; p.midlight = c.borderSubtle; p.mid = c.borderStrong
    p.dark = c.textDisabled; p.placeholderText = c.textDisabled
    p.toolTipBase = c.surfaceRaised; p.toolTipText = c.textPrimary
}

// Where a picture is saved: docs/images/<name>-<dark|light>.png (screensDir is set by the script).
function file(screensDir, name, dark) {
    return screensDir + "/" + name + "-" + (dark ? "dark" : "light") + ".png"
}

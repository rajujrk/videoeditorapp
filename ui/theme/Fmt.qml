pragma Singleton
import QtQuick

// Numbers and dates written the way the app's language writes them (Qt.locale() is set to
// the chosen language, see Language). Sentences take them through qsTr("…%1…").arg(…).
QtObject {
    // `x` with `decimals` places (none by default).
    function num(x, decimals) {
        return Number(x).toLocaleString(Qt.locale(), "f", decimals === undefined ? 0 : decimals)
    }
    // `x` to at most `decimals` places, without trailing zeros (1.5, 2, 1.25).
    function brief(x, decimals) {
        const v = Number(Number(x).toFixed(decimals))
        const s = String(v)
        return num(v, s.indexOf(".") < 0 ? 0 : s.length - s.indexOf(".") - 1)
    }
    // 0..1 as a whole percentage.
    function percent(fraction) {
        return qsTr("%1 %").arg(num(Math.round((fraction || 0) * 100)))
    }
    // Bytes as megabytes.
    function mb(bytes) {
        return qsTr("%1 MB").arg(num(Math.round((bytes || 0) / 1e6)))
    }
    // Seconds with `decimals` places (one by default), e.g. "2.5 s".
    function seconds(s, decimals) {
        return qsTr("%1 s").arg(num(s, decimals === undefined ? 1 : decimals))
    }
    // A date (a Date, or text Date understands) in the language's short form.
    function date(d) {
        return new Date(d).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }
    // A date and time in the language's short form.
    function dateTime(d) {
        return new Date(d).toLocaleString(Qt.locale(), Locale.ShortFormat)
    }
}

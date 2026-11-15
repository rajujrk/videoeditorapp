#include "GlslHighlighter.h"

#include <QRegularExpression>
#include <QTextCharFormat>
#include <QTextDocument>

#include <array>

// The highlighter itself: a pass of rules per line, then comments over everything, carrying
// an open /* … */ to the next line.
class GlslHighlighter::Rules : public QSyntaxHighlighter {
public:
    Rules(GlslHighlighter* owner, QTextDocument* document) : QSyntaxHighlighter(document), m_owner(owner) {}

protected:
    void highlightBlock(const QString& text) override
    {
        static const QRegularExpression keywords(
            QStringLiteral("\\b(if|else|for|while|do|return|break|continue|discard|const|in|out|inout|uniform|"
                           "struct|true|false|precision|highp|mediump|lowp)\\b"));
        static const QRegularExpression types(
            QStringLiteral("\\b(void|bool|int|uint|float|double|[biu]?vec[234]|mat[234](x[234])?|sampler2D)\\b"));
        static const QRegularExpression builtins(
            QStringLiteral("\\b(tve_\\w+|tve[A-Z]\\w*|texture|mix|clamp|smoothstep|step|length|distance|dot|cross|"
                           "normalize|abs|sign|floor|ceil|fract|mod|min|max|pow|exp|log|sqrt|sin|cos|tan|asin|acos|"
                           "atan|radians|degrees|reflect)\\b"));
        static const QRegularExpression numbers(QStringLiteral("\\b\\d+(\\.\\d*)?([eE][-+]?\\d+)?[uUfF]?\\b|\\.\\d+\\b"));

        const auto paint = [&](const QRegularExpression& rule, const QColor& colour, bool bold = false) {
            QTextCharFormat format;
            format.setForeground(colour);
            if (bold)
                format.setFontWeight(QFont::DemiBold);
            for (auto it = rule.globalMatch(text); it.hasNext();) {
                const auto m = it.next();
                setFormat(int(m.capturedStart()), int(m.capturedLength()), format);
            }
        };
        paint(numbers, m_owner->m_number);
        paint(types, m_owner->m_type);
        paint(builtins, m_owner->m_builtin);
        paint(keywords, m_owner->m_keyword, true);

        // Comments last, over anything inside them.
        QTextCharFormat comment;
        comment.setForeground(m_owner->m_comment);
        comment.setFontItalic(true);
        setCurrentBlockState(0);
        qsizetype from = previousBlockState() == 1 ? 0 : -1;
        qsizetype at = 0;
        while (at <= text.size()) {
            if (from < 0) {
                const qsizetype line = text.indexOf(QStringLiteral("//"), at);
                const qsizetype block = text.indexOf(QStringLiteral("/*"), at);
                if (line >= 0 && (block < 0 || line < block)) {
                    setFormat(int(line), int(text.size() - line), comment);
                    return;
                }
                if (block < 0)
                    return;
                from = block;
                at = block + 2;
            }
            const qsizetype end = text.indexOf(QStringLiteral("*/"), at);
            if (end < 0) {
                setFormat(int(from), int(text.size() - from), comment);
                setCurrentBlockState(1);
                return;
            }
            setFormat(int(from), int(end + 2 - from), comment);
            at = end + 2;
            from = -1;
        }
    }

private:
    GlslHighlighter* m_owner;
};

GlslHighlighter::GlslHighlighter(QObject* parent) : QObject(parent) {}

void GlslHighlighter::setDocument(QQuickTextDocument* document)
{
    if (m_document == document)
        return;
    delete m_highlighter;
    m_highlighter = nullptr;
    m_document = document;
    if (document && document->textDocument())
        m_highlighter = new Rules(this, document->textDocument());
    emit documentChanged();
}

void GlslHighlighter::setColour(QColor& member, const QColor& c)
{
    if (member == c)
        return;
    member = c;
    if (m_highlighter)
        m_highlighter->rehighlight();
    emit coloursChanged();
}

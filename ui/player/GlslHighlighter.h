#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QtQml/qqmlregistration.h>

// Colours GLSL in a TextArea for the shader editor: keywords, types, numbers, comments,
// GLSL's own functions and what the app gives an effect (tve_source, tveLuma…). The colours
// come from the theme, so they follow light and dark.
//
//     GlslHighlighter { document: code.textDocument; keyword: Theme.color.accentText; … }
class GlslHighlighter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QColor keyword MEMBER m_keyword WRITE setKeyword NOTIFY coloursChanged)
    Q_PROPERTY(QColor type MEMBER m_type WRITE setType NOTIFY coloursChanged)
    Q_PROPERTY(QColor number MEMBER m_number WRITE setNumber NOTIFY coloursChanged)
    Q_PROPERTY(QColor comment MEMBER m_comment WRITE setComment NOTIFY coloursChanged)
    Q_PROPERTY(QColor builtin MEMBER m_builtin WRITE setBuiltin NOTIFY coloursChanged)

public:
    explicit GlslHighlighter(QObject* parent = nullptr);

    QQuickTextDocument* document() const { return m_document; }
    void setDocument(QQuickTextDocument* document);
    void setKeyword(const QColor& c) { setColour(m_keyword, c); }
    void setType(const QColor& c) { setColour(m_type, c); }
    void setNumber(const QColor& c) { setColour(m_number, c); }
    void setComment(const QColor& c) { setColour(m_comment, c); }
    void setBuiltin(const QColor& c) { setColour(m_builtin, c); }

signals:
    void documentChanged();
    void coloursChanged();

private:
    class Rules;
    void setColour(QColor& member, const QColor& c);

    QPointer<QQuickTextDocument> m_document;
    QSyntaxHighlighter* m_highlighter = nullptr;
    QColor m_keyword{0x6E, 0x95, 0xFF}, m_type{0x3C, 0xC2, 0x8A}, m_number{0xF0, 0xB4, 0x4C},
        m_comment{0xA3, 0xAA, 0xB6}, m_builtin{0xD9, 0x8A, 0x3D};
};

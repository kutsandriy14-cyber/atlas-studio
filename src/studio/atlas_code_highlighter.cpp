#include "atlas_code_highlighter.h"

#include <QTextCharFormat>

namespace atlas::studio {

namespace {

QTextCharFormat makeFormat(const QString &color)
{
    QTextCharFormat format;
    format.setForeground(QColor(color));
    return format;
}

} // namespace

AtlasCodeHighlighter::AtlasCodeHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    const QTextCharFormat keyword = makeFormat(QStringLiteral("#c586c0"));
    const QTextCharFormat builtin = makeFormat(QStringLiteral("#dcdcaa"));
    const QTextCharFormat identifier = makeFormat(QStringLiteral("#9cdcfe"));
    const QTextCharFormat string = makeFormat(QStringLiteral("#ce9178"));
    const QTextCharFormat number = makeFormat(QStringLiteral("#b5cea8"));
    const QTextCharFormat comment = makeFormat(QStringLiteral("#6a9955"));

    m_rules.append({QRegularExpression(QStringLiteral("\\b(on|end|call|set|if|else|elif|loop|break|continue|return|and|or|not|true|false|null)\\b")), keyword});
    m_rules.append({QRegularExpression(QStringLiteral("\\b(event|data|config)\\b")), keyword});

    // Встроенные вызовы вида ui.page.create / runtime.log / io.read.
    m_rules.append({QRegularExpression(QStringLiteral("\\b[a-z0-9]+(?:\\.[a-z0-9]+)+\\b")), builtin});

    // Числа (целые и дробные).
    m_rules.append({QRegularExpression(QStringLiteral("\\b[0-9]+(?:\\.[0-9]+)?\\b")), number});

    // Однострочный комментарий до конца строки.
    m_rules.append({QRegularExpression(QStringLiteral("//.*$")), comment});

    // Строки в двойных кавычках с поддержкой экранирования.
    m_rules.append({QRegularExpression(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"")), string});

    // Простые идентификаторы параметров id=, type= и т.п. после call.
    m_rules.append({QRegularExpression(QStringLiteral("\\b[a-z_][a-z0-9_]*(?==)")), identifier});
}

void AtlasCodeHighlighter::highlightBlock(const QString &text)
{
    for (const TokenRule &rule : m_rules) {
        auto iterator = rule.pattern.globalMatch(text);
        while (iterator.hasNext()) {
            const auto match = iterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}

} // namespace atlas::studio

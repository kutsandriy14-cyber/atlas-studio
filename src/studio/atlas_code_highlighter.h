#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>

#include <QVector>

QT_BEGIN_NAMESPACE
class QTextDocument;
QT_END_NAMESPACE

namespace atlas::studio {

// Подсветка синтаксиса Atlas Code в стиле VS Code Dark+:
// ключевые слова языка (#6a9955), встроенные вызовы (#dcdcaa), строки (#ce9178),
// числа (#b5cea8), комментарии (#6a9955).
class AtlasCodeHighlighter final : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit AtlasCodeHighlighter(QTextDocument *document = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct TokenRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<TokenRule> m_rules;
};

} // namespace atlas::studio

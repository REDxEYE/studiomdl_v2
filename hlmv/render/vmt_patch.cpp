#include "vmt_patch.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

#include <QRegularExpression>
#include <QStringList>

namespace
{
struct Token
{
    enum Kind { Text, Open, Close } kind;
    qsizetype begin, end; // source span, quotes included
    QString text;
};

// KeyValues' tokens: quoted or bare strings, braces; whitespace and // comments separate them.
std::vector<Token> Tokenize(const QString &s)
{
    std::vector<Token> tokens;
    for (qsizetype i = 0; i < s.size();)
    {
        const QChar c = s[i];
        if (c.isSpace() || c == QChar(0xFEFF))
            ++i;
        else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/')
            while (i < s.size() && s[i] != '\n')
                ++i;
        else if (c == '{' || c == '}')
        {
            tokens.push_back({c == '{' ? Token::Open : Token::Close, i, i + 1, {}});
            ++i;
        }
        else if (c == '"')
        {
            const auto end = s.indexOf('"', i + 1);
            if (end < 0)
                throw std::runtime_error("unterminated quoted string in VMT");
            tokens.push_back({Token::Text, i, end + 1, s.mid(i + 1, end - i - 1)});
            i = end + 1;
        }
        else
        {
            qsizetype end = i;
            while (end < s.size() && !s[end].isSpace() && s[end] != '"' && s[end] != '{' && s[end] != '}')
                ++end;
            tokens.push_back({Token::Text, i, end, s.mid(i, end - i)});
            i = end;
        }
    }
    return tokens;
}

struct Pair
{
    QString key;
    qsizetype keyBegin, valueBegin, valueEnd;
};

struct Block
{
    QString name;
    std::vector<Pair> pairs;
    std::vector<Block> blocks;
    qsizetype close = 0; // offset of '}'
};

void ParseBlock(const std::vector<Token> &tokens, size_t &i, Block &block)
{
    while (i < tokens.size())
    {
        const auto &name = tokens[i++];
        if (name.kind == Token::Close)
        {
            block.close = name.begin;
            return;
        }
        if (name.kind != Token::Text || i == tokens.size())
            break;
        const auto &next = tokens[i++];
        if (next.kind == Token::Open)
        {
            auto &child = block.blocks.emplace_back();
            child.name = name.text;
            ParseBlock(tokens, i, child);
        }
        else if (next.kind == Token::Text)
            block.pairs.push_back({name.text, name.begin, next.begin, next.end});
        else
            break;
    }
    throw std::runtime_error("unbalanced braces in VMT");
}

bool Same(const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) == 0; }

Block *Child(Block &block, const QString &name)
{
    const auto it = std::ranges::find_if(block.blocks, [&](const Block &b) { return Same(b.name, name); });
    return it == block.blocks.end() ? nullptr : &*it;
}

const Pair *First(const Block *block, const QString &key)
{
    if (!block)
        return nullptr;
    const auto it = std::ranges::find_if(block->pairs, [&](const Pair &p) { return Same(p.key, key); });
    return it == block->pairs.end() ? nullptr : &*it;
}

QString Quote(const QString &text)
{
    if (text.contains('"') || text.contains('\n') || text.contains('\r'))
        throw std::runtime_error(("VMT values cannot contain quotes or line breaks: " + text).toStdString());
    return '"' + text + '"';
}

qsizetype LineStart(const QString &s, qsizetype at) { return s.lastIndexOf('\n', at - 1) + 1; }

bool OnlySpace(const QString &s, qsizetype begin, qsizetype end)
{
    for (qsizetype i = begin; i < end; ++i)
        if (!s[i].isSpace() && s[i] != QChar(0xFEFF))
            return false;
    return true;
}
} // namespace

QString PatchVmtText(const QString &text, const MaterialEdits &edits)
{
    const auto tokens = Tokenize(text);
    if (tokens.size() < 2 || tokens[0].kind != Token::Text || tokens[1].kind != Token::Open)
        throw std::runtime_error("VMT does not start with a shader block");
    Block root;
    root.name = tokens[0].text;
    size_t next = 2;
    ParseBlock(tokens, next, root);

    for (const auto &[key, value] : edits)
        if (key.isEmpty() || key.contains(QRegularExpression(QStringLiteral(R"([\s"{}])"))))
            throw std::runtime_error(("invalid VMT parameter name: " + key).toStdString());

    const bool patch = Same(root.name, QStringLiteral("patch"));
    Block *fallback = nullptr, *insert = nullptr, *replace = nullptr;
    if (patch)
    {
        insert = Child(root, QStringLiteral("insert"));
        replace = Child(root, QStringLiteral("replace"));
    }
    else
        for (const char *condition : kVmtFallbackConditions)
            if ((fallback = Child(root, QString::fromLatin1(condition))) ||
                (fallback = Child(root, root.name + '_' + QString::fromLatin1(condition))))
                break;
    // Where each parameter lives, in the order the material system reads them (later wins).
    const std::vector<Block *> sources = patch ? std::vector<Block *>{insert, replace} : std::vector<Block *>{&root, fallback};

    struct Change
    {
        qsizetype begin, end;
        QString text;
    };
    std::vector<Change> changes;
    QStringList added;
    for (const auto &[key, value] : edits)
    {
        if (!value)
        {
            for (Block *block : sources)
                if (block)
                    for (const auto &pair : block->pairs)
                        if (Same(pair.key, key))
                        {
                            // Whole line when the pair has it to itself (a trailing comment goes too).
                            const auto start = LineStart(text, pair.keyBegin);
                            auto end = text.indexOf('\n', pair.valueEnd);
                            end = end < 0 ? text.size() : end + 1;
                            const auto rest = text.mid(pair.valueEnd, end - pair.valueEnd).trimmed();
                            if (OnlySpace(text, start, pair.keyBegin) && (rest.isEmpty() || rest.startsWith(QStringLiteral("//"))))
                                changes.push_back({start, end, {}});
                            else
                                changes.push_back({pair.keyBegin, pair.valueEnd, {}});
                        }
            continue;
        }
        const Pair *pair = nullptr;
        for (auto it = sources.rbegin(); it != sources.rend() && !pair; ++it)
            pair = First(*it, key);
        if (pair)
            changes.push_back({pair->valueBegin, pair->valueEnd, Quote(*value)});
        else
            added.append(Quote(key) + ' ' + Quote(*value));
    }

    if (!added.isEmpty())
    {
        const QString eol = text.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
        Block &target = patch && insert ? *insert : root;
        const auto closeLine = LineStart(text, target.close);
        const QString braceIndent = OnlySpace(text, closeLine, target.close) ? text.mid(closeLine, target.close - closeLine) : QString();
        QString indent = braceIndent + '\t';
        if (!target.pairs.empty())
            if (const auto line = LineStart(text, target.pairs.front().keyBegin); OnlySpace(text, line, target.pairs.front().keyBegin))
                indent = text.mid(line, target.pairs.front().keyBegin - line);
        QString lines;
        if (patch && !insert) // a new "insert" block inside the patch's root
        {
            lines = indent + QStringLiteral("\"insert\"") + eol + indent + '{' + eol;
            for (const auto &entry : added)
                lines += indent + '\t' + entry + eol;
            lines += indent + '}' + eol;
        }
        else
            for (const auto &entry : added)
                lines += indent + entry + eol;
        if (OnlySpace(text, closeLine, target.close))
            changes.push_back({closeLine, closeLine, lines});
        else // the brace shares its line with other tokens: insert before it, on new lines
            changes.push_back({target.close, target.close, eol + lines});
    }

    std::ranges::sort(changes, [](const Change &a, const Change &b) { return a.begin > b.begin; });
    QString out = text;
    for (const auto &change : changes)
        out.replace(change.begin, change.end - change.begin, change.text);
    return out;
}

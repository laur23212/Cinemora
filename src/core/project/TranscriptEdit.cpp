#include "core/project/TranscriptEdit.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

QStringList splitOnSpace(const QString &text)
{
    static const QRegularExpression space(QStringLiteral("\\s+"));
    return text.split(space, Qt::SkipEmptyParts);
}

// A word that closes a sentence: ends in . ? ! … or their CJK forms, ignoring closing quotes.
bool endsSentence(const QString &word)
{
    int i = int(word.size()) - 1;
    while (i >= 0 && (word.at(i) == QLatin1Char('"') || word.at(i) == QLatin1Char('\'')
                      || word.at(i) == QChar(0x201D) || word.at(i) == QChar(0x2019)
                      || word.at(i) == QLatin1Char(')')))
        --i;
    if (i < 0)
        return false;
    const QChar c = word.at(i);
    return c == QLatin1Char('.') || c == QLatin1Char('?') || c == QLatin1Char('!')
           || c == QChar(0x2026) || c == QChar(0x3002) || c == QChar(0xFF1F) || c == QChar(0xFF01);
}

TranscriptTokenType typeForText(const QString &text)
{
    return isFillerWord(text) ? TranscriptTokenType::Filler : TranscriptTokenType::Word;
}

} // namespace

QList<TranscriptLine> transcriptLines(const Transcript &transcript, TimeUs inUs, TimeUs outUs,
                                      TimeUs breakOnGapUs, int minSentenceWords, int maxWords)
{
    QList<TranscriptLine> lines;
    TranscriptLine cur;
    int count = 0;
    bool sentenceEnded = false;
    auto flush = [&] {
        if (cur.firstWord >= 0)
            lines.append(cur);
        cur = TranscriptLine{};
        count = 0;
        sentenceEnded = false;
    };

    for (const int i : transcript.wordsInRange(inUs, outUs)) {
        const TranscriptWord &w = transcript.words.at(i);
        if (!isSpeechToken(w)) {
            if (w.type == TranscriptTokenType::AudioEvent)
                flush();
            continue;
        }
        const QString text = w.text.trimmed();
        if (text.isEmpty())
            continue;
        if (cur.firstWord >= 0
            && (w.startUs - cur.endUs >= breakOnGapUs || w.speaker != cur.speaker
                || (sentenceEnded && count >= minSentenceWords) || (maxWords > 0 && count >= maxWords)))
            flush();
        if (cur.firstWord < 0) {
            cur.startUs = w.startUs;
            cur.endUs = w.endUs;
            cur.speaker = w.speaker;
            cur.firstWord = i;
        }
        cur.endUs = std::max(cur.endUs, w.endUs);
        cur.lastWord = i;
        if (!cur.text.isEmpty())
            cur.text += QLatin1Char(' ');
        cur.text += text;
        ++count;
        sentenceEnded = endsSentence(text);
    }
    flush();
    return lines;
}

bool replaceTranscriptWords(Transcript &transcript, int firstWord, int lastWord, const QString &text)
{
    QList<TranscriptWord> &words = transcript.words;
    if (firstWord < 0 || lastWord >= words.size() || firstWord > lastWord)
        return false;

    QList<int> speech;
    for (int i = firstWord; i <= lastWord; ++i) {
        if (isSpeechToken(words.at(i)))
            speech.append(i);
    }
    if (speech.isEmpty())
        return false;

    const QStringList newWords = splitOnSpace(text);
    QStringList oldWords;
    for (const int i : std::as_const(speech))
        oldWords.append(words.at(i).text.trimmed());
    if (oldWords == newWords)
        return false;

    const TimeUs spanStart = words.at(speech.first()).startUs;
    TimeUs spanEnd = spanStart;
    for (const int i : std::as_const(speech))
        spanEnd = std::max(spanEnd, words.at(i).endUs);
    const qint16 speaker = words.at(speech.first()).speaker;

    QList<TranscriptWord> replacement;
    replacement.reserve(newWords.size());
    if (newWords.size() == speech.size()) {
        for (int k = 0; k < speech.size(); ++k) {
            TranscriptWord w = words.at(speech.at(k));
            if (w.text.trimmed() != newWords.at(k)) {
                w.text = newWords.at(k);
                w.type = typeForText(w.text);
                w.confidence = NAN;
            }
            replacement.append(w);
        }
    } else if (!newWords.isEmpty()) {
        int total = 0;
        for (const QString &w : newWords)
            total += std::max(1, int(w.size()));
        const double span = double(std::max<TimeUs>(1, spanEnd - spanStart));
        TimeUs cursor = spanStart;
        int done = 0;
        for (int k = 0; k < newWords.size(); ++k) {
            done += std::max(1, int(newWords.at(k).size()));
            TranscriptWord w;
            w.startUs = cursor;
            w.endUs = k + 1 == newWords.size()
                          ? spanEnd
                          : spanStart + static_cast<TimeUs>(std::llround(span * done / total));
            w.endUs = std::max(w.endUs, w.startUs);
            w.text = newWords.at(k);
            w.type = typeForText(w.text);
            w.speaker = speaker;
            w.interpolated = true;
            replacement.append(w);
            cursor = w.endUs;
        }
    }

    QList<TranscriptWord> out;
    out.reserve(words.size() - speech.size() + replacement.size());
    for (int i = 0; i < firstWord; ++i)
        out.append(words.at(i));
    out.append(replacement);
    for (int i = firstWord; i <= lastWord; ++i) {
        if (words.at(i).type == TranscriptTokenType::AudioEvent)
            out.append(words.at(i));
    }
    for (int i = lastWord + 1; i < words.size(); ++i)
        out.append(words.at(i));
    std::stable_sort(out.begin(), out.end(), [](const TranscriptWord &a, const TranscriptWord &b) {
        return a.startUs < b.startUs;
    });
    words = out;
    return true;
}

QString transcriptTextInWindow(const Transcript &transcript, TimeUs startUs, TimeUs endUs)
{
    QStringList parts;
    if (endUs <= startUs)
        return {};
    // A word whose middle is inside the window overlaps it, so wordsInRange already holds it.
    for (const int i : transcript.wordsInRange(startUs, endUs)) {
        const TranscriptWord &w = transcript.words.at(i);
        if (!isSpeechToken(w))
            continue;
        const QString text = w.text.trimmed();
        if (text.isEmpty())
            continue;
        const TimeUs mid = w.startUs + (std::max(w.endUs, w.startUs) - w.startUs) / 2;
        if (mid >= startUs && mid < endUs)
            parts.append(text);
    }
    return parts.join(QLatin1Char(' '));
}

} // namespace drift

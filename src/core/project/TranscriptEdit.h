#pragma once

#include "core/project/Transcript.h"

#include <QList>
#include <QString>

// Dafin: the pieces behind the editable transcript in the AI panel. Pure functions over a
// Transcript, so they are tested without the app.

namespace drift {

// One line of the transcript panel: the speech tokens words[firstWord..lastWord] (fillers kept,
// spacing and audio events left out), timed in the asset's source µs. `text` is those tokens
// joined by single spaces, so editing it and splitting on white space gives back the same tokens.
struct TranscriptLine
{
    TimeUs startUs = 0;
    TimeUs endUs = 0;
    int firstWord = -1;
    int lastWord = -1;
    int speaker = -1;
    QString text;
};

// Splits the speech in [inUs, outUs) into readable lines. A new line starts after a pause of
// breakOnGapUs or more, when the speaker changes, at an audio event, after a sentence ends once
// the line holds minSentenceWords words, and when it reaches maxWords (0 = no cap).
QList<TranscriptLine> transcriptLines(const Transcript &transcript, TimeUs inUs, TimeUs outUs,
                                      TimeUs breakOnGapUs = 800'000, int minSentenceWords = 4,
                                      int maxWords = 30);

// Replaces the speech tokens in words[firstWord..lastWord] with the words of `text`, split on
// white space. With as many words as before, each new word keeps the time of the one it replaces;
// otherwise the span from the first old word's start to the last one's end is shared out by
// character count and the new words are marked interpolated. A changed word loses its confidence
// and is typed Filler or Word by isFillerWord(); the line's speaker is kept. Spacing tokens in the
// range are dropped and audio events kept. An empty `text` removes the words. Returns false when
// the range holds no speech or nothing would change.
bool replaceTranscriptWords(Transcript &transcript, int firstWord, int lastWord, const QString &text);

// What a caption over the source window [startUs, endUs) says: every speech token whose middle
// falls inside the window, joined by spaces as cuesFromTranscript joins them. Empty when no word
// is left there.
QString transcriptTextInWindow(const Transcript &transcript, TimeUs startUs, TimeUs endUs);

} // namespace drift

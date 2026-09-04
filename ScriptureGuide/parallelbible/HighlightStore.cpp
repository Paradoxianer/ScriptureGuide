/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#include "HighlightStore.h"

#include <algorithm>
#include <stdlib.h>

#include <Locale.h>

#include "../SwordBackend.h"


// #44: a stored highlight's offsets can stop matching the text they
// were taken from -- a module update for Bible text, an edit for a
// note. SG:span:text is the anchor for putting them right: look for the
// snippet again and move the offsets to where it actually is now.
//
// Deliberately the occurrence NEAREST the stored offset, not the first
// one: the same wording can legitimately appear twice in one verse, and
// "first match wins" would silently relocate a highlight to the wrong
// half of it.
//
// Only single-verse spans are healed. A range's offsets belong to two
// different verses with whole verses in between, so the snippet does not
// describe a searchable stretch of any one of them -- see the range
// handling in the caller.
bool
HighlightStore::HealOffsets(const BString& verseText, const BString& snippet,
	int32& start, int32& end)
{
	if (snippet.IsEmpty() || verseText.IsEmpty())
		return false;

	// Offsets are characters, BString::FindFirst() works in bytes.
	int32 startByte = verseText.CountBytes(0, start);
	int32 endByte = verseText.CountBytes(0, end);
	BString current;
	if (startByte >= 0 && endByte > startByte
		&& endByte <= verseText.Length()) {
		verseText.CopyInto(current, startByte, endByte - startByte);
		if (current == snippet)
			return false; // still correct, nothing to do
	}

	int32 bestByte = -1;
	int32 searchFrom = 0;
	while (searchFrom <= verseText.Length()) {
		int32 found = verseText.FindFirst(snippet, searchFrom);
		if (found < 0)
			break;
		if (bestByte < 0
			|| labs((long)found - (long)startByte)
				< labs((long)bestByte - (long)startByte)) {
			bestByte = found;
		}
		searchFrom = found + 1;
	}
	if (bestByte < 0)
		return false;

	// Byte offset back to a character offset: there is no CountChars()
	// over a byte range, so count the prefix itself.
	BString prefix;
	verseText.CopyInto(prefix, 0, bestByte);
	int32 newStart = prefix.CountChars();
	start = newStart;
	end = newStart + snippet.CountChars();
	return true;
}


std::vector<BibleTextDocument::VerseHighlight>
HighlightStore::ForDocument(const std::vector<BookmarkFile>& all,
	const BString& moduleName, BibleTextDocument* document,
	const std::vector<BString>& hiddenColors)
{
	std::vector<BibleTextDocument::VerseHighlight> result;
	if (document == NULL || moduleName.IsEmpty())
		return result;

	// The chapter this document is showing, as a Code() prefix --
	// comparing those answers "same chapter?" without re-parsing any
	// reference under a locale it may not have been written in.
	BString chapterKey(document->Key());
	int32 colon = chapterKey.FindLast(':');
	if (colon > 0) {
		chapterKey.Truncate(colon);
		chapterKey << ":1";
	}
	BString chapterPrefix = BookmarkFile::ChapterCodeFor(chapterKey.String(),
		document->Versification(), CurrentLocaleCode().String());
	if (chapterPrefix.IsEmpty())
		return result;

	for (size_t i = 0; i < all.size(); i++) {
		const BookmarkFile& bookmark = all[i];
		// A highlight with no span module is verse-wide: it belongs to
		// the verse rather than to one translation's character offsets,
		// so it shows in every column (#44's cross-column gesture).
		bool verseWide = !bookmark.HasSpan();
		if (!verseWide && BString(bookmark.SpanModule()) != moduleName)
			continue;

		// Switched off in Options -- matched on the colour rather than
		// the category's name, so renaming a folder does not silently
		// bring its highlights back.
		if (!hiddenColors.empty()) {
			BString value = FormatHighlightColor(bookmark.Color());
			if (std::find(hiddenColors.begin(), hiddenColors.end(), value)
					!= hiddenColors.end()) {
				continue;
			}
		}

		BString code = bookmark.Code();
		if (code.Length() < 9 || bookmark.ChapterCode() != chapterPrefix)
			continue;

		// A stored range is ONE bookmark covering several verses; the
		// renderer works one verse at a time, so expand it here: the
		// first verse from its start offset to its end, the last from
		// its beginning to the end offset, everything between whole.
		int firstVerse = atoi(code.String() + 6);
		int lastVerse = bookmark.SpanEndVerse() > firstVerse
			? bookmark.SpanEndVerse() : firstVerse;

		// Heal a single-verse span whose offsets no longer describe the
		// text they were taken from, and write the correction back so it
		// only has to happen once -- the same self-healing shape
		// BookmarkFile::SetTo() already applies to SG:code.
		int32 healedStart = bookmark.SpanStart();
		int32 healedEnd = bookmark.SpanEnd();
		if (!verseWide && firstVerse == lastVerse
			&& bookmark.SpanText()[0] != '\0') {
			BString verseText = document->VerseText(firstVerse);
			if (HealOffsets(verseText, BString(bookmark.SpanText()),
					healedStart, healedEnd)) {
				BookmarkFile healed;
				if (healed.SetTo(bookmark.Path()) == B_OK) {
					healed.SetSpan(healed.SpanModule(), healedStart,
						healedEnd, healed.SpanText(),
						healed.SpanEndVerse());
					healed.Save();
				}
			}
		}

		for (int verse = firstVerse; verse <= lastVerse; verse++) {
			int32 verseLength = document->VerseTextLength(verse);
			if (verseLength <= 0)
				continue;

			BibleTextDocument::VerseHighlight highlight;
			highlight.verse = verse;
			highlight.start = (verseWide || verse != firstVerse)
				? 0 : healedStart;
			highlight.end = (verseWide || verse != lastVerse)
				? verseLength : healedEnd;
			if (highlight.end > verseLength)
				highlight.end = verseLength;
			if (highlight.end <= highlight.start)
				continue;
			highlight.color = bookmark.Color();
			result.push_back(highlight);
		}
	}

	return result;
}

// Deletes every stored highlight of this module that overlaps the given
// range in the given verse -- the palette's "remove" cell. Overlap
// rather than exact match on purpose: the user selects roughly over a
// mark to clear it, not precisely the range they originally drew.
void
HighlightStore::Remove(const BString& moduleName,
	const HighlightRange& range)
{
	std::vector<BookmarkFile> all = BookmarkFile::ListHighlights();

	// The chapter the selection is in, same prefix comparison.
	BString selectionChapter = BookmarkFile::ChapterCodeFor(
		range.reference.String(), "", CurrentLocaleCode().String());
	if (selectionChapter.IsEmpty())
		return;

	for (size_t i = 0; i < all.size(); i++) {
		BookmarkFile& bookmark = all[i];
		if (BString(bookmark.SpanModule()) != moduleName)
			continue;

		BString code = bookmark.Code();
		if (code.Length() < 9 || bookmark.ChapterCode() != selectionChapter)
			continue;

		// Verse ranges overlap when neither ends before the other
		// begins; within a shared verse the character offsets decide.
		int firstVerse = atoi(code.String() + 6);
		int lastVerse = bookmark.SpanEndVerse() > firstVerse
			? bookmark.SpanEndVerse() : firstVerse;
		if (lastVerse < range.verse || firstVerse > range.endVerse)
			continue;
		if (firstVerse == lastVerse && range.verse == range.endVerse
			&& (bookmark.SpanEnd() <= range.start
				|| bookmark.SpanStart() >= range.end)) {
			continue;
		}

		bookmark.Remove();
	}

}

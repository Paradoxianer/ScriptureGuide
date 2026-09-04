/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef HIGHLIGHT_STORE_H
#define HIGHLIGHT_STORE_H

#include <GraphicsDefs.h>
#include <String.h>

#include <vector>

#include "BibleTextDocument.h"
#include "BookmarkFile.h"


// #44: everything that reads, matches, heals and deletes stored
// highlights. It used to live half in ParallelBibleView (finding,
// filtering, healing, removing) and half in BookmarkFile (the files and
// folders themselves), which meant no single place answered "how does a
// highlight get from disk onto the screen".
//
// A highlight is an ordinary BookmarkFile carrying a colour; the file
// handling stays in BookmarkFile, and this sits on top of it.
class HighlightStore {
public:
	// One contiguous stretch of text to mark. A range spanning several
	// verses stays ONE highlight -- `start` is an offset into `verse`,
	// `end` one into `endVerse`, everything between is covered whole --
	// because splitting it per verse would throw away the very thing
	// that makes it convertible into a verse-list entry like
	// "1. Mose 1:4-10" later.
	struct HighlightRange {
		int		verse;
		int		endVerse;
		int32	start;
		int32	end;
		BString	reference;
		BString	text;
	};

	// Every stored highlight, read fresh. Small files, but a directory
	// walk -- read once per refresh and hand the result to ForDocument()
	// per column rather than repeating it.
	static std::vector<BookmarkFile>	All()
											{ return BookmarkFile::ListHighlights(); }

	// The ones belonging to `moduleName` and the chapter `document` is
	// showing, expanded to one entry per verse and with `hiddenColors`
	// (see Options > Highlight Colours) left out.
	static std::vector<BibleTextDocument::VerseHighlight>	ForDocument(
											const std::vector<BookmarkFile>& all,
											const BString& moduleName,
											BibleTextDocument* document,
											const std::vector<BString>& hiddenColors);

	// Deletes every stored highlight of this module overlapping `range`.
	static void					Remove(const BString& moduleName,
									const HighlightRange& range);

	// Moves `start`/`end` back onto `snippet` when the verse text has
	// shifted under them, returning whether it had to. Offsets are
	// characters, the search is bytes, and the difference is the whole
	// difficulty -- public because it is a pure function on two strings
	// with no store state behind it, and worth testing on its own.
	static bool					HealOffsets(const BString& verseText,
									const BString& snippet,
									int32& start, int32& end);
};

#endif // HIGHLIGHT_STORE_H

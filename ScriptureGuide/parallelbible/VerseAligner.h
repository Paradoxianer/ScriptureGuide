/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef VERSE_ALIGNER_H
#define VERSE_ALIGNER_H

#include <vector>

#include "BibleTextDocument.h"


// Pads each verse's paragraph with extra bottom spacing so the same verse
// starts at the same vertical offset in every column, while the text
// within each column still flows normally (unlike a fixed-row-height
// table layout). As a consequence, every aligned column ends up with the
// same total document height, so a single shared BScrollBar keeps them
// all in sync without any further work.
//
// columnWidths is parallel to columns -- each column is measured at its
// own width (the notes column is narrower than the Bible columns, see
// ParallelBibleView::_NotesColumnWidth()), since a shared width would
// measure at least one of them at the wrong wrap width and produce
// alignment spacing that doesn't match what actually gets rendered.
class VerseAligner {
public:
	static	void				Align(
									const std::vector<BibleTextDocument*>&
										columns,
									const std::vector<float>& columnWidths);
};

#endif // VERSE_ALIGNER_H

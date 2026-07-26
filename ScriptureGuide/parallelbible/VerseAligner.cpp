/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "VerseAligner.h"

#include <map>

#include "ParagraphLayout.h"


void
VerseAligner::Align(const std::vector<BibleTextDocument*>& columns,
	const std::vector<float>& columnWidths)
{
	if (columns.size() < 2 || columns.size() != columnWidths.size())
		return;

	// Measurements below must reflect each verse's natural, un-padded
	// height. A paragraph's current ParagraphStyle may already carry
	// SpacingBottom from a previous Align() pass (e.g. after a resize);
	// measuring that directly would treat padding as content and pile
	// more padding on top of it every time this runs. Clear first.
	for (size_t c = 0; c < columns.size(); c++) {
		if (columns[c] != NULL)
			columns[c]->SetVerseSpacing(std::map<int, float>());
	}

	int maxVerse = 0;
	for (size_t c = 0; c < columns.size(); c++) {
		BibleTextDocument* document = columns[c];
		if (document == NULL)
			continue;
		int32 count = document->CountParagraphs();
		for (int32 i = 0; i < count; i++) {
			int verse = document->VerseForParagraphIndex(i);
			if (verse > maxVerse)
				maxVerse = verse;
		}
	}

	std::vector<std::map<int, float> > spacing(columns.size());

	// Processed in groups, not one verse at a time: a linked commentary
	// entry (see BibleTextDocument::IsLinkedToPrevious(), #10) puts its
	// whole, possibly very long text at the first verse it covers and
	// leaves the rest of that span practically empty in that column,
	// which -- aligned one verse at a time -- forced every OTHER
	// column's entire share of the extra height onto that one first
	// verse's row (reported: notes/commentary visibly "shifted", the
	// span's real content buried under one huge gap instead of flowing
	// naturally across the verses it actually covers). A group is a
	// verse plus every following verse ANY open column considers linked
	// to the one before it; a normal, unlinked verse is its own
	// group of one, so this reduces to the exact same per-verse
	// alignment as before wherever nothing is linked at all.
	int verse = 1;
	while (verse <= maxVerse) {
		int groupEnd = verse;
		while (groupEnd + 1 <= maxVerse) {
			bool linked = false;
			for (size_t c = 0; c < columns.size() && !linked; c++) {
				if (columns[c] != NULL
					&& columns[c]->IsLinkedToPrevious(groupEnd + 1)) {
					linked = true;
				}
			}
			if (!linked)
				break;
			groupEnd++;
		}

		// Each column's natural height is the SUM across the whole
		// group, not a single verse's -- the group's tallest column
		// sets how much total space the group needs; every other
		// column's shortfall gets made up across ITS OWN verses
		// actually present in the group, further down.
		std::vector<float> groupNatural(columns.size(), -1.0f);
		std::vector<int32> presentCount(columns.size(), 0);
		float maxGroupHeight = 0.0f;

		for (size_t c = 0; c < columns.size(); c++) {
			BibleTextDocument* document = columns[c];
			if (document == NULL)
				continue;

			float total = 0.0f;
			int32 count = 0;
			for (int v = verse; v <= groupEnd; v++) {
				int32 index = document->ParagraphIndexForVerse(v);
				if (index < 0)
					continue;

				ParagraphLayout layout;
				layout.SetWidth(columnWidths[c]);
				layout.SetParagraph(document->ParagraphAtIndex(index));
				total += layout.Height();
				count++;
			}

			if (count > 0) {
				groupNatural[c] = total;
				presentCount[c] = count;
				if (total > maxGroupHeight)
					maxGroupHeight = total;
			}
		}

		for (size_t c = 0; c < columns.size(); c++) {
			if (groupNatural[c] < 0.0f || presentCount[c] <= 0)
				continue;
			float deficit = maxGroupHeight - groupNatural[c];
			if (deficit <= 0.1f)
				continue;

			// Spread evenly across this column's own verses actually
			// present in the group -- the whole point of grouping at
			// all -- with any float-rounding remainder going to the
			// last one so the group's total still comes out exact.
			BibleTextDocument* document = columns[c];
			float perVerse = deficit / presentCount[c];
			float assigned = 0.0f;
			int32 remaining = presentCount[c];
			for (int v = verse; v <= groupEnd; v++) {
				int32 index = document->ParagraphIndexForVerse(v);
				if (index < 0)
					continue;
				remaining--;
				float share = (remaining == 0) ? (deficit - assigned)
					: perVerse;
				assigned += share;
				spacing[c][v] = share;
			}
		}

		verse = groupEnd + 1;
	}

	for (size_t c = 0; c < columns.size(); c++) {
		if (columns[c] != NULL)
			columns[c]->SetVerseSpacing(spacing[c]);
	}
}

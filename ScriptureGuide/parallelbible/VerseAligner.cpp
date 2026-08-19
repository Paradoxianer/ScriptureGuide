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
	if (columns.size() != columnWidths.size())
		return;

	// Measurements below must reflect each verse's natural, un-padded
	// height. A paragraph's current ParagraphStyle may already carry
	// SpacingBottom from a previous Align() pass (e.g. after a resize);
	// measuring that directly would treat padding as content and pile
	// more padding on top of it every time this runs. Clear first.
	for (size_t c = 0; c < columns.size(); c++) {
		if (columns[c] != NULL)
			columns[c]->SetRowSpacing(std::map<int32, float>());
	}

	// Deliberately AFTER the clear, not before it. A single column has
	// nothing to align against, but it may well be carrying padding from
	// back when it did -- a chain that was just split apart, or one whose
	// partner was removed. Returning before the clear left that padding
	// in place permanently, since nothing else ever removes it: reported
	// live as a column keeping its old, stretched verse spacing after
	// being disconnected, and keeping it even after navigating to an
	// entirely different book.
	if (columns.size() < 2)
		return;

	// Rows are identified by STEP -- position in what the chain asked
	// each column to render -- not by verse number. In a chapter the two
	// are the same thing less one; across a verse list (#47) they are
	// not, because Genesis 1:1 and Psalms 1:1 are both verse 1. And not
	// by paragraph index either: a Bible column leaves out verses its
	// module has nothing for, so the same row sits at different paragraph
	// indices in different columns.
	int32 maxStep = 0;
	for (size_t c = 0; c < columns.size(); c++) {
		if (columns[c] != NULL && columns[c]->SequenceLength() > maxStep)
			maxStep = columns[c]->SequenceLength();
	}

	std::vector<std::map<int32, float> > spacing(columns.size());

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
	int32 step = 0;
	while (step < maxStep) {
		int32 groupEnd = step;
		while (groupEnd + 1 < maxStep) {
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
			for (int32 v = step; v <= groupEnd; v++) {
				int32 index = document->ParagraphIndexForStep(v);
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
			for (int32 v = step; v <= groupEnd; v++) {
				int32 index = document->ParagraphIndexForStep(v);
				if (index < 0)
					continue;
				remaining--;
				float share = (remaining == 0) ? (deficit - assigned)
					: perVerse;
				assigned += share;
				spacing[c][v] = share;
			}
		}

		step = groupEnd + 1;
	}

	for (size_t c = 0; c < columns.size(); c++) {
		if (columns[c] != NULL)
			columns[c]->SetRowSpacing(spacing[c]);
	}
}

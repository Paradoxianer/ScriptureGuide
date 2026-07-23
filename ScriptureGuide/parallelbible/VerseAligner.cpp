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

	for (int verse = 1; verse <= maxVerse; verse++) {
		float maxHeight = 0.0f;
		std::vector<float> natural(columns.size(), -1.0f);

		for (size_t c = 0; c < columns.size(); c++) {
			BibleTextDocument* document = columns[c];
			if (document == NULL)
				continue;

			int32 index = document->ParagraphIndexForVerse(verse);
			if (index < 0)
				continue;

			ParagraphLayout layout;
			layout.SetWidth(columnWidths[c]);
			layout.SetParagraph(document->ParagraphAtIndex(index));

			float height = layout.Height();
			natural[c] = height;
			if (height > maxHeight)
				maxHeight = height;
		}

		for (size_t c = 0; c < columns.size(); c++) {
			if (natural[c] < 0.0f)
				continue;
			float extra = maxHeight - natural[c];
			if (extra > 0.1f)
				spacing[c][verse] = extra;
		}
	}

	for (size_t c = 0; c < columns.size(); c++) {
		if (columns[c] != NULL)
			columns[c]->SetVerseSpacing(spacing[c]);
	}
}

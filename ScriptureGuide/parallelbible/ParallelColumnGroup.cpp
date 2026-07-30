/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "ParallelColumnGroup.h"

#include <algorithm>

#include <Font.h>
#include <Language.h>
#include <Locale.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <StringView.h>

#include <versekey.h>

#include "BibleTextDocument.h"
#include "NoteFieldView.h"
#include "ParagraphLayout.h"
#include "PersonalNotesModule.h"
#include "TextDocumentView.h"
#include "VerseAligner.h"

using namespace sword;


// Same helper duplicated in ParallelBibleView.cpp/BibleTextDocument.cpp/
// NoteFieldView.cpp -- see any of those for why (locale must be set before
// VerseKey::setText() sees localized book names, or it silently mis-parses).
static void
SetVerseKeyLocale(VerseKey& key)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	key.setLocale(language.Code());
}


// Mirrors ParallelBibleView::kColumnSpacing/kNoteVerseLabelWidth/
// kBibleColumnInset exactly -- kept as this class's own constants rather
// than reaching into ParallelBibleView's (private, and conceptually this
// class shouldn't need to know about its owner's internals beyond what's
// handed to it) so a detached group's spacing/inset always matches a
// group-0 column's, keeping the two visually consistent.
const float ParallelColumnGroup::kColumnSpacing = 8.0f;
const float ParallelColumnGroup::kNoteVerseLabelWidth = 20.0f;
const float ParallelColumnGroup::kBibleColumnInset = 4.0f;


ParallelColumnGroup::ParallelColumnGroup(const char* name,
	PersonalNotesModule* notesModule, ParallelBibleView* owner, int32 group)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS),
	fNotesModule(notesModule),
	fScrollView(NULL),
	fOwner(owner),
	fGroup(group),
	fBibleColumnWidth(0.0f),
	fHasNotes(false),
	fNotesColumnWidth(0.0f),
	fNaturalWidth(0.0f),
	fNaturalHeight(0.0f)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// Same relationship ParallelBibleView has to its own outer
	// BScrollView (LogosMainWindow.cpp) -- this constructs the wrapper
	// around itself; the caller (ParallelBibleView::_RebuildLayout())
	// retrieves it via ScrollView() and AddChild()s it in turn.
	BString scrollName(name);
	scrollName << "Scroll";
	fScrollView = new BScrollView(scrollName.String(), this, 0, false, true,
		B_NO_BORDER);
}


ParallelColumnGroup::~ParallelColumnGroup()
{
	// ~BView() deletes every remaining child (confirmed via the Haiku
	// Book: "Deletes the view and all children"). fBibleViews are
	// non-owning (see the class comment) -- ParallelBibleView::
	// _RebuildLayout() deletes the actual TextDocumentView objects
	// itself, same as for a group-0 column -- so they have to be
	// detached (not deleted) here first, or that delete would be a
	// double-free. This group's own notes fields ARE fully owned (built
	// in _RebuildNoteFields()), so they're left attached and cascade-
	// deleted normally by ~BView().
	for (size_t i = 0; i < fBibleViews.size(); i++)
		RemoveChild(fBibleViews[i]);
}


void
ParallelColumnGroup::AddBibleMember(TextDocumentView* view,
	BibleTextDocument* document)
{
	AddChild(view);
	fBibleViews.push_back(view);
	fBibleDocuments.push_back(document);
}


void
ParallelColumnGroup::SetHasNotes(bool hasNotes)
{
	if (hasNotes == fHasNotes)
		return;

	fHasNotes = hasNotes;
	if (fHasNotes)
		_RebuildNoteFields();
	else {
		for (size_t i = 0; i < fNoteFields.size(); i++) {
			fNoteFields[i]->RemoveSelf();
			delete fNoteFields[i];
		}
		fNoteFields.clear();
		for (size_t i = 0; i < fNoteVerseLabels.size(); i++) {
			fNoteVerseLabels[i]->RemoveSelf();
			delete fNoteVerseLabels[i];
		}
		fNoteVerseLabels.clear();
	}
}


status_t
ParallelColumnGroup::SetKey(const char* key)
{
	fCurrentKey = key;

	for (size_t i = 0; i < fBibleDocuments.size(); i++)
		fBibleDocuments[i]->SetKey(key);

	// Same reasoning as ParallelBibleView::SetKey(): a leftover selection
	// only makes sense against the chapter it was made in.
	for (size_t i = 0; i < fBibleViews.size(); i++)
		fBibleViews[i]->SetSelection(0, 0);

	if (fHasNotes)
		_RebuildNoteFields();

	// Reuse whatever widths the last Relayout() call (from
	// ParallelBibleView::_Realign()) established -- SetKey() doesn't
	// change this group's own available width, so there's nothing fresh
	// to query here.
	Relayout(fBibleColumnWidth, fNotesColumnWidth);

	VerseKey verseKey;
	SetVerseKeyLocale(verseKey);
	verseKey.setText(fCurrentKey.String());
	int verse = verseKey.getVerse();
	if (verse > 1) {
		float y = 0.0f;
		for (int v = 1; v < verse; v++)
			y += _RowHeight(v);
		ScrollTo(Bounds().left, y);
	} else
		ScrollTo(Bounds().left, 0.0f);

	return B_OK;
}


void
ParallelColumnGroup::HighlightVerse(int startVerse, int endVerse)
{
	for (size_t i = 0; i < fBibleDocuments.size(); i++) {
		int32 start, end;
		if (fBibleDocuments[i]->TextRangeForVerseRange(startVerse, endVerse,
				start, end)) {
			fBibleViews[i]->SetSelection(start, end);
		} else {
			fBibleViews[i]->SetSelection(0, 0);
		}
	}
}


// Reads a verse's row height back from the first Bible member's paragraph
// -- mirrors ParallelBibleView::_RowHeight() exactly, just scoped to this
// group's own (already-aligned, if 2+ members) documents instead of
// searching for a group-0 one.
float
ParallelColumnGroup::_RowHeight(int verse) const
{
	if (!fBibleDocuments.empty()) {
		BibleTextDocument* document = fBibleDocuments[0];
		int32 index = document->ParagraphIndexForVerse(verse);
		if (index >= 0) {
			ParagraphLayout layout;
			layout.SetWidth(fBibleColumnWidth);
			layout.SetParagraph(document->ParagraphAtIndex(index));
			return layout.Height();
		}
	}
	return 0.0f;
}


void
ParallelColumnGroup::_RebuildNoteFields()
{
	for (size_t i = 0; i < fNoteFields.size(); i++) {
		fNoteFields[i]->RemoveSelf();
		delete fNoteFields[i];
	}
	fNoteFields.clear();
	for (size_t i = 0; i < fNoteVerseLabels.size(); i++) {
		fNoteVerseLabels[i]->RemoveSelf();
		delete fNoteVerseLabels[i];
	}
	fNoteVerseLabels.clear();

	if (fNotesModule == NULL || fCurrentKey.IsEmpty())
		return;

	VerseKey chapterKey;
	SetVerseKeyLocale(chapterKey);
	chapterKey.setText(fCurrentKey.String());
	int verseCount = chapterKey.getVerseMax();

	for (int verse = 1; verse <= verseCount; verse++) {
		VerseKey verseKey;
		SetVerseKeyLocale(verseKey);
		verseKey.setText(fCurrentKey.String());
		verseKey.setVerse(verse);

		BString verseLabel;
		verseLabel << verse;
		BStringView* label = new BStringView("noteVerseLabel",
			verseLabel.String());
		AddChild(label);
		fNoteVerseLabels.push_back(label);

		NoteFieldView* field = new NoteFieldView(verse, fNotesModule,
			fCurrentKey, fOwner, fGroup);
		field->SetText(fNotesModule->GetNote(verseKey.getText()).String());
		AddChild(field);
		fNoteFields.push_back(field);
	}
}


void
ParallelColumnGroup::Relayout(float bibleColumnWidth, float notesColumnWidth)
{
	fBibleColumnWidth = bibleColumnWidth;
	fNotesColumnWidth = notesColumnWidth;

	std::vector<BibleTextDocument*> columns;
	std::vector<float> widths;
	for (size_t i = 0; i < fBibleDocuments.size(); i++) {
		columns.push_back(fBibleDocuments[i]);
		widths.push_back(fBibleColumnWidth);
	}

	if (columns.size() >= 2)
		VerseAligner::Align(columns, widths);

	_PositionMembers();

	for (size_t i = 0; i < fBibleViews.size(); i++) {
		fBibleViews[i]->Relayout();
		fBibleViews[i]->Invalidate();
	}
}


// Positions every member left to right -- same technique as
// ParallelBibleView::_PositionColumns(), scoped to just this group's own
// members and with no header row/dividers/add-button to worry about.
void
ParallelColumnGroup::_PositionMembers()
{
	float x = 0.0f;
	float contentHeight = 0.0f;

	for (size_t i = 0; i < fBibleViews.size(); i++) {
		TextDocumentView* view = fBibleViews[i];
		float width = fBibleColumnWidth;

		// The document may have been rebuilt more than once in a row (see
		// VerseAligner::Align() above); force a fresh measurement rather
		// than risk GetHeightForWidth() reading a stale layout.
		view->Relayout();

		float min, max, preferred;
		view->GetHeightForWidth(width, &min, &max, &preferred);

		view->MoveTo(x, 0.0f);
		view->ResizeTo(width, preferred);

		contentHeight = std::max(contentHeight, preferred);
		x += width + kColumnSpacing;
	}

	if (fHasNotes) {
		float fieldWidth = std::max(0.0f,
			fNotesColumnWidth - kNoteVerseLabelWidth);
		float y = 0.0f;
		for (size_t v = 0; v < fNoteFields.size(); v++) {
			float rowHeight = _RowHeight((int)(v + 1));

			BFont font;
			fNoteVerseLabels[v]->GetFont(&font);
			font_height fontHeight;
			font.GetHeight(&fontHeight);
			float lineHeight = fontHeight.ascent + fontHeight.descent
				+ fontHeight.leading;

			fNoteVerseLabels[v]->MoveTo(x, y + kBibleColumnInset);
			fNoteVerseLabels[v]->ResizeTo(kNoteVerseLabelWidth, lineHeight);

			fNoteFields[v]->MoveTo(x + kNoteVerseLabelWidth, y);
			fNoteFields[v]->ResizeTo(fieldWidth, rowHeight);

			y += rowHeight;
		}
		contentHeight = std::max(contentHeight, y);
		x += fNotesColumnWidth + kColumnSpacing;
	}

	fNaturalWidth = fBibleViews.empty() && !fHasNotes ? 0.0f
		: x - kColumnSpacing;
	fNaturalHeight = contentHeight;
}

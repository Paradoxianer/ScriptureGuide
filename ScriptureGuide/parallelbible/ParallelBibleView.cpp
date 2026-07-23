/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "ParallelBibleView.h"

#include <algorithm>

#include <ScrollBar.h>

#include <versekey.h>

#include "VerseAligner.h"

const float ParallelBibleView::kMinColumnWidth = 150.0f;
const float ParallelBibleView::kColumnSpacing = 8.0f;


// Persists edits made in the notes column back into the personal SWORD
// module, one verse (== one paragraph) at a time.
class NotesWriteBackListener : public TextListener {
public:
	NotesWriteBackListener(BibleTextDocument* document,
		PersonalNotesModule* notes, const BString& chapterKey)
		:
		fDocument(document),
		fNotes(notes),
		fChapterKey(chapterKey)
	{
	}

	virtual void TextChanged(const TextChangedEvent& event)
	{
		// _Rebuild() fires this notification mid-flight, once for the
		// Remove() that empties the document and once for the Append()
		// calls that repopulate it -- the paragraph range from the first
		// can already be out of bounds by the time this runs, since the
		// document may have fewer (or different) paragraphs than the
		// event's range implies once the whole rebuild has settled.
		int32 first = event.FirstChangedParagraph();
		int32 last = std::min(first + event.ChangedParagraphCount(),
			fDocument->CountParagraphs());
		for (int32 i = first; i < last; i++) {
			int verse = fDocument->VerseForParagraphIndex(i);
			if (verse < 0)
				continue;

			VerseKey key;
			key.setText(fChapterKey.String());
			key.setVerse(verse);

			BString text = fDocument->ParagraphAtIndex(i).Text();
			fNotes->SetNote(key.getText(), text.String());
		}
	}

private:
	BibleTextDocument*		fDocument;
	PersonalNotesModule*	fNotes;
	BString					fChapterKey;
};


ParallelBibleView::ParallelBibleView(const char* name, SWMgr* manager,
	float initialWidth)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS),
	fManager(manager),
	fNotes(NULL),
	fNotesView(NULL),
	fInitialWidth(initialWidth),
	fContentHeight(0.0f),
	fContentWidth(0.0f)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


ParallelBibleView::~ParallelBibleView()
{
	delete fNotes;
}


void
ParallelBibleView::AttachedToWindow()
{
	BView::AttachedToWindow();
	_RebuildLayout();
}


void
ParallelBibleView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	_Realign();
}


status_t
ParallelBibleView::AddColumn(const char* moduleName)
{
	if (fManager == NULL)
		return B_NO_INIT;

	SWModule* module = fManager->getModule(moduleName);
	if (module == NULL)
		return B_NAME_NOT_FOUND;

	// Render filters (GBFPlain etc.) are expected to already be configured
	// on fManager, the same way SwordBackend configures its SWMgr with a
	// MarkupFilterMgr for every module it manages.
	if (!fCurrentKey.IsEmpty())
		module->setKey(fCurrentKey.String());

	fModules.push_back(module);
	fDocuments.push_back(
		BReference<BibleTextDocument>(new BibleTextDocument(module), true));

	_RebuildLayout();
	return B_OK;
}


status_t
ParallelBibleView::RemoveColumn(int32 index)
{
	if (index < 0 || (size_t)index >= fModules.size())
		return B_BAD_INDEX;

	fModules.erase(fModules.begin() + index);
	fDocuments.erase(fDocuments.begin() + index);

	_RebuildLayout();
	return B_OK;
}


int32
ParallelBibleView::CountColumns() const
{
	return (int32)fModules.size();
}


status_t
ParallelBibleView::SetNotesEnabled(bool enabled)
{
	if (enabled == (fNotes != NULL))
		return B_OK;

	if (enabled) {
		fNotes = new PersonalNotesModule();
		status_t status = fNotes->Open();
		if (status != B_OK) {
			delete fNotes;
			fNotes = NULL;
			return status;
		}

		fNotesDocument = BReference<BibleTextDocument>(
			new BibleTextDocument(fNotes->Module()), true);
		fNotesDocument->SetShowVerseNumbers(false);
		fNotesDocument->SetSkipEmptyVerses(false);
		if (!fCurrentKey.IsEmpty())
			fNotesDocument->SetKey(fCurrentKey.String());

		fNotesDocument->AddListener(TextListenerRef(
			new NotesWriteBackListener(fNotesDocument.Get(), fNotes,
				fCurrentKey), true));
	} else {
		fNotesDocument = BReference<BibleTextDocument>();
		delete fNotes;
		fNotes = NULL;
	}

	_RebuildLayout();
	return B_OK;
}


status_t
ParallelBibleView::SetKey(const char* key)
{
	fCurrentKey = key;

	for (size_t i = 0; i < fDocuments.size(); i++)
		fDocuments[i]->SetKey(key);
	if (fNotesDocument.Get() != NULL)
		fNotesDocument->SetKey(key);

	_Realign();
	return B_OK;
}


status_t
ParallelBibleView::NextChapter()
{
	for (size_t i = 0; i < fDocuments.size(); i++)
		fDocuments[i]->NextChapter();
	if (fNotesDocument.Get() != NULL)
		fNotesDocument->NextChapter();

	if (!fDocuments.empty())
		fCurrentKey = fDocuments[0]->Key();
	else if (fNotesDocument.Get() != NULL)
		fCurrentKey = fNotesDocument->Key();

	_Realign();
	return B_OK;
}


status_t
ParallelBibleView::PrevChapter()
{
	for (size_t i = 0; i < fDocuments.size(); i++)
		fDocuments[i]->PrevChapter();
	if (fNotesDocument.Get() != NULL)
		fNotesDocument->PrevChapter();

	if (!fDocuments.empty())
		fCurrentKey = fDocuments[0]->Key();
	else if (fNotesDocument.Get() != NULL)
		fCurrentKey = fNotesDocument->Key();

	_Realign();
	return B_OK;
}


void
ParallelBibleView::_RebuildLayout()
{
	for (size_t i = 0; i < fTextViews.size(); i++) {
		fTextViews[i]->RemoveSelf();
		delete fTextViews[i];
	}
	fTextViews.clear();

	if (fNotesView != NULL) {
		fNotesView->RemoveSelf();
		delete fNotesView;
		fNotesView = NULL;
	}

	for (size_t i = 0; i < fDocuments.size(); i++) {
		TextDocumentView* view = new TextDocumentView("bibleColumn");
		view->SetInsets(4.0f);
		view->SetSelectionEnabled(true);
		view->SetTextDocument(fDocuments[i]);
		AddChild(view);
		fTextViews.push_back(view);
	}

	if (fNotesDocument.Get() != NULL) {
		TextDocumentView* view = new TextDocumentView("notesColumn");
		view->SetInsets(4.0f);
		view->SetSelectionEnabled(true);
		view->SetEditingEnabled(true);
		view->SetTextDocument(fNotesDocument);
		AddChild(view);
		fNotesView = view;
	}

	_Realign();
}


void
ParallelBibleView::_Realign()
{
	std::vector<BibleTextDocument*> columns;
	for (size_t i = 0; i < fDocuments.size(); i++)
		columns.push_back(fDocuments[i].Get());
	if (fNotesDocument.Get() != NULL)
		columns.push_back(fNotesDocument.Get());

	if (columns.size() >= 2)
		VerseAligner::Align(columns, _ColumnWidth());

	_PositionColumns();

	for (size_t i = 0; i < fTextViews.size(); i++) {
		fTextViews[i]->Relayout();
		fTextViews[i]->Invalidate();
	}
	if (fNotesView != NULL) {
		fNotesView->Relayout();
		fNotesView->Invalidate();
	}
}


// Positions and sizes every column view directly (MoveTo/ResizeTo) instead
// of delegating to a BGroupLayout -- see the header comment for why. Each
// column gets its full natural content height (as reported by its own
// GetHeightForWidth()), which may well exceed this view's own Frame(); that
// is fine, since this view's Bounds() acts as the scrolled viewport into
// that taller content, driven by _UpdateScrollBars() below, exactly the
// way TextDocumentView does for itself when it is the direct BScrollView
// target.
void
ParallelBibleView::_PositionColumns()
{
	float width = _ColumnWidth();
	float x = 0.0f;
	float contentHeight = 0.0f;

	std::vector<TextDocumentView*> views(fTextViews);
	if (fNotesView != NULL)
		views.push_back(fNotesView);

	for (size_t i = 0; i < views.size(); i++) {
		// The document may have been rebuilt more than once in a row (once
		// per SetVerseSpacing() call inside VerseAligner::Align()); force a
		// fresh measurement rather than risk GetHeightForWidth() reading a
		// TextDocumentLayout copy whose cache wasn't invalidated for the
		// most recent of those rebuilds.
		views[i]->Relayout();

		float min, max, preferred;
		views[i]->GetHeightForWidth(width, &min, &max, &preferred);

		views[i]->MoveTo(x, 0.0f);
		views[i]->ResizeTo(width, preferred);

		contentHeight = std::max(contentHeight, preferred);
		x += width + kColumnSpacing;
	}

	fContentHeight = contentHeight;
	fContentWidth = views.empty() ? 0.0f : (x - kColumnSpacing);
	_UpdateScrollBars();
}


void
ParallelBibleView::_UpdateScrollBars()
{
	BScrollBar* verticalScrollBar = ScrollBar(B_VERTICAL);
	if (verticalScrollBar != NULL) {
		float viewHeight = Bounds().Height();
		float maxRange = fContentHeight - viewHeight;
		if (maxRange < 0.0f)
			maxRange = 0.0f;

		verticalScrollBar->SetRange(0.0f, maxRange);
		verticalScrollBar->SetProportion(
			fContentHeight > 0.0f ? viewHeight / fContentHeight : 1.0f);
		verticalScrollBar->SetSteps(20.0f, viewHeight);
	}

	BScrollBar* horizontalScrollBar = ScrollBar(B_HORIZONTAL);
	if (horizontalScrollBar != NULL) {
		float viewWidth = Bounds().Width();
		float maxRange = fContentWidth - viewWidth;
		if (maxRange < 0.0f)
			maxRange = 0.0f;

		horizontalScrollBar->SetRange(0.0f, maxRange);
		horizontalScrollBar->SetProportion(
			fContentWidth > 0.0f ? viewWidth / fContentWidth : 1.0f);
		horizontalScrollBar->SetSteps(20.0f, viewWidth);
	}
}


float
ParallelBibleView::_ColumnWidth() const
{
	// Bounds() is degenerate until this view has actually been placed
	// inside its (shown) window's BScrollView; fall back to the width the
	// constructing window was created with.
	float totalWidth = Bounds().Width();
	if (totalWidth <= 0.0f)
		totalWidth = fInitialWidth;
	if (totalWidth <= 0.0f)
		return kMinColumnWidth;

	int32 columnCount = (int32)fModules.size() + (fNotesView != NULL ? 1 : 0);
	if (columnCount == 0)
		return totalWidth;

	// Equal share of the available width, but never below kMinColumnWidth;
	// if that means the columns no longer all fit, _PositionColumns()'s
	// resulting fContentWidth ends up wider than this view's own Bounds(),
	// and the horizontal scrollbar (see _UpdateScrollBars()) is how the
	// overflow columns stay reachable instead of just running off-screen.
	float available = totalWidth - kColumnSpacing * (columnCount - 1);
	float width = available / columnCount;
	if (width < kMinColumnWidth)
		width = kMinColumnWidth;
	return width;
}

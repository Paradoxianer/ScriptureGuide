/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "ParallelBibleView.h"

#include <algorithm>
#include <cstring>

#include <Button.h>
#include <Catalog.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <StringView.h>
#include <TextView.h>

#include <versekey.h>

#include "ParagraphLayout.h"
#include "VerseAligner.h"
#include "constants.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ParallelBibleView"

const float ParallelBibleView::kMinColumnWidth = 150.0f;
const float ParallelBibleView::kColumnSpacing = 8.0f;
const float ParallelBibleView::kHeaderHeight = 24.0f;
const float ParallelBibleView::kRemoveButtonWidth = 20.0f;
const float ParallelBibleView::kMaxNotesWidthFraction = 1.0f / 3.0f;
const float ParallelBibleView::kNoteVerseLabelWidth = 20.0f;


// A single verse's note editor -- see the class comment on ParallelBibleView
// for why the notes column is a stack of these instead of one flowing
// document. Auto-saves on every edit (rather than e.g. only on focus loss)
// so nothing is lost if the window closes with a field still focused;
// writing a short note to the local, file-backed RawCom module on every
// keystroke is cheap.
class NoteFieldView : public BTextView {
public:
	NoteFieldView(int verse, PersonalNotesModule* notes,
		const BString& chapterKey)
		:
		BTextView("noteField"),
		fVerse(verse),
		fNotes(notes),
		fChapterKey(chapterKey)
	{
		SetWordWrap(true);
		SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		SetInsets(2.0f, 2.0f, 2.0f, 2.0f);
	}

protected:
	virtual void InsertText(const char* text, int32 length, int32 offset,
		const text_run_array* runs)
	{
		BTextView::InsertText(text, length, offset, runs);
		_Save();
	}

	virtual void DeleteText(int32 fromOffset, int32 toOffset)
	{
		BTextView::DeleteText(fromOffset, toOffset);
		_Save();
	}

private:
	void _Save()
	{
		VerseKey key;
		key.setText(fChapterKey.String());
		key.setVerse(fVerse);
		fNotes->SetNote(key.getText(), Text());
	}

private:
	int						fVerse;
	PersonalNotesModule*	fNotes;
	BString					fChapterKey;
};


ParallelBibleView::ParallelBibleView(const char* name, SWMgr* manager,
	float initialWidth)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS),
	fManager(manager),
	fNotes(NULL),
	fNotesLabel(NULL),
	fRemoveNotesButton(NULL),
	fHeaderView(NULL),
	fAddColumnButton(NULL),
	fInitialWidth(initialWidth),
	fContentHeight(0.0f),
	fContentWidth(0.0f)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// Not added as a child of this view -- see the class comment on why
	// the header lives outside the scrolled hierarchy. HeaderView() hands
	// this to the caller, who places it in their own layout; ownership
	// passes to whatever parent it ends up with (see ~ParallelBibleView()
	// for the fallback if that never happens).
	fHeaderView = new BView("parallelHeader", B_WILL_DRAW);
	fHeaderView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fHeaderView->SetExplicitMinSize(BSize(B_SIZE_UNSET, kHeaderHeight));
	fHeaderView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, kHeaderHeight));

	fAddColumnButton = new BButton("addColumn", "+",
		new BMessage(PARALLEL_ADD_COLUMN_MENU));
	fAddColumnButton->SetTarget(this);
	fHeaderView->AddChild(fAddColumnButton);
}


ParallelBibleView::~ParallelBibleView()
{
	delete fNotes;

	// HeaderView() is meant to be adopted into the caller's own layout
	// (see ParallelBibleWindow); if that never happened, this view still
	// owns it and must not leak it.
	if (fHeaderView->Parent() == NULL)
		delete fHeaderView;
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


// The one hook every scroll path (drag, mouse wheel, a scrollbar's
// programmatic SetValue()) funnels through -- see the class comment.
// Mirrors only the horizontal component onto the header, which has no
// vertical scroll position of its own.
void
ParallelBibleView::ScrollTo(BPoint where)
{
	BView::ScrollTo(where);
	fHeaderView->ScrollTo(where.x, 0.0f);
}


void
ParallelBibleView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case PARALLEL_SELECT_MODULE:
		{
			int32 index;
			BString module;
			if (message->FindInt32("index", &index) == B_OK
				&& message->FindString("module", &module) == B_OK) {
				if (index < 0)
					AddColumn(module.String());
				else
					ReplaceColumn(index, module.String());
			}
			break;
		}

		case PARALLEL_ADD_COLUMN_MENU:
		{
			BPopUpMenu* menu = _BuildModuleMenu(-1, NULL);
			if (menu != NULL) {
				// Not owned by any BMenuField (unlike the per-column
				// menus built in _RebuildHeader()) -- this one is a
				// fire-and-forget popup, so it must clean itself up.
				menu->SetAsyncAutoDestruct(true);
				BPoint where = fAddColumnButton->Frame().LeftBottom();
				fHeaderView->ConvertToScreen(&where);
				menu->Go(where, true, true, true);
			}
			break;
		}

		case PARALLEL_REMOVE_COLUMN:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK)
				RemoveColumn(index);
			break;
		}

		case PARALLEL_REMOVE_NOTES:
			SetNotesEnabled(false);
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
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
ParallelBibleView::ReplaceColumn(int32 index, const char* moduleName)
{
	if (index < 0 || (size_t)index >= fModules.size())
		return B_BAD_INDEX;
	if (fManager == NULL)
		return B_NO_INIT;

	SWModule* module = fManager->getModule(moduleName);
	if (module == NULL)
		return B_NAME_NOT_FOUND;

	if (!fCurrentKey.IsEmpty())
		module->setKey(fCurrentKey.String());

	fModules[index] = module;
	fDocuments[index] = BReference<BibleTextDocument>(
		new BibleTextDocument(module), true);

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
	} else {
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

	_RebuildNoteFields();
	_Realign();

	VerseKey verseKey;
	verseKey.setText(fCurrentKey.String());
	_ScrollToVerse(verseKey.getVerse());

	return B_OK;
}


status_t
ParallelBibleView::NextChapter()
{
	for (size_t i = 0; i < fDocuments.size(); i++)
		fDocuments[i]->NextChapter();

	if (!fDocuments.empty())
		fCurrentKey = fDocuments[0]->Key();

	_RebuildNoteFields();
	_Realign();
	// A new chapter always starts at verse 1 (both *Chapter() methods
	// force that); reset the viewport too, or a scroll position from the
	// previous chapter could leave the new one looking blank/misplaced.
	ScrollTo(Bounds().left, 0.0f);
	return B_OK;
}


status_t
ParallelBibleView::PrevChapter()
{
	for (size_t i = 0; i < fDocuments.size(); i++)
		fDocuments[i]->PrevChapter();

	if (!fDocuments.empty())
		fCurrentKey = fDocuments[0]->Key();

	_RebuildNoteFields();
	_Realign();
	ScrollTo(Bounds().left, 0.0f);
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

	for (size_t i = 0; i < fDocuments.size(); i++) {
		TextDocumentView* view = new TextDocumentView("bibleColumn");
		// These are a reading surface, not a details panel -- override the
		// B_PANEL_BACKGROUND_COLOR (gray) the class constructs with. Both
		// calls are needed: ViewColor is what a freshly exposed area gets
		// painted with, LowColor is what Draw()'s own background fill
		// (FillRect(..., B_SOLID_LOW)) uses, and the class constructor sets
		// LowColor from the ViewColor it had at construction time, which we
		// are about to change out from under it.
		view->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		view->SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		view->SetInsets(4.0f);
		view->SetSelectionEnabled(true);
		view->SetTextDocument(fDocuments[i]);
		AddChild(view);
		fTextViews.push_back(view);
	}

	_RebuildHeader();
	_RebuildNoteFields();
	_Realign();
}


// Rebuilds the header row's per-column BMenuFields + remove buttons (Bible
// columns) and the notes column's label + remove button. Positioning
// happens in _PositionColumns(), which uses the exact same x-offsets as
// the content columns so header cells and columns always line up.
void
ParallelBibleView::_RebuildHeader()
{
	for (size_t i = 0; i < fHeaderFields.size(); i++) {
		fHeaderFields[i]->RemoveSelf();
		delete fHeaderFields[i];
	}
	fHeaderFields.clear();
	for (size_t i = 0; i < fRemoveButtons.size(); i++) {
		fRemoveButtons[i]->RemoveSelf();
		delete fRemoveButtons[i];
	}
	fRemoveButtons.clear();

	for (size_t i = 0; i < fModules.size(); i++) {
		BPopUpMenu* menu = _BuildModuleMenu((int32)i, fModules[i]->getName());
		BMenuField* field = new BMenuField("columnHeader", NULL, menu);
		field->SetDivider(0.0f);
		fHeaderView->AddChild(field);
		fHeaderFields.push_back(field);

		BMessage* removeMessage = new BMessage(PARALLEL_REMOVE_COLUMN);
		removeMessage->AddInt32("index", (int32)i);
		BButton* removeButton = new BButton("removeColumn", "x",
			removeMessage);
		removeButton->SetTarget(this);
		fHeaderView->AddChild(removeButton);
		fRemoveButtons.push_back(removeButton);
	}

	if (fNotesLabel != NULL) {
		fNotesLabel->RemoveSelf();
		delete fNotesLabel;
		fNotesLabel = NULL;
	}
	if (fRemoveNotesButton != NULL) {
		fRemoveNotesButton->RemoveSelf();
		delete fRemoveNotesButton;
		fRemoveNotesButton = NULL;
	}
	if (fNotes != NULL) {
		// Otherwise there is nothing distinguishing the notes column's
		// header cell from a Bible column's -- it would be a bare "x"
		// with no indication of what it belongs to or that it's editable.
		fNotesLabel = new BStringView("notesLabel", B_TRANSLATE("Notes"));
		fHeaderView->AddChild(fNotesLabel);

		fRemoveNotesButton = new BButton("removeNotes", "x",
			new BMessage(PARALLEL_REMOVE_NOTES));
		fRemoveNotesButton->SetTarget(this);
		fHeaderView->AddChild(fRemoveNotesButton);
	}

	// The add-column button is a permanent child of fHeaderView, added in
	// the constructor; just make sure it stays the last child so
	// _PositionColumns() can place it after everything else.
	fAddColumnButton->RemoveSelf();
	fHeaderView->AddChild(fAddColumnButton);
}


// Rebuilds the notes column's per-verse fields for the current chapter
// (fCurrentKey): one NoteFieldView + verse-number label per verse, loaded
// with whatever note (if any) is already stored for it. Called whenever
// the chapter changes (SetKey()/NextChapter()/PrevChapter()) as well as
// from _RebuildLayout() (column/notes-enabled changes) -- the verse count
// and content can differ per chapter, unlike the Bible TextDocumentViews,
// which refresh their own content in place via BibleTextDocument::
// SetKey() without needing their wrapper view recreated.
//
// Only creates/loads the fields; _PositionColumns() (via _Realign(), which
// every caller of this also calls) does the actual sizing, since a field's
// height depends on Bible-column alignment that hasn't necessarily run yet
// at this point.
void
ParallelBibleView::_RebuildNoteFields()
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

	if (fNotes == NULL || fCurrentKey.IsEmpty())
		return;

	VerseKey chapterKey;
	chapterKey.setText(fCurrentKey.String());
	int verseCount = chapterKey.getVerseMax();

	for (int verse = 1; verse <= verseCount; verse++) {
		VerseKey verseKey;
		verseKey.setText(fCurrentKey.String());
		verseKey.setVerse(verse);

		BString verseLabel;
		verseLabel << verse;
		BStringView* label = new BStringView("noteVerseLabel",
			verseLabel.String());
		AddChild(label);
		fNoteVerseLabels.push_back(label);

		NoteFieldView* field = new NoteFieldView(verse, fNotes, fCurrentKey);
		field->SetText(fNotes->GetNote(verseKey.getText()).String());
		AddChild(field);
		fNoteFields.push_back(field);
	}
}


void
ParallelBibleView::_Realign()
{
	std::vector<BibleTextDocument*> columns;
	std::vector<float> widths;
	for (size_t i = 0; i < fDocuments.size(); i++) {
		columns.push_back(fDocuments[i].Get());
		widths.push_back(_ColumnWidth());
	}

	if (columns.size() >= 2)
		VerseAligner::Align(columns, widths);

	_PositionColumns();

	for (size_t i = 0; i < fTextViews.size(); i++) {
		fTextViews[i]->Relayout();
		fTextViews[i]->Invalidate();
	}
}


// Positions and sizes every column view directly (MoveTo/ResizeTo) instead
// of delegating to a BGroupLayout -- see the header comment for why. Each
// Bible column gets its full natural content height (as reported by its
// own GetHeightForWidth()), which may well exceed this view's own Frame();
// that is fine, since this view's Bounds() acts as the scrolled viewport
// into that taller content, driven by _UpdateScrollBars() below, exactly
// the way TextDocumentView does for itself when it is the direct
// BScrollView target. Each note field's height comes from _RowHeight(),
// which reads back the (already-aligned, if there's more than one Bible
// column) height of that verse's paragraph in the first Bible column.
void
ParallelBibleView::_PositionColumns()
{
	float width = _ColumnWidth();
	float x = 0.0f;
	float contentHeight = 0.0f;

	for (size_t i = 0; i < fTextViews.size(); i++) {
		// The document may have been rebuilt more than once in a row (once
		// per SetVerseSpacing() call inside VerseAligner::Align()); force a
		// fresh measurement rather than risk GetHeightForWidth() reading a
		// TextDocumentLayout copy whose cache wasn't invalidated for the
		// most recent of those rebuilds.
		fTextViews[i]->Relayout();

		float min, max, preferred;
		fTextViews[i]->GetHeightForWidth(width, &min, &max, &preferred);

		fTextViews[i]->MoveTo(x, 0.0f);
		fTextViews[i]->ResizeTo(width, preferred);

		if (i < fHeaderFields.size()) {
			float fieldWidth = std::max(0.0f,
				width - kRemoveButtonWidth - kColumnSpacing / 2.0f);
			fHeaderFields[i]->MoveTo(x, 0.0f);
			fHeaderFields[i]->ResizeTo(fieldWidth, kHeaderHeight);
			fRemoveButtons[i]->MoveTo(x + fieldWidth + kColumnSpacing / 2.0f,
				0.0f);
			fRemoveButtons[i]->ResizeTo(kRemoveButtonWidth, kHeaderHeight);
		}

		contentHeight = std::max(contentHeight, preferred);
		x += width + kColumnSpacing;
	}

	if (fNotes != NULL) {
		float notesWidth = _NotesColumnWidth();
		float fieldWidth = std::max(0.0f, notesWidth - kNoteVerseLabelWidth);

		float y = 0.0f;
		for (size_t i = 0; i < fNoteFields.size(); i++) {
			float rowHeight = _RowHeight((int)(i + 1));

			fNoteVerseLabels[i]->MoveTo(x, y);
			fNoteVerseLabels[i]->ResizeTo(kNoteVerseLabelWidth, rowHeight);

			fNoteFields[i]->MoveTo(x + kNoteVerseLabelWidth, y);
			fNoteFields[i]->ResizeTo(fieldWidth, rowHeight);

			y += rowHeight;
		}

		if (fRemoveNotesButton != NULL) {
			fRemoveNotesButton->MoveTo(
				x + notesWidth - kRemoveButtonWidth, 0.0f);
			fRemoveNotesButton->ResizeTo(kRemoveButtonWidth, kHeaderHeight);
		}
		if (fNotesLabel != NULL) {
			fNotesLabel->MoveTo(x, 0.0f);
			fNotesLabel->ResizeTo(
				std::max(0.0f, notesWidth - kRemoveButtonWidth), kHeaderHeight);
		}

		contentHeight = std::max(contentHeight, y);
		x += notesWidth + kColumnSpacing;
	}

	fAddColumnButton->MoveTo(x, 0.0f);
	fAddColumnButton->ResizeTo(kHeaderHeight, kHeaderHeight);
	x += kHeaderHeight + kColumnSpacing;

	fContentHeight = contentHeight;
	int32 columnCount = (int32)fTextViews.size() + (fNotes != NULL ? 1 : 0);
	fContentWidth = columnCount == 0 ? 0.0f : (x - kColumnSpacing);

	// fHeaderView's own Frame() is left to the window's layout (matching
	// the scroll viewport, same as this view's own Frame() is left to the
	// BScrollView that wraps it); its header cells, like this view's
	// TextDocumentView columns, are positioned above via MoveTo() up to
	// fContentWidth, which can exceed that Frame() -- Bounds()-origin
	// shifting (mirrored from this view's own scroll position in
	// ScrollTo()) is what reveals the overflow, not resizing the view.
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


// Scrolls so the given verse's row is at the top of the viewport. Bible
// columns are aligned per verse (see VerseAligner) and note fields are
// sized to match (see _RowHeight()), so summing row heights up to the
// target verse lands correctly regardless of which columns are open.
void
ParallelBibleView::_ScrollToVerse(int verse)
{
	if (verse <= 1)
		return; // already at the top after a fresh chapter render

	float y = 0.0f;
	for (int v = 1; v < verse; v++)
		y += _RowHeight(v);

	ScrollTo(Bounds().left, y);
}


// The height of the given verse's row, shared by note-field sizing
// (_PositionColumns()) and scroll-target calculation (_ScrollToVerse()).
// Reads it back from the first Bible column's paragraph for that verse --
// which, if VerseAligner::Align() has run (2+ Bible columns), already
// reflects the tallest column's height for that verse, exactly the row
// height every other column needs to match. Mirrors the technique
// VerseAligner itself uses: a standalone ParagraphLayout can measure a
// paragraph's height without a live TextDocumentView. Falls back to
// kHeaderHeight when there's no Bible column to measure against (a notes-
// only view) or the verse was skipped from that column's document.
float
ParallelBibleView::_RowHeight(int verse) const
{
	if (!fDocuments.empty()) {
		BibleTextDocument* document = fDocuments[0].Get();
		int32 index = document->ParagraphIndexForVerse(verse);
		if (index >= 0) {
			ParagraphLayout layout;
			layout.SetWidth(_ColumnWidth());
			layout.SetParagraph(document->ParagraphAtIndex(index));
			return layout.Height();
		}
	}
	return kHeaderHeight;
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

	int32 bibleCount = (int32)fModules.size();
	if (bibleCount == 0)
		return totalWidth;

	int32 columnCount = bibleCount + (fNotes != NULL ? 1 : 0);
	float notesWidth = fNotes != NULL ? _NotesColumnWidth() : 0.0f;

	// Equal share of what's left after the (separately capped, see
	// _NotesColumnWidth()) notes column, but never below kMinColumnWidth;
	// if that means the columns no longer all fit, _PositionColumns()'s
	// resulting fContentWidth ends up wider than this view's own Bounds(),
	// and the horizontal scrollbar (see _UpdateScrollBars()) is how the
	// overflow columns stay reachable instead of just running off-screen.
	float available = totalWidth - kColumnSpacing * (columnCount - 1)
		- notesWidth;
	float width = available / bibleCount;
	if (width < kMinColumnWidth)
		width = kMinColumnWidth;
	return width;
}


// The notes column never claims more than kMaxNotesWidthFraction of the
// total width -- the equal-share split let it eat half the window with
// just one Bible column open. Still just a fixed cap for now; issue #19
// tracks turning this into a real user-draggable divider.
float
ParallelBibleView::_NotesColumnWidth() const
{
	if (fNotes == NULL)
		return 0.0f;

	float totalWidth = Bounds().Width();
	if (totalWidth <= 0.0f)
		totalWidth = fInitialWidth;
	if (totalWidth <= 0.0f)
		return kMinColumnWidth;

	int32 columnCount = (int32)fModules.size() + 1;
	float naturalShare = (totalWidth - kColumnSpacing * (columnCount - 1))
		/ columnCount;

	float width = std::min(naturalShare, totalWidth * kMaxNotesWidthFraction);
	if (width < kMinColumnWidth)
		width = kMinColumnWidth;
	return width;
}


// Builds a popup menu listing every installed "Biblical Texts" module,
// labeled with its short module code (e.g. "KJV") rather than its full
// description -- the latter is too long to be legible once truncated to
// column width. columnIndex is embedded in each item's message so
// MessageReceived() knows whether to AddColumn() (columnIndex < 0, used
// by the trailing "+" button) or ReplaceColumn() (columnIndex >= 0, used
// by a column's own header field). markedModuleName, if given, is checked
// off to show the column's current selection.
BPopUpMenu*
ParallelBibleView::_BuildModuleMenu(int32 columnIndex,
	const char* markedModuleName)
{
	if (fManager == NULL)
		return NULL;

	// radioMode so BMenu itself keeps exactly one item marked as the user
	// picks different translations; labelFromMarked so the field displays
	// that marked item's label instead of this constructor's own `name`.
	BPopUpMenu* menu = new BPopUpMenu("translation", true, true);

	const ModMap& modules = fManager->getModules();
	for (ModMap::const_iterator it = modules.begin(); it != modules.end();
			++it) {
		SWModule* module = it->second;
		if (module == NULL
			|| strcmp(module->getType(), "Biblical Texts") != 0) {
			continue;
		}

		BMessage* message = new BMessage(PARALLEL_SELECT_MODULE);
		message->AddInt32("index", columnIndex);
		message->AddString("module", module->getName());

		BMenuItem* item = new BMenuItem(module->getName(), message);
		if (markedModuleName != NULL
			&& strcmp(module->getName(), markedModuleName) == 0) {
			item->SetMarked(true);
		}
		menu->AddItem(item);
	}

	menu->SetTargetForItems(this);
	return menu;
}

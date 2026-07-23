/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "ParallelBibleView.h"

#include <algorithm>
#include <cstring>

#include <Button.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>

#include <versekey.h>

#include "VerseAligner.h"
#include "constants.h"

const float ParallelBibleView::kMinColumnWidth = 150.0f;
const float ParallelBibleView::kColumnSpacing = 8.0f;
const float ParallelBibleView::kHeaderHeight = 24.0f;


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
	fHeaderContainer(NULL),
	fContentView(NULL),
	fAddColumnButton(NULL),
	fInitialWidth(initialWidth),
	fContentHeight(0.0f),
	fContentWidth(0.0f)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fHeaderContainer = new BView("parallelHeader", B_WILL_DRAW);
	fHeaderContainer->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	AddChild(fHeaderContainer);

	fContentView = new BView("parallelContent", B_WILL_DRAW | B_FRAME_EVENTS);
	fContentView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	AddChild(fContentView);

	fAddColumnButton = new BButton("addColumn", "+",
		new BMessage(PARALLEL_ADD_COLUMN_MENU));
	fAddColumnButton->SetTarget(this);
	fHeaderContainer->AddChild(fAddColumnButton);
}


ParallelBibleView::~ParallelBibleView()
{
	delete fNotes;
}


void
ParallelBibleView::AttachedToWindow()
{
	BView::AttachedToWindow();

	// The vertical scrollbar is wired to this view by BScrollView's
	// default single-target construction; redirect it to fContentView so
	// vertical scrolling never moves fHeaderContainer (see header comment
	// on why the header/content split exists in the first place).
	BScrollBar* verticalScrollBar = ScrollBar(B_VERTICAL);
	if (verticalScrollBar != NULL)
		verticalScrollBar->SetTarget(fContentView);

	_RebuildLayout();
}


void
ParallelBibleView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	_Realign();
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
				ConvertToScreen(&where);
				menu->Go(where, true, true, true);
			}
			break;
		}

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
		fContentView->AddChild(view);
		fTextViews.push_back(view);
	}

	if (fNotesDocument.Get() != NULL) {
		TextDocumentView* view = new TextDocumentView("notesColumn");
		view->SetInsets(4.0f);
		view->SetSelectionEnabled(true);
		view->SetEditingEnabled(true);
		view->SetTextDocument(fNotesDocument);
		fContentView->AddChild(view);
		fNotesView = view;
	}

	_RebuildHeader();
	_Realign();
}


// Rebuilds the header row's per-column BMenuFields (Bible columns) and
// the notes column's plain label. Positioning happens in
// _PositionColumns(), which uses the exact same x-offsets as the content
// columns so header cells and columns always line up.
void
ParallelBibleView::_RebuildHeader()
{
	for (size_t i = 0; i < fHeaderFields.size(); i++) {
		fHeaderFields[i]->RemoveSelf();
		delete fHeaderFields[i];
	}
	fHeaderFields.clear();

	for (size_t i = 0; i < fModules.size(); i++) {
		BPopUpMenu* menu = _BuildModuleMenu((int32)i, fModules[i]->getName());
		BMenuField* field = new BMenuField("columnHeader", NULL, menu);
		field->SetDivider(0.0f);
		fHeaderContainer->AddChild(field);
		fHeaderFields.push_back(field);
	}

	// The add-column button is a permanent child added in the constructor;
	// just make sure it stays the frontmost/last child so it draws above
	// nothing else in particular, but mainly so _PositionColumns() can
	// place it after the last header field.
	fAddColumnButton->RemoveSelf();
	fHeaderContainer->AddChild(fAddColumnButton);
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
// GetHeightForWidth()), which may well exceed fContentView's own Frame();
// that is fine, since fContentView's Bounds() acts as the scrolled
// viewport into that taller content, driven by _UpdateScrollBars() below,
// exactly the way TextDocumentView does for itself when it is the direct
// BScrollView target.
void
ParallelBibleView::_PositionColumns()
{
	float width = _ColumnWidth();
	float x = 0.0f;
	float contentHeight = 0.0f;

	std::vector<TextDocumentView*> views(fTextViews);
	if (fNotesView != NULL)
		views.push_back(fNotesView);

	std::vector<BView*> headers(fHeaderFields.begin(), fHeaderFields.end());
	// One header cell per Bible column; the notes column (if present, and
	// therefore the last entry in `views`) has no corresponding entry in
	// `headers` since it doesn't get a module picker -- see _RebuildHeader().

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

		if (i < headers.size()) {
			headers[i]->MoveTo(x, 0.0f);
			headers[i]->ResizeTo(width, kHeaderHeight);
		}

		contentHeight = std::max(contentHeight, preferred);
		x += width + kColumnSpacing;
	}

	fAddColumnButton->MoveTo(x, 0.0f);
	fAddColumnButton->ResizeTo(kHeaderHeight, kHeaderHeight);
	x += kHeaderHeight + kColumnSpacing;

	fContentHeight = contentHeight;
	fContentWidth = views.empty() ? 0.0f : (x - kColumnSpacing);

	float viewportWidth = std::max(fContentWidth, Bounds().Width());
	float viewportHeight = std::max(0.0f, Bounds().Height() - kHeaderHeight);

	fHeaderContainer->MoveTo(0.0f, 0.0f);
	fHeaderContainer->ResizeTo(viewportWidth, kHeaderHeight);

	fContentView->MoveTo(0.0f, kHeaderHeight);
	fContentView->ResizeTo(viewportWidth, viewportHeight);

	_UpdateScrollBars();
}


void
ParallelBibleView::_UpdateScrollBars()
{
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

	BScrollBar* verticalScrollBar = ScrollBar(B_VERTICAL);
	if (verticalScrollBar != NULL) {
		float viewHeight = std::max(0.0f, Bounds().Height() - kHeaderHeight);
		float maxRange = fContentHeight - viewHeight;
		if (maxRange < 0.0f)
			maxRange = 0.0f;

		verticalScrollBar->SetRange(0.0f, maxRange);
		verticalScrollBar->SetProportion(
			fContentHeight > 0.0f ? viewHeight / fContentHeight : 1.0f);
		verticalScrollBar->SetSteps(20.0f, viewHeight);
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


// Builds a popup menu listing every installed "Biblical Texts" module.
// columnIndex is embedded in each item's message so MessageReceived()
// knows whether to AddColumn() (columnIndex < 0, used by the trailing "+"
// button) or ReplaceColumn() (columnIndex >= 0, used by a column's own
// header field). markedModuleName, if given, is checked off to show the
// column's current selection.
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

		BMenuItem* item = new BMenuItem(module->getDescription(), message);
		if (markedModuleName != NULL
			&& strcmp(module->getName(), markedModuleName) == 0) {
			item->SetMarked(true);
		}
		menu->AddItem(item);
	}

	menu->SetTargetForItems(this);
	return menu;
}

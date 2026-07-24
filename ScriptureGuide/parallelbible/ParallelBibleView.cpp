/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "ParallelBibleView.h"

#include <algorithm>
#include <cstring>

#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <Font.h>
#include <Language.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
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


// See the identical helper in BibleTextDocument.cpp: VerseKey::setText()
// only recognizes localized book names (e.g. German "1. Mose") if the
// key's locale has been set first, otherwise it fails silently and the
// key is left unchanged. fCurrentKey here ultimately comes from the main
// window's (localized) book menu.
static void
SetVerseKeyLocale(VerseKey& key)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	key.setLocale(language.Code());
}

const float ParallelBibleView::kMinColumnWidth = 150.0f;
const float ParallelBibleView::kColumnSpacing = 8.0f;
const float ParallelBibleView::kHeaderHeight = 24.0f;
const float ParallelBibleView::kRemoveButtonWidth = 20.0f;
const float ParallelBibleView::kMaxNotesWidthFraction = 1.0f / 3.0f;
const float ParallelBibleView::kNoteVerseLabelWidth = 20.0f;
const float ParallelBibleView::kBibleColumnInset = 4.0f;


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
		SetVerseKeyLocale(key);
		key.setText(fChapterKey.String());
		key.setVerse(fVerse);
		fNotes->SetNote(key.getText(), Text());
	}

private:
	int						fVerse;
	PersonalNotesModule*	fNotes;
	BString					fChapterKey;
};


// A Bible/Commentary column's TextDocumentView, with drag-out support for
// selected text -- the old, pre-parallel-columns reading pane was a plain
// BTextView, which drags selected text out for free, no extra code
// required; the new TextDocumentView engine (vendored from HaikuDepot) has
// none of that, so it's added here (see issue #23).
//
// The gesture: MouseDown() on top of the *existing* selection doesn't
// touch the selection or move the caret -- it just remembers the down
// point and waits. If the mouse moves past a small threshold before
// MouseUp(), that's a drag: build a text/plain clipping (with the verse
// reference and translation name prefixed) and call DragMessage(). If the
// mouse never moves that far, MouseUp() treats it as the plain click it
// actually was and places the caret there, same as clicking anywhere else
// in the text. A MouseDown() that doesn't land inside the current
// selection at all skips this entirely and behaves exactly as before.
class BibleColumnView : public TextDocumentView {
public:
	BibleColumnView(const char* name, BibleTextDocument* document,
		const char* translationName)
		:
		TextDocumentView(name),
		fBibleDocument(document),
		fTranslationName(translationName),
		fTrackingForDrag(false)
	{
	}

	virtual void MouseDown(BPoint where)
	{
		fTrackingForDrag = false;
		if (HasSelection()) {
			int32 start, end;
			GetSelection(start, end);
			int32 offset = TextOffsetAt(where);
			if (start != end && offset >= start && offset < end) {
				MakeFocus();
				fTrackingForDrag = true;
				fDragStartPoint = where;
				SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
				return;
			}
		}
		TextDocumentView::MouseDown(where);
	}

	virtual void MouseMoved(BPoint where, uint32 transit,
		const BMessage* dragMessage)
	{
		if (fTrackingForDrag) {
			// Squared-distance check avoids pulling in libm for a plain
			// sqrt() just to compare against a threshold.
			float dx = where.x - fDragStartPoint.x;
			float dy = where.y - fDragStartPoint.y;
			const float kDragThreshold = 4.0f;
			if (dx * dx + dy * dy > kDragThreshold * kDragThreshold) {
				fTrackingForDrag = false;
				_StartDrag();
			}
			return;
		}
		TextDocumentView::MouseMoved(where, transit, dragMessage);
	}

	virtual void MouseUp(BPoint where)
	{
		if (fTrackingForDrag) {
			// Pressed inside the selection, released again without
			// moving far enough to start a drag -- a plain click after
			// all, meant to move the caret there like normal.
			fTrackingForDrag = false;
			SetCaret(where, false);
		}
		TextDocumentView::MouseUp(where);
	}

private:
	void _StartDrag()
	{
		int32 start, end;
		GetSelection(start, end);
		if (start >= end || fBibleDocument == NULL)
			return;

		BString text = fBibleDocument->Text(start, end - start);
		if (text.IsEmpty())
			return;

		BString reference = _ReferenceFor(start, end);

		BString clipText;
		clipText << reference << " (" << fTranslationName << ")\n\n" << text;

		BMessage drag(B_SIMPLE_DATA);
		drag.AddData("text/plain", B_MIME_TYPE, clipText.String(),
			clipText.Length());
		BString clipName(reference);
		clipName << " (" << fTranslationName << ")";
		drag.AddString("be:clip_name", clipName);
		// Not consumed by anything yet -- for a future drop target (see
		// issue #23) that wants the reference/translation without having
		// to re-parse them back out of the plain-text clipping.
		drag.AddString("scriptureguide:reference", reference);
		drag.AddString("scriptureguide:translation", fTranslationName);

		BString snippet(text);
		snippet.ReplaceAll("\n", " ");

		BBitmap* dragBitmap = _CreateDragBitmap(clipName, snippet);
		if (dragBitmap != NULL && dragBitmap->IsValid()) {
			DragMessage(&drag, dragBitmap, B_OP_ALPHA, BPoint(8.0f, 8.0f),
				this);
		} else {
			delete dragBitmap;
			BRect dragRect(fDragStartPoint.x - 4.0f,
				fDragStartPoint.y - 4.0f, fDragStartPoint.x + 200.0f,
				fDragStartPoint.y + 20.0f);
			DragMessage(&drag, dragRect & Bounds(), this);
		}
	}

	// A small "sticky note" style label following the cursor for the
	// whole drag -- shows what's being dragged (reference + a one-line
	// snippet of the actual text) instead of the bare outline rectangle
	// DragMessage() falls back to on its own. Ownership passes to
	// DragMessage() once called; the caller must not delete the
	// returned bitmap itself.
	BBitmap* _CreateDragBitmap(const BString& reference,
		const BString& snippet) const
	{
		BFont font;
		GetFont(&font);
		font_height fontHeight;
		font.GetHeight(&fontHeight);
		float lineHeight = ceilf(fontHeight.ascent + fontHeight.descent
			+ fontHeight.leading);

		BFont snippetFont(font);
		snippetFont.SetSize(font.Size() * 0.85f);
		font_height snippetFontHeight;
		snippetFont.GetHeight(&snippetFontHeight);
		float snippetLineHeight = ceilf(snippetFontHeight.ascent
			+ snippetFontHeight.descent + snippetFontHeight.leading);

		const float kMaxTextWidth = 240.0f;
		const float kPadding = 6.0f;

		BString clippedSnippet(snippet);
		snippetFont.TruncateString(&clippedSnippet, B_TRUNCATE_END,
			kMaxTextWidth);

		float width = std::min(std::max(font.StringWidth(reference.String()),
			snippetFont.StringWidth(clippedSnippet.String())),
			kMaxTextWidth) + kPadding * 2.0f;
		float height = lineHeight + snippetLineHeight + kPadding * 2.0f;

		BRect bounds(0.0f, 0.0f, ceilf(width) - 1.0f, ceilf(height) - 1.0f);
		BBitmap* bitmap = new BBitmap(bounds, B_RGBA32, true);
		BView* view = new BView(bounds, "dragPreview", B_FOLLOW_NONE,
			B_WILL_DRAW);
		bitmap->AddChild(view);

		if (bitmap->Lock()) {
			view->SetDrawingMode(B_OP_ALPHA);
			view->SetHighColor(255, 250, 210, 235);
			view->SetLowColor(255, 250, 210, 0);
			view->FillRoundRect(bounds, 5.0f, 5.0f);
			view->SetHighColor(150, 120, 40, 255);
			view->StrokeRoundRect(bounds, 5.0f, 5.0f);

			view->SetDrawingMode(B_OP_OVER);
			view->SetHighColor(20, 20, 20, 255);
			view->SetFont(&font);
			view->DrawString(reference.String(),
				BPoint(kPadding, kPadding + fontHeight.ascent));

			view->SetHighColor(90, 90, 90, 255);
			view->SetFont(&snippetFont);
			view->DrawString(clippedSnippet.String(),
				BPoint(kPadding, kPadding + lineHeight
					+ snippetFontHeight.ascent));

			view->Sync();
			bitmap->RemoveChild(view);
			bitmap->Unlock();
		}
		delete view;

		return bitmap;
	}

	// "<Book> <Chapter>:<StartVerse>[-<EndVerse>]" for the given text
	// offset range, e.g. "Genesis 1:1-3".
	BString _ReferenceFor(int32 start, int32 end) const
	{
		int32 paragraphOffset;
		int32 startParagraph = fBibleDocument->ParagraphIndexFor(start,
			paragraphOffset);
		int32 endParagraph = fBibleDocument->ParagraphIndexFor(
			end > start ? end - 1 : end, paragraphOffset);

		int startVerse = fBibleDocument->VerseForParagraphIndex(
			startParagraph);
		int endVerse = fBibleDocument->VerseForParagraphIndex(endParagraph);

		BString reference(fBibleDocument->BookName());
		reference << " " << fBibleDocument->Chapter() << ":" << startVerse;
		if (endVerse > startVerse)
			reference << "-" << endVerse;
		return reference;
	}

private:
	BibleTextDocument*	fBibleDocument;
	BString				fTranslationName;
	bool				fTrackingForDrag;
	BPoint				fDragStartPoint;
};


// The header row's container view (see HeaderView()/the class comment on
// why it's not a child of ParallelBibleView itself). Whoever HeaderView()
// is handed to (see SGMainWindow) may or may not ever adopt it via
// AddChild() -- and if they do, exactly when varies with how that caller's
// layout gets built (BLayoutBuilder does not necessarily attach views in
// .Add() call order; measured to fire *after* ParallelBibleView's own
// AttachedToWindow() in practice, which made an earlier attempt at
// tracking "did someone else adopt it yet" via Parent() at that point
// unreliable and caused a double-free). Rather than depend on timing at
// all, this notifies ParallelBibleView when it is actually destroyed --
// by the window's own child teardown if adopted, or directly by
// ~ParallelBibleView() otherwise -- so fHeaderView there is nulled out
// no matter which side's destructor runs first, and the other side's
// `delete` (on an already-null pointer) is always a safe no-op.
class ParallelHeaderView : public BView {
public:
	ParallelHeaderView(const char* name, uint32 flags,
		ParallelBibleView* owner)
		:
		BView(name, flags),
		fOwner(owner)
	{
	}

	virtual ~ParallelHeaderView()
	{
		if (fOwner != NULL)
			fOwner->_HeaderViewDestroyed();
	}

private:
	ParallelBibleView*	fOwner;
};


ParallelBibleView::ParallelBibleView(const char* name, SWMgr* manager,
	float initialWidth)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS),
	fManager(manager),
	fNotes(NULL),
	fHeaderView(NULL),
	fAddColumnButton(NULL),
	fShowVerseNumbers(true),
	fInitialWidth(initialWidth),
	fContentHeight(0.0f),
	fContentWidth(0.0f)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// Not added as a child of this view -- see the class comment on why
	// the header lives outside the scrolled hierarchy. HeaderView() hands
	// this to the caller, who places it in their own layout; see
	// ParallelHeaderView above for how ownership/deletion is handled
	// either way.
	fHeaderView = new ParallelHeaderView("parallelHeader", B_WILL_DRAW, this);
	fHeaderView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fHeaderView->SetExplicitMinSize(BSize(B_SIZE_UNSET, kHeaderHeight));
	fHeaderView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, kHeaderHeight));

	// SetTarget(this) is deliberately NOT called here: this constructor
	// runs before this view (or fHeaderView) has been attached to any
	// window, so this->Looper() is still NULL and the BMessenger
	// SetTarget() builds from it would be permanently invalid --
	// BMessenger doesn't re-resolve later, so the button's clicks would
	// silently fall back to this view's Window() instead (which has no
	// matching message case, so nothing visibly happens: exactly the
	// "+ button does nothing" bug this fixed). Done in AttachedToWindow()
	// instead, once this view actually has a Looper -- the same timing
	// every other SetTarget(this) call in this class already relies on,
	// since those all happen inside _RebuildHeader(), itself only ever
	// reachable after attachment.
	fAddColumnButton = new BButton("addColumn", "+",
		new BMessage(PARALLEL_ADD_COLUMN_MENU));
	fHeaderView->AddChild(fAddColumnButton);
}


ParallelBibleView::~ParallelBibleView()
{
	delete fNotes;

	// A no-op if fHeaderView was already destroyed by its adoptive
	// parent's own teardown (see ParallelHeaderView above) -- in that
	// case _HeaderViewDestroyed() already nulled this out.
	delete fHeaderView;
}


void
ParallelBibleView::_HeaderViewDestroyed()
{
	fHeaderView = NULL;
}


void
ParallelBibleView::AttachedToWindow()
{
	BView::AttachedToWindow();
	// See the constructor comment on fAddColumnButton: this is the first
	// point at which this view actually has a Looper, so it's the first
	// point SetTarget(this) can build a valid BMessenger.
	fAddColumnButton->SetTarget(this);
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


// A thin vertical line at each column boundary (see fColumnDividerX, kept
// up to date by _PositionColumns()) -- without it, one column's white
// background runs directly into the next with only kColumnSpacing of
// this view's own panel-gray background between them, no real edge to
// tell them apart at a glance.
void
ParallelBibleView::Draw(BRect updateRect)
{
	BView::Draw(updateRect);

	if (fColumnDividerX.empty() || fContentHeight <= 0.0f)
		return;

	float top = updateRect.top;
	float bottom = std::min(fContentHeight, updateRect.bottom);
	if (top > bottom)
		return;

	SetHighColor(0, 0, 0);
	for (size_t i = 0; i < fColumnDividerX.size(); i++) {
		float x = fColumnDividerX[i];
		if (x < updateRect.left || x > updateRect.right)
			continue;
		StrokeLine(BPoint(x, top), BPoint(x, bottom));
	}
}


void
ParallelBibleView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case PARALLEL_SELECT_MODULE:
		case PARALLEL_SELECT_NOTES:
		case PARALLEL_REMOVE_COLUMN:
		{
			// Selecting an item in a column's own dropdown, or clicking
			// its remove button, ultimately calls _RebuildLayout(),
			// which deletes and recreates every header BMenuField --
			// including, in the dropdown case, the very one whose click
			// this message came from. BMenuField::MouseDown() spawns a
			// dedicated background thread to track that click
			// (BMenuField.cpp's "_m_task_"), and ~BMenuField() blocks in
			// wait_for_thread() for it to finish; that thread still
			// needs this window's lock for its own cleanup after
			// invoking us. Rebuilding synchronously, in the same
			// dispatch that thread's Invoke() call landed in, can beat
			// it to that cleanup and deadlock: this (the window) thread
			// blocked in wait_for_thread() inside the delete, that
			// thread blocked trying to lock a window this thread already
			// holds. BLooper releases its lock between dispatching each
			// queued message, so re-posting once here -- handling it for
			// real only the second time through -- gives that thread the
			// gap it needs to finish first.
			if (!message->HasBool("deferred")) {
				BMessage deferred(*message);
				deferred.AddBool("deferred", true);
				BMessenger(this).SendMessage(&deferred);
				break;
			}

			if (message->what == PARALLEL_SELECT_MODULE) {
				int32 index;
				BString module;
				if (message->FindInt32("index", &index) == B_OK
					&& message->FindString("module", &module) == B_OK) {
					_SetColumnToBible(index, module.String());
				}
			} else if (message->what == PARALLEL_SELECT_NOTES) {
				int32 index;
				if (message->FindInt32("index", &index) == B_OK)
					_SetColumnToNotes(index);
			} else {
				int32 index;
				if (message->FindInt32("index", &index) == B_OK)
					RemoveColumn(index);
			}
			break;
		}

		case PARALLEL_ADD_COLUMN_MENU:
		{
			BPopUpMenu* menu = _BuildModuleMenu(-1, NULL, false);
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

		default:
			BView::MessageReceived(message);
			break;
	}
}


status_t
ParallelBibleView::AddColumn(const char* moduleName)
{
	return _SetColumnToBible(-1, moduleName);
}


status_t
ParallelBibleView::ReplaceColumn(int32 position, const char* moduleName)
{
	if (position < 0 || (size_t)position >= fColumnOrder.size())
		return B_BAD_INDEX;
	return _SetColumnToBible(position, moduleName);
}


status_t
ParallelBibleView::RemoveColumn(int32 position)
{
	if (position < 0 || (size_t)position >= fColumnOrder.size())
		return B_BAD_INDEX;

	if (fColumnOrder[position] == COLUMN_BIBLE) {
		int32 bibleIndex = _BibleIndexForPosition(position);
		fModules.erase(fModules.begin() + bibleIndex);
		fDocuments.erase(fDocuments.begin() + bibleIndex);
	} else {
		delete fNotes;
		fNotes = NULL;
	}
	fColumnOrder.erase(fColumnOrder.begin() + position);

	_RebuildLayout();
	return B_OK;
}


int32
ParallelBibleView::CountColumns() const
{
	return (int32)fColumnOrder.size();
}


int32
ParallelBibleView::FirstBibleColumnPosition() const
{
	for (size_t i = 0; i < fColumnOrder.size(); i++) {
		if (fColumnOrder[i] == COLUMN_BIBLE)
			return (int32)i;
	}
	return -1;
}


status_t
ParallelBibleView::SetNotesEnabled(bool enabled)
{
	int32 position = _NotesPosition();
	if (enabled == (position >= 0))
		return B_OK;

	if (enabled)
		return _SetColumnToNotes(-1);

	return RemoveColumn(position);
}


// Applies to every current Bible/Commentary column, and is remembered
// (fShowVerseNumbers) so columns added afterward (see _SetColumnToBible())
// start out matching it too -- this is a single, window-wide setting, not
// something that varies per column, mirroring how it worked before there
// were multiple columns at all.
status_t
ParallelBibleView::SetShowVerseNumbers(bool show)
{
	fShowVerseNumbers = show;
	for (size_t i = 0; i < fDocuments.size(); i++)
		fDocuments[i]->SetShowVerseNumbers(show);
	_Realign();
	return B_OK;
}


// Applies to every current Bible/Commentary column, and is remembered
// (fBaseFont) so columns added afterward (see _SetColumnToBible()) start
// out matching it too -- same rationale as SetShowVerseNumbers() above.
status_t
ParallelBibleView::SetBaseFont(const BFont& font)
{
	fBaseFont = font;
	for (size_t i = 0; i < fDocuments.size(); i++)
		fDocuments[i]->SetBaseFont(font);
	_Realign();
	return B_OK;
}


// Counts the COLUMN_BIBLE slots in fColumnOrder before `position`, which is
// exactly the index that slot's module/document pair has in fModules/
// fDocuments -- those arrays only ever hold Bible/Commentary columns, in
// the same relative order as they appear in fColumnOrder.
int32
ParallelBibleView::_BibleIndexForPosition(int32 position) const
{
	int32 index = 0;
	for (int32 i = 0; i < position && (size_t)i < fColumnOrder.size(); i++) {
		if (fColumnOrder[i] == COLUMN_BIBLE)
			index++;
	}
	return index;
}


// The position of the (single, shared) notes column in fColumnOrder, or -1
// if none of the current columns is one.
int32
ParallelBibleView::_NotesPosition() const
{
	for (size_t i = 0; i < fColumnOrder.size(); i++) {
		if (fColumnOrder[i] == COLUMN_NOTES)
			return (int32)i;
	}
	return -1;
}


// Makes the column at `position` a Bible/Commentary column showing
// `moduleName`, or appends a new one at the end if position < 0. If that
// slot was the notes column, the notes column is given up entirely (there
// is only ever one) and a new Bible slot is inserted in its place.
status_t
ParallelBibleView::_SetColumnToBible(int32 position, const char* moduleName)
{
	if (fManager == NULL)
		return B_NO_INIT;

	SWModule* module = fManager->getModule(moduleName);
	if (module == NULL)
		return B_NAME_NOT_FOUND;

	// Render filters (GBFPlain etc.) are expected to already be configured
	// on fManager, the same way SwordBackend configures its SWMgr with a
	// MarkupFilterMgr for every module it manages.
	if (!fCurrentKey.IsEmpty()) {
		// fCurrentKey is a localized reference (e.g. "1. Mose 1:1" under a
		// German locale, from the book menu) -- setText() needs the key's
		// locale set first or it fails silently and the module's key is
		// left at whatever it defaulted to, same class of bug already
		// worked around elsewhere in this file (see SetVerseKeyLocale()
		// above). Missing this exact call is why a freshly added column
		// used to land on the module's own default position instead of
		// matching the other columns.
		VerseKey verseKey(module->getKeyText());
		SetVerseKeyLocale(verseKey);
		verseKey.setText(fCurrentKey.String());
		module->setKey(verseKey);
	}

	BReference<BibleTextDocument> document(new BibleTextDocument(module),
		true);
	// Match whatever the other columns are currently showing (see
	// SetShowVerseNumbers()) -- BibleTextDocument defaults to true, so
	// this only matters when the setting has been turned off.
	document->SetShowVerseNumbers(fShowVerseNumbers);
	// Match the current base font (see SetBaseFont()) -- only matters once
	// the user has actually picked something other than be_plain_font.
	document->SetBaseFont(fBaseFont);

	if (position < 0) {
		fModules.push_back(module);
		fDocuments.push_back(document);
		fColumnOrder.push_back(COLUMN_BIBLE);
	} else if ((size_t)position >= fColumnOrder.size()) {
		return B_BAD_INDEX;
	} else if (fColumnOrder[position] == COLUMN_BIBLE) {
		int32 bibleIndex = _BibleIndexForPosition(position);
		fModules[bibleIndex] = module;
		fDocuments[bibleIndex] = document;
	} else {
		delete fNotes;
		fNotes = NULL;
		int32 bibleIndex = _BibleIndexForPosition(position);
		fModules.insert(fModules.begin() + bibleIndex, module);
		fDocuments.insert(fDocuments.begin() + bibleIndex, document);
		fColumnOrder[position] = COLUMN_BIBLE;
	}

	_RebuildLayout();
	return B_OK;
}


// Makes the column at `position` the notes column, or appends a new one at
// the end if position < 0. Only one notes column can exist at a time (they
// all share the same PersonalNotesModule) -- if a different column already
// is the notes column, this fails rather than moving it (the per-column
// dropdown only offers "Notes" when it would succeed, see
// _BuildModuleMenu()).
status_t
ParallelBibleView::_SetColumnToNotes(int32 position)
{
	int32 existing = _NotesPosition();
	if (existing >= 0) {
		if (existing == position)
			return B_OK;
		return B_NOT_ALLOWED;
	}

	fNotes = new PersonalNotesModule();
	status_t status = fNotes->Open();
	if (status != B_OK) {
		delete fNotes;
		fNotes = NULL;
		return status;
	}

	if (position < 0) {
		fColumnOrder.push_back(COLUMN_NOTES);
	} else if ((size_t)position >= fColumnOrder.size()) {
		delete fNotes;
		fNotes = NULL;
		return B_BAD_INDEX;
	} else {
		int32 bibleIndex = _BibleIndexForPosition(position);
		fModules.erase(fModules.begin() + bibleIndex);
		fDocuments.erase(fDocuments.begin() + bibleIndex);
		fColumnOrder[position] = COLUMN_NOTES;
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
	SetVerseKeyLocale(verseKey);
	verseKey.setText(fCurrentKey.String());
	_ScrollToVerse(verseKey.getVerse());

	return B_OK;
}


void
ParallelBibleView::HighlightVerse(int startVerse, int endVerse)
{
	for (size_t i = 0; i < fDocuments.size(); i++) {
		int32 start, end;
		if (fDocuments[i]->TextRangeForVerseRange(startVerse, endVerse,
				start, end)) {
			fTextViews[i]->SetSelection(start, end);
		} else {
			fTextViews[i]->SetSelection(0, 0);
		}
	}
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
		TextDocumentView* view = new BibleColumnView("bibleColumn",
			fDocuments[i].Get(), fModules[i]->getName());
		// These are a reading surface, not a details panel -- override the
		// B_PANEL_BACKGROUND_COLOR (gray) the class constructs with. Both
		// calls are needed: ViewColor is what a freshly exposed area gets
		// painted with, LowColor is what Draw()'s own background fill
		// (FillRect(..., B_SOLID_LOW)) uses, and the class constructor sets
		// LowColor from the ViewColor it had at construction time, which we
		// are about to change out from under it.
		view->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		view->SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		view->SetInsets(kBibleColumnInset);
		view->SetSelectionEnabled(true);
		view->SetTextDocument(fDocuments[i]);
		AddChild(view);
		fTextViews.push_back(view);
	}

	_RebuildHeader();
	_RebuildNoteFields();
	_Realign();
}


// Rebuilds the header row's per-column BMenuFields + remove buttons -- one
// pair per fColumnOrder slot, Bible/Commentary columns and the notes column
// alike, so every column's dropdown offers switching to any of those (see
// _BuildModuleMenu()). Positioning happens in _PositionColumns(), which
// uses the exact same x-offsets as the content columns so header cells and
// columns always line up.
void
ParallelBibleView::_RebuildHeader()
{
	// If the number of columns hasn't changed, this is a content-only
	// update (a column's dropdown picked a different module/Notes) --
	// update each existing BMenuField's menu contents and label in
	// place instead of deleting and recreating it. This matters because
	// BMenuField::MouseDown() (Haiku's Interface Kit) spawns a
	// background thread ("_m_task_") to track the very click that
	// produced this rebuild, and ~BMenuField() blocks in
	// wait_for_thread() for it; if that thread still needs this
	// window's lock for its own post-click cleanup while this dispatch
	// already holds it (BLooper holds its lock for a dispatch's full
	// duration), deleting the field synchronously here deadlocks the
	// whole window -- reproduced live, not just theoretical. Structural
	// changes (a column added or removed, below) don't have this risk:
	// those are triggered by a plain BButton click ("+"/"x"), which
	// tracks synchronously with no spawned thread, so by the time this
	// runs no *other* field's tracking thread should still be active.
	if (fHeaderFields.size() == fColumnOrder.size()) {
		for (size_t i = 0; i < fColumnOrder.size(); i++) {
			bool isNotes = (fColumnOrder[i] == COLUMN_NOTES);
			const char* label = isNotes
				? B_TRANSLATE("Notes")
				: fModules[_BibleIndexForPosition((int32)i)]->getName();

			BMenuField* field = fHeaderFields[i];
			BMenu* menu = field->Menu();
			while (menu->CountItems() > 0)
				delete menu->RemoveItem((int32)0);
			_PopulateModuleMenu(menu, (int32)i, isNotes ? NULL : label,
				isNotes);
			if (field->MenuItem() != NULL)
				field->MenuItem()->SetLabel(label);
		}
		return;
	}

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

	for (size_t i = 0; i < fColumnOrder.size(); i++) {
		bool isNotes = (fColumnOrder[i] == COLUMN_NOTES);
		const char* label = isNotes
			? B_TRANSLATE("Notes")
			: fModules[_BibleIndexForPosition((int32)i)]->getName();

		BPopUpMenu* menu = _BuildModuleMenu((int32)i,
			isNotes ? NULL : label, isNotes);
		BMenuField* field = new BMenuField("columnHeader", NULL, menu);
		field->SetDivider(0.0f);
		// labelFromMarked (see _BuildModuleMenu()) only looks at the
		// popup's direct items, not the category submenus (or, for
		// "Notes", the plain top-level item) the marked item actually
		// lives in now, so the field would otherwise show the popup's own
		// internal name ("translation") instead of the selected module --
		// set it explicitly instead.
		if (field->MenuItem() != NULL)
			field->MenuItem()->SetLabel(label);
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

	if (columns.size() >= 2) {
		VerseAligner::Align(columns, widths);
	}

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
	float bibleWidth = _ColumnWidth();
	float x = 0.0f;
	float contentHeight = 0.0f;
	fColumnDividerX.clear();

	size_t bibleIndex = 0;
	for (size_t i = 0; i < fColumnOrder.size(); i++) {
		bool isNotes = (fColumnOrder[i] == COLUMN_NOTES);
		float width = isNotes ? _NotesColumnWidth() : bibleWidth;

		if (i < fHeaderFields.size()) {
			// Sized to its own natural (preferred) width -- like the
			// original toolbar's module field -- rather than stretched
			// to fill the column. A stretched fixed-size BMenuField
			// still draws its native pop-up marker at its own right
			// edge (see BMenuField/_BMCMenuBar_ in the Interface Kit;
			// nothing here draws it), which the remove button, sitting
			// right there, would otherwise cover; bounding the field to
			// its content and placing the button right after it instead
			// keeps that marker visible.
			float preferredWidth, preferredHeight;
			fHeaderFields[i]->GetPreferredSize(&preferredWidth,
				&preferredHeight);
			float fieldWidth = std::min(preferredWidth,
				std::max(0.0f, width - kRemoveButtonWidth));

			fHeaderFields[i]->MoveTo(x, 0.0f);
			fHeaderFields[i]->ResizeTo(fieldWidth, kHeaderHeight);
			fRemoveButtons[i]->MoveTo(x + fieldWidth, 0.0f);
			fRemoveButtons[i]->ResizeTo(kRemoveButtonWidth, kHeaderHeight);
		}

		if (isNotes) {
			float fieldWidth = std::max(0.0f, width - kNoteVerseLabelWidth);
			float y = 0.0f;
			for (size_t v = 0; v < fNoteFields.size(); v++) {
				float rowHeight = _RowHeight((int)(v + 1));

				// The Bible column's verse number is only ever drawn on
				// the verse's first line, at the row's top -- so the
				// label needs to line up with that regardless of how tall
				// the row grows for wrapped verse text. Sizing the label
				// to the *entire* row let BStringView's own (vertically
				// centering) Draw() place the digit wherever the row's
				// midpoint happened to fall, which drifted further from
				// the top the taller (more wrapped) a verse's row was.
				// Giving it a box exactly one font-line tall, anchored at
				// the same y the row (and so the Bible column's own first
				// line) starts at, keeps the two in sync at any row
				// height instead. kBibleColumnInset accounts for the
				// Bible TextDocumentView's own top inset (see
				// _RebuildLayout()), which shifts its actual rendered
				// text down from the view's row-relative y=0 by that much
				// -- without it, every label sits a few pixels above its
				// Bible-column counterpart.
				BFont font;
				fNoteVerseLabels[v]->GetFont(&font);
				font_height fontHeight;
				font.GetHeight(&fontHeight);
				float lineHeight = fontHeight.ascent + fontHeight.descent
					+ fontHeight.leading;

				fNoteVerseLabels[v]->MoveTo(x, y + kBibleColumnInset);
				fNoteVerseLabels[v]->ResizeTo(kNoteVerseLabelWidth,
					lineHeight);

				fNoteFields[v]->MoveTo(x + kNoteVerseLabelWidth, y);
				fNoteFields[v]->ResizeTo(fieldWidth, rowHeight);

				y += rowHeight;
			}
			contentHeight = std::max(contentHeight, y);
		} else {
			// The document may have been rebuilt more than once in a row
			// (once per SetVerseSpacing() call inside
			// VerseAligner::Align()); force a fresh measurement rather
			// than risk GetHeightForWidth() reading a TextDocumentLayout
			// copy whose cache wasn't invalidated for the most recent of
			// those rebuilds.
			TextDocumentView* view = fTextViews[bibleIndex];
			view->Relayout();

			float min, max, preferred;
			view->GetHeightForWidth(width, &min, &max, &preferred);

			view->MoveTo(x, 0.0f);
			view->ResizeTo(width, preferred);

			contentHeight = std::max(contentHeight, preferred);
			bibleIndex++;
		}

		x += width + kColumnSpacing;
		fColumnDividerX.push_back(x - kColumnSpacing / 2.0f);
	}

	fAddColumnButton->MoveTo(x, 0.0f);
	fAddColumnButton->ResizeTo(kHeaderHeight, kHeaderHeight);
	x += kHeaderHeight + kColumnSpacing;

	fContentHeight = contentHeight;
	fContentWidth = fColumnOrder.empty() ? 0.0f : (x - kColumnSpacing);

	// fHeaderView's own Frame() is left to the window's layout (matching
	// the scroll viewport, same as this view's own Frame() is left to the
	// BScrollView that wraps it); its header cells, like this view's
	// TextDocumentView columns, are positioned above via MoveTo() up to
	// fContentWidth, which can exceed that Frame() -- Bounds()-origin
	// shifting (mirrored from this view's own scroll position in
	// ScrollTo()) is what reveals the overflow, not resizing the view.
	_UpdateScrollBars();

	// fColumnDividerX just changed; Draw() (which paints the divider
	// lines) needs to run again to reflect the new positions.
	Invalidate();
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
	// _NotesColumnWidth()) notes column and the trailing "+" button --
	// without reserving the button's own width here, a single column
	// would claim the entire viewport and push "+" off-screen, reachable
	// only by a horizontal scroll nothing hints exists. If that means the
	// columns no longer all fit, _PositionColumns()'s resulting
	// fContentWidth ends up wider than this view's own Bounds(), and the
	// horizontal scrollbar (see _UpdateScrollBars()) is how the overflow
	// columns (and the button) stay reachable instead of just running
	// off-screen.
	float available = totalWidth - kColumnSpacing * (columnCount - 1)
		- notesWidth - kHeaderHeight - kColumnSpacing;
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


// Fills an already-constructed, already-empty BMenu with every installed
// module, split into per-category submenus (Biblical Texts, Commentaries
// -- matching the categories the old toolbar's module field used to split
// into before the main window/Parallel View merge), labeled with each
// module's short code (e.g. "KJV") rather than its full description --
// the latter is too long to be legible once truncated to column width,
// plus a "Notes" entry so any column can be turned into the notes column
// from its own dropdown. columnIndex is embedded in each item's message
// so MessageReceived() knows whether to append a new column (columnIndex
// < 0, used by the trailing "+" button) or replace an existing one
// (columnIndex >= 0, used by a column's own header field).
// markedModuleName, if given, is checked off to show the column's
// current selection; markNotes does the same for the "Notes" entry.
// There's only ever one notes column (backed by the single, shared
// PersonalNotesModule, see fNotes), so "Notes" is only offered here if
// this column already is the notes column (markNotes) or no column
// currently is one (fNotes == NULL) -- otherwise picking it would have
// nothing sensible to do.
//
// Split out from _BuildModuleMenu() (below) so _RebuildHeader() can
// repopulate an *existing* column's menu in place instead of building a
// fresh one -- see there for why: deleting the BMenuField that wraps it
// is the risky part, not deleting/rebuilding what's inside it.
void
ParallelBibleView::_PopulateModuleMenu(BMenu* menu, int32 columnIndex,
	const char* markedModuleName, bool markNotes)
{
	BMenu* bibleMenu = new BMenu(B_TRANSLATE("Biblical Texts"));
	BMenu* commentaryMenu = new BMenu(B_TRANSLATE("Commentaries"));

	const ModMap& modules = fManager->getModules();
	for (ModMap::const_iterator it = modules.begin(); it != modules.end();
			++it) {
		SWModule* module = it->second;
		if (module == NULL)
			continue;

		BMenu* category = NULL;
		if (strcmp(module->getType(), "Biblical Texts") == 0)
			category = bibleMenu;
		else if (strcmp(module->getType(), "Commentaries") == 0)
			category = commentaryMenu;
		else
			continue;

		BMessage* message = new BMessage(PARALLEL_SELECT_MODULE);
		message->AddInt32("index", columnIndex);
		message->AddString("module", module->getName());

		BMenuItem* item = new BMenuItem(module->getName(), message);
		if (markedModuleName != NULL
			&& strcmp(module->getName(), markedModuleName) == 0) {
			item->SetMarked(true);
		}
		category->AddItem(item);
	}

	// SetTargetForItems() doesn't descend into submenus, so each category
	// needs its own call -- not just one on the top-level popup menu.
	bibleMenu->SetTargetForItems(this);
	commentaryMenu->SetTargetForItems(this);

	if (bibleMenu->CountItems() > 0)
		menu->AddItem(bibleMenu);
	else
		delete bibleMenu;
	if (commentaryMenu->CountItems() > 0)
		menu->AddItem(commentaryMenu);
	else
		delete commentaryMenu;

	if (markNotes || fNotes == NULL) {
		BMessage* notesMessage = new BMessage(PARALLEL_SELECT_NOTES);
		notesMessage->AddInt32("index", columnIndex);
		BMenuItem* notesItem = new BMenuItem(B_TRANSLATE("Notes"),
			notesMessage);
		notesItem->SetTarget(this);
		notesItem->SetMarked(markNotes);
		menu->AddItem(notesItem);
	}
}


BPopUpMenu*
ParallelBibleView::_BuildModuleMenu(int32 columnIndex,
	const char* markedModuleName, bool markNotes)
{
	if (fManager == NULL)
		return NULL;

	// radioMode so BMenu itself keeps exactly one item marked as the user
	// picks different translations; labelFromMarked so the field displays
	// that marked item's label instead of this constructor's own `name`.
	BPopUpMenu* menu = new BPopUpMenu("translation", true, true);
	_PopulateModuleMenu(menu, columnIndex, markedModuleName, markNotes);
	return menu;
}

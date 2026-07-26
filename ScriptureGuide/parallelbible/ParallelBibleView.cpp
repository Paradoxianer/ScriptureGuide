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
#include <Cursor.h>
#include <Entry.h>
#include <File.h>
#include <Font.h>
#include <Language.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <StringView.h>
#include <TextView.h>
#include <Window.h>

#include <versekey.h>

#include "ParagraphLayout.h"
#include "SwordBackend.h"
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
const float ParallelBibleView::kHeaderBottomGap = 2.0f;
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
// selection at all starts a *fresh* selection instead (see below), rather
// than falling straight through to TextDocumentView's own handling.
//
// Fresh selections coordinate across columns through fOwner (see
// ParallelBibleView::_ColumnSelectionStarted() and friends): the whole
// gesture is one selection rectangle from the MouseDown point to wherever
// the cursor currently is, same as dragging a selection rectangle in any
// two-dimensional view -- which verses (the rectangle's y-extent) and
// which columns (its x-extent) are resolved once per move and applied to
// every column whose own frame overlaps it, rather than each column
// growing its own selection independently in isolation. Verse-level
// granularity, not character-level, once more than one column can be
// involved -- half a verse in one translation and a different half in
// another don't correspond to anything; verses are the unit two
// translations can actually agree on. _StartDrag() then gathers every
// column with a non-empty selection into one combined clipping, matching
// issue #23's original spec (one translation+reference block per
// participating column).
class BibleColumnView : public TextDocumentView {
public:
	BibleColumnView(const char* name, BibleTextDocument* document,
		const char* translationName, ParallelBibleView* owner)
		:
		TextDocumentView(name),
		fBibleDocument(document),
		fTranslationName(translationName),
		fOwner(owner),
		fTrackingForDrag(false),
		fShowingLinkCursor(false)
	{
	}

	// A dropped Bible reference navigates every column (see #23 -- for
	// now they're all chained to the same key via fOwner->SetKey();
	// once columns can be unchained this'll need to target just this one
	// instead), whichever column it happens to land on. Anything that
	// isn't a drop, or doesn't resolve to a reference, falls through to
	// the base class unchanged.
	virtual void MessageReceived(BMessage* message)
	{
		if (message->WasDropped() && _HandleReferenceDrop(message))
			return;
		TextDocumentView::MessageReceived(message);
	}

	virtual void MouseDown(BPoint where)
	{
		fMouseDownPoint = where;
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

		// A fresh selection starts here -- claim ownership of the
		// gesture for as long as the mouse stays down, so sibling
		// columns (which still get their own MouseMoved() as the cursor
		// passes over them -- see the class comment) stay passive
		// instead of independently growing their own selection from
		// wherever they happened to be sitting. fOwner resolves the
		// whole gesture as one selection rectangle from this anchor
		// point to wherever the cursor currently is -- see
		// ParallelBibleView::_ColumnSelectionMoved().
		if (fOwner != NULL)
			fOwner->_ColumnSelectionStarted(this, ConvertToScreen(where));

		TextDocumentView::MouseDown(where);
	}

	virtual void MouseMoved(BPoint where, uint32 transit,
		const BMessage* dragMessage)
	{
		_UpdateLinkCursor(where, transit);

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

		if (fOwner != NULL && fOwner->_IsColumnSelectionOwnedByOther(this)) {
			// Some other column owns the active fresh-selection gesture
			// right now -- do NOT fall through to the base class, which
			// would otherwise extend this column's own selection just
			// because a mouse button happens to still be down.
			return;
		}

		if (fOwner != NULL && fOwner->_HasActiveColumnSelection()) {
			uint32 buttons = 0;
			if (Window() != NULL) {
				Window()->CurrentMessage()->FindInt32("buttons",
					(int32*)&buttons);
			}
			if (buttons > 0) {
				fOwner->_ColumnSelectionMoved(this, ConvertToScreen(where));
				return;
			}
		}

		TextDocumentView::MouseMoved(where, transit, dragMessage);
	}

	virtual void MouseUp(BPoint where)
	{
		// A plain click, not a drag -- same threshold/rationale as
		// MouseMoved()'s own fTrackingForDrag check just above, just
		// measured from MouseDown() to here instead of continuously.
		// Needed here (rather than reusing fDragStartPoint) because a
		// fresh-selection click (the `else if` branch below) never sets
		// fDragStartPoint at all -- only the "pressed inside an existing
		// selection" branch does.
		float dx = where.x - fMouseDownPoint.x;
		float dy = where.y - fMouseDownPoint.y;
		const float kClickThreshold = 4.0f;
		bool wasPlainClick
			= dx * dx + dy * dy <= kClickThreshold * kClickThreshold;

		if (fTrackingForDrag) {
			// Pressed inside the selection, released again without
			// moving far enough to start a drag -- a plain click after
			// all, meant to move the caret there like normal (unless it
			// landed on a cross-reference -- see #28 -- or a Strong's
			// number -- see #27 -- in which case following that takes
			// over instead).
			fTrackingForDrag = false;
			bool followedLink = wasPlainClick
				&& (_TryFollowReferenceAt(where)
					|| _TryFollowStrongsNumberAt(where));
			if (!followedLink)
				SetCaret(where, false);
		} else if (fOwner != NULL) {
			fOwner->_ColumnSelectionEnded();
			if (wasPlainClick) {
				if (!_TryFollowReferenceAt(where))
					_TryFollowStrongsNumberAt(where);
			}
		}
		TextDocumentView::MouseUp(where);
	}

	// The rest of these are called by ParallelBibleView::
	// _ColumnSelectionMoved() -- see the class comment. Once a
	// fresh-selection gesture has crossed into a different column at
	// least once, _ColumnSelectionMoved() switches every participating
	// column (source included) over to these: the whole gesture becomes
	// one shared verse range applied uniformly, since "half a verse in
	// one translation, a different half in another" doesn't mean
	// anything -- verses are the unit two translations can actually
	// agree on, characters aren't.

	// The verse at this column's own local point `where`, or -1 if it
	// doesn't resolve to one (e.g. outside the loaded chapter).
	int VerseAt(BPoint where)
	{
		if (fBibleDocument == NULL)
			return -1;
		int32 paragraphOffset;
		int32 offset = TextOffsetAt(where);
		int32 paragraphIndex = fBibleDocument->ParagraphIndexFor(offset,
			paragraphOffset);
		return fBibleDocument->VerseForParagraphIndex(paragraphIndex);
	}

	// lowVerse..highVerse translated through this column's own paragraph
	// layout (a verse number means the same paragraph index in every
	// column -- see VerseAligner) and applied as its selection. Clears
	// the selection instead if this column doesn't have that range at
	// all (e.g. a commentary without an entry for one of those verses).
	void SelectVerseRange(int lowVerse, int highVerse)
	{
		int32 start, end;
		if (fBibleDocument != NULL
			&& fBibleDocument->TextRangeForVerseRange(lowVerse, highVerse,
				start, end)) {
			SetSelectionEnabled(true);
			SetSelection(start, end);
		} else {
			SetSelection(0, 0);
		}
	}

	void ClearColumnSelection()
	{
		SetSelection(0, 0);
	}

private:
	void _StartDrag()
	{
		if (fOwner == NULL)
			return;

		// Gather every column (this one included) that currently has a
		// non-empty selection -- a fresh cross-column gesture (see
		// SelectVerseRange() above) can leave more than one populated
		// before the user picks any of them back up to drag. Matches
		// issue #23's original spec: one translation block per
		// participating column. The reference itself (book/chapter/verse)
		// is the same across every participating column no matter its
		// translation -- VerseAligner keeps them all on the same verse
		// range -- so it's computed once from this column's own current
		// selection, not repeated per column.
		int32 dragStart, dragEnd;
		GetSelection(dragStart, dragEnd);
		BString reference = _ReferenceFor(dragStart, dragEnd);

		const std::vector<TextDocumentView*>& columns = fOwner->_ColumnViews();

		BString clipText(reference);
		clipText << "\n\n";
		int32 partCount = 0;

		for (size_t i = 0; i < columns.size(); i++) {
			BibleColumnView* column
				= dynamic_cast<BibleColumnView*>(columns[i]);
			if (column == NULL || column->fBibleDocument == NULL)
				continue;

			int32 start, end;
			column->GetSelection(start, end);
			if (start >= end)
				continue;

			BString text = column->fBibleDocument->Text(start, end - start);
			if (text.IsEmpty())
				continue;

			if (partCount > 0)
				clipText << "\n\n";
			clipText << column->fTranslationName << ":\n" << text;

			partCount++;
		}

		if (partCount == 0)
			return;

		// "be:clip_name" ends up as a file name if this gets dropped onto
		// Tracker (see BPoseView::CreateClippingFile()) -- a literal '/'
		// in it is a path separator there, not a character a file name can
		// contain. References are built from book/chapter/verse numbers
		// so they shouldn't ever contain one, but scrub it anyway rather
		// than assume.
		BString clipName(reference);
		clipName.ReplaceAll("/", "-");

		// B_MIME_DATA, not B_SIMPLE_DATA -- this is the "what" a BTextView
		// itself puts on its own drag messages, and the one text drop
		// targets (StyledEdit and friends) actually recognize. B_SIMPLE_DATA
		// is the convention for file-ref drags (Tracker), not text.
		BMessage drag(B_MIME_DATA);
		drag.AddData("text/plain", B_MIME_TYPE, clipText.String(),
			clipText.Length());
		drag.AddString("be:clip_name", clipName);
		// Not consumed by anything yet -- for a future drop target (see
		// issue #23) that wants the reference/translation without having
		// to re-parse them back out of the plain-text clipping. Only the
		// dragging column's own translation when more than one
		// participated -- there's no single "the" translation once
		// several columns are involved.
		if (partCount == 1)
			drag.AddString("scriptureguide:translation", fTranslationName);
		drag.AddString("scriptureguide:reference", reference);

		BPoint dragScreenPoint = ConvertToScreen(fDragStartPoint);
		BBitmap* dragBitmap = _CreateDragBitmap(dragScreenPoint);
		if (dragBitmap != NULL && dragBitmap->IsValid()) {
			BRect bounds = dragBitmap->Bounds();
			BPoint hotspot(bounds.Width() / 2.0f, bounds.Height() / 2.0f);
			DragMessage(&drag, dragBitmap, B_OP_COPY, hotspot, this);
		} else {
			delete dragBitmap;
			BRect dragRect(fDragStartPoint.x - 4.0f,
				fDragStartPoint.y - 4.0f, fDragStartPoint.x + 200.0f,
				fDragStartPoint.y + 20.0f);
			DragMessage(&drag, dragRect & Bounds(), this);
		}
	}

	// A small screenshot excerpt centered on the drag point -- just
	// enough to show the user what they're carrying, not the whole
	// selection. Ownership passes to DragMessage() once called; the
	// caller must not delete the returned bitmap itself.
	BBitmap* _CreateDragBitmap(BPoint screenPoint) const
	{
		const float kHalfWidth = 90.0f;
		const float kHalfHeight = 24.0f;

		BScreen screen(Window());
		if (!screen.IsValid())
			return NULL;

		BRect screenRect(screenPoint.x - kHalfWidth,
			screenPoint.y - kHalfHeight, screenPoint.x + kHalfWidth,
			screenPoint.y + kHalfHeight);
		screenRect = screenRect & screen.Frame();
		if (!screenRect.IsValid())
			return NULL;

		BBitmap* bitmap = new BBitmap(screenRect, B_RGB32);
		if (!bitmap->IsValid()
			|| screen.ReadBitmap(bitmap, false, &screenRect) != B_OK) {
			delete bitmap;
			return NULL;
		}

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

		// German convention uses a comma between chapter and verse
		// ("Epheser 6, 9"), not a colon -- ParseVerseReference() already
		// accepts both on input, so this stays round-trip safe when the
		// reference gets dropped back into the app.
		BLanguage language;
		BLocale::Default()->GetLanguage(&language);
		const char* separator
			= strcmp(language.Code(), "de") == 0 ? ", " : ":";

		BString reference(fBibleDocument->BookName());
		reference << " " << fBibleDocument->Chapter() << separator
			<< startVerse;
		if (endVerse > startVerse)
			reference << "-" << endVerse;
		return reference;
	}

	// A plain click (not a drag) landing on a cross-reference detected in
	// this column's own text (see #28,
	// BibleTextDocument::ReferenceLinkAt()) jumps every column there --
	// same SG_BIBLE path _HandleReferenceDrop() below already uses for a
	// dropped reference, so the toolbar's book/chapter/verse fields stay
	// in sync too, not just the reading pane. False (a no-op) anywhere
	// else in the text.
	bool _TryFollowReferenceAt(BPoint where)
	{
		if (fBibleDocument == NULL)
			return false;

		int32 offset = TextOffsetAt(where);
		BString key;
		if (!fBibleDocument->ReferenceLinkAt(offset, key))
			return false;

		BWindow* window = Window();
		if (window == NULL)
			return false;

		BMessage jump(SG_BIBLE);
		jump.AddString("key", key);
		window->PostMessage(&jump);
		return true;
	}


	// Same idea as _TryFollowReferenceAt() above, for a word tagged with
	// a Strong's number (see #27, BibleTextDocument::StrongsNumberAt())
	// -- opens/reuses the dictionary window instead of jumping a verse.
	bool _TryFollowStrongsNumberAt(BPoint where)
	{
		if (fBibleDocument == NULL)
			return false;

		int32 offset = TextOffsetAt(where);
		BString number;
		if (!fBibleDocument->StrongsNumberAt(offset, number))
			return false;

		BWindow* window = Window();
		if (window == NULL)
			return false;

		BMessage lookup(SG_STRONGS_LOOKUP);
		lookup.AddString("number", number);
		window->PostMessage(&lookup);
		return true;
	}


	// True if `message` carried a Bible reference to navigate to --
	// "scriptureguide:reference" if this came from our own drag source
	// (see _StartDrag()), otherwise falling back to parsing whatever
	// plain text was dropped so drops from outside the app work too.
	// Only the first line matters for the fallback case, since our own
	// multi-column clippings put the reference alone on the first line
	// with each translation's own text following it.
	bool _HandleReferenceDrop(BMessage* message)
	{
		if (fOwner == NULL)
			return false;

		BString reference;
		if (message->FindString("scriptureguide:reference", &reference)
				!= B_OK) {
			BString firstLine;
			const char* text;
			ssize_t length;
			if (message->FindData("text/plain", B_MIME_TYPE,
					(const void**)&text, &length) == B_OK) {
				firstLine.SetTo(text, length);
			} else {
				// Dragging a file -- a Tracker clipping, e.g. -- delivers
				// an entry_ref, not inline text; Tracker's own drag
				// protocol for files never includes the data itself, only
				// a pointer to it on disk. Read a bounded amount directly
				// rather than assume anything about the file's size.
				entry_ref ref;
				if (message->FindRef("refs", &ref) != B_OK)
					return false;

				BFile file(&ref, B_READ_ONLY);
				if (file.InitCheck() != B_OK)
					return false;

				const size_t kMaxClippingSize = 4096;
				char buffer[kMaxClippingSize];
				ssize_t bytesRead = file.Read(buffer, sizeof(buffer));
				if (bytesRead <= 0)
					return false;
				firstLine.SetTo(buffer, bytesRead);
			}

			int32 newline = firstLine.FindFirst('\n');
			if (newline >= 0)
				firstLine.Truncate(newline);

			if (!ParseVerseReference(firstLine.String(), reference))
				return false;
		}

		// Post to the window rather than call fOwner->SetKey() directly
		// -- SetKey() only updates this view's own columns. SGMainWindow
		// keeps separate book-menu/chapter/verse UI state in sync with
		// the current key exclusively through its own JumpToKey(), which
		// SG_BIBLE routes to (see SGMainWindow::MessageReceived()) -- the
		// same path the universal search box and search results already
		// use, so a dropped reference stays consistent with every other
		// way to navigate instead of updating the reading pane while
		// leaving the book/chapter/verse fields showing the old position.
		BWindow* window = Window();
		if (window == NULL)
			return false;

		BMessage jump(SG_BIBLE);
		jump.AddString("key", reference);
		window->PostMessage(&jump);
		return true;
	}

	// Swaps in the system's "follow link" cursor while hovering a
	// cross-reference or Strong's number (either kind of clickable span
	// -- see _TryFollowReferenceAt()/_TryFollowStrongsNumberAt() above),
	// the plain arrow everywhere else -- requested alongside toning
	// down the Strong's highlight color, since a plain underline alone
	// doesn't read as "clickable" as clearly as the color did.
	// fShowingLinkCursor avoids calling SetViewCursor() on every single
	// MouseMoved() when the hover state hasn't actually changed.
	void _UpdateLinkCursor(BPoint where, uint32 transit)
	{
		bool overLink = false;
		if (fBibleDocument != NULL && transit != B_EXITED_VIEW
			&& transit != B_OUTSIDE_VIEW) {
			int32 offset = TextOffsetAt(where);
			BString unused;
			overLink = fBibleDocument->ReferenceLinkAt(offset, unused)
				|| fBibleDocument->StrongsNumberAt(offset, unused);
		}

		if (overLink == fShowingLinkCursor)
			return;
		fShowingLinkCursor = overLink;

		static BCursor sLinkCursor(B_CURSOR_ID_FOLLOW_LINK);
		static BCursor sDefaultCursor(B_CURSOR_ID_SYSTEM_DEFAULT);
		SetViewCursor(overLink ? &sLinkCursor : &sDefaultCursor);
	}

private:
	BibleTextDocument*	fBibleDocument;
	BString				fTranslationName;
	ParallelBibleView*	fOwner;
	bool				fTrackingForDrag;
	BPoint				fDragStartPoint;
	BPoint				fMouseDownPoint;
	bool				fShowingLinkCursor;
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

	// A thin line along the bottom edge, the same style as the vertical
	// column dividers in ParallelBibleView::Draw() -- without it the
	// toolbar/header area blends directly into the reading pane below
	// with no clean edge between them.
	virtual void Draw(BRect updateRect)
	{
		BView::Draw(updateRect);

		BRect bounds = Bounds();
		SetHighColor(0, 0, 0);
		StrokeLine(BPoint(bounds.left, bounds.bottom),
			BPoint(bounds.right, bounds.bottom));
	}

	virtual ~ParallelHeaderView()
	{
		if (fOwner != NULL)
			fOwner->_HeaderViewDestroyed();
	}

	// Reordering a column (see #23) is started from a MouseDown that
	// lands directly on this view rather than on one of its children --
	// Haiku only dispatches MouseDown() here when the point isn't inside
	// a child's frame, so this only fires on the blank strip of header
	// background a column's BMenuField/remove button don't cover (their
	// own width is their *preferred* size, not the full column width --
	// see _PositionColumns()), never hijacking an actual click on either
	// control. No separate drag handle needed as a result.
	virtual void MouseDown(BPoint where)
	{
		if (fOwner == NULL) {
			BView::MouseDown(where);
			return;
		}

		uint32 buttons = 0;
		BMessage* message = Window()->CurrentMessage();
		if (message != NULL)
			message->FindInt32("buttons", (int32*)&buttons);
		if (buttons != B_PRIMARY_MOUSE_BUTTON) {
			BView::MouseDown(where);
			return;
		}

		int32 index = fOwner->_ColumnIndexForX(where.x);
		if (index < 0) {
			BView::MouseDown(where);
			return;
		}

		BMessage dragMessage(PARALLEL_REORDER_COLUMN);
		dragMessage.AddInt32("from", index);
		DragMessage(&dragMessage, Bounds());
	}

	// The other half of the gesture MouseDown() above starts: a dropped
	// PARALLEL_REORDER_COLUMN message lands here via the normal
	// MessageReceived() path (Haiku delivers a dropped BMessage to
	// whatever view is under the drop point, the same as any other
	// message), not through some separate drag-and-drop callback.
	virtual void MessageReceived(BMessage* message)
	{
		if (message->WasDropped() && message->what == PARALLEL_REORDER_COLUMN
			&& fOwner != NULL) {
			int32 from;
			if (message->FindInt32("from", &from) == B_OK) {
				BPoint dropPoint = message->DropPoint();
				ConvertFromScreen(&dropPoint);
				int32 to = fOwner->_ColumnIndexForX(dropPoint.x);
				fOwner->_MoveColumn(from, to);
			}
			return;
		}
		BView::MessageReceived(message);
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
	fActiveSelectionColumn(NULL),
	fSelectionLastEndVerse(-1),
	fShowVerseNumbers(true),
	fInitialWidth(initialWidth),
	fContentHeight(0.0f),
	fContentWidth(0.0f),
	fNotesWidthFraction(-1.0f),
	fNotesSplitDragGuideX(-1.0f)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// Not added as a child of this view -- see the class comment on why
	// the header lives outside the scrolled hierarchy. HeaderView() hands
	// this to the caller, who places it in their own layout; see
	// ParallelHeaderView above for how ownership/deletion is handled
	// either way.
	fHeaderView = new ParallelHeaderView("parallelHeader", B_WILL_DRAW, this);
	fHeaderView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fHeaderView->SetExplicitMinSize(
		BSize(B_SIZE_UNSET, kHeaderHeight + kHeaderBottomGap));
	fHeaderView->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, kHeaderHeight + kHeaderBottomGap));

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

	// The notes-splitter drag in progress (see MouseDown()/MouseMoved()
	// below) -- a plain guide line at the tracked x, not an actual
	// resize; only MouseUp() commits a real width/relayout, so this is
	// the only visual feedback while the drag is live.
	if (fNotesSplitDragGuideX >= 0.0f
		&& fNotesSplitDragGuideX >= updateRect.left
		&& fNotesSplitDragGuideX <= updateRect.right) {
		StrokeLine(BPoint(fNotesSplitDragGuideX, top),
			BPoint(fNotesSplitDragGuideX, bottom));
	}
}


// Starts dragging the notes-column splitter (see #19) if the click landed
// within a few pixels of _NotesSplitDividerX() -- the only divider that's
// ever draggable, all the others being purely cosmetic boundaries between
// equal-share Bible/Commentary columns. Falls through to the base class
// otherwise, same as ParallelHeaderView::MouseDown() does for its own
// column-reorder gesture (see #23) -- both rely on this view only ever
// getting MouseDown() at all when the point isn't inside a child view's
// frame, i.e. only in the gap between columns.
void
ParallelBibleView::MouseDown(BPoint where)
{
	float dividerX = _NotesSplitDividerX();
	static const float kSplitDragTolerance = 4.0f;
	if (dividerX >= 0.0f) {
		float distance = where.x > dividerX ? where.x - dividerX
			: dividerX - where.x;
		if (distance <= kSplitDragTolerance) {
			fNotesSplitDragGuideX = dividerX;
			SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
			Invalidate();
			return;
		}
	}
	BView::MouseDown(where);
}


void
ParallelBibleView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (fNotesSplitDragGuideX >= 0.0f) {
		fNotesSplitDragGuideX = where.x;
		Invalidate();
		return;
	}
	BView::MouseMoved(where, transit, dragMessage);
}


// Commits the drag started in MouseDown(): the notes column always sits
// immediately after _NotesSplitDividerX(), regardless of what's on the
// other side (including after #23's column reordering), so the space to
// the right of wherever the guide ended up, as a fraction of total
// width, becomes fNotesWidthFraction -- _NotesColumnWidth() applies it
// from here on. A single _Realign() here is the only relayout the whole
// drag causes, however far the mouse moved (see fNotesSplitDragGuideX's
// class comment).
void
ParallelBibleView::MouseUp(BPoint where)
{
	if (fNotesSplitDragGuideX >= 0.0f) {
		float totalWidth = Bounds().Width();
		if (totalWidth <= 0.0f)
			totalWidth = fInitialWidth;

		if (totalWidth > 0.0f) {
			float notesWidth = totalWidth - fNotesSplitDragGuideX;
			float fraction = notesWidth / totalWidth;
			if (fraction < 0.0f)
				fraction = 0.0f;
			if (fraction > 1.0f)
				fraction = 1.0f;
			fNotesWidthFraction = fraction;
		}

		fNotesSplitDragGuideX = -1.0f;
		_Realign();
		return;
	}
	BView::MouseUp(where);
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


int32
ParallelBibleView::_ColumnIndexForX(float x) const
{
	if (fColumnOrder.empty())
		return -1;

	for (size_t i = 0; i < fColumnDividerX.size(); i++) {
		if (x < fColumnDividerX[i])
			return (int32)i;
	}
	return (int32)fColumnOrder.size() - 1;
}


void
ParallelBibleView::_MoveColumn(int32 from, int32 to)
{
	if (from < 0 || (size_t)from >= fColumnOrder.size())
		return;
	if (to < 0)
		to = (int32)fColumnOrder.size() - 1;
	if ((size_t)to > fColumnOrder.size() - 1)
		to = (int32)fColumnOrder.size() - 1;
	if (from == to)
		return;

	std::vector<ColumnDescription> columns = ColumnLayout();
	ColumnDescription moved = columns[from];
	columns.erase(columns.begin() + from);
	columns.insert(columns.begin() + to, moved);

	while (CountColumns() > 0)
		RemoveColumn(0);

	for (size_t i = 0; i < columns.size(); i++) {
		if (columns[i].isNotes)
			SetNotesEnabled(true);
		else if (!columns[i].moduleName.IsEmpty())
			AddColumn(columns[i].moduleName.String());
	}
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

	// A selection's text offsets are only meaningful for the chapter they
	// were made in -- SetKey() rebuilds every column's document out from
	// under it (see BibleTextDocument::_Rebuild()), so a leftover
	// selection either highlights unrelated text at the same offsets in
	// the new chapter or, once the new text is shorter, points past the
	// end of it entirely. Every navigation path (book/chapter/verse
	// fields, the universal search box, a dropped reference) funnels
	// through here, so clearing it in this one place covers all of them.
	for (size_t i = 0; i < fTextViews.size(); i++)
		fTextViews[i]->SetSelection(0, 0);

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


std::vector<BString>
ParallelBibleView::ColumnModuleNames() const
{
	std::vector<BString> names;
	for (size_t i = 0; i < fModules.size(); i++)
		names.push_back(fModules[i]->getName());
	return names;
}


std::vector<ParallelBibleView::ColumnDescription>
ParallelBibleView::ColumnLayout() const
{
	std::vector<ColumnDescription> result;
	for (size_t i = 0; i < fColumnOrder.size(); i++) {
		ColumnDescription desc;
		if (fColumnOrder[i] == COLUMN_NOTES) {
			desc.isNotes = true;
		} else {
			desc.isNotes = false;
			int32 bibleIndex = _BibleIndexForPosition((int32)i);
			if (bibleIndex >= 0 && (size_t)bibleIndex < fModules.size())
				desc.moduleName = fModules[bibleIndex]->getName();
		}
		result.push_back(desc);
	}
	return result;
}


std::vector<ParallelBibleView::ExportRow>
ParallelBibleView::BuildExportRows() const
{
	std::vector<ExportRow> rows;
	if (fCurrentKey.IsEmpty())
		return rows;

	VerseKey chapterKey;
	SetVerseKeyLocale(chapterKey);
	chapterKey.setText(fCurrentKey.String());
	int verseCount = chapterKey.getVerseMax();

	for (int verse = 1; verse <= verseCount; verse++) {
		ExportRow row;
		row.verse = verse;

		BString verseNumberPrefix;
		verseNumberPrefix << " " << verse << " ";

		for (size_t i = 0; i < fDocuments.size(); i++) {
			BString cellText;
			int32 start, end;
			if (fDocuments[i]->TextRangeForVerseRange(verse, verse, start,
					end)) {
				cellText = fDocuments[i]->Text(start, end - start);
				if (cellText.FindFirst(verseNumberPrefix) == 0)
					cellText.Remove(0, verseNumberPrefix.Length());
				cellText.Trim();
			}
			row.columnText.push_back(cellText);
		}

		if (fNotes != NULL) {
			VerseKey verseKey;
			SetVerseKeyLocale(verseKey);
			verseKey.setText(fCurrentKey.String());
			verseKey.setVerse(verse);
			row.notesText = fNotes->GetNote(verseKey.getText());
		}

		rows.push_back(row);
	}

	return rows;
}


void
ParallelBibleView::_ColumnSelectionStarted(BibleColumnView* source,
	BPoint screenPoint)
{
	fActiveSelectionColumn = source;
	fSelectionAnchorScreen = screenPoint;
	fSelectionLastEndVerse = -1;
}


void
ParallelBibleView::_ColumnSelectionEnded()
{
	fActiveSelectionColumn = NULL;
}


bool
ParallelBibleView::_HasActiveColumnSelection() const
{
	return fActiveSelectionColumn != NULL;
}


bool
ParallelBibleView::_IsColumnSelectionOwnedByOther(BibleColumnView* column)
	const
{
	return fActiveSelectionColumn != NULL && fActiveSelectionColumn != column;
}


// Called by the owning column (fActiveSelectionColumn) on every
// MouseMoved() of an active fresh-selection gesture -- see the class
// comment on BibleColumnView. The gesture is one selection rectangle
// from fSelectionAnchorScreen (set in _ColumnSelectionStarted()) to
// screenPoint; which columns' own frames (in this view's local
// coordinates) overlap its x-extent decides the granularity for this
// specific move, re-evaluated fresh every time rather than latched once
// a threshold is crossed:
//
//  - exactly one column overlaps (still purely within the source, the
//    ordinary case): plain character-precise text selection, identical
//    to how a single column always selected before cross-column
//    dragging existed -- half a verse in one translation means exactly
//    as much as it ever did, nothing snaps to verse boundaries;
//  - more than one column overlaps: verses are the unit multiple
//    translations can actually agree on, so this switches to verse-
//    level instead -- both ends resolved via the SOURCE column's own
//    coordinate mapping alone (every column's rows sit at the same y
//    position for the same verse, see VerseAligner, so one column's
//    lookup already speaks for all of them), applied the same to every
//    overlapping column. If the current point doesn't resolve to a
//    verse (dragged into the header row, past the last line, etc.) the
//    previous end verse is kept rather than collapsing the range.
//
// Either way, any column outside the current x-extent gets its
// selection cleared rather than left stranded from earlier in the same
// gesture.
void
ParallelBibleView::_ColumnSelectionMoved(BibleColumnView* source,
	BPoint screenPoint)
{
	if (source == NULL)
		return;

	BPoint anchorLocal = ConvertFromScreen(fSelectionAnchorScreen);
	BPoint currentLocal = ConvertFromScreen(screenPoint);
	float left = std::min(anchorLocal.x, currentLocal.x);
	float right = std::max(anchorLocal.x, currentLocal.x);

	std::vector<BibleColumnView*> participating;
	for (size_t i = 0; i < fTextViews.size(); i++) {
		BibleColumnView* column
			= dynamic_cast<BibleColumnView*>(fTextViews[i]);
		if (column == NULL)
			continue;
		BRect frame = column->Frame();
		if (frame.right >= left && frame.left <= right)
			participating.push_back(column);
	}

	if (participating.size() <= 1) {
		BPoint sourceLocal = source->ConvertFromScreen(screenPoint);
		source->SetCaret(sourceLocal, true);
		for (size_t i = 0; i < fTextViews.size(); i++) {
			BibleColumnView* column
				= dynamic_cast<BibleColumnView*>(fTextViews[i]);
			if (column != NULL && column != source)
				column->ClearColumnSelection();
		}
		return;
	}

	BPoint anchorInSource = source->ConvertFromScreen(fSelectionAnchorScreen);
	BPoint currentInSource = source->ConvertFromScreen(screenPoint);
	int startVerse = source->VerseAt(anchorInSource);
	int endVerse = source->VerseAt(currentInSource);
	if (endVerse < 0)
		endVerse = fSelectionLastEndVerse >= 0 ? fSelectionLastEndVerse
			: startVerse;
	else
		fSelectionLastEndVerse = endVerse;
	if (startVerse < 0)
		startVerse = endVerse;

	int lowVerse = std::min(startVerse, endVerse);
	int highVerse = std::max(startVerse, endVerse);

	for (size_t i = 0; i < fTextViews.size(); i++) {
		BibleColumnView* column
			= dynamic_cast<BibleColumnView*>(fTextViews[i]);
		if (column == NULL)
			continue;

		bool isParticipating = std::find(participating.begin(),
			participating.end(), column) != participating.end();
		if (isParticipating)
			column->SelectVerseRange(lowVerse, highVerse);
		else
			column->ClearColumnSelection();
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
			fDocuments[i].Get(), fModules[i]->getName(), this);
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
		// A reading surface, not an editable one -- selection (for copy/
		// drag) is wanted, typing into scripture text is not. TextEditor
		// defaults to editing enabled, and nothing here ever turned it
		// off, so a Bible/Commentary column silently accepted keystrokes
		// as edits to its own in-memory document (never saved, but still
		// corrupting what was displayed) until this was added.
		view->SetEditingEnabled(false);
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


// Before the user has ever dragged the splitter (see #19),
// fNotesWidthFraction is -1 and the notes column never claims more than
// kMaxNotesWidthFraction of the total width -- the equal-share split let
// it eat half the window with just one Bible column open. Once dragged,
// fNotesWidthFraction takes over completely (still floor-clamped to
// kMinColumnWidth, and implicitly ceiling-clamped by MouseMoved() never
// letting the fraction get set high enough to starve the bible columns
// below kMinColumnWidth each -- see MouseMoved()), and stays in effect
// across resizes/column changes rather than resetting to the 1/3 cap on
// every _PositionColumns() pass, since it's a fraction of totalWidth
// applied fresh each call, not a one-time pixel snapshot.
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

	float width;
	if (fNotesWidthFraction >= 0.0f) {
		width = totalWidth * fNotesWidthFraction;
	} else {
		int32 columnCount = (int32)fModules.size() + 1;
		float naturalShare
			= (totalWidth - kColumnSpacing * (columnCount - 1)) / columnCount;
		width = std::min(naturalShare, totalWidth * kMaxNotesWidthFraction);
	}

	if (width < kMinColumnWidth)
		width = kMinColumnWidth;
	return width;
}


// Content-space x of the divider immediately before the notes column --
// the only one that's ever draggable (see #19) -- or a negative value if
// there's no notes column, or nothing to its left to negotiate width
// with (fColumnDividerX has no earlier entry to use).
float
ParallelBibleView::_NotesSplitDividerX() const
{
	int32 notesPosition = _NotesPosition();
	if (notesPosition <= 0)
		return -1.0f;
	if ((size_t)(notesPosition - 1) >= fColumnDividerX.size())
		return -1.0f;
	return fColumnDividerX[notesPosition - 1];
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

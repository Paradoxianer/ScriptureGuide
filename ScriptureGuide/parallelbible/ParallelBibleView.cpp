/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "ParallelBibleView.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

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
#include <Clipboard.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <OS.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <ScrollView.h>
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
// key is left unchanged. Keys handled in this file ultimately come from
// the main window's (localized) book menu.
static void
SetVerseKeyLocale(VerseKey& key)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	key.setLocale(language.Code());
}

const float ParallelBibleView::kMinColumnWidth = 150.0f;
// Wide enough for the link/unlink toggle button (see kLinkButtonWidth)
// to sit centered on a column's divider line without crowding either
// column's own header cell -- empirically verify/adjust on the Haiku VM.
const float ParallelBibleView::kColumnSpacing = 20.0f;
const float ParallelBibleView::kHeaderHeight = 24.0f;
const float ParallelBibleView::kHeaderBottomGap = 2.0f;
const float ParallelBibleView::kRemoveButtonWidth = 20.0f;
const float ParallelBibleView::kInsertButtonWidth = 20.0f;
const float ParallelBibleView::kLinkButtonWidth = 16.0f;
const float ParallelBibleView::kMaxNotesWidthFraction = 1.0f / 3.0f;
const float ParallelBibleView::kBibleColumnInset = 4.0f;
const float ParallelBibleView::kNoteGutterWidth = 20.0f;

// How long typing has to pause before an edited note's row is realigned
// with the Bible columns beside it (see NoteTextEdited()). Short enough
// that the realign reads as part of the same gesture rather than as a
// separate, later event -- 0.4s was reported as a visible, distracting
// jump. It can be this short because a realign no longer re-shapes every
// paragraph in the document (see TextDocumentLayout's LayoutTextListener);
// only a pause shorter than one keystroke interval still coalesces.
static const bigtime_t kNotesRealignDelay = 120000; // 0.12s

// Line-step for a chain's shared vertical scrollbar, matching the value
// TextDocumentView uses for its own (kVerticalScrollBarStep there).
static const float kVerticalScrollStep = 12.0f;

// Simple, font-safe glyphs for the link/unlink header button -- purely
// cosmetic, easy to swap for a real icon later.
static const char* kLinkedGlyph = "=";
static const char* kBrokenLinkGlyph = "/";


// Saves a notes column's edits back to its PersonalNotesModule as they
// happen -- attached once to a notes column's BibleTextDocument (see
// _SetColumnToNotes()) and left there for the document's whole lifetime
// (TextDocument::operator=(), which BibleTextDocument::_Rebuild() uses to
// reset itself on every SetKey(), deliberately never touches
// fTextListeners -- confirmed against its own implementation and by the
// existing TestListenerSurvivesRepeatedRebuilds test -- so this listener
// stays attached across every chapter navigation, not just the first).
//
// fDocument spans the whole chapter, one paragraph per verse (see the
// NotesColumn comment in the header), so an edit has to be attributed
// back to the verse it landed in. TextChangedEvent already carries
// exactly the paragraph range that changed, and fParagraphVerse (via
// VerseForParagraphIndex()) already maps a paragraph index to its verse
// -- so only the paragraphs the user actually touched get written back,
// not the whole chapter. That matters: SetNote() writes through to the
// SWORD module on disk, and re-saving all 176 verses of Psalm 119 on
// every keystroke would be pointless I/O.
//
// BibleTextDocument::_Rebuild() resets the document via
// TextDocument::operator=() and Append(), neither of which notifies
// listeners, so a chapter change never masquerades as a user edit here.
class NotesSaveListener : public TextListener {
public:
	NotesSaveListener(BibleTextDocument* document, PersonalNotesModule* notes,
		ParallelBibleView* owner)
		:
		fDocument(document),
		fNotes(notes),
		fOwner(owner)
	{
	}

	virtual void TextChanged(const TextChangedEvent& event)
	{
		BString chapterKey = fDocument->Key();
		if (chapterKey.IsEmpty())
			return;

		int32 first = event.FirstChangedParagraph();
		int32 last = first + event.ChangedParagraphCount();
		if (last > fDocument->CountParagraphs())
			last = fDocument->CountParagraphs();

		VerseKey key;
		SetVerseKeyLocale(key);
		key.setText(chapterKey.String());

		for (int32 i = std::max((int32)0, first); i < last; i++) {
			int verse = fDocument->VerseForParagraphIndex(i);
			if (verse <= 0)
				continue;

			// Paragraph-local text, not the document's -- and Trim()
			// takes the trailing "\n" every notes paragraph carries (see
			// BibleTextDocument::SetParagraphsEndWithNewline()) back off
			// before it reaches the stored note.
			BString text = fDocument->ParagraphAtIndex(i).Text();
			text.Trim();
			// Soft line breaks (see NotesDisplayView::
			// _InsertSoftLineBreak()) exist only inside the live
			// document, to keep a multi-line note one paragraph. What
			// gets stored is an ordinary newline, so a note file stays
			// readable by anything else that opens it.
			text.ReplaceAll('\v', '\n');

			key.setVerse(verse);
			fNotes->SetNote(key.getText(), text.String());
		}

		if (fOwner != NULL)
			fOwner->NoteTextEdited();
	}

private:
	BibleTextDocument*		fDocument;
	PersonalNotesModule*	fNotes;
	ParallelBibleView*		fOwner;
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
//
// fPosition is this column's own slot in fColumnOrder (see
// ParallelBibleView), set by _RebuildLayout() right after construction --
// used to find its own chain (see ScrollTo()/MessageReceived() below,
// and _SetActiveColumn() calls).
class BibleColumnView : public TextDocumentView {
public:
	BibleColumnView(const char* name, BibleTextDocument* document,
		const char* translationName, ParallelBibleView* owner)
		:
		TextDocumentView(name),
		fBibleDocument(document),
		fTranslationName(translationName),
		fOwner(owner),
		fPosition(-1),
		fTrackingForDrag(false),
		fShowingLinkCursor(false)
	{
	}

	void SetPosition(int32 position) { fPosition = position; }

	// One BScrollView per column now (see the class comment on
	// ParallelBibleView) -- this is the one hook every scroll path for
	// THIS column funnels through, same principle ParallelBibleView's
	// own ScrollTo() already uses for its horizontal scroll. Propagates
	// to the rest of this column's chain.
	virtual void ScrollTo(BPoint where)
	{
		TextDocumentView::ScrollTo(where);
		if (fOwner != NULL)
			fOwner->_ColumnScrolled(fPosition, where.y);
	}

	// See the identical override/rationale on NotesDisplayView.
	virtual void FrameResized(float width, float height)
	{
		TextDocumentView::FrameResized(width, height);
		if (fOwner != NULL)
			fOwner->_ColumnResized(fPosition);
	}

	// A dropped Bible reference, or a follower column's unhandled mouse
	// wheel, are handled here; anything else falls through to the base
	// class unchanged. A "follower" column (not its chain's rightmost,
	// see the class comment) has no vertical BScrollBar of its own, so
	// Haiku's default B_MOUSE_WHEEL_CHANGED handling (BView::
	// MessageReceived(), which only ever looks at ITS OWN attached
	// scrollbars) silently does nothing over it -- confirmed against the
	// Haiku source, not assumed. Forwarding the message straight to the
	// chain's driving (rightmost) column lets that column's own base
	// class handle it normally, which in turn propagates back out via
	// ScrollTo() above.
	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == B_MOUSE_WHEEL_CHANGED
			&& ScrollBar(B_VERTICAL) == NULL && fOwner != NULL) {
			fOwner->_ForwardWheelToChain(fPosition, message);
			return;
		}
		if (message->WasDropped() && _HandleReferenceDrop(message))
			return;
		TextDocumentView::MessageReceived(message);
	}

	virtual void MouseDown(BPoint where)
	{
		if (fOwner != NULL)
			fOwner->_SetActiveColumn(fPosition);

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

		if (fOwner != NULL)
			fOwner->_SetActiveColumn(fPosition);

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

		// A dropped reference lands in THIS column's chain -- activate it
		// before posting SG_BIBLE below, since that round-trips through
		// the window (JumpToKey() -> fParallelView->SetKey()), which acts
		// on whichever chain is active at the time it actually runs.
		fOwner->_SetActiveColumn(fPosition);

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
	int32				fPosition;
	bool				fTrackingForDrag;
	BPoint				fDragStartPoint;
	BPoint				fMouseDownPoint;
	bool				fShowingLinkCursor;
};


// A notes column's own view -- a single BibleTextDocument spanning the
// whole chapter (one paragraph per verse), shown and edited exactly the
// way a Bible/Commentary column's own document is shown in a
// BibleColumnView: same self-healing scrollbar, same VerseAligner group,
// one view and one TextEditor for the whole column no matter how long
// the chapter is (see the class comment on NotesColumn).
//
// Editing is ON and stays on. Everything that makes typing here feel
// like typing anywhere else -- the caret landing exactly where it was
// clicked, drag-selection, arrow keys walking from one verse's note into
// the next, a note's row growing as it is typed -- comes straight from
// TextDocumentView, because there is no mode to switch and no separate
// editor view to build, position or tear down.
//
// What this class adds on top is exactly one thing: it refuses the
// keystrokes that would break the ONE PARAGRAPH PER VERSE invariant the
// gutter, VerseAligner and note saving all read the document through
// (see _WouldBreakVerseParagraphs()). Everything else is passed straight
// to the base class.
class NotesDisplayView : public TextDocumentView {
public:
	NotesDisplayView(const char* name, BibleTextDocument* document,
		ParallelBibleView* owner)
		:
		TextDocumentView(name),
		fDocument(document),
		fOwner(owner),
		fPosition(-1)
	{
	}

	void SetPosition(int32 position) { fPosition = position; }

	// One BScrollView per column (see the class comment on
	// ParallelBibleView) -- this is the one hook every scroll path for
	// THIS column funnels through. Propagates to the rest of this
	// column's chain.
	virtual void ScrollTo(BPoint where)
	{
		TextDocumentView::ScrollTo(where);
		if (fOwner != NULL)
			fOwner->_ColumnScrolled(fPosition, where.y);
	}

	// The moment this column's real width is known: BView::ResizeTo() only
	// QUEUES the resize, so everything measured before this ran still
	// reflected the previous width. The chain's shared scrollbar is sized
	// from measured content, so it has to be (re)applied here rather than
	// wherever the resize was requested -- see
	// ParallelBibleView::_UpdateChainScrollBar().
	virtual void FrameResized(float width, float height)
	{
		TextDocumentView::FrameResized(width, height);
		if (fOwner != NULL)
			fOwner->_ColumnResized(fPosition);
	}

	// See the identical override/rationale on BibleColumnView for the
	// wheel case. B_PASTE is intercepted because the base class's own
	// Paste() explicitly treats "\n" as a permitted character (see
	// TextDocumentView::_IsAllowedChar()) -- pasting multi-line text
	// would split one verse's note across several paragraphs and shift
	// every following verse's note onto the wrong verse.
	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == B_MOUSE_WHEEL_CHANGED
			&& ScrollBar(B_VERTICAL) == NULL && fOwner != NULL) {
			fOwner->_ForwardWheelToChain(fPosition, message);
			return;
		}
		if (message->what == B_PASTE) {
			_PasteSingleParagraph();
			return;
		}
		TextDocumentView::MessageReceived(message);
	}

	// Places the caret / starts a selection exactly where the click
	// landed, like any other text view -- the base class already does
	// all of it.
	virtual void MouseDown(BPoint where)
	{
		if (fOwner != NULL)
			fOwner->_SetActiveColumn(fPosition);
		TextDocumentView::MouseDown(where);
	}

	// The one place the one-paragraph-per-verse invariant can be broken
	// from, and therefore the one place it is enforced.
	virtual void KeyDown(const char* bytes, int32 numBytes)
	{
		if (_RawChar(bytes, numBytes) == B_ENTER) {
			_InsertSoftLineBreak();
			return;
		}
		if (_WouldBreakVerseParagraphs(bytes, numBytes))
			return;
		TextDocumentView::KeyDown(bytes, numBytes);
	}

	// Verse numbers, painted in a left inset gutter (kNoteGutterWidth
	// wide, see _RebuildLayout()'s SetInsets() call) -- never part of
	// the document text itself (SetShowVerseNumbers(false), see
	// ParallelBibleView::_BuildNotesDocument()).
	virtual void Draw(BRect updateRect)
	{
		TextDocumentView::Draw(updateRect);
		// Defensive, on top of the actual fix in TextDocumentView::
		// _DrawCaret() (which left B_OP_INVERT set after drawing a
		// blinked-on caret, corrupting every draw call after it) -- make
		// sure the gutter always starts from the normal drawing mode
		// regardless of what the base class's own Draw() left behind.
		SetDrawingMode(B_OP_COPY);
		_DrawGutter(updateRect);
	}

private:
	// Same raw_char lookup TextDocumentView::KeyDown() itself does before
	// handing the event to the editor, so everything here keys off
	// exactly the value the editor would have switched on.
	int32 _RawChar(const char* bytes, int32 numBytes)
	{
		int32 rawChar = 0;
		if (Window() != NULL && Window()->CurrentMessage() != NULL)
			Window()->CurrentMessage()->FindInt32("raw_char", &rawChar);
		if (rawChar == 0 && numBytes > 0)
			rawChar = (unsigned char)bytes[0];
		return rawChar;
	}

	// Return breaks the LINE without breaking the PARAGRAPH: "\v" is
	// rendered as a line break by ParagraphLayout but is not what
	// TextDocument::NormalizeText() splits paragraphs at ("\n" is, and
	// only "\n"), so a note can be as many lines as the user wants while
	// still being exactly one paragraph belonging to exactly one verse.
	// Stored notes keep ordinary "\n" -- the two are translated at the
	// document boundary (see BibleTextDocument::_Rebuild() reading and
	// NotesSaveListener::TextChanged() writing).
	void _InsertSoftLineBreak()
	{
		if (!Editor().IsSet() || !Editor()->IsEditingEnabled())
			return;

		int32 start, end;
		GetSelection(start, end);
		if (start != end) {
			if (_ParagraphIndexAt(start) != _ParagraphIndexAt(end))
				return;
			Editor()->Replace(start, end - start, BString("\v"));
		} else {
			Editor()->Insert(Editor()->CaretOffset(), BString("\v"));
		}

		Invalidate();
		Relayout();
		if (fOwner != NULL)
			fOwner->NoteTextEdited();
	}

	// True if letting this keystroke through would change how many
	// paragraphs the document has, or which verse a given paragraph
	// belongs to -- in which case KeyDown() drops it.
	//
	// Both cases come down to the "\n" that terminates every notes
	// paragraph (see BibleTextDocument::SetParagraphsEndWithNewline()):
	//
	//  - Backspace with the caret at a paragraph's start REMOVES the
	//    PREVIOUS paragraph's one, merging that verse's note with this
	//    one. (Backspace anywhere else, including just after a note's
	//    last visible character, is an ordinary delete and is allowed.)
	//  - Delete with the caret sitting just before this paragraph's own
	//    "\n" removes it, merging with the FOLLOWING verse.
	//
	// Return is NOT one of them any more -- see _InsertSoftLineBreak().
	//
	// A selection spanning more than one paragraph makes every
	// destructive key a merge, so those are refused wholesale rather
	// than silently rewritten -- selecting across verses and then typing
	// is rare, and quietly deleting only part of what is visibly
	// selected would be worse than doing nothing.
	bool _WouldBreakVerseParagraphs(const char* bytes, int32 numBytes)
	{
		if (!Editor().IsSet() || !Editor()->IsEditingEnabled())
			return false;

		int32 rawChar = _RawChar(bytes, numBytes);
		bool destructive = rawChar == B_BACKSPACE || rawChar == B_DELETE;

		int32 start, end;
		GetSelection(start, end);
		if (start != end) {
			// Typed text replaces the selection, so a multi-paragraph
			// selection is dangerous for ANY text-producing key, not
			// just the two explicitly destructive ones.
			return _ParagraphIndexAt(start) != _ParagraphIndexAt(end);
		}

		if (!destructive)
			return false;

		int32 caret = Editor()->CaretOffset();
		int32 paragraphOffset = 0;
		int32 index = fDocument->ParagraphIndexFor(caret, paragraphOffset);
		if (index < 0)
			return false;

		if (rawChar == B_BACKSPACE)
			return caret == paragraphOffset;

		// B_DELETE: the last character of the paragraph is its "\n".
		int32 length = fDocument->ParagraphAtIndex(index).Length();
		return caret >= paragraphOffset + length - 1;
	}

	int32 _ParagraphIndexAt(int32 offset) const
	{
		int32 paragraphOffset = 0;
		return fDocument->ParagraphIndexFor(offset, paragraphOffset);
	}

	// Pastes the clipboard's plain text into the current verse's note
	// with every line break turned into a SOFT one (see
	// _InsertSoftLineBreak()), so pasted text keeps its line structure
	// without turning one verse's paragraph into several (see
	// MessageReceived()). Refuses outright if the selection it would
	// replace spans verses, for the same reason KeyDown() does.
	void _PasteSingleParagraph()
	{
		if (!Editor().IsSet() || !Editor()->IsEditingEnabled())
			return;
		if (be_clipboard == NULL || !be_clipboard->Lock())
			return;

		BString text;
		BMessage* clip = be_clipboard->Data();
		if (clip != NULL) {
			const void* data;
			ssize_t length;
			if (clip->FindData("text/plain", B_MIME_TYPE, &data, &length)
					== B_OK && length > 0) {
				text.SetTo((const char*)data, length);
			}
		}
		be_clipboard->Unlock();

		if (text.IsEmpty())
			return;
		text.ReplaceAll("\r\n", "\v");
		text.ReplaceAll('\n', '\v');
		text.ReplaceAll('\r', '\v');
		text.ReplaceAll('\t', ' ');

		int32 start, end;
		GetSelection(start, end);
		if (start != end) {
			if (_ParagraphIndexAt(start) != _ParagraphIndexAt(end))
				return;
			Editor()->Replace(start, end - start, text);
		} else {
			Editor()->Insert(Editor()->CaretOffset(), text);
		}

		Invalidate();
		Relayout();
		if (fOwner != NULL)
			fOwner->NoteTextEdited();
	}

	void _DrawGutter(BRect updateRect)
	{
		BRect bounds = Bounds();
		BRect gutter(bounds.left, bounds.top,
			bounds.left + ParallelBibleView::kNoteGutterWidth,
			bounds.bottom);

		PushState();

		SetHighColor(tint_color(ViewColor(), B_DARKEN_1_TINT));
		FillRect(gutter & updateRect);

		BString chapterKey = fDocument->Key();
		if (!chapterKey.IsEmpty()) {
			VerseKey chapterVerseKey;
			SetVerseKeyLocale(chapterVerseKey);
			chapterVerseKey.setText(chapterKey.String());
			int verseCount = chapterVerseKey.getVerseMax();

			// Pass 1: every verse's own row Y. By paragraph INDEX (see
			// GetParagraphBounds()), not a flat offset -- an offset
			// exactly at a paragraph boundary is inherently ambiguous
			// (simultaneously "end of paragraph N" and "start of
			// paragraph N+1"), which independently broke this exact
			// gutter earlier this session when it tried to resolve verse
			// starts that way instead.
			struct Row { int verse; float y1; float y2; };
			std::vector<Row> rows;
			for (int verse = 1; verse <= verseCount; verse++) {
				int32 paragraphIndex = fDocument->ParagraphIndexForVerse(
					verse);
				if (paragraphIndex < 0)
					continue;
				float y1, y2;
				GetParagraphBounds(paragraphIndex, y1, y2);
				if (y1 > updateRect.bottom)
					break;
				if (y2 < updateRect.top)
					continue;
				Row row = { verse, y1, y2 };
				rows.push_back(row);
			}

			// Pass 2: draw, now that every row's real position (measured
			// against the note text's own, unmodified font) is known --
			// a bit smaller than the note text itself, just for these
			// digits, keeps a 3-digit verse number (Psalm 119 etc.)
			// comfortably inside kNoteGutterWidth without needing that
			// any wider.
			BFont font;
			GetFont(&font);
			font.SetSize(font.Size() * 0.85f);
			SetFont(&font);
			font_height fontHeight;
			GetFontHeight(&fontHeight);

			for (size_t i = 0; i < rows.size(); i++) {
				const Row& row = rows[i];

				if (row.verse > 1) {
					SetHighColor(tint_color(ViewColor(),
						B_DARKEN_1_TINT));
					StrokeLine(BPoint(bounds.left, row.y1),
						BPoint(bounds.right, row.y1));
				}

				BString numberText;
				numberText << row.verse;
				float numberWidth = font.StringWidth(numberText.String());
				// Plain black, not a tint of the (light gray) gutter
				// background -- B_DARKEN_3_TINT read as barely-there
				// gray on top of it, not legible (reported).
				SetHighColor(0, 0, 0);
				DrawString(numberText.String(),
					BPoint(bounds.left + ParallelBibleView::kNoteGutterWidth
							- numberWidth - 4.0f,
						row.y1 + fontHeight.ascent));
			}
		}

		PopState();
	}

private:
	BibleTextDocument*	fDocument;
	ParallelBibleView*	fOwner;
	int32				fPosition;
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
	// with no clean edge between them. A tinted band behind the active
	// chain's own header cells (see ParallelBibleView::
	// _ActiveChainHeaderRange()) makes it clear at a glance which chain
	// the toolbar's book/chapter/verse fields will actually move, once
	// more than one chain exists -- left == right == 0 (nothing drawn)
	// whenever there's only one.
	virtual void Draw(BRect updateRect)
	{
		BView::Draw(updateRect);

		if (fOwner != NULL) {
			float left, right;
			fOwner->_ActiveChainHeaderRange(left, right);
			if (right > left) {
				BRect bounds = Bounds();
				SetHighColor(tint_color(ViewColor(), B_DARKEN_1_TINT));
				FillRect(BRect(left, bounds.top, right, bounds.bottom));
			}
		}

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
	// background a column's BMenuField/remove/link button don't cover
	// (their own width is their *preferred* size, not the full column
	// width -- see _PositionColumns()), never hijacking an actual click
	// on any of those controls. No separate drag handle needed as a
	// result.
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

		fOwner->_SetActiveColumn(index);

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
	fActivePosition(-1),
	fSuppressScrollPropagation(false),
	fNotes(NULL),
	fNotesRealignRunner(NULL),
	fHeaderView(NULL),
	fActiveSelectionColumn(NULL),
	fSelectionLastEndVerse(-1),
	fShowVerseNumbers(true),
	fShowStrongsNumbers(true),
	fShowCrossReferences(true),
	fInitialWidth(initialWidth),
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
}


// Order matters: every column's own BScrollView is this view's direct
// child (see _RebuildLayout()), never owned by the content view it
// wraps -- so the content view (a Bible column's TextDocumentView, or a
// notes column's NotesDisplayView) is explicitly detached+deleted FIRST,
// then its now-empty BScrollView, rather than letting ~BView() cascade-
// delete a scroller's remaining children.
ParallelBibleView::~ParallelBibleView()
{
	for (size_t i = 0; i < fTextViews.size(); i++) {
		BView* view = fTextViews[i];
		BScrollView* scroller = dynamic_cast<BScrollView*>(view->Parent());
		view->RemoveSelf();
		delete view;
		if (scroller != NULL) {
			scroller->RemoveSelf();
			delete scroller;
		}
	}

	for (size_t i = 0; i < fNotesColumns.size(); i++) {
		BView* view = fNotesColumns[i].view;
		if (view != NULL) {
			BScrollView* scroller
				= dynamic_cast<BScrollView*>(view->Parent());
			view->RemoveSelf();
			delete view;
			if (scroller != NULL) {
				scroller->RemoveSelf();
				delete scroller;
			}
		}
	}

	delete fNotes;
	// Stops any still-pending debounced realign (see NoteTextEdited())
	// from being delivered to a half-destroyed view.
	delete fNotesRealignRunner;

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
	fprintf(stderr, "[SG] AttachedToWindow: Bounds()=(%.1f,%.1f,%.1f,%.1f)\n",
		Bounds().left, Bounds().top, Bounds().right, Bounds().bottom);
	_RebuildLayout();
}


void
ParallelBibleView::FrameResized(float width, float height)
{
	fprintf(stderr, "[SG] FrameResized(width=%.1f, height=%.1f)\n",
		width, height);
	BView::FrameResized(width, height);
	_Realign();

	// Re-applies whatever verse the active chain was last navigated to.
	// Confirmed empirically (a real bug report, not theoretical): during
	// startup restore, SetKey()'s own scroll call runs while this view is
	// still attached but not yet actually shown on screen -- Bounds().
	// Height() reads as a degenerate placeholder at that point, not this
	// view's real, final viewport size, so the scroll position each
	// column computed and applied at that time is against the wrong
	// range. This view's *first* FrameResized() with the real on-screen
	// size fires strictly afterward, once the window is actually shown --
	// and before this fix, nothing ever re-scrolled once that real size
	// arrived, silently landing back at the top instead of the verse that
	// was actually being navigated to. Re-deriving the verse from the
	// active chain's own current key and re-scrolling here fixes that,
	// and keeps the same verse at the top across any later resize too
	// (including ones the user triggers by hand) -- both harmless when
	// nothing about the scroll position actually needed to change. Only
	// the active chain is covered -- at startup every column still
	// belongs to the one default chain anyway (see the class comment),
	// so this is the realistic case; a chain the user has since split off
	// and scrolled independently keeps whatever position it already had.
	BString key = _ChainKey(fActivePosition);
	if (!key.IsEmpty()) {
		VerseKey verseKey;
		SetVerseKeyLocale(verseKey);
		verseKey.setText(key.String());
		int verse = verseKey.getVerse();
		_ScrollChainTo(fActivePosition,
			verse > 1 ? _ChainVerseY(fActivePosition, verse) : 0.0f);
	}
}


// The one hook every horizontal scroll path (drag, mouse wheel,
// programmatic) funnels through -- see the class comment. Mirrors only
// the horizontal component onto the header, which has no vertical scroll
// position of its own. This view is no longer a vertically scrolled
// target at all (see the class comment), so there is nothing else to do
// here -- back to exactly what this method did before issue #12 existed.
void
ParallelBibleView::ScrollTo(BPoint where)
{
	BView::ScrollTo(where);
	fHeaderView->ScrollTo(where.x, 0.0f);
}


// Vertical divider lines at each column boundary (see fColumnDividerX,
// kept up to date by _PositionColumns()) -- without them, one column's
// white background runs directly into the next with only kColumnSpacing
// of this view's own panel-gray background between them, no real edge to
// tell them apart at a glance. Drawn across the whole visible viewport
// height, not clamped to any particular column's content height -- each
// column now manages its own height/scrolling independently (see the
// class comment), so there's no single shared content height to clamp
// against any more.
void
ParallelBibleView::Draw(BRect updateRect)
{
	BView::Draw(updateRect);

	if (fColumnDividerX.empty())
		return;

	float top = updateRect.top;
	float bottom = updateRect.bottom;

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


// Commits the drag started in MouseDown(): a notes column always sits
// immediately after _NotesSplitDividerX(), regardless of what's on the
// other side (including after #23's column reordering), so the space to
// the right of wherever the guide ended up, as a fraction of total
// width, becomes fNotesWidthFraction -- _NotesColumnWidth() applies it
// from here on, to every notes column uniformly. A single _Realign() here
// is the only relayout the whole drag causes, however far the mouse
// moved (see fNotesSplitDragGuideX's class comment).
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
		case PARALLEL_INSERT_MODULE:
		case PARALLEL_INSERT_NOTES:
		{
			// Selecting an item in a column's own dropdown, or clicking
			// its remove button, ultimately calls _RebuildLayout(),
			// which deletes and recreates every header BMenuField --
			// including, in the dropdown case, the very one whose click
			// this message came from. BMenuField::MouseDown() (Haiku's
			// Interface Kit) spawns a background thread ("_m_task_") to
			// track the very click that produced this rebuild, and
			// ~BMenuField() blocks in wait_for_thread() for it; if that
			// thread still needs this window's lock for its own
			// post-click cleanup while this dispatch already holds it
			// (BLooper holds its lock for a dispatch's full duration),
			// deleting the field synchronously here deadlocks the whole
			// window -- reproduced live, not just theoretical. Structural
			// changes (a column added or removed, below) don't have this
			// risk: those are triggered by a plain BButton click
			// ("+"/"x"), which tracks synchronously with no spawned
			// thread, so by the time this runs no *other* field's
			// tracking thread should still be active.
			if (!message->HasBool("deferred")) {
				BMessage deferred(*message);
				deferred.AddBool("deferred", true);
				BMessenger(this).SendMessage(&deferred);
				break;
			}

			int32 index;
			if (message->FindInt32("index", &index) == B_OK)
				_SetActiveColumn(index);

			if (message->what == PARALLEL_SELECT_MODULE) {
				BString module;
				if (message->FindInt32("index", &index) == B_OK
					&& message->FindString("module", &module) == B_OK) {
					_SetColumnToBible(index, module.String());
				}
			} else if (message->what == PARALLEL_SELECT_NOTES) {
				if (message->FindInt32("index", &index) == B_OK)
					_SetColumnToNotes(index);
			} else if (message->what == PARALLEL_REMOVE_COLUMN) {
				if (message->FindInt32("index", &index) == B_OK)
					RemoveColumn(index);
			} else if (message->what == PARALLEL_INSERT_MODULE) {
				BString module;
				if (message->FindInt32("index", &index) == B_OK
					&& message->FindString("module", &module) == B_OK) {
					InsertColumn(index, module.String());
					_SetActiveColumn(index + 1);
				}
			} else {
				if (message->FindInt32("index", &index) == B_OK) {
					InsertNotesColumn(index);
					_SetActiveColumn(index + 1);
				}
			}
			break;
		}

		case PARALLEL_TOGGLE_LINK:
		{
			// A plain BButton click (see _RebuildHeader()) -- no spawned
			// tracking thread, so unlike the dropdown/remove cases above
			// this can be handled synchronously with no deadlock risk.
			int32 gapIndex;
			if (message->FindInt32("gap", &gapIndex) == B_OK)
				_ToggleLink(gapIndex);
			break;
		}

		case PARALLEL_NOTES_TEXT_CHANGED:
		{
			// The debounce timer armed by NoteTextEdited() has expired --
			// typing has paused, so it's worth paying for a realign to
			// bring the edited note's row back into line with the Bible
			// columns beside it. Disarm first: the runner is one-shot,
			// but leaving it around would make the next NoteTextEdited()
			// delete a spent object for no reason.
			delete fNotesRealignRunner;
			fNotesRealignRunner = NULL;
			_Realign();
			break;
		}

		case PARALLEL_INSERT_COLUMN_MENU:
		{
			// A plain BButton click (see _RebuildHeader()) -- same
			// no-deadlock-risk reasoning as PARALLEL_TOGGLE_LINK above;
			// the popup this opens posts PARALLEL_INSERT_MODULE/
			// PARALLEL_INSERT_NOTES, handled synchronously (deferred)
			// in the case block above once chosen.
			int32 index;
			if (message->FindInt32("index", &index) != B_OK
				|| index < 0 || (size_t)index >= fInsertButtons.size()) {
				break;
			}
			BPopUpMenu* menu = _BuildModuleMenu(index, NULL, false, true);
			if (menu != NULL) {
				// Not owned by any BMenuField (unlike the per-column
				// menus built in _RebuildHeader()) -- this one is a
				// fire-and-forget popup, so it must clean itself up.
				menu->SetAsyncAutoDestruct(true);
				BPoint where = fInsertButtons[index]->Frame().LeftBottom();
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
ParallelBibleView::AddNotesColumn()
{
	return _SetColumnToNotes(-1);
}


// Splices fLinkedToNext for a brand-new gap opening up right after
// afterPosition (used by both InsertColumn() and InsertNotesColumn()) --
// the new column joins afterPosition's own chain (gap set to true), and
// whatever gap used to sit right after afterPosition (if any) moves one
// slot over unchanged, preserving the rest of that chain's continuity
// exactly as it was.
static void
SpliceLinkedToNextForInsert(std::vector<bool>& linkedToNext,
	int32 afterPosition)
{
	if (afterPosition < (int32)linkedToNext.size()) {
		bool oldGap = linkedToNext[afterPosition];
		linkedToNext[afterPosition] = true;
		linkedToNext.insert(linkedToNext.begin() + afterPosition + 1,
			oldGap);
	} else {
		linkedToNext.push_back(true);
	}
}


status_t
ParallelBibleView::InsertColumn(int32 afterPosition, const char* moduleName)
{
	if (afterPosition < 0 || (size_t)afterPosition >= fColumnOrder.size())
		return B_BAD_INDEX;
	if (fManager == NULL)
		return B_NO_INIT;

	SWModule* module = fManager->getModule(moduleName);
	if (module == NULL)
		return B_NAME_NOT_FOUND;

	// Seed with whatever the anchor's own chain is currently showing --
	// same reasoning as _SetColumnToBible()'s own seeding.
	BString seedKey = _ChainKey(afterPosition);
	if (!seedKey.IsEmpty()) {
		VerseKey verseKey;
		SetVerseKeyLocale(verseKey);
		verseKey.setText(seedKey.String());
		module->setKey(verseKey);
	}

	BReference<BibleTextDocument> document(new BibleTextDocument(module),
		true);
	document->SetShowVerseNumbers(fShowVerseNumbers);
	document->SetShowStrongsNumbers(fShowStrongsNumbers);
	// Only offer a Strong's link where a dictionary could actually answer
	// it -- see BibleTextDocument::SetResolvableStrongsPrefixes().
	document->SetResolvableStrongsPrefixes(
		HasStrongsDictionary(fManager, 'G'),
		HasStrongsDictionary(fManager, 'H'));
	document->SetShowCrossReferences(fShowCrossReferences);
	document->SetBaseFont(fBaseFont);

	int32 bibleIndex = _BibleIndexForPosition(afterPosition + 1);
	fModules.insert(fModules.begin() + bibleIndex, module);
	fDocuments.insert(fDocuments.begin() + bibleIndex, document);

	SpliceLinkedToNextForInsert(fLinkedToNext, afterPosition);
	fColumnOrder.insert(fColumnOrder.begin() + afterPosition + 1,
		COLUMN_BIBLE);

	if (fActivePosition < 0)
		fActivePosition = 0;

	_RebuildLayout();
	return B_OK;
}


status_t
ParallelBibleView::InsertNotesColumn(int32 afterPosition)
{
	fprintf(stderr, "[SG] InsertNotesColumn(afterPosition=%d)\n",
		(int)afterPosition);
	if (afterPosition < 0 || (size_t)afterPosition >= fColumnOrder.size())
		return B_BAD_INDEX;

	if (fNotes == NULL) {
		fNotes = new PersonalNotesModule();
		status_t status = fNotes->Open();
		if (status != B_OK) {
			delete fNotes;
			fNotes = NULL;
			return status;
		}
	}

	// The new column joins afterPosition's own chain (see this method's
	// own doc comment), so afterPosition's key is the right one to seed
	// from, not the active chain's.
	NotesColumn notes;
	notes.document = _BuildNotesDocument(afterPosition);
	notes.view = NULL;
	int32 notesIndex = _NotesIndexForPosition(afterPosition + 1);
	fNotesColumns.insert(fNotesColumns.begin() + notesIndex, notes);

	SpliceLinkedToNextForInsert(fLinkedToNext, afterPosition);
	fColumnOrder.insert(fColumnOrder.begin() + afterPosition + 1,
		COLUMN_NOTES);

	if (fActivePosition < 0)
		fActivePosition = 0;

	_RebuildLayout();
	return B_OK;
}


status_t
ParallelBibleView::ReplaceColumn(int32 position, const char* moduleName)
{
	if (position < 0 || (size_t)position >= fColumnOrder.size())
		return B_BAD_INDEX;
	return _SetColumnToBible(position, moduleName);
}


// Erasing a middle column merges its two neighbors' link state: they end
// up linked to each other iff they were BOTH already linked to the column
// being removed (leftLinked/rightLinked below) -- an edge column just
// loses its one gap outright, no merge needed. This is the whole of what
// used to require group-ID bookkeeping in earlier #12 attempts.
status_t
ParallelBibleView::RemoveColumn(int32 position)
{
	if (position < 0 || (size_t)position >= fColumnOrder.size())
		return B_BAD_INDEX;

	bool leftLinked = position > 0 && fLinkedToNext[position - 1];
	bool rightLinked = (size_t)position < fLinkedToNext.size()
		&& fLinkedToNext[position];

	if (fColumnOrder[position] == COLUMN_BIBLE) {
		int32 bibleIndex = _BibleIndexForPosition(position);
		fModules.erase(fModules.begin() + bibleIndex);
		fDocuments.erase(fDocuments.begin() + bibleIndex);
	} else {
		int32 notesIndex = _NotesIndexForPosition(position);
		if (notesIndex >= 0 && (size_t)notesIndex < fNotesColumns.size()) {
			// This entry is about to be erased from fNotesColumns, which
			// would otherwise leave its view (and, cascading, any
			// active overlay editor) dangling -- _RebuildLayout()'s own
			// teardown loop, called below, only tears down entries
			// still IN fNotesColumns by the time it runs.
			_TearDownNotesColumnView(fNotesColumns[notesIndex]);
			fNotesColumns.erase(fNotesColumns.begin() + notesIndex);
		}
		if (fNotesColumns.empty()) {
			delete fNotes;
			fNotes = NULL;
		}
	}
	fColumnOrder.erase(fColumnOrder.begin() + position);

	if (position == 0) {
		if (!fLinkedToNext.empty())
			fLinkedToNext.erase(fLinkedToNext.begin());
	} else if ((size_t)position >= fLinkedToNext.size()) {
		if (!fLinkedToNext.empty())
			fLinkedToNext.erase(fLinkedToNext.end() - 1);
	} else {
		fLinkedToNext.erase(fLinkedToNext.begin() + position);
		fLinkedToNext[position - 1] = leftLinked && rightLinked;
	}

	if (fColumnOrder.empty())
		fActivePosition = -1;
	else if (fActivePosition == position)
		fActivePosition = std::min(fActivePosition,
			(int32)fColumnOrder.size() - 1);
	else if (fActivePosition > position)
		fActivePosition--;

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
		return AddNotesColumn();

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
	// Deliberately NOT applied to fNotesColumns -- a notes column's own
	// verse numbers always render in NotesDisplayView's own gutter,
	// never inline in the document text, so this Bible/Commentary-
	// specific toggle doesn't apply to it either way (see
	// _BuildNotesDocument()).
	_Realign();
	return B_OK;
}


// Same idea as SetShowVerseNumbers() above -- also deliberately NOT
// applied to fNotesColumns: a personal note is free-typed text, never
// SWORD Strong's-tagged markup, so this toggle has nothing to show or
// hide there either way (see _BuildNotesDocument()).
status_t
ParallelBibleView::SetShowStrongsNumbers(bool show)
{
	fShowStrongsNumbers = show;
	for (size_t i = 0; i < fDocuments.size(); i++)
		fDocuments[i]->SetShowStrongsNumbers(show);
	_Realign();
	return B_OK;
}


// Same idea as SetShowVerseNumbers() above, but this one DOES apply to
// notes columns -- a note can genuinely cite "(Joh 3:16)" the same way
// a commentary can (see _BuildNotesDocument()). Applies to the active
// overlay editor too, if any, so a toggle flipped mid-edit doesn't wait
// for the next click to take effect.
status_t
ParallelBibleView::SetShowCrossReferences(bool show)
{
	fShowCrossReferences = show;
	for (size_t i = 0; i < fDocuments.size(); i++)
		fDocuments[i]->SetShowCrossReferences(show);
	for (size_t i = 0; i < fNotesColumns.size(); i++)
		fNotesColumns[i].document->SetShowCrossReferences(show);
	_Realign();
	return B_OK;
}


// Applies to every current Bible/Commentary column, and is remembered
// (fBaseFont) so columns added afterward (see _SetColumnToBible()) start
// out matching it too -- same rationale as SetShowVerseNumbers() above.
// Applies to notes columns' own document too, same reasoning as
// SetShowCrossReferences() above.
status_t
ParallelBibleView::SetBaseFont(const BFont& font)
{
	fBaseFont = font;
	for (size_t i = 0; i < fDocuments.size(); i++)
		fDocuments[i]->SetBaseFont(font);
	for (size_t i = 0; i < fNotesColumns.size(); i++)
		fNotesColumns[i].document->SetBaseFont(font);
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


// Same idea as _BibleIndexForPosition(), for the COLUMN_NOTES slots
// (fNotesColumns).
int32
ParallelBibleView::_NotesIndexForPosition(int32 position) const
{
	int32 index = 0;
	for (int32 i = 0; i < position && (size_t)i < fColumnOrder.size(); i++) {
		if (fColumnOrder[i] == COLUMN_NOTES)
			index++;
	}
	return index;
}


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
ParallelBibleView::_NotesPositionInRange(int32 start, int32 end) const
{
	for (int32 i = start; i <= end && (size_t)i < fColumnOrder.size(); i++) {
		if (fColumnOrder[i] == COLUMN_NOTES)
			return i;
	}
	return -1;
}


int32
ParallelBibleView::_ChainStart(int32 position) const
{
	while (position > 0 && fLinkedToNext[position - 1])
		position--;
	return position;
}


int32
ParallelBibleView::_ChainEnd(int32 position) const
{
	while ((size_t)position < fLinkedToNext.size() && fLinkedToNext[position])
		position++;
	return position;
}


int32
ParallelBibleView::_ChainCount() const
{
	if (fColumnOrder.empty())
		return 0;
	int32 count = 1;
	for (size_t i = 0; i < fLinkedToNext.size(); i++) {
		if (!fLinkedToNext[i])
			count++;
	}
	return count;
}


BView*
ParallelBibleView::_ColumnScrollTarget(int32 position) const
{
	if (position < 0 || (size_t)position >= fColumnOrder.size())
		return NULL;
	if (fColumnOrder[position] == COLUMN_BIBLE)
		return fTextViews[_BibleIndexForPosition(position)];

	int32 notesIndex = _NotesIndexForPosition(position);
	if (notesIndex < 0 || (size_t)notesIndex >= fNotesColumns.size())
		return NULL;
	return fNotesColumns[notesIndex].view;
}


BScrollView*
ParallelBibleView::_ColumnScroller(int32 position) const
{
	BView* target = _ColumnScrollTarget(position);
	return target != NULL ? dynamic_cast<BScrollView*>(target->Parent())
		: NULL;
}


BString
ParallelBibleView::_ChainKey(int32 anchorPosition) const
{
	if (anchorPosition < 0)
		return fLastKnownKey;

	int32 start = _ChainStart(anchorPosition);
	int32 end = _ChainEnd(anchorPosition);
	for (int32 i = start; i <= end; i++) {
		if (fColumnOrder[i] == COLUMN_BIBLE) {
			const char* key
				= fDocuments[_BibleIndexForPosition(i)]->Key();
			if (key != NULL)
				return BString(key);
		}
	}
	return fLastKnownKey;
}


// The tallest laid-out content among a chain's columns -- what the chain
// as a whole can scroll through, as opposed to what any one column can.
// Every column in a chain gets its own copy of this, because they scroll
// together: a chain is only as scrollable as its tallest member, and only
// scrollable at all if that member overflows the viewport.
float
ParallelBibleView::_ChainContentHeight(int32 anchorPosition)
{
	float height = 0.0f;
	int32 end = _ChainEnd(anchorPosition);
	for (int32 i = _ChainStart(anchorPosition); i <= end; i++) {
		TextDocumentView* view
			= dynamic_cast<TextDocumentView*>(_ColumnScrollTarget(i));
		if (view != NULL)
			height = std::max(height, view->ContentHeight());
	}
	return height;
}


// The furthest a chain can scroll down: zero when its content fits the
// viewport, which is the case this exists for. Jumping to a verse asks
// to scroll to that verse's own y offset (see _ChainVerseY()), and in a
// chapter short enough to fit entirely on screen that offset is a
// position the chain cannot actually reach.
float
ParallelBibleView::_MaxChainScroll(int32 anchorPosition)
{
	return std::max(0.0f,
		_ChainContentHeight(anchorPosition) - std::max(0.0f,
			Bounds().Height()));
}


// Sizes the ONE visible vertical BScrollBar a chain has (it hangs off the
// chain's rightmost column -- see _RebuildLayout()) to span the chain's
// TALLEST column rather than just the column it is attached to.
// TextDocumentView::_UpdateScrollBars() has always sized it from that one
// column's own content, which is right for a standalone view but too
// short whenever a linked neighbour is taller -- leaving the bottom of
// that neighbour unreachable, since the neighbours have no bar of their
// own and only follow this one.
//
// Called from _ColumnResized() (i.e. once a queued resize has actually
// landed and the layout has re-measured at the real width) and at the end
// of _Realign(), where widths are unchanged so the layout is already
// valid.
void
ParallelBibleView::_UpdateChainScrollBar(int32 anchorPosition)
{
	int32 driver = _ChainEnd(anchorPosition);
	BScrollView* scroller = _ColumnScroller(driver);
	BScrollBar* bar = scroller != NULL
		? scroller->ScrollBar(B_VERTICAL) : NULL;
	if (bar == NULL)
		return;

	float viewportHeight = std::max(0.0f, Bounds().Height());
	float contentHeight = _ChainContentHeight(anchorPosition);
	long maxRange = (long)ceilf(contentHeight) - (long)viewportHeight;

	bar->SetRange(0.0f, (float)std::max(maxRange, 0L));
	if (contentHeight > 0.0f)
		bar->SetProportion(viewportHeight / contentHeight);
	bar->SetSteps(kVerticalScrollStep, viewportHeight);
}


// A column's queued resize has landed, so its layout has now re-measured
// at its real width -- the first moment its chain's true content height
// can be read (see _UpdateChainScrollBar()'s own comment).
void
ParallelBibleView::_ColumnResized(int32 position)
{
	if (position < 0 || (size_t)position >= fColumnOrder.size())
		return;
	_UpdateChainScrollBar(position);
}


void
ParallelBibleView::_ScrollChainTo(int32 anchorPosition, float y)
{
	if (anchorPosition < 0)
		return;

	int32 start = _ChainStart(anchorPosition);
	int32 end = _ChainEnd(anchorPosition);

	// Clamped, because the columns of a chain do NOT all refuse an
	// out-of-range scroll the same way: only the chain's rightmost column
	// has a real BScrollBar attached (see _RebuildLayout()), and BView
	// clamps ScrollTo() against an attached bar's range. So an
	// unreachable y left the bar-less columns scrolled down and the one
	// with the bar where it was -- and with the bar's range at zero
	// (nothing to scroll), there was no way to bring the others back.
	// Reported live: jumping to 1 John 1:3, a chapter short enough to fit
	// on screen, scrolled the Bible column to verse 3 and stranded it
	// there while the notes column beside it stayed on verse 1.
	y = std::max(0.0f, std::min(y, _MaxChainScroll(anchorPosition)));

	fSuppressScrollPropagation = true;
	for (int32 i = start; i <= end; i++) {
		BView* target = _ColumnScrollTarget(i);
		if (target != NULL)
			target->ScrollTo(target->Bounds().left, y);
	}
	fSuppressScrollPropagation = false;
}


float
ParallelBibleView::_ChainVerseY(int32 anchorPosition, int verse) const
{
	float y = 0.0f;
	for (int v = 1; v < verse; v++)
		y += _RowHeight(v, anchorPosition);
	return y;
}


void
ParallelBibleView::_ToggleLink(int32 gapIndex)
{
	if (gapIndex < 0 || (size_t)gapIndex >= fLinkedToNext.size())
		return;
	SetColumnLinked(gapIndex, !fLinkedToNext[gapIndex]);
}


void
ParallelBibleView::SetColumnLinked(int32 gapIndex, bool linked)
{
	fprintf(stderr, "[SG] SetColumnLinked(gapIndex=%d, linked=%d)\n",
		(int)gapIndex, linked);
	if (gapIndex < 0 || (size_t)gapIndex >= fLinkedToNext.size())
		return;
	if (fLinkedToNext[gapIndex] == linked)
		return;
	fLinkedToNext[gapIndex] = linked;
	if (fActivePosition < 0 && !fColumnOrder.empty())
		fActivePosition = 0;
	_RebuildLayout();
}


void
ParallelBibleView::_SetActiveColumn(int32 position)
{
	if (position < 0 || (size_t)position >= fColumnOrder.size())
		return;
	if (fActivePosition == position)
		return;
	fActivePosition = position;
	if (fHeaderView != NULL)
		fHeaderView->Invalidate();

	// Lets the owning window sync its own book/chapter/verse toolbar
	// fields to this chain's own current key (see ActiveColumn()/
	// ChainKey()) -- posted rather than called directly so this class
	// stays unaware of what kind of window embeds it, same reasoning as
	// SG_BIBLE elsewhere in this file.
	BWindow* window = Window();
	if (window != NULL)
		window->PostMessage(PARALLEL_ACTIVE_COLUMN_CHANGED);
}


void
ParallelBibleView::_ColumnScrolled(int32 position, float y)
{
	if (fSuppressScrollPropagation)
		return;

	int32 start = _ChainStart(position);
	int32 end = _ChainEnd(position);
	if (start == end)
		return;

	fSuppressScrollPropagation = true;
	for (int32 i = start; i <= end; i++) {
		if (i == position)
			continue;
		BView* target = _ColumnScrollTarget(i);
		if (target != NULL)
			target->ScrollTo(target->Bounds().left, y);
	}
	fSuppressScrollPropagation = false;
}


void
ParallelBibleView::_ForwardWheelToChain(int32 position, BMessage* message)
{
	BView* driver = _ColumnScrollTarget(_ChainEnd(position));
	if (driver != NULL)
		driver->MessageReceived(message);
}


void
ParallelBibleView::_ActiveChainHeaderRange(float& left, float& right) const
{
	left = 0.0f;
	right = 0.0f;
	if (fActivePosition < 0 || _ChainCount() <= 1)
		return;

	int32 start = _ChainStart(fActivePosition);
	int32 end = _ChainEnd(fActivePosition);
	if (end >= (int32)fColumnDividerX.size())
		return;

	// fColumnDividerX[i] is the divider immediately after column i --
	// the active chain's own header cells span roughly from the divider
	// before `start` (or content-space 0 for the very first column) to
	// the divider at `end`.
	left = start > 0 ? fColumnDividerX[start - 1] : 0.0f;
	right = fColumnDividerX[end];
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

	// The moved column's OLD neighbors are relinked to each other iff
	// they were both linked to it before -- same rule as RemoveColumn().
	bool leftLinked = from > 0 && columns[from - 1].linkedToNext;
	bool rightLinked = (size_t)from < columns.size() - 1
		&& columns[from].linkedToNext;
	if (from > 0)
		columns[from - 1].linkedToNext = leftLinked && rightLinked;

	ColumnDescription moved = columns[from];
	// Arrives unlinked from its new right-hand neighbor -- a drag never
	// silently merges a chain it wasn't asked to.
	moved.linkedToNext = false;
	columns.erase(columns.begin() + from);
	columns.insert(columns.begin() + to, moved);
	// ...and from its new left-hand neighbor too.
	if (to > 0)
		columns[to - 1].linkedToNext = false;

	while (CountColumns() > 0)
		RemoveColumn(0);

	for (size_t i = 0; i < columns.size(); i++) {
		if (columns[i].isNotes)
			AddNotesColumn();
		else if (!columns[i].moduleName.IsEmpty())
			AddColumn(columns[i].moduleName.String());
	}
	for (size_t i = 0; i + 1 < columns.size(); i++)
		SetColumnLinked((int32)i, columns[i].linkedToNext);

	// AddColumn()/AddNotesColumn() above (via _SetColumnToBible()/
	// _BuildNotesDocument()) seed each rebuilt column from
	// _ChainKey(fActivePosition) -- correct for a genuinely new column,
	// but during this teardown/rebuild fActivePosition is pinned
	// wherever the *first* AddColumn call left it, so every column after
	// that silently inherited THAT one's chapter instead of its own.
	// Restore each column's own captured key directly (bypassing the
	// chain-scoped SetKey(), since two still-linked columns already
	// share the same key here anyway) and refresh the layout once more
	// now that the real keys are back.
	for (size_t i = 0; i < columns.size(); i++) {
		if (columns[i].key.IsEmpty())
			continue;
		if (columns[i].isNotes) {
			int32 notesIndex = _NotesIndexForPosition((int32)i);
			if (notesIndex >= 0 && (size_t)notesIndex < fNotesColumns.size())
				fNotesColumns[notesIndex].document->SetKey(
					columns[i].key.String());
		} else {
			int32 bibleIndex = _BibleIndexForPosition((int32)i);
			if (bibleIndex >= 0 && (size_t)bibleIndex < fDocuments.size())
				fDocuments[bibleIndex]->SetKey(columns[i].key.String());
		}
	}
	_Realign();
}


// Makes the column at `position` a Bible/Commentary column showing
// `moduleName`, or appends a new one at the end if position < 0. If that
// slot was a notes column, that notes column is given up entirely and a
// new Bible slot is inserted in its place.
status_t
ParallelBibleView::_SetColumnToBible(int32 position, const char* moduleName)
{
	if (fManager == NULL)
		return B_NO_INIT;

	SWModule* module = fManager->getModule(moduleName);
	if (module == NULL)
		return B_NAME_NOT_FOUND;

	fprintf(stderr, "[SG] _SetColumnToBible(position=%d, module=%s) "
		"module ptr=%p\n", (int)position, moduleName, (void*)module);

	// Seed the new/replaced module with whatever its own chain (the
	// active one, for an append; its own for an in-place conversion) is
	// currently showing, so it opens in step with its neighbors instead
	// of wherever its own default key happens to be.
	BString seedKey = position < 0 ? _ChainKey(fActivePosition)
		: _ChainKey(position);
	if (!seedKey.IsEmpty()) {
		// seedKey is a localized reference (e.g. "1. Mose 1:1" under a
		// German locale) -- setText() needs the key's locale set first or
		// it fails silently and the module's key is left at whatever it
		// defaulted to, same class of bug already worked around elsewhere
		// in this file (see SetVerseKeyLocale()). Deliberately default-
		// constructed rather than seeded via the VerseKey(const char*)
		// constructor -- that form parses its argument immediately,
		// before SetVerseKeyLocale() below ever runs, so if the module's
		// own current key text (getKeyText()) already happens to be
		// localized (e.g. this module was previously navigated in
		// another column sharing the same fManager), that parse fails at
		// the still-default locale and leaves the key -- and every
		// setText() call after it, including the one actually meant to
		// apply seedKey -- stuck on SWORD's failed-parse fallback of
		// "Revelation of John" 1:1 instead of matching its chain (confirmed
		// empirically: this is exactly the bug already root-caused and
		// fixed in BibleTextDocument::SetKey() -- same anti-pattern,
		// different call site).
		VerseKey verseKey;
		SetVerseKeyLocale(verseKey);
		verseKey.setText(seedKey.String());
		module->setKey(verseKey);
	}

	BReference<BibleTextDocument> document(new BibleTextDocument(module),
		true);
	// Match whatever the other columns are currently showing (see
	// SetShowVerseNumbers()) -- BibleTextDocument defaults to true, so
	// this only matters when the setting has been turned off.
	document->SetShowVerseNumbers(fShowVerseNumbers);
	document->SetShowStrongsNumbers(fShowStrongsNumbers);
	// Only offer a Strong's link where a dictionary could actually answer
	// it -- see BibleTextDocument::SetResolvableStrongsPrefixes().
	document->SetResolvableStrongsPrefixes(
		HasStrongsDictionary(fManager, 'G'),
		HasStrongsDictionary(fManager, 'H'));
	document->SetShowCrossReferences(fShowCrossReferences);
	// Match the current base font (see SetBaseFont()) -- only matters once
	// the user has actually picked something other than be_plain_font.
	document->SetBaseFont(fBaseFont);

	if (position < 0) {
		fModules.push_back(module);
		fDocuments.push_back(document);
		fColumnOrder.push_back(COLUMN_BIBLE);
		// A brand-new gap starts out linked (see the class comment) --
		// none needed at all for the very first column.
		if (fColumnOrder.size() > 1)
			fLinkedToNext.push_back(true);
	} else if ((size_t)position >= fColumnOrder.size()) {
		return B_BAD_INDEX;
	} else if (fColumnOrder[position] == COLUMN_BIBLE) {
		int32 bibleIndex = _BibleIndexForPosition(position);
		fModules[bibleIndex] = module;
		fDocuments[bibleIndex] = document;
	} else {
		int32 notesIndex = _NotesIndexForPosition(position);
		if (notesIndex >= 0 && (size_t)notesIndex < fNotesColumns.size()) {
			// Same dangling-container hazard RemoveColumn() guards
			// against -- see its own comment.
			_TearDownNotesColumnView(fNotesColumns[notesIndex]);
			fNotesColumns.erase(fNotesColumns.begin() + notesIndex);
			if (fNotesColumns.empty()) {
				delete fNotes;
				fNotes = NULL;
			}
		}
		int32 bibleIndex = _BibleIndexForPosition(position);
		fModules.insert(fModules.begin() + bibleIndex, module);
		fDocuments.insert(fDocuments.begin() + bibleIndex, document);
		fColumnOrder[position] = COLUMN_BIBLE;
	}

	if (fActivePosition < 0)
		fActivePosition = 0;

	_RebuildLayout();
	return B_OK;
}


// Requires fNotes to already be open (every caller ensures that first).
BReference<BibleTextDocument>
ParallelBibleView::_BuildNotesDocument(int32 seedAnchorPosition)
{
	// Seed with whatever seedAnchorPosition's own chain (the active one,
	// if < 0) is currently showing -- same reasoning/anti-pattern-
	// avoidance as _SetColumnToBible()'s identical seeding block, since
	// PersonalNotesModule::Module() is a real SWModule (a RawCom*) and
	// has the exact same shared-live-key hazard.
	BString seedKey = seedAnchorPosition < 0 ? _ChainKey(fActivePosition)
		: _ChainKey(seedAnchorPosition);
	if (!seedKey.IsEmpty()) {
		VerseKey verseKey;
		SetVerseKeyLocale(verseKey);
		verseKey.setText(seedKey.String());
		fNotes->Module()->setKey(verseKey);
	}

	BReference<BibleTextDocument> document(
		new BibleTextDocument(fNotes->Module()), true);
	// Never inline, regardless of the Bible/Commentary "Show Verse
	// Numbers" toggle -- a notes column always renders its own numbers
	// in NotesDisplayView's own gutter instead (see its class comment),
	// which needs them to NOT also be part of the document text or
	// they'd show up twice.
	document->SetShowVerseNumbers(false);
	// A personal note is free-typed text, never SWORD Strong's-tagged
	// markup, so FindStrongsWordsInText() could never find anything
	// here regardless of fShowStrongsNumbers -- always off, not just
	// following the app-wide toggle.
	document->SetShowStrongsNumbers(false);
	document->SetShowCrossReferences(fShowCrossReferences);
	document->SetBaseFont(fBaseFont);
	// Keeps an empty verse's paragraph around instead of dropping it (the
	// default, right for Bible/Commentary columns), so every verse still
	// has a row to click into -- see the class comment.
	document->SetSkipEmptyVerses(false);
	// This document IS edited directly (see NotesDisplayView), so its
	// paragraphs need the explicit terminator the editing engine assumes
	// -- see BibleTextDocument::SetParagraphsEndWithNewline() for what
	// silently breaks without it.
	document->SetParagraphsEndWithNewline(true);
	// Attached once and left for the document's whole lifetime -- see the
	// class comment on NotesSaveListener for why a rebuild never detaches
	// it and never looks like a user edit.
	document->AddListener(TextListenerRef(
		new NotesSaveListener(document.Get(), fNotes, this), true));
	return document;
}


// Reports that the user has just typed in a notes column (called by
// NotesSaveListener, which has already written the edit through to the
// SWORD module). Arms -- or re-arms, by replacing the pending one, which
// restarts its interval -- a one-shot timer instead of realigning now:
// _Realign() re-measures every verse of every column in the chain, which
// is fine once typing pauses and hopeless per keystroke.
void
ParallelBibleView::NoteTextEdited()
{
	// BMessageRunner needs a valid BMessenger, which needs this view to
	// be attached to a window. Nothing to realign before then anyway.
	if (Window() == NULL)
		return;

	delete fNotesRealignRunner;
	BMessage message(PARALLEL_NOTES_TEXT_CHANGED);
	fNotesRealignRunner = new BMessageRunner(BMessenger(this), &message,
		kNotesRealignDelay, 1);
}


void
ParallelBibleView::_TearDownNotesColumnView(NotesColumn& notes)
{
	if (notes.view == NULL)
		return;

	BScrollView* scroller = dynamic_cast<BScrollView*>(notes.view->Parent());
	notes.view->RemoveSelf();
	delete notes.view;
	notes.view = NULL;

	if (scroller != NULL) {
		scroller->RemoveSelf();
		delete scroller;
	}
}


// Makes the column at `position` a notes column, or appends a new one at
// the end if position < 0. Any number of notes columns can exist at once
// (see the class comment) -- all backed by the same shared fNotes,
// opened the first time any notes column is created, closed once the
// last one is gone (see RemoveColumn()).
status_t
ParallelBibleView::_SetColumnToNotes(int32 position)
{
	if (fNotes == NULL) {
		fNotes = new PersonalNotesModule();
		status_t status = fNotes->Open();
		if (status != B_OK) {
			delete fNotes;
			fNotes = NULL;
			return status;
		}
	}

	NotesColumn notes;
	notes.document = _BuildNotesDocument(position);
	notes.view = NULL;

	if (position < 0) {
		fColumnOrder.push_back(COLUMN_NOTES);
		if (fColumnOrder.size() > 1)
			fLinkedToNext.push_back(true);
		fNotesColumns.push_back(notes);
	} else if ((size_t)position >= fColumnOrder.size()) {
		return B_BAD_INDEX;
	} else if (fColumnOrder[position] == COLUMN_NOTES) {
		return B_OK;
	} else {
		int32 bibleIndex = _BibleIndexForPosition(position);
		fModules.erase(fModules.begin() + bibleIndex);
		fDocuments.erase(fDocuments.begin() + bibleIndex);
		fColumnOrder[position] = COLUMN_NOTES;
		int32 notesIndex = _NotesIndexForPosition(position);
		fNotesColumns.insert(fNotesColumns.begin() + notesIndex, notes);
	}

	if (fActivePosition < 0)
		fActivePosition = 0;

	_RebuildLayout();
	return B_OK;
}


// Acts on whichever chain the active column belongs to -- see the class
// comment. Every navigation path (book/chapter/verse fields, the
// universal search box, a dropped reference) funnels through here.
status_t
ParallelBibleView::SetKey(const char* key)
{
	if (fActivePosition < 0)
		return B_OK;

	bigtime_t perfStart = system_time();

	int32 start = _ChainStart(fActivePosition);
	int32 end = _ChainEnd(fActivePosition);

	bool changedAnyBible = false;
	for (int32 i = start; i <= end; i++) {
		if (fColumnOrder[i] != COLUMN_BIBLE)
			continue;
		int32 bibleIndex = _BibleIndexForPosition(i);
		fDocuments[bibleIndex]->SetKey(key);
		// A selection's text offsets are only meaningful for the chapter
		// they were made in -- SetKey() rebuilds the column's document
		// out from under it (see BibleTextDocument::_Rebuild()), so a
		// leftover selection either highlights unrelated text at the
		// same offsets in the new chapter or, once the new text is
		// shorter, points past the end of it entirely.
		fTextViews[bibleIndex]->SetSelection(0, 0);
		changedAnyBible = true;
	}
	if (changedAnyBible)
		fLastKnownKey = key;
	bigtime_t perfAfterBible = system_time();

	// A notes column now navigates with its own chain exactly like a
	// Bible column does (its own BibleTextDocument, see
	// _BuildNotesDocument()) -- no separate "rebuild the view for every
	// notes column's own current chain key" pass needed any more.
	for (int32 i = start; i <= end; i++) {
		if (fColumnOrder[i] != COLUMN_NOTES)
			continue;
		NotesColumn& notes = fNotesColumns[_NotesIndexForPosition(i)];
		notes.document->SetKey(key);
	}
	bigtime_t perfAfterNotes = system_time();

	_Realign();
	bigtime_t perfAfterRealign = system_time();

	VerseKey verseKey;
	SetVerseKeyLocale(verseKey);
	verseKey.setText(key);
	int verse = verseKey.getVerse();
	_ScrollChainTo(fActivePosition,
		verse > 1 ? _ChainVerseY(fActivePosition, verse) : 0.0f);
	bigtime_t perfEnd = system_time();

	fprintf(stderr, "[SG-PERF] SetKey(\"%s\") total=%.2fms "
		"bibleLoop=%.2fms notesLoop=%.2fms realign=%.2fms scroll=%.2fms\n",
		key, (perfEnd - perfStart) / 1000.0,
		(perfAfterBible - perfStart) / 1000.0,
		(perfAfterNotes - perfAfterBible) / 1000.0,
		(perfAfterRealign - perfAfterNotes) / 1000.0,
		(perfEnd - perfAfterRealign) / 1000.0);

	return B_OK;
}


void
ParallelBibleView::HighlightVerse(int startVerse, int endVerse)
{
	if (fActivePosition < 0)
		return;

	int32 start = _ChainStart(fActivePosition);
	int32 end = _ChainEnd(fActivePosition);
	for (int32 i = start; i <= end; i++) {
		if (fColumnOrder[i] != COLUMN_BIBLE)
			continue;
		int32 bibleIndex = _BibleIndexForPosition(i);
		int32 s, e;
		if (fDocuments[bibleIndex]->TextRangeForVerseRange(startVerse,
				endVerse, s, e)) {
			fTextViews[bibleIndex]->SetSelection(s, e);
		} else {
			fTextViews[bibleIndex]->SetSelection(0, 0);
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
			int32 notesIndex = _NotesIndexForPosition((int32)i);
			if (notesIndex >= 0 && (size_t)notesIndex < fNotesColumns.size()) {
				const char* key = fNotesColumns[notesIndex].document->Key();
				if (key != NULL)
					desc.key = key;
			}
		} else {
			desc.isNotes = false;
			int32 bibleIndex = _BibleIndexForPosition((int32)i);
			if (bibleIndex >= 0 && (size_t)bibleIndex < fModules.size()) {
				desc.moduleName = fModules[bibleIndex]->getName();
				const char* key = fDocuments[bibleIndex]->Key();
				if (key != NULL)
					desc.key = key;
			}
		}
		desc.linkedToNext = i < fLinkedToNext.size() ? fLinkedToNext[i]
			: false;
		result.push_back(desc);
	}
	return result;
}


std::vector<ParallelBibleView::ExportRow>
ParallelBibleView::BuildExportRows() const
{
	std::vector<ExportRow> rows;
	if (fActivePosition < 0)
		return rows;

	BString chainKey = _ChainKey(fActivePosition);
	if (chainKey.IsEmpty())
		return rows;

	VerseKey chapterKey;
	SetVerseKeyLocale(chapterKey);
	chapterKey.setText(chainKey.String());
	int verseCount = chapterKey.getVerseMax();

	int32 start = _ChainStart(fActivePosition);
	int32 end = _ChainEnd(fActivePosition);
	int32 notesPosition = _NotesPositionInRange(start, end);

	for (int verse = 1; verse <= verseCount; verse++) {
		ExportRow row;
		row.verse = verse;

		BString verseNumberPrefix;
		verseNumberPrefix << " " << verse << " ";

		for (int32 i = start; i <= end; i++) {
			if (fColumnOrder[i] != COLUMN_BIBLE)
				continue;
			BibleTextDocument* document
				= fDocuments[_BibleIndexForPosition(i)].Get();

			BString cellText;
			int32 s, e;
			if (document->TextRangeForVerseRange(verse, verse, s, e)) {
				cellText = document->Text(s, e - s);
				if (cellText.FindFirst(verseNumberPrefix) == 0)
					cellText.Remove(0, verseNumberPrefix.Length());
				cellText.Trim();
			}
			row.columnText.push_back(cellText);
		}

		if (fNotes != NULL && notesPosition >= 0) {
			VerseKey verseKey;
			SetVerseKeyLocale(verseKey);
			verseKey.setText(chainKey.String());
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
		// Each column now lives inside its own BScrollView (see the
		// class comment) -- column->Frame() alone is relative to THAT
		// wrapper, not to this view, so the wrapper's own Frame() (which
		// _PositionColumns() actually positions in this view's
		// coordinate space) is what needs comparing here instead.
		BView* columnParent = column->Parent();
		BRect frame = columnParent != NULL ? columnParent->Frame()
			: column->Frame();
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
	if (fActivePosition < 0)
		return B_OK;

	int32 start = _ChainStart(fActivePosition);
	int32 end = _ChainEnd(fActivePosition);
	bool changedAnyBible = false;
	for (int32 i = start; i <= end; i++) {
		if (fColumnOrder[i] != COLUMN_BIBLE)
			continue;
		fDocuments[_BibleIndexForPosition(i)]->NextChapter();
		changedAnyBible = true;
	}
	if (changedAnyBible)
		fLastKnownKey = _ChainKey(fActivePosition);

	{
		BString chainKey = _ChainKey(fActivePosition);
		for (int32 i = start; i <= end; i++) {
			if (fColumnOrder[i] != COLUMN_NOTES)
				continue;
			NotesColumn& notes = fNotesColumns[_NotesIndexForPosition(i)];
			notes.document->SetKey(chainKey.String());
		}
	}
	_Realign();
	// A new chapter always starts at verse 1 (both *Chapter() methods
	// force that); reset the viewport too, or a scroll position from the
	// previous chapter could leave the new one looking blank/misplaced.
	_ScrollChainTo(fActivePosition, 0.0f);
	return B_OK;
}


status_t
ParallelBibleView::PrevChapter()
{
	if (fActivePosition < 0)
		return B_OK;

	int32 start = _ChainStart(fActivePosition);
	int32 end = _ChainEnd(fActivePosition);
	bool changedAnyBible = false;
	for (int32 i = start; i <= end; i++) {
		if (fColumnOrder[i] != COLUMN_BIBLE)
			continue;
		fDocuments[_BibleIndexForPosition(i)]->PrevChapter();
		changedAnyBible = true;
	}
	if (changedAnyBible)
		fLastKnownKey = _ChainKey(fActivePosition);

	{
		BString chainKey = _ChainKey(fActivePosition);
		for (int32 i = start; i <= end; i++) {
			if (fColumnOrder[i] != COLUMN_NOTES)
				continue;
			NotesColumn& notes = fNotesColumns[_NotesIndexForPosition(i)];
			notes.document->SetKey(chainKey.String());
		}
	}
	_Realign();
	_ScrollChainTo(fActivePosition, 0.0f);
	return B_OK;
}


// Tears down every currently-attached column view (and, alongside it,
// the BScrollView wrapping it -- see the class comment on
// ~ParallelBibleView() for why detach-then-delete order matters) and
// rebuilds them fresh from fDocuments/fColumnOrder. Only the RIGHTMOST
// column of each chain (see _IsChainRightmost()) gets an actual, visible
// vertical BScrollBar -- every other column still gets its own
// BScrollView (so ScrollTo() behaves identically either way, see
// _ColumnScrolled()), just with no scrollbar chrome attached.
void
ParallelBibleView::_RebuildLayout()
{
	for (size_t i = 0; i < fTextViews.size(); i++) {
		BView* view = fTextViews[i];
		BScrollView* scroller = dynamic_cast<BScrollView*>(view->Parent());
		view->RemoveSelf();
		delete view;
		if (scroller != NULL) {
			scroller->RemoveSelf();
			delete scroller;
		}
	}
	fTextViews.clear();

	for (size_t i = 0; i < fNotesColumns.size(); i++)
		_TearDownNotesColumnView(fNotesColumns[i]);

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
		fTextViews.push_back(view);
	}

	{
		BString gaps;
		for (size_t g = 0; g < fLinkedToNext.size(); g++)
			gaps << (fLinkedToNext[g] ? "1" : "0");
		fprintf(stderr, "[SG] _RebuildLayout: %zu columns, gaps=[%s]\n",
			fColumnOrder.size(), gaps.String());
	}

	int32 bibleIndex = 0;
	for (size_t i = 0; i < fColumnOrder.size(); i++) {
		bool vertical = _IsChainRightmost((int32)i);
		fprintf(stderr, "[SG]   pos %zu: %s vertical=%d "
			"(chainStart=%d chainEnd=%d)\n", i,
			fColumnOrder[i] == COLUMN_BIBLE ? "BIBLE" : "NOTES",
			vertical, (int)_ChainStart((int32)i), (int)_ChainEnd((int32)i));
		if (fColumnOrder[i] == COLUMN_BIBLE) {
			TextDocumentView* view = fTextViews[bibleIndex];
			static_cast<BibleColumnView*>(view)->SetPosition((int32)i);
			BScrollView* scroller = new BScrollView("columnScroll", view, 0,
				false, vertical, B_NO_BORDER);
			AddChild(scroller);
			bibleIndex++;
		} else {
			int32 notesIndex = _NotesIndexForPosition((int32)i);
			NotesColumn& notes = fNotesColumns[notesIndex];
			NotesDisplayView* view = new NotesDisplayView("notesColumn",
				notes.document.Get(), this);
			view->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
			view->SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
			// Extra left inset reserves room for this column's own
			// verse-number gutter (see NotesDisplayView::Draw()) -- the
			// document text itself starts to the right of it.
			view->SetInsets(kBibleColumnInset + kNoteGutterWidth,
				kBibleColumnInset, kBibleColumnInset, kBibleColumnInset);
			// Directly editable, always -- there is no read-only mode and
			// no separate editor view (see NotesDisplayView). The document
			// has to be in place first: SetEditingEnabled() reaches the
			// view's editor, but the editor only has something to edit
			// once SetTextDocument() has handed it the document.
			view->SetTextDocument(notes.document);
			view->SetEditingEnabled(true);
			view->SetPosition((int32)i);
			notes.view = view;
			BScrollView* scroller = new BScrollView("columnScroll",
				view, 0, false, vertical, B_NO_BORDER);
			AddChild(scroller);
			fprintf(stderr, "[SG]     notes view=%p\n", (void*)view);
		}
	}

	_RebuildHeader();
	_Realign();
}


// Rebuilds the header row's per-column BMenuFields + remove buttons, plus
// one link/unlink toggle per gap between adjacent columns -- Bible/
// Commentary columns and notes columns alike, so every column's dropdown
// offers switching to any of those (see _BuildModuleMenu()). Positioning
// happens in _PositionColumns(), which uses the exact same x-offsets as
// the content columns so header cells and columns always line up.
void
ParallelBibleView::_RebuildHeader()
{
	// If the number of columns hasn't changed, this is a content-only
	// update (a column's dropdown picked a different module/Notes, or a
	// link was toggled) -- update each existing BMenuField's menu
	// contents/label and each link button's glyph in place instead of
	// deleting and recreating them. This matters because BMenuField::
	// MouseDown() (Haiku's Interface Kit) spawns a background thread
	// ("_m_task_") to track the very click that produced this rebuild,
	// and ~BMenuField() blocks in wait_for_thread() for it; if that
	// thread still needs this window's lock for its own post-click
	// cleanup while this dispatch already holds it (BLooper holds its
	// lock for a dispatch's full duration), deleting the field
	// synchronously here deadlocks the whole window -- reproduced live,
	// not just theoretical. Structural changes (a column added or
	// removed, below) don't have this risk: those are triggered by a
	// plain BButton click ("+"/"x"/link toggle), which tracks
	// synchronously with no spawned thread, so by the time this runs no
	// *other* field's tracking thread should still be active.
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
		for (size_t i = 0; i < fLinkButtons.size(); i++) {
			fLinkButtons[i]->SetLabel(fLinkedToNext[i]
				? kLinkedGlyph : kBrokenLinkGlyph);
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
	for (size_t i = 0; i < fInsertButtons.size(); i++) {
		fInsertButtons[i]->RemoveSelf();
		delete fInsertButtons[i];
	}
	fInsertButtons.clear();
	for (size_t i = 0; i < fLinkButtons.size(); i++) {
		fLinkButtons[i]->RemoveSelf();
		delete fLinkButtons[i];
	}
	fLinkButtons.clear();

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

		// Inserts a brand-new column right after this one (see
		// InsertColumn()/InsertNotesColumn()) -- replaces the old single
		// trailing "+" (always appended at the very end); every column
		// gets its own now, so there's always an unambiguous anchor for
		// "add something new right here" instead of one global button
		// whose neighbor was whatever happened to be last.
		BMessage* insertMessage = new BMessage(PARALLEL_INSERT_COLUMN_MENU);
		insertMessage->AddInt32("index", (int32)i);
		BButton* insertButton = new BButton("insertColumn", "+",
			insertMessage);
		insertButton->SetTarget(this);
		fHeaderView->AddChild(insertButton);
		fInsertButtons.push_back(insertButton);

		BMessage* removeMessage = new BMessage(PARALLEL_REMOVE_COLUMN);
		removeMessage->AddInt32("index", (int32)i);
		BButton* removeButton = new BButton("removeColumn", "x",
			removeMessage);
		removeButton->SetTarget(this);
		fHeaderView->AddChild(removeButton);
		fRemoveButtons.push_back(removeButton);

		// One fewer than columns -- nothing to link the last column to.
		// Centered on the divider line between this column and the next
		// (see _PositionColumns()), so it reads as "these two belong
		// together" (or don't) right on the seam it toggles.
		if (i + 1 < fColumnOrder.size()) {
			BMessage* linkMessage = new BMessage(PARALLEL_TOGGLE_LINK);
			linkMessage->AddInt32("gap", (int32)i);
			BButton* linkButton = new BButton("linkColumns",
				fLinkedToNext[i] ? kLinkedGlyph : kBrokenLinkGlyph,
				linkMessage);
			linkButton->SetTarget(this);
			fHeaderView->AddChild(linkButton);
			fLinkButtons.push_back(linkButton);
		}
	}
}


// Aligns each chain's own Bible/Commentary/notes documents independently
// -- VerseAligner::Align() only ever sees one chain's members at a time,
// so two columns in different chains (possibly showing entirely
// different books) are never cross-aligned. A notes column's own
// document joins the SAME alignment group as its chain's Bible/
// Commentary ones -- otherwise a note's paragraph would size purely to
// its own typed text and no longer line up with the verse it's next to
// (the whole point of a parallel column). VerseAligner::Align() doesn't
// care which kind of column a BibleTextDocument* came from.
void
ParallelBibleView::_Realign()
{
	int32 start = 0;
	while ((size_t)start < fColumnOrder.size()) {
		int32 end = _ChainEnd(start);

		std::vector<BibleTextDocument*> columns;
		std::vector<float> widths;
		for (int32 i = start; i <= end; i++) {
			BibleTextDocument* document = NULL;
			if (fColumnOrder[i] == COLUMN_BIBLE)
				document = fDocuments[_BibleIndexForPosition(i)].Get();
			else
				document = fNotesColumns[_NotesIndexForPosition(i)]
					.document.Get();
			columns.push_back(document);
			// The live TextDocumentView wraps at _MeasurementWidth(), not
			// the raw slot width (see there for why) -- measuring at the
			// full column width here would let VerseAligner's standalone
			// ParagraphLayout plan for more horizontal room than a
			// verse's text actually gets, so a borderline-wrapping line
			// the real view wraps one line further than planned, which
			// no alignment padding then accounted for (confirmed via two
			// real screenshots in an earlier fix -- see _RowHeight()).
			widths.push_back(_MeasurementWidth(i));
		}

		// Called for EVERY chain, including one-column ones. A single
		// column has nothing to align against, but Align() is also what
		// clears alignment padding left over from when that column had a
		// partner -- see its own comment on why the clear happens before
		// it gives up on the size check, and why skipping the call here
		// left a disconnected column permanently stretched.
		{
			bigtime_t alignStart = system_time();
			VerseAligner::Align(columns, widths);
			fprintf(stderr, "[SG-PERF] VerseAligner::Align chain [%d,%d] "
				"columns=%zu elapsed=%.2fms\n", (int)start, (int)end,
				columns.size(), (system_time() - alignStart) / 1000.0);

			// Deliberately NOT calling view->GetHeightForWidth() here for
			// a debug print the way an earlier version of this loop did:
			// it measures via a COPY of the view's own TextDocumentLayout
			// (see TextDocumentView::GetHeightForWidth()), and that copy's
			// SetWidth(_MeasurementWidth(i)) call almost always differs
			// from whatever width the copy inherited, invalidating it and
			// forcing a full re-layout of every paragraph in the document
			// -- confirmed live as a genuine, not just theoretical,
			// performance bug: for a long chapter (Psalm 119, 176 verses)
			// this happened once per column, on every _Realign() call,
			// entirely to produce a log line nobody was reading in the
			// hot path. fprintf(stderr, "[SG] _Realign chain [%d,%d] "
			// paragraph counts alone (no height) are cheap and don't need
			// removing:
			fprintf(stderr, "[SG] _Realign chain [%d,%d] after Align: "
				"paragraphs=[", (int)start, (int)end);
			for (int32 i = start; i <= end; i++) {
				BibleTextDocument* doc;
				if (fColumnOrder[i] == COLUMN_BIBLE)
					doc = fDocuments[_BibleIndexForPosition(i)].Get();
				else
					doc = fNotesColumns[_NotesIndexForPosition(i)]
						.document.Get();
				fprintf(stderr, "%d%s", (int)doc->CountParagraphs(),
					i < end ? "," : "");
			}
			fprintf(stderr, "]\n");
		}

		start = end + 1;
	}

	_PositionColumns();

	for (size_t i = 0; i < fTextViews.size(); i++) {
		fTextViews[i]->Relayout();
		fTextViews[i]->Invalidate();
	}
	for (size_t i = 0; i < fNotesColumns.size(); i++) {
		if (fNotesColumns[i].view != NULL) {
			fNotesColumns[i].view->Relayout();
			fNotesColumns[i].view->Invalidate();
		}
	}

	// Every column has just been re-laid out at its (unchanged) width, so
	// each chain's real content height is readable now -- see
	// _UpdateChainScrollBar().
	for (int32 i = 0; (size_t)i < fColumnOrder.size(); i++) {
		if (_IsChainRightmost(i))
			_UpdateChainScrollBar(i);
	}
}


// Positions and sizes every column's own BScrollView directly (MoveTo/
// ResizeTo) instead of delegating to a BGroupLayout -- see the header
// comment for why. Every column's BScrollView is resized to the shared
// viewport height (this view's own Bounds().Height()) rather than to any
// particular content height: each column scrolls its own, potentially
// much taller, content independently now (see the class comment), so
// there is no single shared content height to size this view's columns
// to any more. Both kinds of column -- Bible/Commentary and notes alike,
// both a TextDocumentView -- are resized automatically by their own
// wrapping BScrollView (which in turn triggers their own FrameResized()/
// _UpdateScrollBars(), already fully self-contained -- see the class
// comment); the explicit Relayout() call just before that forces a fresh
// measurement in case the document was rebuilt more than once in a row
// (once per SetVerseSpacing() call inside VerseAligner::Align()) since
// the last pass.
void
ParallelBibleView::_PositionColumns()
{
	bigtime_t perfStart = system_time();
	float x = 0.0f;
	// Bounds() is degenerate (a canonical invalid BRect, Height() == -1)
	// until this view has actually been placed inside its (shown)
	// window's BScrollView -- same caveat _ColumnWidth() already
	// documents for its own totalWidth. Confirmed via debug logging: a
	// negative viewportHeight passed into a column's own ResizeTo()
	// computed a garbage (negative) scrollbar proportion. Clamping to 0
	// here is harmless (an empty/invisible range) and self-corrects the
	// next time this runs with a real size -- e.g. from FrameResized(),
	// once the window is actually shown (see also SGMainWindow's forced
	// Layout(true) right after BuildGUI(), which gets a real size in
	// place well before that -- this clamp is the fallback for whatever
	// still runs before even that).
	float viewportHeight = std::max(0.0f, Bounds().Height());
	fprintf(stderr, "[SG] _PositionColumns: Bounds()=(%.1f,%.1f,%.1f,%.1f) "
		"Window()=%p Frame()=(%.1f,%.1f,%.1f,%.1f)\n",
		Bounds().left, Bounds().top, Bounds().right, Bounds().bottom,
		(void*)Window(), Frame().left, Frame().top, Frame().right,
		Frame().bottom);
	fColumnDividerX.clear();

	size_t bibleIndex = 0;
	size_t notesIndex = 0;
	for (size_t i = 0; i < fColumnOrder.size(); i++) {
		bool isNotes = (fColumnOrder[i] == COLUMN_NOTES);
		float width = isNotes ? _NotesColumnWidth() : _ColumnWidth();

		if (i < fHeaderFields.size()) {
			// Sized to its own natural (preferred) width -- like the
			// original toolbar's module field -- rather than stretched
			// to fill the column. A stretched fixed-size BMenuField
			// still draws its native pop-up marker at its own right
			// edge (see BMenuField/_BMCMenuBar_ in the Interface Kit;
			// nothing here draws it), which the insert/remove buttons,
			// sitting right there, would otherwise cover; bounding the
			// field to its content and placing the buttons right after
			// it instead keeps that marker visible.
			float preferredWidth, preferredHeight;
			fHeaderFields[i]->GetPreferredSize(&preferredWidth,
				&preferredHeight);
			float reserved = kInsertButtonWidth + kRemoveButtonWidth;
			float fieldWidth = std::min(preferredWidth,
				std::max(0.0f, width - reserved));

			float cellX = x;
			fHeaderFields[i]->MoveTo(cellX, 0.0f);
			fHeaderFields[i]->ResizeTo(fieldWidth, kHeaderHeight);
			cellX += fieldWidth;

			fInsertButtons[i]->MoveTo(cellX, 0.0f);
			fInsertButtons[i]->ResizeTo(kInsertButtonWidth, kHeaderHeight);
			cellX += kInsertButtonWidth;

			fRemoveButtons[i]->MoveTo(cellX, 0.0f);
			fRemoveButtons[i]->ResizeTo(kRemoveButtonWidth, kHeaderHeight);
		}

		// A notes column's own TextDocumentView (NotesDisplayView) is
		// positioned/sized exactly like a Bible column's -- both are
		// TextDocumentViews wrapped in their own BScrollView, both track
		// a taller-than-viewport content region purely through their own
		// scrollbar's range rather than their own Frame() (see
		// TextDocumentView::_UpdateScrollBars(), which every FrameResized()/
		// SetTextDocument()/Relayout() already calls on itself -- no
		// manual scrollbar-range bookkeeping needed here for either kind
		// of column).
		TextDocumentView* view = isNotes
			? fNotesColumns[notesIndex].view : fTextViews[bibleIndex];
		if (view != NULL)
			view->Relayout();

		BScrollView* scroller = view != NULL
			? dynamic_cast<BScrollView*>(view->Parent()) : NULL;
		if (scroller != NULL) {
			scroller->MoveTo(x, 0.0f);
			scroller->ResizeTo(width, viewportHeight);
		}
		if (_IsChainRightmost((int32)i)) {
			BScrollBar* bar = scroller != NULL
				? scroller->ScrollBar(B_VERTICAL) : NULL;
			// Deliberately NOT sized here from _ChainContentHeight():
			// the scroller->ResizeTo() just above only QUEUES a
			// B_VIEW_RESIZED for the view inside it, so its
			// FrameResized() -- and with it the layout re-measurement at
			// the new width -- has not run yet. Measuring now reads the
			// PREVIOUS width's line breaks: confirmed live, a column
			// whose content really needed 1256px reported 596px here and
			// got a zero scroll range as a result. _ColumnResized()
			// applies the chain range once the resize has actually
			// landed.
			float rangeMin = 0.0f, rangeMax = 0.0f;
			if (bar != NULL)
				bar->GetRange(&rangeMin, &rangeMax);
			fprintf(stderr, "[SG] _PositionColumns %s pos %zu "
				"(chain-rightmost): width=%.1f viewportHeight=%.1f "
				"viewBounds=(%.1f,%.1f) bar=%p range=[%.1f,%.1f] "
				"barFrame=(%.1f,%.1f,%.1f,%.1f) barHidden=%d\n",
				isNotes ? "NOTES" : "BIBLE", i, width, viewportHeight,
				view != NULL ? view->Bounds().Width() : -999.0f,
				view != NULL ? view->Bounds().Height() : -999.0f,
				(void*)bar, rangeMin, rangeMax,
				bar != NULL ? bar->Frame().left : -999.0f,
				bar != NULL ? bar->Frame().top : -999.0f,
				bar != NULL ? bar->Frame().right : -999.0f,
				bar != NULL ? bar->Frame().bottom : -999.0f,
				bar != NULL ? (int)bar->IsHidden() : -1);
		}
		if (isNotes)
			notesIndex++;
		else
			bibleIndex++;

		x += width + kColumnSpacing;
		fColumnDividerX.push_back(x - kColumnSpacing / 2.0f);

		// Centered right on the divider line it toggles, between this
		// column's own cell and the next one's.
		if (i + 1 < fColumnOrder.size()) {
			fLinkButtons[i]->MoveTo(fColumnDividerX.back()
				- kLinkButtonWidth / 2.0f, 0.0f);
			fLinkButtons[i]->ResizeTo(kLinkButtonWidth, kHeaderHeight);
		}
	}

	// No trailing "+" reservation any more -- every column has its own
	// insert button in its own header cell now (see _RebuildHeader()),
	// so there's nothing left to reserve space for at the end.
	fContentWidth = fColumnOrder.empty() ? 0.0f : (x - kColumnSpacing);

	// fHeaderView's own Frame() is left to the window's layout (matching
	// the scroll viewport, same as this view's own Frame() is left to the
	// BScrollView that wraps it); its header cells, like this view's
	// column BScrollViews, are positioned above via MoveTo() up to
	// fContentWidth, which can exceed that Frame() -- Bounds()-origin
	// shifting (mirrored from this view's own scroll position in
	// ScrollTo()) is what reveals the overflow, not resizing the view.
	_UpdateScrollBars();

	// fColumnDividerX just changed; Draw() (which paints the divider
	// lines) needs to run again to reflect the new positions. fHeaderView
	// also needs a repaint of its own -- the active chain's own x-span
	// (and so its tint) may have just moved, and it's a different BView
	// with its own separate Invalidate().
	Invalidate();
	if (fHeaderView != NULL)
		fHeaderView->Invalidate();

	fprintf(stderr, "[SG-PERF] _PositionColumns elapsed=%.2fms\n",
		(system_time() - perfStart) / 1000.0);
}


// This view's own scrollbar is horizontal-only now (see the class
// comment) -- it is never itself the target of a vertical BScrollBar any
// more, so ScrollBar(B_VERTICAL) is always NULL here and there is
// nothing for this method to do on that axis (every column's own
// vertical bar is driven by its own TextDocumentView instead, see the
// class comment).
void
ParallelBibleView::_UpdateScrollBars()
{
	BScrollBar* horizontalScrollBar = ScrollBar(B_HORIZONTAL);
	if (horizontalScrollBar == NULL) {
		fprintf(stderr, "[SG] _UpdateScrollBars: NO horizontal bar "
			"attached at all (fContentWidth=%.1f Bounds().Width()=%.1f)\n",
			fContentWidth, Bounds().Width());
		return;
	}

	float viewWidth = Bounds().Width();
	float maxRange = fContentWidth - viewWidth;
	if (maxRange < 0.0f)
		maxRange = 0.0f;

	horizontalScrollBar->SetRange(0.0f, maxRange);
	horizontalScrollBar->SetProportion(
		fContentWidth > 0.0f ? viewWidth / fContentWidth : 1.0f);
	horizontalScrollBar->SetSteps(20.0f, viewWidth);

	float curMin, curMax;
	horizontalScrollBar->GetRange(&curMin, &curMax);
	fprintf(stderr, "[SG] _UpdateScrollBars (outer horizontal): "
		"fContentWidth=%.1f viewWidth=%.1f maxRange=%.1f "
		"barRangeAfterSet=[%.1f,%.1f] barValue=%.1f\n",
		fContentWidth, viewWidth, maxRange, curMin, curMax,
		horizontalScrollBar->Value());
}


// The height of the given verse's row, used by scroll-target calculation
// (_ChainVerseY(), for scrolling a chain to a specific verse) and the
// notes click-to-edit overlay's own positioning
// Reads it back from
// the FIRST column (Bible/Commentary or notes -- see _Realign(), they
// all share the same VerseAligner group) of the chain containing
// chainAnchorPosition that has this verse's own paragraph -- which,
// after Align() has run (2+ columns in that chain), already reflects
// the tallest column's height for that verse, exactly the row height
// every other column in the chain needs to match. Falls back to
// kHeaderHeight if nothing in the chain has this verse at all.
// Mirrors the technique VerseAligner itself uses: a standalone
// ParagraphLayout can measure a paragraph's height without a live
// TextDocumentView.
//
// ParagraphLayout::Height() only ever sums wrapped-line heights -- it has
// no idea about ParagraphStyle::SpacingBottom() at all, which is exactly
// the extra padding VerseAligner::SetVerseSpacing() adds to make a
// shorter column's verse match a taller one. Reporting Height() alone
// therefore under-counted every row VerseAligner had actually padded, by
// exactly that padding -- confirmed via two real screenshots in an
// earlier fix (drift between a notes column and two plain, unlinked
// Bible-text columns, where nothing about linked commentary entries was
// even in play). Adding the paragraph's own SpacingBottom back in gives
// the true on-screen row height.
float
ParallelBibleView::_RowHeight(int verse, int32 chainAnchorPosition) const
{
	int32 start = _ChainStart(chainAnchorPosition);
	int32 end = _ChainEnd(chainAnchorPosition);
	for (int32 i = start; i <= end; i++) {
		BibleTextDocument* document;
		if (fColumnOrder[i] == COLUMN_BIBLE)
			document = fDocuments[_BibleIndexForPosition(i)].Get();
		else
			document = fNotesColumns[_NotesIndexForPosition(i)]
				.document.Get();

		int32 index = document->ParagraphIndexForVerse(verse);
		if (index < 0)
			continue;

		const Paragraph& paragraph = document->ParagraphAtIndex(index);
		ParagraphLayout layout;
		// Same effective width the live TextDocumentView actually wraps
		// at (see _Realign()'s widths.push_back()) -- measuring at the
		// full column width here would under-count wrapped lines the
		// same way it did for VerseAligner's own planning.
		layout.SetWidth(_MeasurementWidth(i));
		layout.SetParagraph(paragraph);
		return layout.Height() + paragraph.Style().SpacingBottom();
	}
	return kHeaderHeight;
}


// Shared by every Bible/Commentary column -- the actual on-screen SLOT
// width (what _PositionColumns() resizes each column's own BScrollView
// to), same for every one of them regardless of which chain it's in or
// whether it's that chain's rightmost. Deliberately does NOT reserve
// any extra room for a chain-rightmost column's real BScrollBar -- a
// BScrollView already auto-narrows its OWN target to fit its scrollbar
// within whatever slot width it's given (same mechanism that makes
// TextDocumentView work at all as a direct BScrollView target, see the
// class comment), so reserving that space a second time here left an
// unclaimed gap the width of one scrollbar per chain (confirmed via a
// live test: a visible gray strip next to the real scrollbar that
// nothing actually used). See _MeasurementWidth() for the narrower
// value VerseAligner/_RowHeight() need instead.
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

	int32 notesCount = (int32)fNotesColumns.size();
	int32 columnCount = bibleCount + notesCount;
	float notesWidth = notesCount > 0
		? notesCount * _NotesColumnWidth() : 0.0f;

	// Equal share of what's left after every notes column (each
	// separately capped, see _NotesColumnWidth()). No separate
	// reservation for the "+" button any more -- every column has its
	// own insert button inside its own header cell now (see
	// _RebuildHeader()/_PositionColumns()), not a floating extra slot at
	// the end. If columns still don't all fit, _PositionColumns()'s
	// resulting fContentWidth ends up wider than this view's own
	// Bounds(), and the horizontal scrollbar (see _UpdateScrollBars())
	// is how the overflow columns stay reachable instead of just
	// running off-screen.
	float available = totalWidth - kColumnSpacing * (columnCount - 1)
		- notesWidth;
	float width = available / bibleCount;
	if (width < kMinColumnWidth)
		width = kMinColumnWidth;
	return width;
}


float
ParallelBibleView::_MeasurementWidth(int32 position) const
{
	bool isNotes = position >= 0 && (size_t)position < fColumnOrder.size()
		&& fColumnOrder[position] == COLUMN_NOTES;
	float width = (isNotes ? _NotesColumnWidth() : _ColumnWidth())
		- 2.0f * kBibleColumnInset;
	// The notes column's own verse-number gutter (see NotesDisplayView::
	// Draw()) eats into its left inset -- must match the SetInsets()
	// call in _RebuildLayout() or VerseAligner/wrapping plans for more
	// horizontal room than the live view actually wraps at (see this
	// method's own class-level doc comment).
	if (isNotes)
		width -= kNoteGutterWidth;
	if (_IsChainRightmost(position))
		width -= B_V_SCROLL_BAR_WIDTH;
	if (width < 0.0f)
		width = 0.0f;
	return width;
}


// Before the user has ever dragged the splitter (see #19),
// fNotesWidthFraction is -1 and a notes column never claims more than
// kMaxNotesWidthFraction of the total width -- the equal-share split let
// it eat half the window with just one Bible column open. Once dragged,
// fNotesWidthFraction takes over completely (still floor-clamped to
// kMinColumnWidth), applied uniformly to every notes column (see the
// class comment) -- and stays in effect across resizes/column changes
// rather than resetting to the 1/3 cap on every _PositionColumns() pass,
// since it's a fraction of totalWidth applied fresh each call, not a
// one-time pixel snapshot. Same SLOT-width-only rationale as
// _ColumnWidth() -- see there for why no chain-rightmost scrollbar
// adjustment happens here either; _PositionColumns() applies it locally
// to the note fields' own content width instead.
float
ParallelBibleView::_NotesColumnWidth() const
{
	if (fNotesColumns.empty())
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
		int32 columnCount = (int32)fModules.size()
			+ (int32)fNotesColumns.size();
		float naturalShare
			= (totalWidth - kColumnSpacing * (columnCount - 1)) / columnCount;
		width = std::min(naturalShare, totalWidth * kMaxNotesWidthFraction);
	}

	if (width < kMinColumnWidth)
		width = kMinColumnWidth;
	return width;
}


// Content-space x of the divider immediately before the first/leftmost
// notes column -- the only one that's ever draggable (see #19) -- or a
// negative value if there's no notes column, or nothing to its left to
// negotiate width with (fColumnDividerX has no earlier entry to use).
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
// plus a "Notes" entry so any column can be turned into a notes column
// from its own dropdown -- any number of columns can be one at once (see
// the class comment), so unlike the module list, "Notes" is always
// offered here, not gated on any existing notes column. columnIndex is
// embedded in each item's message so MessageReceived() knows which slot
// is affected -- forInsert false: replace what that slot shows (a
// column's own header dropdown), and markedModuleName/markNotes are
// checked off to show that slot's current selection; forInsert true:
// insert a brand-new column right after columnIndex instead (that
// column's own "+" button, see InsertColumn()/InsertNotesColumn()),
// posting PARALLEL_INSERT_MODULE/PARALLEL_INSERT_NOTES rather than
// PARALLEL_SELECT_MODULE/PARALLEL_SELECT_NOTES -- there's never a
// "current selection" to mark in that case, so callers pass NULL/false
// for markedModuleName/markNotes together with forInsert.
//
// Split out from _BuildModuleMenu() (below) so _RebuildHeader() can
// repopulate an *existing* column's menu in place instead of building a
// fresh one -- see there for why: deleting the BMenuField that wraps it
// is the risky part, not deleting/rebuilding what's inside it.
void
ParallelBibleView::_PopulateModuleMenu(BMenu* menu, int32 columnIndex,
	const char* markedModuleName, bool markNotes, bool forInsert)
{
	BMenu* bibleMenu = new BMenu(B_TRANSLATE("Biblical Texts"));
	BMenu* commentaryMenu = new BMenu(B_TRANSLATE("Commentaries"));

	uint32 moduleMessageWhat
		= forInsert ? PARALLEL_INSERT_MODULE : PARALLEL_SELECT_MODULE;
	uint32 notesMessageWhat
		= forInsert ? PARALLEL_INSERT_NOTES : PARALLEL_SELECT_NOTES;

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

		BMessage* message = new BMessage(moduleMessageWhat);
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

	BMessage* notesMessage = new BMessage(notesMessageWhat);
	notesMessage->AddInt32("index", columnIndex);
	BMenuItem* notesItem = new BMenuItem(B_TRANSLATE("Notes"),
		notesMessage);
	notesItem->SetTarget(this);
	notesItem->SetMarked(markNotes);
	menu->AddItem(notesItem);
}


BPopUpMenu*
ParallelBibleView::_BuildModuleMenu(int32 columnIndex,
	const char* markedModuleName, bool markNotes, bool forInsert)
{
	if (fManager == NULL)
		return NULL;

	// radioMode so BMenu itself keeps exactly one item marked as the user
	// picks different translations; labelFromMarked so the field displays
	// that marked item's label instead of this constructor's own `name`.
	BPopUpMenu* menu = new BPopUpMenu("translation", true, true);
	_PopulateModuleMenu(menu, columnIndex, markedModuleName, markNotes,
		forInsert);
	return menu;
}

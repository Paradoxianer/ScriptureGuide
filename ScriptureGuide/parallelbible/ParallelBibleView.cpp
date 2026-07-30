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
#include <ScrollView.h>
#include <StringView.h>
#include <TextView.h>
#include <Window.h>

#include <versekey.h>

#include "NoteFieldView.h"
#include "ParagraphLayout.h"
#include "ParallelColumnGroup.h"
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
// Wide enough to fit the link-toggle button (see SetColumnsLinked()) that
// now lives centered in this gap, right on the divider line it toggles --
// widened from the original 8.0f for that reason. Shared by every column
// gap (header AND content, group 0 or not -- see ParallelColumnGroup's
// own identical constant), so gaps stay visually consistent everywhere,
// not just where a link button happens to render.
const float ParallelBibleView::kColumnSpacing = 24.0f;
const float ParallelBibleView::kHeaderHeight = 24.0f;
const float ParallelBibleView::kHeaderBottomGap = 2.0f;
const float ParallelBibleView::kRemoveButtonWidth = 20.0f;
const float ParallelBibleView::kInsertButtonWidth = 20.0f;
const float ParallelBibleView::kLinkButtonWidth = 20.0f;
const float ParallelBibleView::kMaxNotesWidthFraction = 1.0f / 3.0f;
const float ParallelBibleView::kNoteVerseLabelWidth = 20.0f;
const float ParallelBibleView::kBibleColumnInset = 4.0f;

// Header-cell link-toggle glyphs (see SetColumnsLinked()) -- linked/broken
// chain link. Still needs a visual check on the Haiku VM (dev.sh shot) --
// swap for plain text ("L"/"B") if the default UI font doesn't render
// these two legibly.
static const char* const kLinkedGlyph = "\xF0\x9F\x94\x97";
static const char* const kBrokenLinkGlyph = "\xE2\x9B\x93";


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
		fTrackingForDrag(false)
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
		// Any click into this column -- even one that turns out to just
		// place the caret -- makes its group the active one (see
		// ParallelBibleView::SetActiveGroup(), issue #12), same as
		// focusing a notes field does (see NoteFieldView::MakeFocus()).
		// This is what lets the one existing book/chapter/verse toolbar
		// drive whichever group the user is currently working in.
		if (fOwner != NULL) {
			int32 group = fOwner->_GroupForColumnView(this);
			if (group >= 0)
				fOwner->SetActiveGroup(group);
		}

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
		if (fTrackingForDrag) {
			// Pressed inside the selection, released again without
			// moving far enough to start a drag -- a plain click after
			// all, meant to move the caret there like normal.
			fTrackingForDrag = false;
			SetCaret(where, false);
		} else if (fOwner != NULL) {
			fOwner->_ColumnSelectionEnded();
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

		// Make the column the reference actually landed on the active
		// group (see ParallelBibleView::SetActiveGroup(), issue #12)
		// *before* posting the jump below -- JumpToKey() ultimately calls
		// fParallelView->SetKey(), which acts on whichever group is
		// active, so this is what makes a reference dropped onto a
		// detached column navigate just that column's group instead of
		// group 0.
		int32 group = fOwner->_GroupForColumnView(this);
		if (group >= 0)
			fOwner->SetActiveGroup(group);

		// Post to the window rather than call fOwner->SetKey() directly
		// -- SGMainWindow keeps separate book-menu/chapter/verse UI state
		// in sync with the current key exclusively through its own
		// JumpToKey(), which SG_BIBLE routes to (see SGMainWindow::
		// MessageReceived()) -- the same path the universal search box
		// and search results already use, so a dropped reference stays
		// consistent with every other way to navigate instead of
		// updating the reading pane while leaving the book/chapter/verse
		// fields showing the old position.
		BWindow* window = Window();
		if (window == NULL)
			return false;

		BMessage jump(SG_BIBLE);
		jump.AddString("key", reference);
		window->PostMessage(&jump);
		return true;
	}

private:
	BibleTextDocument*	fBibleDocument;
	BString				fTranslationName;
	ParallelBibleView*	fOwner;
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

	// A thin line along the bottom edge, the same style as the vertical
	// column dividers in ParallelBibleView::Draw() -- without it the
	// toolbar/header area blends directly into the reading pane below
	// with no clean edge between them. Also draws a "docking" cue over
	// the active group's own header cells when it isn't group 0 (see
	// ParallelBibleView::SetActiveGroup(), issue #12) -- a colored
	// underline plus two curves sweeping up from both ends of that
	// group's header cells to meet at this view's own top edge, right
	// where LogosMainWindow's book/chapter/verse toolbar sits just
	// above (a sibling view, not a child -- see the class comment on
	// HeaderView(), so drawing has to stay within this view's own
	// Bounds(), which is exactly what "converging at y=0" achieves).
	virtual void Draw(BRect updateRect)
	{
		BView::Draw(updateRect);

		BRect bounds = Bounds();
		SetHighColor(0, 0, 0);
		StrokeLine(BPoint(bounds.left, bounds.bottom),
			BPoint(bounds.right, bounds.bottom));

		if (fOwner == NULL || fOwner->ActiveGroup() == 0)
			return;

		float left = fOwner->fActiveGroupHeaderLeft;
		float right = fOwner->fActiveGroupHeaderRight;
		if (right <= left)
			return;

		rgb_color highlight = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
		SetHighColor(highlight);
		SetPenSize(2.0f);

		// The visible, unmissable part -- a colored underline right
		// under the active group's own header cells.
		float cellBottom = ParallelBibleView::kHeaderHeight;
		StrokeLine(BPoint(left, cellBottom), BPoint(right, cellBottom));

		// The decorative part -- both ends sweep up and inward to meet
		// at this view's top edge, reading as "this group reaches up
		// toward the toolbar above it."
		float midX = (left + right) / 2.0f;
		BPoint leftCurve[4] = {
			BPoint(left, cellBottom),
			BPoint(left, cellBottom * 0.4f),
			BPoint(midX - 6.0f, 0.0f),
			BPoint(midX, 0.0f)
		};
		BPoint rightCurve[4] = {
			BPoint(right, cellBottom),
			BPoint(right, cellBottom * 0.4f),
			BPoint(midX + 6.0f, 0.0f),
			BPoint(midX, 0.0f)
		};
		StrokeBezier(leftCurve);
		StrokeBezier(rightCurve);

		SetPenSize(1.0f);
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
	fNotesModule(NULL),
	fNextGroupId(1),
	fActiveGroup(0),
	fActiveGroupHeaderLeft(0.0f),
	fActiveGroupHeaderRight(0.0f),
	fHeaderView(NULL),
	fAddColumnButton(NULL),
	fActiveSelectionColumn(NULL),
	fSelectionLastEndVerse(-1),
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
	// Order matters: each group's BScrollView owns (and will cascade-
	// delete, per ~BView() -- confirmed via the Haiku Book: "Deletes the
	// view and all children") its ParallelColumnGroup, whose own
	// destructor detaches (RemoveChild(), not delete -- see
	// ParallelColumnGroup.cpp) the Bible TextDocumentViews it doesn't
	// own. Deleting every scroller first, before touching fTextViews
	// below, is what leaves each of those (group 0 or not) in the same
	// state: unparented but not yet deleted, ready for the plain
	// RemoveSelf()+delete every other teardown in this file already uses
	// (RemoveSelf() on an already-unparented view is a safe no-op).
	for (size_t i = 0; i < fGroupRuns.size(); i++)
		delete fGroupRuns[i].scroller;
	fGroupRuns.clear();

	for (size_t i = 0; i < fTextViews.size(); i++) {
		fTextViews[i]->RemoveSelf();
		delete fTextViews[i];
	}

	delete fNotesModule;

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

	// Every detached group's scroller needs to keep filling the current
	// viewport height -- see ScrollTo() below for why its Y position is
	// counter-scrolled the same way; its height has to track a real
	// resize the same way for the same reason (it isn't part of the
	// shared fContentHeight/_UpdateScrollBars() machinery that normally
	// handles this).
	for (size_t i = 0; i < fGroupRuns.size(); i++) {
		BScrollView* scroller = fGroupRuns[i].scroller;
		scroller->ResizeTo(scroller->Frame().Width(), Bounds().Height());
	}
}


// The one hook every scroll path (drag, mouse wheel, a scrollbar's
// programmatic SetValue()) funnels through -- see the class comment.
// Mirrors only the horizontal component onto the header, which has no
// vertical scroll position of its own.
//
// Every detached group's own BScrollView is still a plain child of this
// view (see fGroupRuns/_RebuildLayout()), so without the loop below it
// would scroll along with the shared vertical BScrollBar just like a
// group-0 column does -- exactly what a detached group must NOT do, since
// it has its own independent scroll position via its own inner
// BScrollView. Giving it a Y position that always equals the new scroll
// offset exactly cancels this view's own coordinate shift out, so it stays
// visually pinned to the top of the viewport regardless of how far group
// 0 has scrolled -- the same counter-scrolling trick this method already
// uses for fHeaderView above, just on the vertical axis and applied
// internally instead of by an external caller.
void
ParallelBibleView::ScrollTo(BPoint where)
{
	BView::ScrollTo(where);
	fHeaderView->ScrollTo(where.x, 0.0f);

	for (size_t i = 0; i < fGroupRuns.size(); i++) {
		BScrollView* scroller = fGroupRuns[i].scroller;
		scroller->MoveTo(scroller->Frame().left, where.y);
	}
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
		case PARALLEL_TOGGLE_LINK:
		case PARALLEL_INSERT_MODULE:
		case PARALLEL_INSERT_NOTES:
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

			int32 index;
			switch (message->what) {
				case PARALLEL_SELECT_MODULE:
				{
					BString module;
					if (message->FindInt32("index", &index) == B_OK
						&& message->FindString("module", &module) == B_OK) {
						_SetColumnToBible(index, module.String());
					}
					break;
				}
				case PARALLEL_SELECT_NOTES:
					if (message->FindInt32("index", &index) == B_OK)
						_SetColumnToNotes(index);
					break;
				case PARALLEL_REMOVE_COLUMN:
					if (message->FindInt32("index", &index) == B_OK)
						RemoveColumn(index);
					break;
				case PARALLEL_INSERT_MODULE:
				{
					BString module;
					if (message->FindInt32("index", &index) == B_OK
						&& message->FindString("module", &module) == B_OK) {
						InsertColumn(index, module.String());
					}
					break;
				}
				case PARALLEL_INSERT_NOTES:
					if (message->FindInt32("index", &index) == B_OK)
						InsertNotesColumn(index);
					break;
				default:
					// PARALLEL_TOGGLE_LINK -- "index" is the gap position
					// (see the class comment on SetColumnsLinked()), not
					// a fColumnOrder slot index like the others above.
					if (message->FindInt32("index", &index) == B_OK) {
						SetColumnsLinked(index,
							!AreColumnsLinked(index));
					}
					break;
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

		case PARALLEL_INSERT_COLUMN_MENU:
		{
			// "index" here is the position whose own "+" was clicked --
			// what InsertColumn()/InsertNotesColumn() call afterPosition
			// (see the class comment on SetColumnsLinked()/InsertColumn()).
			int32 afterPosition;
			if (message->FindInt32("index", &afterPosition) != B_OK)
				break;

			BPopUpMenu* menu = _BuildModuleMenu(afterPosition, NULL, false,
				true);
			if (menu != NULL) {
				menu->SetAsyncAutoDestruct(true);
				BPoint where(0.0f, 0.0f);
				if (afterPosition >= 0
					&& (size_t)afterPosition < fInsertButtons.size()) {
					where = fInsertButtons[afterPosition]->Frame()
						.LeftBottom();
				}
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
		int32 group = fColumnGroup[bibleIndex];
		fModules.erase(fModules.begin() + bibleIndex);
		fDocuments.erase(fDocuments.begin() + bibleIndex);
		fColumnGroup.erase(fColumnGroup.begin() + bibleIndex);

		// If that was `group`'s last Bible member, any notes column(s)
		// still claiming it (see InsertNotesColumn()) are orphaned --
		// fall them back to group 0 rather than leaving them pointing at
		// a group with nothing left to align against or navigate.
		if (group != 0) {
			bool groupStillHasBibleMember = false;
			for (size_t i = 0; i < fColumnGroup.size(); i++) {
				if (fColumnGroup[i] == group) {
					groupStillHasBibleMember = true;
					break;
				}
			}
			if (!groupStillHasBibleMember) {
				for (size_t i = 0; i < fNotesColumns.size(); i++) {
					if (fNotesColumns[i].group == group)
						fNotesColumns[i].group = 0;
				}
				if (fActiveGroup == group)
					SetActiveGroup(0);
			}
		}
	} else {
		int32 notesIndex = _NotesIndexForPosition(position);
		fNotesColumns.erase(fNotesColumns.begin() + notesIndex);
		if (fNotesColumns.empty()) {
			delete fNotesModule;
			fNotesModule = NULL;
		}
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

	// Always group 0 -- appending via _SetColumnToNotes(-1) never joins
	// a detached group (only InsertNotesColumn() does that).
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


// Same idea as _BibleIndexForPosition(), for the COLUMN_NOTES slots
// (fNotesColumns) -- more than one can exist now, one per group that has
// its own paired notes column.
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


// The position of the group-0 notes column specifically, or -1 -- what
// SetNotesEnabled()/NotesEnabled() and the global "Notes" button in
// LogosMainWindow mean by "the" notes column; a detached group's own
// notes column doesn't count here.
int32
ParallelBibleView::_NotesPosition() const
{
	int32 notesIndex = 0;
	for (size_t i = 0; i < fColumnOrder.size(); i++) {
		if (fColumnOrder[i] != COLUMN_NOTES)
			continue;
		if (fNotesColumns[notesIndex].group == 0)
			return (int32)i;
		notesIndex++;
	}
	return -1;
}


// The group whatever currently sits at `position` belongs to, or 0 if
// `position` isn't a valid existing slot -- see the class comment on
// InsertColumn()/SetColumnsLinked().
int32
ParallelBibleView::_GroupForPosition(int32 position) const
{
	if (position < 0 || (size_t)position >= fColumnOrder.size())
		return 0;
	if (fColumnOrder[position] == COLUMN_BIBLE)
		return fColumnGroup[_BibleIndexForPosition(position)];
	return fNotesColumns[_NotesIndexForPosition(position)].group;
}


ParallelBibleView::ColumnGroupRun*
ParallelBibleView::_RunForGroup(int32 group)
{
	for (size_t i = 0; i < fGroupRuns.size(); i++) {
		if (fGroupRuns[i].group == group)
			return &fGroupRuns[i];
	}
	return NULL;
}


int32
ParallelBibleView::_GroupForColumnView(BibleColumnView* view) const
{
	for (size_t i = 0; i < fTextViews.size(); i++) {
		if (fTextViews[i] == view)
			return fColumnGroup[i];
	}
	return -1;
}


void
ParallelBibleView::SetActiveGroup(int32 group)
{
	if (fActiveGroup == group)
		return;
	fActiveGroup = group;
	if (fHeaderView != NULL)
		fHeaderView->Invalidate();
}


// Breaks or restores the link between fColumnOrder[position] and
// fColumnOrder[position + 1] -- see the class comment.
status_t
ParallelBibleView::SetColumnsLinked(int32 position, bool linked)
{
	if (position < 0 || (size_t)(position + 1) >= fColumnOrder.size())
		return B_BAD_INDEX;

	int32 leftGroup = _GroupForPosition(position);
	int32 rightGroup = _GroupForPosition(position + 1);

	if (linked == (leftGroup == rightGroup))
		return B_OK;

	// Either direction only ever touches the CONTIGUOUS run starting at
	// position + 1 that currently shares rightGroup -- walking outward
	// from there, not "every column anywhere that happens to be in
	// rightGroup". That distinction matters specifically for group 0:
	// it isn't one single run, it's whatever every column defaults to
	// until explicitly split off, so several disconnected runs can all
	// be "group 0" at once (e.g. a column spliced in on the far side of
	// an already-detached group). Sweeping the whole array by group id
	// would incorrectly reassign every one of those, including ones on
	// the *left* of `position` when rightGroup == leftGroup == 0, not
	// just the intended right-hand run.
	int32 targetGroup = linked ? leftGroup : fNextGroupId++;
	for (size_t p = (size_t)(position + 1);
			p < fColumnOrder.size() && _GroupForPosition((int32)p) == rightGroup;
			p++) {
		if (fColumnOrder[p] == COLUMN_BIBLE)
			fColumnGroup[_BibleIndexForPosition((int32)p)] = targetGroup;
		else
			fNotesColumns[_NotesIndexForPosition((int32)p)].group = targetGroup;
	}

	if (!linked)
		SetActiveGroup(targetGroup);
	else if (fActiveGroup == rightGroup)
		SetActiveGroup(leftGroup);

	_RebuildLayout();
	return B_OK;
}


bool
ParallelBibleView::AreColumnsLinked(int32 position) const
{
	if (position < 0 || (size_t)(position + 1) >= fColumnOrder.size())
		return true;
	return _GroupForPosition(position) == _GroupForPosition(position + 1);
}


// Inserts a brand-new Bible/Commentary column right after afterPosition,
// inheriting its group -- see the class comment. Unlike _SetColumnToBible(),
// this never replaces an existing slot.
status_t
ParallelBibleView::InsertColumn(int32 afterPosition, const char* moduleName)
{
	if (fManager == NULL)
		return B_NO_INIT;
	if (afterPosition >= (int32)fColumnOrder.size())
		return B_BAD_INDEX;

	SWModule* module = fManager->getModule(moduleName);
	if (module == NULL)
		return B_NAME_NOT_FOUND;

	int32 group = _GroupForPosition(afterPosition);

	// A new member of an already-open detached group starts on that
	// group's own current chapter, not fCurrentKey (which only tracks
	// group 0) -- see ParallelColumnGroup::CurrentKey().
	BString seedKey = fCurrentKey;
	if (group != 0) {
		ColumnGroupRun* run = _RunForGroup(group);
		if (run != NULL)
			seedKey = run->view->CurrentKey();
	}

	if (!seedKey.IsEmpty()) {
		// See the identical locale-ordering comment in
		// _SetColumnToBible() -- same reasoning applies here.
		VerseKey verseKey;
		SetVerseKeyLocale(verseKey);
		verseKey.setText(seedKey.String());
		module->setKey(verseKey);
	}

	BReference<BibleTextDocument> document(new BibleTextDocument(module),
		true);
	document->SetShowVerseNumbers(fShowVerseNumbers);
	document->SetBaseFont(fBaseFont);

	int32 insertPosition = afterPosition + 1;
	int32 bibleIndex = _BibleIndexForPosition(insertPosition);
	fModules.insert(fModules.begin() + bibleIndex, module);
	fDocuments.insert(fDocuments.begin() + bibleIndex, document);
	fColumnGroup.insert(fColumnGroup.begin() + bibleIndex, group);
	fColumnOrder.insert(fColumnOrder.begin() + insertPosition, COLUMN_BIBLE);

	if (group != 0)
		SetActiveGroup(group);

	_RebuildLayout();
	return B_OK;
}


// Same idea as InsertColumn(), for a brand-new notes column -- one per
// group, same restriction _SetColumnToNotes() already enforces.
status_t
ParallelBibleView::InsertNotesColumn(int32 afterPosition)
{
	if (afterPosition >= (int32)fColumnOrder.size())
		return B_BAD_INDEX;

	int32 group = _GroupForPosition(afterPosition);
	for (size_t i = 0; i < fNotesColumns.size(); i++) {
		if (fNotesColumns[i].group == group)
			return B_NOT_ALLOWED;
	}

	if (fNotesModule == NULL) {
		fNotesModule = new PersonalNotesModule();
		status_t status = fNotesModule->Open();
		if (status != B_OK) {
			delete fNotesModule;
			fNotesModule = NULL;
			return status;
		}
	}

	NotesColumnState state;
	state.group = group;

	int32 insertPosition = afterPosition + 1;
	int32 notesIndex = _NotesIndexForPosition(insertPosition);
	fNotesColumns.insert(fNotesColumns.begin() + notesIndex, state);
	fColumnOrder.insert(fColumnOrder.begin() + insertPosition, COLUMN_NOTES);

	if (group != 0)
		SetActiveGroup(group);

	_RebuildLayout();
	return B_OK;
}


// Makes the column at `position` a Bible/Commentary column showing
// `moduleName`, or appends a new one at the end if position < 0 (always
// group 0 -- this is AddColumn(), not InsertColumn()). If that slot was a
// notes column, this converts it in place, keeping its own group (see
// _GroupForPosition()) -- changing what a slot shows doesn't change which
// group it's in.
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
		// above). Deliberately default-constructed rather than seeded via
		// the VerseKey(const char*) constructor -- that form parses its
		// argument immediately, before SetVerseKeyLocale() below ever
		// runs, so if the module's own current key text (getKeyText())
		// already happens to be localized (e.g. this module was
		// previously navigated in another column sharing the same
		// fManager), that parse fails at the still-default locale and
		// left the key -- and every setText() call after it, including
		// the one actually meant to apply fCurrentKey -- stuck on
		// SWORD's failed-parse fallback of "Revelation of John" 1:1
		// instead of matching the other columns (confirmed empirically:
		// this is exactly the bug already root-caused and fixed in
		// BibleTextDocument::SetKey() -- same anti-pattern, different
		// call site).
		VerseKey verseKey;
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
		fColumnGroup.push_back(0);
		fColumnOrder.push_back(COLUMN_BIBLE);
	} else if ((size_t)position >= fColumnOrder.size()) {
		return B_BAD_INDEX;
	} else if (fColumnOrder[position] == COLUMN_BIBLE) {
		int32 bibleIndex = _BibleIndexForPosition(position);
		fModules[bibleIndex] = module;
		fDocuments[bibleIndex] = document;
	} else {
		int32 group = _GroupForPosition(position);
		int32 notesIndex = _NotesIndexForPosition(position);
		fNotesColumns.erase(fNotesColumns.begin() + notesIndex);
		if (fNotesColumns.empty()) {
			delete fNotesModule;
			fNotesModule = NULL;
		}

		int32 bibleIndex = _BibleIndexForPosition(position);
		fModules.insert(fModules.begin() + bibleIndex, module);
		fDocuments.insert(fDocuments.begin() + bibleIndex, document);
		fColumnGroup.insert(fColumnGroup.begin() + bibleIndex, group);
		fColumnOrder[position] = COLUMN_BIBLE;
	}

	_RebuildLayout();
	return B_OK;
}


// Makes the column at `position` a notes column, or appends a new one at
// the end if position < 0 (always group 0 -- see InsertNotesColumn() for
// joining a detached group). Only one notes column can exist per group
// (they all share the same fNotesModule) -- if that group already has one,
// this fails rather than moving it (the per-column dropdown only offers
// "Notes" when it would succeed, see _BuildModuleMenu()). If that slot was
// a Bible/Commentary column, this converts it in place, keeping its own
// group -- same reasoning as _SetColumnToBible().
status_t
ParallelBibleView::_SetColumnToNotes(int32 position)
{
	if (position >= 0 && (size_t)position < fColumnOrder.size()
			&& fColumnOrder[position] == COLUMN_NOTES) {
		return B_OK; // already a notes column -- nothing to do
	}

	int32 group = _GroupForPosition(position);
	for (size_t i = 0; i < fNotesColumns.size(); i++) {
		if (fNotesColumns[i].group == group)
			return B_NOT_ALLOWED;
	}

	if (fNotesModule == NULL) {
		fNotesModule = new PersonalNotesModule();
		status_t status = fNotesModule->Open();
		if (status != B_OK) {
			delete fNotesModule;
			fNotesModule = NULL;
			return status;
		}
	}

	NotesColumnState state;
	state.group = group;

	if (position < 0) {
		fColumnOrder.push_back(COLUMN_NOTES);
		fNotesColumns.push_back(state);
	} else if ((size_t)position >= fColumnOrder.size()) {
		if (fNotesColumns.empty()) {
			delete fNotesModule;
			fNotesModule = NULL;
		}
		return B_BAD_INDEX;
	} else {
		int32 bibleIndex = _BibleIndexForPosition(position);
		fModules.erase(fModules.begin() + bibleIndex);
		fDocuments.erase(fDocuments.begin() + bibleIndex);
		fColumnGroup.erase(fColumnGroup.begin() + bibleIndex);
		fColumnOrder[position] = COLUMN_NOTES;
		int32 notesIndex = _NotesIndexForPosition(position);
		fNotesColumns.insert(fNotesColumns.begin() + notesIndex, state);
	}

	_RebuildLayout();
	return B_OK;
}


// Acts on whichever group is currently active (see ActiveGroup()) --
// group 0 unless a detached column, its notes column, or a reference drop
// made a different group active more recently (see the class comment).
status_t
ParallelBibleView::SetKey(const char* key)
{
	if (fActiveGroup != 0) {
		ColumnGroupRun* run = _RunForGroup(fActiveGroup);
		if (run != NULL)
			return run->view->SetKey(key);
		return B_OK;
	}

	fCurrentKey = key;

	for (size_t i = 0; i < fDocuments.size(); i++) {
		if (fColumnGroup[i] == 0)
			fDocuments[i]->SetKey(key);
	}

	// A selection's text offsets are only meaningful for the chapter they
	// were made in -- SetKey() rebuilds every column's document out from
	// under it (see BibleTextDocument::_Rebuild()), so a leftover
	// selection either highlights unrelated text at the same offsets in
	// the new chapter or, once the new text is shorter, points past the
	// end of it entirely. Every navigation path (book/chapter/verse
	// fields, the universal search box, a dropped reference) funnels
	// through here, so clearing it in this one place covers all of them.
	for (size_t i = 0; i < fTextViews.size(); i++) {
		if (fColumnGroup[i] == 0)
			fTextViews[i]->SetSelection(0, 0);
	}

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
	if (fActiveGroup != 0) {
		ColumnGroupRun* run = _RunForGroup(fActiveGroup);
		if (run != NULL)
			run->view->HighlightVerse(startVerse, endVerse);
		return;
	}

	for (size_t i = 0; i < fDocuments.size(); i++) {
		if (fColumnGroup[i] != 0)
			continue;
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


// Not currently called by LogosMainWindow.cpp or any test (confirmed dead
// code), but kept "active group"-aware for consistency with SetKey()/
// HighlightVerse() -- a detached group has no NextChapter()/PrevChapter()
// of its own on ParallelColumnGroup (nothing needs it yet); if that
// changes, this is a no-op for now rather than acting on the wrong group.
status_t
ParallelBibleView::NextChapter()
{
	if (fActiveGroup != 0)
		return B_OK;

	for (size_t i = 0; i < fDocuments.size(); i++) {
		if (fColumnGroup[i] == 0)
			fDocuments[i]->NextChapter();
	}

	for (size_t i = 0; i < fDocuments.size(); i++) {
		if (fColumnGroup[i] == 0) {
			fCurrentKey = fDocuments[i]->Key();
			break;
		}
	}

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
	if (fActiveGroup != 0)
		return B_OK;

	for (size_t i = 0; i < fDocuments.size(); i++) {
		if (fColumnGroup[i] == 0)
			fDocuments[i]->PrevChapter();
	}

	for (size_t i = 0; i < fDocuments.size(); i++) {
		if (fColumnGroup[i] == 0) {
			fCurrentKey = fDocuments[i]->Key();
			break;
		}
	}

	_RebuildNoteFields();
	_Realign();
	ScrollTo(Bounds().left, 0.0f);
	return B_OK;
}


void
ParallelBibleView::_RebuildLayout()
{
	// Tear down every currently-open detached group first -- deleting
	// each scroller cascades (per ~BView(), confirmed via the Haiku Book:
	// "Deletes the view and all children") to delete that group's
	// ParallelColumnGroup, whose own destructor detaches (RemoveChild(),
	// not delete -- see ParallelColumnGroup.cpp) the Bible
	// TextDocumentViews it doesn't own. That leaves every TextDocumentView
	// (group 0 or not) in the same state below: unparented but not yet
	// deleted, ready for the plain RemoveSelf()+delete that already
	// worked here before groups existed.
	for (size_t i = 0; i < fGroupRuns.size(); i++)
		delete fGroupRuns[i].scroller;
	fGroupRuns.clear();

	for (size_t i = 0; i < fTextViews.size(); i++) {
		fTextViews[i]->RemoveSelf();
		delete fTextViews[i];
	}
	fTextViews.clear();

	// Build every Bible/Commentary TextDocumentView fresh, bucketing
	// contiguous runs of the same non-0 group into their own
	// ParallelColumnGroup + BScrollView (see the class comment) instead
	// of adding them directly -- a group-0 column keeps being added
	// directly, exactly as before groups existed.
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

		int32 group = fColumnGroup[i];
		if (group == 0) {
			AddChild(view);
		} else {
			ColumnGroupRun* run = _RunForGroup(group);
			if (run == NULL) {
				ParallelColumnGroup* groupView = new ParallelColumnGroup(
					"columnGroup", fNotesModule, this, group);
				ColumnGroupRun newRun;
				newRun.group = group;
				newRun.view = groupView;
				newRun.scroller = groupView->ScrollView();
				AddChild(newRun.scroller);
				fGroupRuns.push_back(newRun);
				run = &fGroupRuns.back();
			}
			run->view->AddBibleMember(view, fDocuments[i].Get());
		}

		fTextViews.push_back(view);
	}

	// Give each currently-open group's own notes column its member
	// fields too, if it has one -- group 0's own notes column stays
	// entirely this view's own responsibility (see _RebuildNoteFields()).
	for (size_t i = 0; i < fGroupRuns.size(); i++) {
		bool hasNotes = false;
		for (size_t n = 0; n < fNotesColumns.size(); n++) {
			if (fNotesColumns[n].group == fGroupRuns[i].group) {
				hasNotes = true;
				break;
			}
		}
		fGroupRuns[i].view->SetHasNotes(hasNotes);
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
		// Column count didn't change, but SetColumnsLinked() also comes
		// through here without one -- refresh every link button's own
		// linked/broken glyph in place, same reasoning as the dropdowns
		// just above.
		for (size_t i = 0; i < fLinkButtons.size(); i++) {
			fLinkButtons[i]->SetLabel(AreColumnsLinked((int32)i)
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
		// InsertColumn()/InsertNotesColumn()) -- distinct from the
		// existing trailing "+" (fAddColumnButton), which always appends
		// into group 0 regardless of which column, if any, was clicked.
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
		// (see _PositionColumns()) -- right on the seam it toggles,
		// which is what actually makes it read as "these two belong
		// together" at a glance rather than being just another button
		// floating inside one column's own cell.
		if (i + 1 < fColumnOrder.size()) {
			BMessage* linkMessage = new BMessage(PARALLEL_TOGGLE_LINK);
			linkMessage->AddInt32("index", (int32)i);
			BButton* linkButton = new BButton("linkColumns",
				AreColumnsLinked((int32)i) ? kLinkedGlyph : kBrokenLinkGlyph,
				linkMessage);
			linkButton->SetTarget(this);
			fHeaderView->AddChild(linkButton);
			fLinkButtons.push_back(linkButton);
		}
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
	// Only ever the group-0 entry (see _NotesPosition()) -- a detached
	// group's own notes column rebuilds its own fields internally (see
	// ParallelColumnGroup::SetKey()/_RebuildNoteFields()).
	NotesColumnState* state = NULL;
	for (size_t i = 0; i < fNotesColumns.size(); i++) {
		if (fNotesColumns[i].group == 0) {
			state = &fNotesColumns[i];
			break;
		}
	}

	if (state != NULL) {
		for (size_t i = 0; i < state->fields.size(); i++) {
			state->fields[i]->RemoveSelf();
			delete state->fields[i];
		}
		state->fields.clear();
		for (size_t i = 0; i < state->verseLabels.size(); i++) {
			state->verseLabels[i]->RemoveSelf();
			delete state->verseLabels[i];
		}
		state->verseLabels.clear();
	}

	if (state == NULL || fNotesModule == NULL || fCurrentKey.IsEmpty())
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
		state->verseLabels.push_back(label);

		NoteFieldView* field = new NoteFieldView(verse, fNotesModule,
			fCurrentKey, this, 0);
		field->SetText(fNotesModule->GetNote(verseKey.getText()).String());
		AddChild(field);
		state->fields.push_back(field);
	}
}


void
ParallelBibleView::_Realign()
{
	std::vector<BibleTextDocument*> columns;
	std::vector<float> widths;
	for (size_t i = 0; i < fDocuments.size(); i++) {
		if (fColumnGroup[i] != 0)
			continue;
		columns.push_back(fDocuments[i].Get());
		widths.push_back(_ColumnWidth());
	}

	if (columns.size() >= 2) {
		VerseAligner::Align(columns, widths);
	}

	// Every open detached group aligns/measures only itself (see the
	// class comment) -- same per-column widths every group-0 column also
	// gets, so a Bible/notes column looks the same size regardless of
	// which group it's in. Must run before _PositionColumns() below,
	// which reads each group's just-recomputed NaturalHeight().
	for (size_t i = 0; i < fGroupRuns.size(); i++)
		fGroupRuns[i].view->Relayout(_ColumnWidth(), _NotesColumnWidth());

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
// Positions and sizes every column view directly (MoveTo/ResizeTo) instead
// of delegating to a BGroupLayout -- see the header comment for why. A
// group-0 column gets its full natural content height (as reported by its
// own GetHeightForWidth()), which may well exceed this view's own Frame();
// that is fine, since this view's Bounds() acts as the scrolled viewport
// into that taller content, driven by _UpdateScrollBars() below, exactly
// the way TextDocumentView does for itself when it is the direct
// BScrollView target. A group-0 notes field's height comes from
// _RowHeight(), which reads back the (already-aligned, if there's more
// than one Bible column) height of that verse's paragraph in the first
// group-0 Bible column.
//
// A non-0 group's own content is NOT touched here at all -- that already
// happened inside its own ParallelColumnGroup::Relayout() (called from
// _Realign() just before this runs); this method only needs to know the
// x-span its whole run of columns collectively occupies, to position that
// group's single scroller as one unit once the run ends.
void
ParallelBibleView::_PositionColumns()
{
	float bibleWidth = _ColumnWidth();
	float notesWidth = _NotesColumnWidth();
	float x = 0.0f;
	float contentHeight = 0.0f;
	fColumnDividerX.clear();
	fActiveGroupHeaderLeft = 0.0f;
	fActiveGroupHeaderRight = 0.0f;

	size_t bibleIndex = 0;
	size_t notesIndex = 0;

	// The currently-open non-0 run, if any -- its total x-span is
	// accumulated across however many columns share it before its
	// scroller is positioned once, as a whole, when the run ends.
	int32 runGroup = 0;
	float runStartX = 0.0f;
	float runWidth = 0.0f;

	for (size_t i = 0; i < fColumnOrder.size(); i++) {
		bool isNotes = (fColumnOrder[i] == COLUMN_NOTES);
		float width = isNotes ? notesWidth : bibleWidth;
		int32 group = isNotes ? fNotesColumns[notesIndex].group
			: fColumnGroup[bibleIndex];
		bool hasLinkButton = (i + 1 < fColumnOrder.size());

		if (i < fHeaderFields.size()) {
			// Sized to its own natural (preferred) width -- like the
			// original toolbar's module field -- rather than stretched
			// to fill the column. A stretched fixed-size BMenuField
			// still draws its native pop-up marker at its own right
			// edge (see BMenuField/_BMCMenuBar_ in the Interface Kit;
			// nothing here draws it), which the buttons after it would
			// otherwise cover; bounding the field to its content and
			// placing the buttons right after it instead keeps that
			// marker visible.
			float preferredWidth, preferredHeight;
			fHeaderFields[i]->GetPreferredSize(&preferredWidth,
				&preferredHeight);
			// The link button (positioned below, once x has advanced
			// into the gap after this column) lives in the gap between
			// columns now, not in this column's own cell -- only the
			// insert/remove buttons are reserved for here.
			float reserved = kRemoveButtonWidth + kInsertButtonWidth;
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

			if (group != 0 && group == fActiveGroup) {
				if (fActiveGroupHeaderRight <= fActiveGroupHeaderLeft) {
					fActiveGroupHeaderLeft = x;
					fActiveGroupHeaderRight = x + width;
				} else {
					fActiveGroupHeaderLeft
						= std::min(fActiveGroupHeaderLeft, x);
					fActiveGroupHeaderRight
						= std::max(fActiveGroupHeaderRight, x + width);
				}
			}
		}

		if (group == 0) {
			// Flush a run that just ended (this slot broke it) before
			// handling this group-0 slot.
			if (runGroup != 0) {
				ColumnGroupRun* run = _RunForGroup(runGroup);
				if (run != NULL) {
					run->scroller->MoveTo(runStartX, Bounds().top);
					run->scroller->ResizeTo(runWidth,
						run->view->NaturalHeight());
				}
				runGroup = 0;
			}

			if (isNotes) {
				NotesColumnState& state = fNotesColumns[notesIndex];
				float fieldWidth = std::max(0.0f,
					width - kNoteVerseLabelWidth);
				float y = 0.0f;
				for (size_t v = 0; v < state.fields.size(); v++) {
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
					state.verseLabels[v]->GetFont(&font);
					font_height fontHeight;
					font.GetHeight(&fontHeight);
					float lineHeight = fontHeight.ascent + fontHeight.descent
						+ fontHeight.leading;

					state.verseLabels[v]->MoveTo(x, y + kBibleColumnInset);
					state.verseLabels[v]->ResizeTo(kNoteVerseLabelWidth,
						lineHeight);

					state.fields[v]->MoveTo(x + kNoteVerseLabelWidth, y);
					state.fields[v]->ResizeTo(fieldWidth, rowHeight);

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
			}
		} else if (runGroup != group) {
			runGroup = group;
			runStartX = x;
			runWidth = width;
		} else {
			runWidth += kColumnSpacing + width;
		}

		if (isNotes)
			notesIndex++;
		else
			bibleIndex++;

		x += width + kColumnSpacing;
		float dividerX = x - kColumnSpacing / 2.0f;
		fColumnDividerX.push_back(dividerX);

		// Centered right on the divider line it toggles, between this
		// column's own cell and the next one's -- see the class comment
		// on SetColumnsLinked() for why it belongs exactly here rather
		// than tucked inside either column's own cell.
		if (hasLinkButton) {
			fLinkButtons[i]->MoveTo(dividerX - kLinkButtonWidth / 2.0f,
				0.0f);
			fLinkButtons[i]->ResizeTo(kLinkButtonWidth, kHeaderHeight);
		}
	}

	// A run that reached the very end of fColumnOrder never got flushed
	// by the "this slot broke it" branch above.
	if (runGroup != 0) {
		ColumnGroupRun* run = _RunForGroup(runGroup);
		if (run != NULL) {
			run->scroller->MoveTo(runStartX, Bounds().top);
			run->scroller->ResizeTo(runWidth, run->view->NaturalHeight());
		}
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
	// lines) needs to run again to reflect the new positions. fHeaderView
	// also needs a repaint of its own -- a group's x-span (and so its
	// docking cue) may have just moved, and it's a different BView with
	// its own separate Invalidate().
	Invalidate();
	if (fHeaderView != NULL)
		fHeaderView->Invalidate();
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


// The height of the given verse's row, shared by group-0 note-field
// sizing (_PositionColumns()) and scroll-target calculation
// (_ScrollToVerse()). Reads it back from the first group-0 Bible column's
// paragraph for that verse -- which, if VerseAligner::Align() has run (2+
// group-0 Bible columns), already reflects the tallest column's height
// for that verse, exactly the row height every other group-0 column needs
// to match. Mirrors the technique VerseAligner itself uses: a standalone
// ParagraphLayout can measure a paragraph's height without a live
// TextDocumentView. Falls back to kHeaderHeight when there's no group-0
// Bible column to measure against (a notes-only view) or the verse was
// skipped from that column's document.
float
ParallelBibleView::_RowHeight(int verse) const
{
	BibleTextDocument* document = NULL;
	for (size_t i = 0; i < fDocuments.size(); i++) {
		if (fColumnGroup[i] == 0) {
			document = fDocuments[i].Get();
			break;
		}
	}

	if (document != NULL) {
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


// Shared by every Bible/Commentary column, group 0 or not (see the class
// comment on ParallelColumnGroup) -- grouping only changes how a column
// scrolls/navigates, never its width.
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
	// separately capped, see _NotesColumnWidth()) and the trailing "+"
	// button -- without reserving the button's own width here, a single
	// column would claim the entire viewport and push "+" off-screen,
	// reachable only by a horizontal scroll nothing hints exists. If
	// that means the columns no longer all fit, _PositionColumns()'s
	// resulting fContentWidth ends up wider than this view's own
	// Bounds(), and the horizontal scrollbar (see _UpdateScrollBars())
	// is how the overflow columns (and the button) stay reachable
	// instead of just running off-screen.
	float available = totalWidth - kColumnSpacing * (columnCount - 1)
		- notesWidth - kHeaderHeight - kColumnSpacing;
	float width = available / bibleCount;
	if (width < kMinColumnWidth)
		width = kMinColumnWidth;
	return width;
}


// No notes column claims more than kMaxNotesWidthFraction of the total
// width -- the equal-share split let it eat half the window with just one
// Bible column open. Still just a fixed cap for now; issue #19 tracks
// turning this into a real user-draggable divider. Shared by every notes
// column, group 0 or not -- same reasoning as _ColumnWidth().
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

	int32 columnCount = (int32)fModules.size() + (int32)fNotesColumns.size();
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
// plus a "Notes" entry so any column can be turned into a notes column
// from its own dropdown. columnIndex is embedded in each item's message;
// what it means depends on forInsert (see the header comment) -- a
// fColumnOrder position being replaced in place (forInsert = false,
// MessageReceived()'s PARALLEL_SELECT_MODULE/PARALLEL_SELECT_NOTES), or
// the anchor column a brand-new one gets inserted after (forInsert =
// true, PARALLEL_INSERT_MODULE/PARALLEL_INSERT_NOTES -- see
// InsertColumn()/InsertNotesColumn()). columnIndex < 0 always means the
// trailing "+" (AddColumn(), always group 0, never forInsert).
// markedModuleName, if given, is checked off to show the column's
// current selection; markNotes does the same for the "Notes" entry.
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

	uint32 moduleMessageWhat = forInsert ? PARALLEL_INSERT_MODULE
		: PARALLEL_SELECT_MODULE;

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

	// A group can have at most one notes column of its own (they'd all
	// share the same fNotesModule, nothing sensible to do with a second
	// one) -- offer "Notes" here only if the group this item would
	// affect (columnIndex's own current group when replacing in place,
	// or the group a new column at columnIndex + 1 would inherit when
	// inserting -- either way _GroupForPosition(columnIndex)) doesn't
	// already have one. markNotes means this item already IS that
	// group's own notes column -- re-picking it is a harmless no-op, so
	// it stays offered (and marked) regardless.
	int32 targetGroup = _GroupForPosition(columnIndex);
	bool groupHasNotes = false;
	for (size_t i = 0; i < fNotesColumns.size(); i++) {
		if (fNotesColumns[i].group == targetGroup) {
			groupHasNotes = true;
			break;
		}
	}

	if (markNotes || !groupHasNotes) {
		BMessage* notesMessage = new BMessage(
			forInsert ? PARALLEL_INSERT_NOTES : PARALLEL_SELECT_NOTES);
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

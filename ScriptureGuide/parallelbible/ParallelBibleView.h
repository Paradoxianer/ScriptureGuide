/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef PARALLEL_BIBLE_VIEW_H
#define PARALLEL_BIBLE_VIEW_H

#include <vector>

#include <Referenceable.h>
#include <String.h>
#include <View.h>

#include <swmgr.h>
#include <swmodule.h>

#include "BibleTextDocument.h"
#include "PersonalNotesModule.h"
#include "TextDocumentView.h"

using namespace sword;

class BButton;
class BFont;
class BMenu;
class BMenuField;
class BPopUpMenu;
class BScrollView;
class BStringView;
class BibleColumnView;
class NoteFieldView;
class NotesColumnContainer;
class ParallelHeaderView;


// One notes column's own verse-label/NoteFieldView pairs plus the
// container view they actually live in (see NotesColumnContainer in
// ParallelBibleView.cpp) -- container is only NULL momentarily during
// _RebuildLayout()'s teardown/rebuild.
struct NotesColumn {
	NotesColumnContainer*			container;
	std::vector<BStringView*>		verseLabels;
	std::vector<NoteFieldView*>	fields;
};


// Displays one or more Bible translations side by side, each in its own
// TextDocumentView, plus any number of notes columns backed by a shared
// personal SWORD module (see PersonalNotesModule). Unlike the Bible
// columns (one flowing TextDocumentView each), a notes column is a
// separate small BTextView-derived NoteFieldView per verse, each exactly
// as tall as that verse's row -- the point being that each verse gets its
// own distinct, obviously-editable input field rather than one
// continuous document a user has to click into the right spot of.
//
// Every column -- Bible, Commentary, or a notes column -- has the same
// kind of header cell: a BMenuField listing every installed "Biblical
// Texts"/"Commentaries" module plus a "Notes" entry, so any column can be
// switched to any of those at any time from its own dropdown, plus a
// small "x" button to remove that column and a small "+" button to
// insert a brand-new one immediately after it, offering the same
// choices (see issue #11) -- every column has its own "+", not one
// shared trailing button, so there's always an unambiguous anchor for
// "add something new right here" (see InsertColumn()/
// InsertNotesColumn()). Any number of columns can be a notes column at
// once -- there is no "only one"
// restriction; they all share the one PersonalNotesModule backend, whose
// GetNote()/SetNote() already take the verse key per call, so nothing
// about the module itself is bound to a single column. A notes column
// never claims more than kMaxNotesWidthFraction of the total width (see
// _NotesColumnWidth()) -- a fixed cap for now; issue #19 tracks turning
// this into a real user-draggable divider (the one drag handle there is
// -- see _NotesSplitDividerX() -- always anchors on the first/leftmost
// notes column and its width fraction applies uniformly to every notes
// column, not an independent one per column). Every TextDocumentView/
// NoteFieldView gets B_DOCUMENT_BACKGROUND_COLOR instead of the
// B_PANEL_BACKGROUND_COLOR TextDocumentView's class default constructs
// with, since these are reading/editing surfaces, not a details panel.
//
// Columns are grouped into "chains" (see SetColumnsLinked() -- issue
// #12): by default every column is linked to its right-hand neighbor,
// forming one big chain that scrolls, aligns (see VerseAligner), and
// navigates together, same as before chains existed. Breaking the link
// between two adjacent columns (the icon between their header cells)
// splits the chain there -- each resulting chain keeps its own
// independent scroll position, its own VerseAligner alignment (only
// among that chain's own members), and its own current chapter/verse.
// This is implemented with NO group/ID bookkeeping at all: fLinkedToNext
// is a flat bool per gap between adjacent columns, and a chain is simply
// a maximal run of linked gaps (see _ChainStart()/_ChainEnd()).
//
// Every column -- Bible/Commentary or notes -- gets its own BScrollView.
// TextDocumentView already fully drives its own vertical BScrollBar when
// it's a direct BScrollView target (confirmed against its own source,
// not assumed); only the RIGHTMOST column of each chain is built with an
// actual, visible vertical BScrollBar (see _IsChainRightmost()) -- every
// other column in that chain still has its own BScrollView (so it can be
// BView::ScrollTo()'d programmatically the exact same way), just with no
// scrollbar chrome attached. Scrolling the one visible bar of a chain
// propagates to the rest of that chain via _ColumnScrolled(), which every
// column's own ScrollTo() override calls into -- the same "one hook every
// scroll path funnels through" principle this class already uses for its
// own horizontal scroll (see ScrollTo() below), just per column instead
// of once for the whole view. This view itself is no longer a vertically
// scrolled target at all (its own outer BScrollView, built by
// LogosMainWindow, is horizontal-only) -- ScrollTo()/FrameResized() below
// are back to only doing what they did before issue #12 existed (mirror
// horizontal scroll onto the header).
//
// Exactly one column is "active" at a time (see _SetActiveColumn()) --
// clicking into a column, focusing one of its note fields, or starting a
// header drag on it, makes it active. SetKey()/HighlightVerse()/
// NextChapter()/PrevChapter() all act on whichever CHAIN the active
// column belongs to, not necessarily every open column -- this is what
// lets the one existing book/chapter/verse toolbar in LogosMainWindow
// drive whichever chain the user is currently looking at, with no
// changes to LogosMainWindow.cpp and no second set of navigation
// controls. ParallelHeaderView::Draw() tints the active chain's own
// header cells so it's clear at a glance which chain typing a new
// reference will actually move.
//
// Meant to be embedded as the target of a BScrollView with only a
// horizontal scrollbar -- for when there isn't enough width to give every
// column at least kMinColumnWidth (columns never shrink below that, so
// excess columns run off the right edge instead -- reachable by
// scrolling right rather than becoming unreachable). Columns are
// positioned and sized manually (MoveTo/ResizeTo) rather than through a
// BGroupLayout: BGroupLayout/BTwoDimensionalLayout does not correctly
// distribute space to more than one simultaneous HasHeightForWidth()
// child (see issue #13) -- with several TextDocumentView columns in one
// horizontal group, only the last-added one ever received a Draw() call.
// Driving frames directly sidesteps that entirely.
//
// The header row is its own top-level BView, NOT a child of this one --
// it needs to follow horizontal scrolling but must never move during
// vertical scrolling, and nesting it as another scrolled child (tried in
// an earlier version of this class) made the vertical BScrollBar span the
// header's height too, and put a visible seam between two separately
// clipped/backed views where a single control was expected. Instead,
// HeaderView() returns a plain BView this class builds and keeps
// positioned, but does not itself add as a child; the caller (see
// SGMainWindow, which embeds this view directly as its own reading pane
// rather than hosting it in a separate window) places it directly above
// the BScrollView that wraps this view, outside the scrolled hierarchy
// entirely, and this view mirrors its own horizontal scroll position onto
// it by overriding ScrollTo() -- the one hook every horizontal scroll
// path (drag, wheel, programmatic) ultimately funnels through.
class ParallelBibleView : public BView {
public:
								// initialWidth seeds the first column-width
								// calculation. It matters because this view
								// is still unattached (Bounds() degenerate)
								// while its window is being built, so pass
								// the constructing window's own frame width,
								// which unlike this view's own Bounds() is
								// already valid at that point.
								ParallelBibleView(const char* name,
									SWMgr* manager, float initialWidth = 0);
		virtual						~ParallelBibleView();

		virtual	void				AttachedToWindow();
		virtual	void				FrameResized(float width, float height);
		virtual	void				ScrollTo(BPoint where);
				using BView::ScrollTo; // un-hide BView::ScrollTo(float, float)
		virtual	void				MessageReceived(BMessage* message);
		virtual	void				Draw(BRect updateRect);
		virtual	void				MouseDown(BPoint where);
		virtual	void				MouseMoved(BPoint where, uint32 transit,
										const BMessage* dragMessage);
		virtual	void				MouseUp(BPoint where);

				BView*				HeaderView() const { return fHeaderView; }
				float				HeaderHeight() const
										{ return kHeaderHeight; }

				status_t			AddColumn(const char* moduleName);
				// Appends a brand-new notes column at the very end,
				// joined to whatever chain the current last column
				// belongs to (or starting its own chain if this is the
				// first column). See the class comment -- any number of
				// notes columns can exist at once.
				status_t			AddNotesColumn();
				// Inserts a brand-new column immediately after
				// afterPosition -- what each column's own "+" button
				// (see _RebuildHeader()) actually calls. Unlike
				// AddColumn()/AddNotesColumn() (always at the very end)
				// or ReplaceColumn()/the header dropdown's own module
				// pick (changes what an EXISTING slot shows, doesn't add
				// one), this always adds a genuinely new slot right next
				// to an explicit anchor. Defaults to joining the anchor's
				// own chain (linked to it); the gap on the new column's
				// OTHER side preserves whatever the anchor's own old
				// right-hand gap was, so the rest of that chain's
				// continuity (or lack of it) is undisturbed.
				status_t			InsertColumn(int32 afterPosition,
										const char* moduleName);
				status_t			InsertNotesColumn(int32 afterPosition);
				status_t			ReplaceColumn(int32 position,
										const char* moduleName);
				status_t			RemoveColumn(int32 position);
				int32				CountColumns() const;
				int32				FirstBibleColumnPosition() const;

				// Toggles whether at least one notes column exists
				// anywhere in the view -- adds one at the end (enabled)
				// or removes the first one found (disabled). A coarse,
				// view-wide convenience for the single "Notes" toolbar
				// button (see LogosMainWindow); AddNotesColumn()/
				// RemoveColumn() are the precise per-column primitives.
				status_t			SetNotesEnabled(bool enabled);
				bool				NotesEnabled() const
										{ return !fNotesColumns.empty(); }

				status_t			SetShowVerseNumbers(bool show);
				bool				ShowVerseNumbers() const
										{ return fShowVerseNumbers; }

				status_t			SetShowStrongsNumbers(bool show);
				bool				ShowStrongsNumbers() const
										{ return fShowStrongsNumbers; }

				status_t			SetShowCrossReferences(bool show);
				bool				ShowCrossReferences() const
										{ return fShowCrossReferences; }

				// Family/style/size for verse text and (still bold)
				// verse numbers, applied to every current Bible/
				// Commentary column and remembered for columns
				// added afterward, same as SetShowVerseNumbers().
				status_t			SetBaseFont(const BFont& font);

				// Act on whichever chain the active column (see
				// _SetActiveColumn()) currently belongs to -- not
				// necessarily every open column. See the class comment.
				status_t			SetKey(const char* key);
				status_t			NextChapter();
				status_t			PrevChapter();

				// Breaks or restores the link between the columns at
				// `gapIndex` and `gapIndex + 1`. No-op if already in the
				// requested state. Neither direction touches any
				// column's current key/scroll position -- only which
				// chain each ends up belonging to.
				void				SetColumnLinked(int32 gapIndex,
										bool linked);
				bool				AreColumnsLinked(int32 gapIndex) const
										{ return (size_t)gapIndex
												< fLinkedToNext.size()
											&& fLinkedToNext[gapIndex]; }

				// The chapter/verse key the chain containing anchorPosition
				// is currently showing, read directly from the first Bible
				// document in that chain (see the class comment -- no
				// separate per-chain key is cached anywhere). Falls back
				// to whatever the most recent SetKey() anywhere applied
				// for a chain with no Bible column of its own (a pure
				// notes chain).
				BString				ChainKey(int32 anchorPosition) const
										{ return _ChainKey(anchorPosition); }
				// The column most recently interacted with -- see the
				// class comment -- or -1 if there are no columns at all.
				// The owning window (see PARALLEL_ACTIVE_COLUMN_CHANGED)
				// uses this together with ChainKey() to sync its own
				// book/chapter/verse toolbar fields to whichever chain
				// is now active.
				int32				ActiveColumn() const
										{ return fActivePosition; }

				// Selects verses startVerse..endVerse (inclusive) in
				// every Bible/Commentary column of the active chain that
				// has them, deselecting in any that don't (e.g. a
				// commentary without an entry for that verse). Callers
				// decide when this is warranted -- SetKey() itself
				// doesn't call this, since ordinary chapter/verse
				// navigation isn't supposed to select anything, only a
				// jump from search is (see #22).
				void				HighlightVerse(int startVerse,
										int endVerse);

				// SWORD module names of every Bible/Commentary column
				// currently open anywhere in the view, left-to-right
				// (never a notes column). For the search window
				// (SGMainWindow::EnsureSearchWindow()), which otherwise
				// has no way to know what's actually shown in the
				// reading pane -- intentionally NOT scoped to the active
				// chain, since search should cover everything open.
				std::vector<BString> ColumnModuleNames() const;

				// One entry per on-screen column, left to right, --
				// unlike ColumnModuleNames() above, which drops position
				// information because it only serves the search window's
				// module picker. Used to save/restore a window's whole
				// layout (see #9); moduleName is empty when isNotes is
				// true. linkedToNext mirrors fLinkedToNext (meaningless
				// for the last entry).
				struct ColumnDescription {
					bool	isNotes;
					BString	moduleName;
					bool	linkedToNext;
				};
				std::vector<ColumnDescription> ColumnLayout() const;

				// One row per verse of the active chain's current
				// chapter (columnText, same left-to-right order as that
				// chain's own Bible/Commentary columns; notesText from
				// the first notes column in that chain, empty if none)
				// -- structured data for #8's export feature to format
				// as plain text/TSV/Markdown/HTML. Each cell is that
				// verse's plain text with the leading " N " verse-number
				// prefix _Rebuild() prepends (when ShowVerseNumbers() is
				// on) stripped back out, since the row already carries
				// its own verse number.
				struct ExportRow {
					int				verse;
					std::vector<BString>	columnText;
					BString			notesText;
				};
				std::vector<ExportRow> BuildExportRows() const;

				// Cross-column selection coordination (see #23) -- called
				// by BibleColumnView instances, not meant for other
				// callers. A selection-drag that starts in one column and
				// crosses into a sibling needs a coordinator: each
				// TextDocumentView still gets its own independent
				// MouseMoved() as the cursor passes over it (nothing about
				// BView::SetMouseEventMask()/B_LOCK_WINDOW_FOCUS suppresses
				// that -- confirmed against the BView reference, it only
				// keeps a *window* from losing activation), so without
				// this a sibling column would silently grow its own
				// selection from wherever it happened to be sitting
				// instead of participating in the same gesture.
				void				_ColumnSelectionStarted(
										BibleColumnView* source,
										BPoint screenPoint);
				void				_ColumnSelectionMoved(
										BibleColumnView* source, BPoint screenPoint);
				void				_ColumnSelectionEnded();
				bool				_HasActiveColumnSelection() const;
				bool				_IsColumnSelectionOwnedByOther(
										BibleColumnView* column) const;
				const std::vector<TextDocumentView*>&
										_ColumnViews() const
											{ return fTextViews; }

				// Called by BibleColumnView/NotesColumnContainer (unrelated
				// classes, not friends -- same reasoning as the selection
				// coordination methods above) -- not meant for other
				// callers.
				//
				// Propagates a scroll from the column at `position` to
				// every other column in the same chain, so a chain's one
				// visible BScrollBar keeps every member (bible or notes,
				// visible bar or not) in sync. Guarded against re-entrant
				// cascades (see fSuppressScrollPropagation).
				void				_ColumnScrolled(int32 position, float y);
				// Makes the column at `position` active -- see the class
				// comment. Only changes bookkeeping and repaints the
				// header's tint; never touches any column's key/scroll.
				void				_SetActiveColumn(int32 position);
				// A follower column (no attached vertical BScrollBar, see
				// the class comment) re-dispatches an unhandled
				// B_MOUSE_WHEEL_CHANGED straight to its chain's driving
				// (rightmost) column, whose own BView::MessageReceived()
				// then scrolls it normally -- which in turn propagates
				// back out via _ColumnScrolled() above.
				void				_ForwardWheelToChain(int32 position,
										BMessage* message);

private:
				friend class ParallelHeaderView;
				// Called from ~ParallelHeaderView() so this view never holds a
				// dangling fHeaderView -- see the class comment on HeaderView()
				// ownership and ~ParallelBibleView().
				void				_HeaderViewDestroyed();

				// A column "slot" holds either a Bible/Commentary module or a
				// notes column -- see the class comment. Order here is the
				// on-screen left-to-right order of all columns;
				// fModules/fDocuments/fTextViews only ever hold the
				// COLUMN_BIBLE slots, in the same relative order, so a slot's
				// position in fColumnOrder has to be translated to an index
				// into those via _BibleIndexForPosition(); fNotesColumns is
				// the same idea for COLUMN_NOTES slots, via
				// _NotesIndexForPosition().
				enum ColumnType { COLUMN_BIBLE, COLUMN_NOTES };

				// The maximal run of linked columns position belongs to,
				// inclusive -- e.g. for fLinkedToNext = [true, false,
				// true], _ChainStart(0) == _ChainStart(1) == 0,
				// _ChainEnd(0) == _ChainEnd(1) == 1; _ChainStart(2) ==
				// _ChainEnd(2) == _ChainEnd(3) == _ChainStart(3) == 2..3.
				int32				_ChainStart(int32 position) const;
				int32				_ChainEnd(int32 position) const;
				bool				_IsChainRightmost(int32 position) const
										{ return position == _ChainEnd(position); }
				// Number of chains currently open (maximal linked runs);
				// 0 if there are no columns at all.
				int32				_ChainCount() const;
				// The actual scrolled content view for a column -- a
				// Bible/Commentary column's own TextDocumentView, or a
				// notes column's NotesColumnContainer. NULL if position
				// isn't a valid current slot.
				BView*				_ColumnScrollTarget(int32 position) const;
				// The BScrollView wrapping _ColumnScrollTarget(position)
				// -- always that view's own direct Parent() (BScrollView's
				// constructor makes its target a child of itself, see
				// _RebuildLayout()), so this needs no separate bookkeeping
				// of its own.
				BScrollView*		_ColumnScroller(int32 position) const;
				// The chapter/verse key the chain containing anchorPosition
				// is currently showing, read directly from the first Bible
				// document in that chain (see the class comment -- no
				// separate per-chain key is cached). Falls back to
				// fLastKnownKey for a chain with no Bible column of its
				// own (a pure notes chain).
				BString				_ChainKey(int32 anchorPosition) const;
				// Scrolls every column of the chain containing
				// anchorPosition to vertical offset y, all at once
				// (wrapped in fSuppressScrollPropagation so moving each
				// column doesn't also redundantly propagate to its
				// already-correct siblings).
				void				_ScrollChainTo(int32 anchorPosition,
										float y);
				// The vertical offset the given verse's row starts at,
				// within the chain containing anchorPosition -- sums
				// _RowHeight() for every verse before it.
				float				_ChainVerseY(int32 anchorPosition,
										int verse) const;
				void				_ToggleLink(int32 gapIndex);

				void				_RebuildLayout();
				void				_RebuildHeader();
				void				_RebuildNoteFields();
				void				_ClearNoteFieldViews(
										NotesColumn& notes);
				// Re-aligns every chain's own Bible/Commentary documents
				// via VerseAligner (each chain independently -- see the
				// class comment), then repositions everything via
				// _PositionColumns(). Called whenever content that
				// affects layout changes: SetKey(), font/verse-number/
				// Strong's/cross-reference toggles, FrameResized().
				void				_Realign();
				void				_PositionColumns();
				void				_UpdateScrollBars();
				// Same three-line SetRange()/SetProportion()/SetSteps()
				// dance _UpdateScrollBars() does for this view's own
				// (horizontal-only, see the class comment) BScrollBar --
				// retargeted at a single column's own vertical
				// BScrollBar, needed because a plain BView (unlike
				// TextDocumentView) has no built-in "content taller than
				// viewport" tracking of its own (see NotesColumnContainer).
				void				_UpdateColumnScrollBar(
										BScrollView* scroller,
										float contentHeight,
										float viewportHeight);
				// The height of the given verse's row within the chain
				// containing chainAnchorPosition, shared by note-field
				// sizing (_PositionColumns()) and scroll-target
				// calculation (_ChainVerseY()). Reads it back from the
				// chain's first Bible column's paragraph for that verse.
				float				_RowHeight(int verse,
										int32 chainAnchorPosition) const;
				// The on-screen SLOT width shared by every Bible/
				// Commentary column -- what each column's own BScrollView
				// is actually resized to (see _PositionColumns()). Does
				// NOT account for a chain-rightmost column's real,
				// visible BScrollBar eating into its own usable content
				// width -- see _MeasurementWidth() for that. Keeping
				// these separate is what a chain-rightmost column's
				// BScrollView already does for free (it auto-narrows
				// its OWN target to fit a real scrollbar within
				// whatever slot width it's given, see the class
				// comment) -- folding the same reduction into the slot
				// width here as well double-counted it, leaving an
				// unclaimed gap the width of one scrollbar per chain
				// (confirmed via a live test, not just derived).
				float				_ColumnWidth() const;
				float				_NotesColumnWidth() const;
				// The real usable text width for the Bible/Commentary
				// column at position, after both kBibleColumnInset (each
				// side) and, if this column is its chain's rightmost
				// (see _IsChainRightmost()), B_V_SCROLL_BAR_WIDTH -- must
				// match what the live TextDocumentView actually wraps at
				// (see _Realign()'s own comment), or VerseAligner/
				// _RowHeight()'s measurements drift from the real
				// rendered heights.
				float				_MeasurementWidth(int32 position) const;
				int32				_BibleIndexForPosition(int32 position) const;
				// Same idea as _BibleIndexForPosition(), for the
				// COLUMN_NOTES slots (fNotesColumns).
				int32				_NotesIndexForPosition(int32 position) const;
				// Position of the first notes column anywhere in the
				// view, or -1 -- what SetNotesEnabled()/NotesEnabled()
				// mean by "the" notes column for their coarse, view-wide
				// convenience toggle (see the class comment).
				int32				_NotesPosition() const;
				// Same idea, bounded to [start, end] -- used by
				// BuildExportRows() to find the active chain's own notes
				// column, if it has one.
				int32				_NotesPositionInRange(int32 start,
										int32 end) const;

				// Column index whose on-screen cell contains content-space x
				// (same coordinate space as fColumnDividerX/the header
				// fields' own MoveTo() calls), clamped to the last column if
				// x falls past every divider. -1 if there are no columns at
				// all. Used for column-header drag-to-reorder (see #23):
				// ParallelHeaderView::MouseDown() to find which column a
				// drag started on, MessageReceived() to find where it was
				// dropped.
				int32				_ColumnIndexForX(float x) const;

				// Moves the column currently at `from` so it ends up at
				// `to` in the final on-screen order (not "insert before the
				// element currently at `to`" -- `to` is the position in the
				// array *after* `from` has already been removed from it).
				// Implemented by reading the current order via
				// ColumnLayout(), reordering that list, tearing down every
				// column, and re-adding them via the same AddColumn()/
				// _SetColumnToNotes() calls RestoreColumnLayout() (see #9)
				// uses -- simplest way to keep this correct without
				// duplicating the COLUMN_BIBLE/fModules/fDocuments/
				// fTextViews index bookkeeping AddColumn()/RemoveColumn()
				// already maintain. The moved column always arrives
				// unlinked from both its new neighbors (a drag never
				// silently merges a chain it wasn't asked to); its OLD
				// neighbors are relinked to each other if they were both
				// linked to it before (same rule as RemoveColumn()).
				void				_MoveColumn(int32 from, int32 to);

				// Content-space x of the one divider that's ever draggable --
				// the one immediately before the first/leftmost notes column
				// (see #19) -- or < 0.0f if there's no notes column, or it's
				// the leftmost column with nothing to its left to negotiate
				// space with.
				float				_NotesSplitDividerX() const;
				status_t			_SetColumnToBible(int32 position,
										const char* moduleName);
				status_t			_SetColumnToNotes(int32 position);
				// forInsert: items post PARALLEL_INSERT_MODULE/
				// PARALLEL_INSERT_NOTES (see InsertColumn()/
				// InsertNotesColumn()) instead of PARALLEL_SELECT_MODULE/
				// PARALLEL_SELECT_NOTES -- used by a column's own "+"
				// button, which always adds a new column rather than
				// changing what columnIndex itself shows.
				void				_PopulateModuleMenu(BMenu* menu,
										int32 columnIndex,
										const char* markedModuleName,
										bool markNotes,
										bool forInsert = false);
				BPopUpMenu*			_BuildModuleMenu(int32 columnIndex,
										const char* markedModuleName,
										bool markNotes,
										bool forInsert = false);
				// Content-space x-range of the active column's chain's own
				// header cells -- left == right == 0 if there's only one
				// chain open (nothing to visually distinguish it from).
				// Called by ParallelHeaderView::Draw() (a friend) for its
				// tinted-background cue.
				void				_ActiveChainHeaderRange(float& left,
										float& right) const;

private:
				SWMgr*				fManager;

				std::vector<ColumnType>	fColumnOrder;

				std::vector<SWModule*>	fModules;
				std::vector<BReference<BibleTextDocument> > fDocuments;
				std::vector<TextDocumentView*> fTextViews;
				std::vector<BMenuField*> fHeaderFields;
				std::vector<BButton*>	fRemoveButtons;
				// One per fColumnOrder slot -- inserts a brand-new column
				// right after this one (see InsertColumn()/
				// InsertNotesColumn()), parallel to fHeaderFields/
				// fRemoveButtons. Replaces the old single trailing "+"
				// (fAddColumnButton) entirely -- no more reserved space
				// for a global append button, see _ColumnWidth().
				std::vector<BButton*>	fInsertButtons;
				// One per GAP between adjacent columns (size ==
				// fColumnOrder.size() - 1) -- the link/unlink toggle
				// button centered on that gap's divider line.
				std::vector<BButton*>	fLinkButtons;

				// One per gap between adjacent columns (size ==
				// fColumnOrder.size() - 1, empty when there are 0 or 1
				// columns). true means the columns on either side of
				// that gap share one scroll-locked, same-chapter "chain"
				// -- see _ChainStart()/_ChainEnd(). Starts (and stays,
				// unless explicitly toggled) fully true whenever the
				// column count changes -- see the class comment.
				std::vector<bool>	fLinkedToNext;
				// Position (into fColumnOrder) of the column most
				// recently interacted with -- see _SetActiveColumn().
				// -1 when there are no columns at all.
				int32				fActivePosition;
				// Reentrancy guard for scroll propagation within a chain
				// -- see _ColumnScrolled().
				bool				fSuppressScrollPropagation;
				// The most recent key any SetKey() call actually applied
				// to at least one Bible document -- used only as
				// _ChainKey()'s fallback for a chain with no Bible
				// column of its own (a pure notes chain), so it starts
				// out showing notes for a sane chapter instead of an
				// empty one.
				BString				fLastKnownKey;

				// Storage backend shared by every notes column -- see the
				// class comment. Opened when the first notes column
				// anywhere is created, closed when the last one is removed.
				PersonalNotesModule*	fNotes;

				// One entry per COLUMN_NOTES slot in fColumnOrder, same
				// left-to-right order.
				std::vector<NotesColumn>	fNotesColumns;

				BView*				fHeaderView;

				// Non-owning; NULL when no cross-column selection gesture
				// is currently active (see _ColumnSelectionStarted()).
				BibleColumnView*	fActiveSelectionColumn;
				// Screen-space anchor of the active gesture's MouseDown --
				// the selection rectangle for any given MouseMoved() is
				// this point to the current one (see _ColumnSelectionMoved()).
				BPoint				fSelectionAnchorScreen;
				// Last verse the cursor resolved to; kept stable while the
				// cursor is briefly over a gap/margin rather than
				// resetting/collapsing the range. -1 when unset.
				int					fSelectionLastEndVerse;

				bool				fShowVerseNumbers;
				bool				fShowStrongsNumbers;
				bool				fShowCrossReferences;
				BFont				fBaseFont;

				float				fInitialWidth;
				float				fContentWidth;
				std::vector<float>	fColumnDividerX;

				// User-set notes column width, as a fraction of total content
				// width, from dragging the splitter (see #19); -1 (the
				// default) means no drag has happened yet, so
				// _NotesColumnWidth() falls back to its original automatic
				// natural-share/kMaxNotesWidthFraction-cap behavior. Set once
				// on MouseUp, not continuously during the drag -- see
				// fNotesSplitDragGuideX. Applies uniformly to every notes
				// column, not independently per one.
				float				fNotesWidthFraction;

				// Content-space x of the live drag guide line while the
				// splitter is being dragged, -1 when it isn't. Deliberately
				// separate from actually resizing the notes column: only this
				// thin guide line (drawn in Draw(), not the columns
				// themselves) updates on every MouseMoved(), so a drag
				// doesn't force a full relayout of every column's text per
				// pixel moved -- the real resize (_Realign()) only happens
				// once, in MouseUp(), from wherever the guide ended up.
				float				fNotesSplitDragGuideX;

	static	const float			kMinColumnWidth;
	static	const float			kColumnSpacing;
	static	const float			kHeaderHeight;
	// Extra blank space left below the header row's controls, between
	// their bottom edge and the dividing line ParallelHeaderView::Draw()
	// strokes along its own bottom edge -- without it the controls'
	// white backgrounds ran right up against the line with no breathing
	// room, which looked wrong. Only grows the header row itself, not
	// the controls within it (still sized/positioned off kHeaderHeight
	// alone, unchanged).
	static	const float			kHeaderBottomGap;
	static	const float			kRemoveButtonWidth;
	static	const float			kInsertButtonWidth;
	static	const float			kLinkButtonWidth;
	static	const float			kMaxNotesWidthFraction;
	static	const float			kNoteVerseLabelWidth;
	static	const float			kBibleColumnInset;
};

#endif // PARALLEL_BIBLE_VIEW_H

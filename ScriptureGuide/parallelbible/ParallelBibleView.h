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
class ParallelColumnGroup;
class ParallelHeaderView;


// Displays one or more Bible translations side by side, each in its own
// TextDocumentView, plus an optional notes column backed by a personal
// SWORD module (see PersonalNotesModule). Unlike the Bible columns (one
// flowing TextDocumentView each), the notes column is a separate small
// BTextView-derived NoteFieldView per verse, each exactly as tall as that
// verse's row -- the point being that each verse gets its own distinct,
// obviously-editable input field rather than one continuous document a
// user has to click into the right spot of. Bible columns are kept
// vertically aligned per verse (see VerseAligner) so a single BScrollBar
// is enough to scroll every column, and the note fields, in sync; a note
// field's height is simply read back from its corresponding Bible
// column's post-alignment paragraph height (see _RebuildNoteFields()),
// not something VerseAligner needs to know about separately.
//
// Every column -- Bible, Commentary, or a notes column -- has the same
// kind of header cell: a BMenuField listing every installed "Biblical
// Texts"/"Commentaries" module plus a "Notes" entry, so any column can be
// switched to any of those at any time from its own dropdown, plus a small
// "x" button to remove that column. A trailing "+" button appends another
// column, offering the same choices (see issue #11). A group (see the
// class comment further down on SetColumnsLinked(), issue #12) can have
// at most one notes column of its own -- all backed by the same shared
// PersonalNotesModule (see fNotesModule) -- so picking "Notes" in a
// column whose dropdown doesn't offer it (because the group it would join
// already has one) isn't possible; remove that group's existing notes
// column first. A notes column never claims more than
// kMaxNotesWidthFraction of the total width its own group has to offer
// (see _NotesColumnWidth()) -- a fixed cap for now; issue #19
// tracks turning this into a real user-draggable divider. Every
// TextDocumentView/NoteFieldView gets B_DOCUMENT_BACKGROUND_COLOR instead
// of the B_PANEL_BACKGROUND_COLOR TextDocumentView's class default
// constructs with, since these are reading/editing surfaces, not a
// details panel.
//
// Meant to be embedded as the target of a BScrollView with both
// scrollbars: vertical for scrolling the (equal-height, post-alignment)
// columns together, horizontal for when there isn't enough width to give
// every column at least kMinColumnWidth (columns never shrink below that,
// so excess columns run off the right edge instead -- reachable by
// scrolling right rather than becoming unreachable). Columns are
// positioned and sized manually (MoveTo/ResizeTo)
// rather than through a BGroupLayout: BGroupLayout/BTwoDimensionalLayout
// does not correctly distribute space to more than one simultaneous
// HasHeightForWidth() child (see issue #13) -- with several TextDocumentView
// columns in one horizontal group, only the last-added one ever received a
// Draw() call. Driving frames directly sidesteps that entirely.
//
// The header row is its own top-level BView, NOT a child of this one --
// it needs to follow horizontal scrolling but must never move during
// vertical scrolling, and nesting it as a nother scrolled child (tried in
// an earlier version of this class) made the vertical BScrollBar span the
// header's height too, and put a visible seam between two separately
// clipped/backed views where a single control was expected. Instead,
// HeaderView() returns a plain BView this class builds and keeps
// positioned, but does not itself add as a child; the caller (see
// SGMainWindow, which embeds this view directly as its own reading pane
// rather than hosting it in a separate window) places it directly above
// the BScrollView that wraps this view, outside the scrolled hierarchy
// entirely, and this view
// mirrors its own horizontal scroll position onto it by overriding
// ScrollTo() -- the one hook every scroll path (drag, wheel, programmatic)
// ultimately funnels through.
//
// A Bible/Commentary column can be pulled out of the shared/main group
// (group 0, everything described above) into its own independent group --
// see SetColumnsLinked(), issue #12. The motivating case is following a
// cross-reference into a neighboring column without losing your place in
// the one you were reading. A detached group keeps its own current key,
// its own VerseAligner alignment (shared only with whatever else is in
// that same group -- a column inserted via a specific column's own "+"
// automatically joins its group, see InsertColumn()/InsertNotesColumn()),
// and its own scroll position, all managed by one ParallelColumnGroup +
// BScrollView pair (see fGroupRuns). Group 0 keeps behaving exactly as
// described above -- unchanged, still the only group the outer
// BScrollView/fContentHeight ever measures.
//
// Exactly one group is "active" at a time (fActiveGroup, default 0) --
// clicking into a column, or dropping a reference onto one, makes its
// group active (see _GroupForColumnView(), BibleColumnView::MouseDown(),
// NoteFieldView::MakeFocus()). SetKey()/HighlightVerse()/NextChapter()/
// PrevChapter() all act on whichever group is currently active instead of
// always meaning group 0 -- this is what lets the ONE existing book/
// chapter/verse toolbar in LogosMainWindow drive a detached group too,
// with no second set of navigation controls and no changes to
// LogosMainWindow.cpp at all. ParallelHeaderView::Draw() draws a visual
// cue (a couple of curves, plus a plain underline as a more visible
// fallback) connecting the active group's header cells up toward that
// toolbar, so it's clear at a glance which group typing a new reference
// will actually move.
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

			BView*				HeaderView() const { return fHeaderView; }
			float				HeaderHeight() const
									{ return kHeaderHeight; }

			status_t			AddColumn(const char* moduleName);
			status_t			ReplaceColumn(int32 position,
									const char* moduleName);
			status_t			RemoveColumn(int32 position);
			int32				CountColumns() const;
			int32				FirstBibleColumnPosition() const;

			status_t			SetNotesEnabled(bool enabled);
			bool				NotesEnabled() const
									{ return !fNotesColumns.empty(); }

			status_t			SetShowVerseNumbers(bool show);
			bool				ShowVerseNumbers() const
									{ return fShowVerseNumbers; }

			// Family/style/size for verse text and (still bold)
			// verse numbers, applied to every current Bible/
			// Commentary column and remembered for columns
			// added afterward, same as SetShowVerseNumbers().
			status_t			SetBaseFont(const BFont& font);

			// Act on whichever group is currently active (see
			// ActiveGroup()) -- group 0 unless a detached column/its
			// notes column was clicked into, or a reference dropped onto
			// one, more recently than any group-0 interaction.
			status_t			SetKey(const char* key);
			status_t			NextChapter();
			status_t			PrevChapter();

			// Breaks or restores the link between the columns at
			// `position` and `position + 1` in fColumnOrder -- see the
			// class comment. linked = false: if they're currently in the
			// same group, the whole right-hand run (position + 1 and
			// everything that shared its group) is peeled off into a
			// fresh independent group, which becomes active. No-op if
			// they're already in different groups. linked = true: the
			// whole right-hand run's group is merged into the left-hand
			// column's group. No-op if they're already the same group.
			// Neither direction touches any column's current key.
			status_t			SetColumnsLinked(int32 position,
										bool linked);
			bool				AreColumnsLinked(int32 position) const;

			// Inserts a brand-new column immediately after afterPosition
			// (afterPosition < 0 -- at the very start), inheriting that
			// neighbor's group (0 if afterPosition < 0) -- unlike
			// ReplaceColumn()/the header dropdown's own module pick,
			// which changes what an EXISTING slot shows without adding
			// one. This is the per-column "+" (see the class comment);
			// the existing trailing "+" (AddColumn()) is unaffected and
			// keeps always appending into group 0.
			status_t			InsertColumn(int32 afterPosition,
										const char* moduleName);
			status_t			InsertNotesColumn(int32 afterPosition);

			// Which group SetKey()/HighlightVerse()/NextChapter()/
			// PrevChapter() currently act on -- see the class comment.
			// SetActiveGroup() only changes this and repaints the header
			// cue; it never touches any column's key.
			void				SetActiveGroup(int32 group);
			int32				ActiveGroup() const
										{ return fActiveGroup; }

			// Selects verses startVerse..endVerse (inclusive) in every
			// Bible/Commentary column that has them, deselecting in any
			// column that doesn't (e.g. a commentary without an entry for
			// that verse). Callers decide when this is warranted -- SetKey()
			// itself doesn't call this, since ordinary chapter/verse
			// navigation isn't supposed to select anything, only a jump
			// from search is (see #22).
			void				HighlightVerse(int startVerse,
									int endVerse);

			// SWORD module names of every Bible/Commentary column
			// currently open, left-to-right (never the notes column --
			// fModules only ever holds the COLUMN_BIBLE slots, see the
			// class comment on fColumnOrder). For the search window
			// (SGMainWindow::EnsureSearchWindow()), which otherwise has
			// no way to know what's actually shown in the reading pane.
			std::vector<BString> ColumnModuleNames() const;

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

			// The group a given BibleColumnView's own Bible/Commentary
			// column belongs to, or -1 if it can't be found among
			// fTextViews. Used by BibleColumnView::MouseDown() and
			// _HandleReferenceDrop() to make that column's group the
			// active one (see SetActiveGroup()).
			int32				_GroupForColumnView(
										BibleColumnView* view) const;

private:
			friend class ParallelHeaderView;
			// Called from ~ParallelHeaderView() so this view never holds a
			// dangling fHeaderView -- see the class comment on HeaderView()
			// ownership and ~ParallelBibleView().
			void				_HeaderViewDestroyed();

			// A column "slot" holds either a Bible/Commentary module or the
			// (single, shared) notes column -- see the class comment. Order
			// here is the on-screen left-to-right order of all columns;
			// fModules/fDocuments/fTextViews only ever hold the
			// COLUMN_BIBLE slots, in the same relative order, so a slot's
			// position in fColumnOrder has to be translated to an index
			// into those via _BibleIndexForPosition().
			enum ColumnType { COLUMN_BIBLE, COLUMN_NOTES };

			// One entry per currently-open non-0 group (see
			// SetColumnsLinked()) -- view actually holds/positions/
			// aligns that group's member columns, scroller is the
			// BScrollView wrapping it that's this view's real child (see
			// ScrollTo()/FrameResized() for how its vertical position is
			// kept independent of this view's own shared scroll).
			struct ColumnGroupRun {
				int32				group;
				ParallelColumnGroup*	view;
				BScrollView*		scroller;
			};

			void				_RebuildLayout();
			void				_RebuildHeader();
			void				_RebuildNoteFields();
			void				_PositionColumns();
			void				_UpdateScrollBars();
			void				_Realign();
			void				_ScrollToVerse(int verse);
			// Always means group 0 -- the only notes column this view
			// still sizes rows for directly; a detached group's own notes
			// column is sized by its own ParallelColumnGroup instead.
			float				_RowHeight(int verse) const;
			float				_ColumnWidth() const;
			float				_NotesColumnWidth() const;
			int32				_BibleIndexForPosition(int32 position) const;
			// Same idea as _BibleIndexForPosition(), for the COLUMN_NOTES
			// slots (fNotesColumns) -- more than one can exist now.
			int32				_NotesIndexForPosition(int32 position) const;
			// Position of the group-0 notes column specifically, or -1 --
			// what SetNotesEnabled()/NotesEnabled() and the global "Notes"
			// button in LogosMainWindow mean by "the" notes column; a
			// detached group's own notes column doesn't count here.
			int32				_NotesPosition() const;
			// The group whatever currently sits at `position` in
			// fColumnOrder belongs to (Bible/Commentary or notes alike),
			// or 0 if `position` isn't a valid existing slot. Used by
			// InsertColumn()/InsertNotesColumn() (the new column at
			// afterPosition + 1 inherits _GroupForPosition(afterPosition))
			// and by _SetColumnToBible()/_SetColumnToNotes() when
			// converting an existing slot's type in place (see there).
			int32				_GroupForPosition(int32 position) const;
			// NULL if no run is currently open for `group` (e.g. group 0,
			// always -- group 0 has no ColumnGroupRun of its own).
			ColumnGroupRun*		_RunForGroup(int32 group);
			// Replaces what an EXISTING slot shows (position < 0 appends
			// at the end instead, group 0, matching AddColumn()) --
			// doesn't insert a new column. See InsertColumn() for that.
			// Converting an existing notes slot to Bible in place keeps
			// that slot's own group (_GroupForPosition(position)) rather
			// than resetting it -- this only changes what the slot
			// displays, not which group it's in.
			status_t			_SetColumnToBible(int32 position,
									const char* moduleName);
			// Same idea as _SetColumnToBible(), for notes -- see
			// InsertNotesColumn() for inserting a brand-new one instead.
			status_t			_SetColumnToNotes(int32 position);
			// forInsert: items post PARALLEL_INSERT_MODULE/
			// PARALLEL_INSERT_NOTES (see InsertColumn()/
			// InsertNotesColumn()) instead of PARALLEL_SELECT_MODULE/
			// PARALLEL_SELECT_NOTES, and the Notes entry's per-group
			// availability check targets _GroupForPosition(columnIndex)
			// (what the new column would inherit) instead of
			// columnIndex's own current group.
			void				_PopulateModuleMenu(BMenu* menu,
									int32 columnIndex,
									const char* markedModuleName,
									bool markNotes, bool forInsert = false);
			BPopUpMenu*			_BuildModuleMenu(int32 columnIndex,
									const char* markedModuleName,
									bool markNotes, bool forInsert = false);

private:
			SWMgr*				fManager;

			std::vector<ColumnType>	fColumnOrder;

			std::vector<SWModule*>	fModules;
			std::vector<BReference<BibleTextDocument> > fDocuments;
			std::vector<TextDocumentView*> fTextViews;
			// 0 = shared/main group (see the class comment on
			// SetColumnsLinked()); parallel to fModules/fDocuments/
			// fTextViews.
			std::vector<int32>	fColumnGroup;
			std::vector<BMenuField*> fHeaderFields;
			std::vector<BButton*>	fRemoveButtons;
			// One per fColumnOrder slot (Bible/Commentary AND notes
			// alike) -- inserts a brand-new column right after this one
			// (see InsertColumn()/InsertNotesColumn()), parallel to
			// fHeaderFields/fRemoveButtons.
			std::vector<BButton*>	fInsertButtons;
			// One per GAP between adjacent slots (size ==
			// fColumnOrder.size() - 1, empty entirely when there's fewer
			// than 2 columns) -- toggles the link between the slots on
			// either side of it (see SetColumnsLinked()).
			std::vector<BButton*>	fLinkButtons;

			// Storage backend shared by every notes column regardless of
			// group -- GetNote()/SetNote() take the verse key per call, so
			// nothing about the module itself is bound to one column (see
			// PersonalNotesModule.h). Opened when fNotesColumns first
			// stops being empty, closed when it goes back to empty.
			PersonalNotesModule*	fNotesModule;

			// One entry per COLUMN_NOTES slot in fColumnOrder, same
			// left-to-right order -- more than one can exist now (one per
			// group that has its own paired notes column), unlike the
			// single fNotes this used to be. A group-0 entry's fields are
			// this view's own children, sized by _RowHeight()/
			// _PositionColumns() same as always; a non-0 entry's fields
			// belong to that group's own ParallelColumnGroup instead (see
			// _RebuildLayout()).
			struct NotesColumnState {
				int32				group;
				std::vector<BStringView*>	verseLabels;
				std::vector<NoteFieldView*>	fields;
			};
			std::vector<NotesColumnState>	fNotesColumns;

			// See the ColumnGroupRun struct definition above.
			std::vector<ColumnGroupRun>	fGroupRuns;
			// Next fresh group id SetColumnsLinked(position, false) will hand
			// out -- monotonically increasing, never reused; group ids
			// are purely internal bookkeeping, never shown to the user.
			int32				fNextGroupId;
			// Which group SetKey()/HighlightVerse()/NextChapter()/
			// PrevChapter() currently act on -- see ActiveGroup().
			int32				fActiveGroup;
			// Content-space x-range of the active group's own header
			// cells, kept up to date by _PositionColumns() -- what
			// ParallelHeaderView::Draw() connects up toward the toolbar
			// with its docking cue. Both 0 while fActiveGroup == 0 (no
			// cue needed -- there's nothing to visually distinguish it
			// from).
			float				fActiveGroupHeaderLeft;
			float				fActiveGroupHeaderRight;

			BView*				fHeaderView;
			BButton*			fAddColumnButton;

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
			BFont				fBaseFont;

			BString				fCurrentKey;
			float				fInitialWidth;
			float				fContentHeight;
			float				fContentWidth;
			std::vector<float>	fColumnDividerX;

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

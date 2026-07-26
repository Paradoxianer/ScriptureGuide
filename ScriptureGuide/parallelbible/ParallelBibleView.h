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
class BStringView;
class BibleColumnView;
class NoteFieldView;
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
// Every column -- Bible, Commentary, or the notes column -- has the same
// kind of header cell: a BMenuField listing every installed "Biblical
// Texts"/"Commentaries" module plus a "Notes" entry, so any column can be
// switched to any of those at any time from its own dropdown, plus a small
// "x" button to remove that column. A trailing "+" button appends another
// column, offering the same choices (see issue #11). Since there is only
// ever one notes column (backed by the single, shared PersonalNotesModule),
// picking "Notes" in a column whose dropdown doesn't offer it (because
// another column already is the notes column) isn't possible -- remove the
// existing notes column first. The notes column never claims more than
// kMaxNotesWidthFraction of the total width (see _NotesColumnWidth()) --
// a fixed cap for now; issue #19
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
			status_t			ReplaceColumn(int32 position,
									const char* moduleName);
			status_t			RemoveColumn(int32 position);
			int32				CountColumns() const;
			int32				FirstBibleColumnPosition() const;

			status_t			SetNotesEnabled(bool enabled);
			bool				NotesEnabled() const
									{ return fNotes != NULL; }

			status_t			SetShowVerseNumbers(bool show);
			bool				ShowVerseNumbers() const
									{ return fShowVerseNumbers; }

			// Family/style/size for verse text and (still bold)
			// verse numbers, applied to every current Bible/
			// Commentary column and remembered for columns
			// added afterward, same as SetShowVerseNumbers().
			status_t			SetBaseFont(const BFont& font);

			status_t			SetKey(const char* key);
			status_t			NextChapter();
			status_t			PrevChapter();

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

			// One entry per on-screen column, left to right, including
			// the notes column at its actual position -- unlike
			// ColumnModuleNames() above, which drops position
			// information because it only serves the search window's
			// module picker. Used to save/restore a window's whole
			// layout (see #9); moduleName is empty when isNotes is true.
			struct ColumnDescription {
				bool	isNotes;
				BString	moduleName;
			};
			std::vector<ColumnDescription> ColumnLayout() const;

			// One row per verse of the current chapter, verse-aligned
			// across every open Bible/Commentary column (columnText,
			// same left-to-right order as ColumnModuleNames()) and the
			// notes column if one is open (notesText, empty otherwise)
			// -- structured data for #8's export feature to format as
			// plain text/TSV/Markdown/HTML, not a rendering of anything
			// already on screen. Each cell is that verse's plain text
			// with the leading " N " verse-number prefix _Rebuild()
			// prepends (when ShowVerseNumbers() is on) stripped back
			// out, since the row already carries its own verse number.
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

			void				_RebuildLayout();
			void				_RebuildHeader();
			void				_RebuildNoteFields();
			void				_PositionColumns();
			void				_UpdateScrollBars();
			void				_Realign();
			void				_ScrollToVerse(int verse);
			float				_RowHeight(int verse) const;
			float				_ColumnWidth() const;
			float				_NotesColumnWidth() const;
			int32				_BibleIndexForPosition(int32 position) const;
			int32				_NotesPosition() const;

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
			// SetNotesEnabled() calls RestoreColumnLayout() (see #9)
			// uses -- simplest way to keep this correct without
			// duplicating the COLUMN_BIBLE/fModules/fDocuments/
			// fTextViews index bookkeeping AddColumn()/RemoveColumn()
			// already maintain.
			void				_MoveColumn(int32 from, int32 to);

			// Content-space x of the one divider that's ever draggable --
			// the one immediately before the notes column (see #19) --
			// or < 0.0f if there's no notes column, or it's the leftmost
			// column with nothing to its left to negotiate space with.
			float				_NotesSplitDividerX() const;
			status_t			_SetColumnToBible(int32 position,
									const char* moduleName);
			status_t			_SetColumnToNotes(int32 position);
			void				_PopulateModuleMenu(BMenu* menu,
									int32 columnIndex,
									const char* markedModuleName,
									bool markNotes);
			BPopUpMenu*			_BuildModuleMenu(int32 columnIndex,
									const char* markedModuleName,
									bool markNotes);

private:
			SWMgr*				fManager;

			std::vector<ColumnType>	fColumnOrder;

			std::vector<SWModule*>	fModules;
			std::vector<BReference<BibleTextDocument> > fDocuments;
			std::vector<TextDocumentView*> fTextViews;
			std::vector<BMenuField*> fHeaderFields;
			std::vector<BButton*>	fRemoveButtons;

			PersonalNotesModule*	fNotes;
			std::vector<BStringView*> fNoteVerseLabels;
			std::vector<NoteFieldView*> fNoteFields;

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

			// User-set notes column width, as a fraction of total content
			// width, from dragging the splitter (see #19); -1 (the
			// default) means no drag has happened yet, so
			// _NotesColumnWidth() falls back to its original automatic
			// natural-share/kMaxNotesWidthFraction-cap behavior. Set once
			// on MouseUp, not continuously during the drag -- see
			// fNotesSplitDragGuideX.
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
	static	const float			kMaxNotesWidthFraction;
	static	const float			kNoteVerseLabelWidth;
	static	const float			kBibleColumnInset;
};

#endif // PARALLEL_BIBLE_VIEW_H

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
class BMenuField;
class BPopUpMenu;
class BStringView;
class NoteFieldView;


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

			status_t			SetKey(const char* key);
			status_t			NextChapter();
			status_t			PrevChapter();

private:
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
			status_t			_SetColumnToBible(int32 position,
									const char* moduleName);
			status_t			_SetColumnToNotes(int32 position);
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

			BString				fCurrentKey;
			float				fInitialWidth;
			float				fContentHeight;
			float				fContentWidth;
			std::vector<float>	fColumnDividerX;

	static	const float			kMinColumnWidth;
	static	const float			kColumnSpacing;
	static	const float			kHeaderHeight;
	static	const float			kRemoveButtonWidth;
	static	const float			kMaxNotesWidthFraction;
	static	const float			kNoteVerseLabelWidth;
	static	const float			kBibleColumnInset;
};

#endif // PARALLEL_BIBLE_VIEW_H

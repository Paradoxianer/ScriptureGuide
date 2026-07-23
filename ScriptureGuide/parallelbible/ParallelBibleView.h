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


// Displays one or more Bible translations side by side, each in its own
// TextDocumentView, plus an optional editable notes column backed by a
// personal SWORD module (see PersonalNotesModule). All columns are kept
// vertically aligned per verse (see VerseAligner) so a single BScrollBar
// is enough to scroll every column in sync. Each column has a header cell
// (a BMenuField for Bible columns, listing every installed "Biblical
// Texts" module, plus a small "x" button to remove that column; a plain
// "Notes" label + "x" for the notes column) plus a trailing "+" button to
// append another column -- see issue #11. The notes column never claims
// more than kMaxNotesWidthFraction of the total width (see
// _NotesColumnWidth()) -- a fixed cap for now; issue #19 tracks turning
// this into a real user-draggable divider. Every TextDocumentView gets
// B_DOCUMENT_BACKGROUND_COLOR instead of the B_PANEL_BACKGROUND_COLOR its
// class default constructs with, since these are reading/editing surfaces
// (Bible text, personal notes), not a details panel.
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
// ParallelBibleWindow) places it directly above the BScrollView that
// wraps this view, outside the scrolled hierarchy entirely, and this view
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

			BView*				HeaderView() const { return fHeaderView; }
			float				HeaderHeight() const
									{ return kHeaderHeight; }

			status_t			AddColumn(const char* moduleName);
			status_t			ReplaceColumn(int32 index,
									const char* moduleName);
			status_t			RemoveColumn(int32 index);
			int32				CountColumns() const;

			status_t			SetNotesEnabled(bool enabled);
			bool				NotesEnabled() const
									{ return fNotesView != NULL; }

			status_t			SetKey(const char* key);
			status_t			NextChapter();
			status_t			PrevChapter();

private:
			void				_RebuildLayout();
			void				_RebuildHeader();
			void				_PositionColumns();
			void				_UpdateScrollBars();
			void				_Realign();
			void				_ScrollToVerse(int verse);
			float				_ColumnWidth() const;
			float				_NotesColumnWidth() const;
			BPopUpMenu*			_BuildModuleMenu(int32 columnIndex,
									const char* markedModuleName);

private:
			SWMgr*				fManager;

			std::vector<SWModule*>	fModules;
			std::vector<BReference<BibleTextDocument> > fDocuments;
			std::vector<TextDocumentView*> fTextViews;
			std::vector<BMenuField*> fHeaderFields;
			std::vector<BButton*>	fRemoveButtons;

			PersonalNotesModule*	fNotes;
			BReference<BibleTextDocument> fNotesDocument;
			TextDocumentView*	fNotesView;
			BStringView*		fNotesLabel;
			BButton*			fRemoveNotesButton;

			BView*				fHeaderView;
			BButton*			fAddColumnButton;

			BString				fCurrentKey;
			float				fInitialWidth;
			float				fContentHeight;
			float				fContentWidth;

	static	const float			kMinColumnWidth;
	static	const float			kColumnSpacing;
	static	const float			kHeaderHeight;
	static	const float			kRemoveButtonWidth;
	static	const float			kMaxNotesWidthFraction;
};

#endif // PARALLEL_BIBLE_VIEW_H

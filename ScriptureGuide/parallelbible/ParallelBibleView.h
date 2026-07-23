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


// Displays one or more Bible translations side by side, each in its own
// TextDocumentView, plus an optional editable notes column backed by a
// personal SWORD module (see PersonalNotesModule). All columns are kept
// vertically aligned per verse (see VerseAligner) so a single BScrollBar
// is enough to scroll every column in sync. Each column has a header cell
// (a BMenuField for Bible columns, listing every installed "Biblical
// Texts" module; a plain label for the notes column) plus a trailing "+"
// button to append another column -- see issue #11.
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
// The header row needs to follow horizontal scrolling but must stay
// pinned while the columns scroll vertically, so this view is split into
// two nested children instead of positioning everything directly: this
// view itself (fParallelView, the BScrollView's target) owns only the
// horizontal scroll (its Bounds() origin.x moves, .y never does); it
// contains fHeaderContainer (the header cells, at a fixed y) and
// fContentView (the actual TextDocumentView columns), and it is
// fContentView -- not this view -- whose vertical scrollbar target is set
// explicitly in AttachedToWindow(), so only fContentView's Bounds()
// origin.y moves. Both children live in this view's coordinate space, so
// they automatically move together when this view's own horizontal
// Bounds() origin shifts -- no manual header/content sync code needed.
// Because none of this is layout-managed, and because none of the child
// TextDocumentViews are directly wrapped by their own BScrollView (this
// view is, so ScrollBar() inside a child returns NULL and its own
// _UpdateScrollBars() is a no-op), this view mirrors TextDocumentView's
// scrollbar-sync pattern for itself.
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
	virtual	void				MessageReceived(BMessage* message);

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
			float				_ColumnWidth() const;
			BPopUpMenu*			_BuildModuleMenu(int32 columnIndex,
									const char* markedModuleName);

private:
			SWMgr*				fManager;

			std::vector<SWModule*>	fModules;
			std::vector<BReference<BibleTextDocument> > fDocuments;
			std::vector<TextDocumentView*> fTextViews;
			std::vector<BMenuField*> fHeaderFields;

			PersonalNotesModule*	fNotes;
			BReference<BibleTextDocument> fNotesDocument;
			TextDocumentView*	fNotesView;

			BView*				fHeaderContainer;
			BView*				fContentView;
			BButton*			fAddColumnButton;

			BString				fCurrentKey;
			float				fInitialWidth;
			float				fContentHeight;
			float				fContentWidth;

	static	const float			kMinColumnWidth;
	static	const float			kColumnSpacing;
	static	const float			kHeaderHeight;
};

#endif // PARALLEL_BIBLE_VIEW_H

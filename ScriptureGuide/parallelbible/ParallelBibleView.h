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

class BGroupLayout;


// Displays one or more Bible translations side by side, each in its own
// TextDocumentView, plus an optional editable notes column backed by a
// personal SWORD module (see PersonalNotesModule). All columns are kept
// vertically aligned per verse (see VerseAligner) so a single BScrollBar
// is enough to scroll every column in sync.
//
// Meant to be embedded as the target of a BScrollView with a vertical
// scrollbar; this view does not create its own scrollbar.
class ParallelBibleView : public BView {
public:
								// initialWidth seeds the first column-width
								// calculation. It matters because this view
								// is still unattached (Bounds() degenerate)
								// while its window is being built, so pass
								// the constructing window's own frame width,
								// which unlike a layout-managed view's
								// Bounds() is already valid at that point.
								ParallelBibleView(const char* name,
									SWMgr* manager, float initialWidth = 0);
	virtual						~ParallelBibleView();

	virtual	void				AttachedToWindow();
	virtual	void				FrameResized(float width, float height);

			status_t			AddColumn(const char* moduleName);
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
			void				_Realign();
			float				_ColumnWidth() const;
			void				_ApplyColumnWidth(TextDocumentView* view,
									float width);

private:
			SWMgr*				fManager;
			BGroupLayout*		fGroupLayout;

			std::vector<SWModule*>	fModules;
			std::vector<BReference<BibleTextDocument> > fDocuments;
			std::vector<TextDocumentView*> fTextViews;

			PersonalNotesModule*	fNotes;
			BReference<BibleTextDocument> fNotesDocument;
			TextDocumentView*	fNotesView;

			BString				fCurrentKey;
			float				fInitialWidth;

	static	const float			kMinColumnWidth;
};

#endif // PARALLEL_BIBLE_VIEW_H

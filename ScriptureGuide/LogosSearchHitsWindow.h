/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef SEARCH_HITS_WINDOW_H
#define SEARCH_HITS_WINDOW_H

#include <vector>

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include "SwordBackend.h"

class BStringView;
class BScrollView;
// Both defined in LogosSearchHitsWindow.cpp, next to their one use --
// same reasoning as DictionaryEntryView (DictionaryWindow.cpp): each is
// a self-contained drawing view with no reuse elsewhere in this app.
class ChapterGridView;
class TreemapView;

#define SEARCHHITS_QUIT		'SHqu'
#define SEARCHHITS_JUMP		'SHjp'

// A generic "here is where a search landed" visualization -- modeled on
// bibleanalyzer.com's "Interactive Search Hits Chart": a per-chapter
// grid (one row per book, one square per chapter, highlighting which
// chapters actually have a hit, hoverable for a verse preview) plus a
// treemap (one rectangle per book that has at least one hit, sized by
// hit count). Takes a plain std::vector<SearchHit> rather than running
// a search itself, so it works for #83's Strong's-number search, a
// plain text search, or any future search that can produce the same
// shape -- the caller already did the actual searching (and, for a
// Strong's-number search, the SearchModule()-vs-FindStrongsWordsInText()
// validation #83 needed after real false positives turned up -- see
// LogosSearchWindow.cpp's own comment on that).
class SGSearchHitsWindow : public BWindow {
public:
							// Takes ownership of owner (deleted in the
							// destructor), same idiom as
							// SGSearchWindow/SGDictionaryWindow. `title`
							// is shown as a heading, e.g. "13 occurrences
							// of H7456 in ASV" or "104 chapters with
							// \"grace\" (AKJV)".
							SGSearchHitsWindow(BRect frame,
								const std::vector<SearchHit>& hits,
								const char* title, BMessenger* owner);
	virtual					~SGSearchHitsWindow();
	virtual bool			QuitRequested();
	virtual void			MessageReceived(BMessage* message);

						// Reuses the already-built window for a later
						// search instead of constructing a second one --
						// same lazy-build-then-Show()/Hide() idiom as
						// SGMainWindow's own fSearchWindow/fDictionaryWindow.
			void			SetHits(const std::vector<SearchHit>& hits,
								const char* title);

private:
			void			_BuildGUI();

			BMessenger*		fOwner;
			std::vector<SearchHit>	fHits;
			BString			fTitle;

			BStringView*	fTitleView;
			ChapterGridView*	fGridView;
			TreemapView*	fTreemapView;
};

#endif // SEARCH_HITS_WINDOW_H

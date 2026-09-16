/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef DICTIONARY_WINDOW_H
#define DICTIONARY_WINDOW_H

#include <map>
#include <vector>

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include "SwordBackend.h"

class BButton;
class BMenuField;
class BTextControl;
class SGSearchHitsWindow;
// #32: a plain BTextView subclass, not the app's own TextDocumentView
// engine every OTHER reference-clickable surface (Bible columns, notes,
// the description field) is built on -- defined in DictionaryWindow.cpp
// only, next to the one place it's used. A smaller, self-contained
// addition was the point: this window's entry display had no rich-text
// machinery at all before this, so there was nothing bigger to extend.
class DictionaryEntryView;
// A thin BColumnListView wrapper (defined in DictionaryWindow.cpp, next
// to its one use) -- replaced a plain BListView after it turned out to
// scale badly to a Strong's-numbered lexicon's several thousand keys:
// BListView::FrameResized() unconditionally re-measures every item on
// every resize tick (confirmed by reading Haiku's own ListView.cpp),
// which is what made dragging the sidebar/entry divider (see
// _BuildGUI()) laggy. BColumnListView's own OutlineView::FrameResized()
// only touches its tracked visible-rect bookkeeping, no per-item work.
class DictionaryResultListView;

#define DICT_SELECT_MODULE	'DCsm'
#define DICT_LOOKUP			'DClk'
#define DICT_SELECT_RESULT	'DCsr'
#define DICT_SHOW_STRONGS	'DCst'
#define DICT_QUIT			'DCqu'
// Which Bible module "Show Hits Chart" (below) searches -- a plain
// BMenuField over SwordBackend::SearchableModuleNames(), same idea as
// SGSearchWindow's own "Search in" field, since this window (unlike
// that one) has no reading-pane columns of its own to default to.
#define DICT_SELECT_BIBLE_MODULE	'DCbm'
// #107: statistics for whichever Strong's number is currently shown --
// same chapter-grid + treemap window a search result or a verse list
// opens (see SGSearchHitsWindow's own comment). Only enabled while
// fCurrentKey is actually a Strong's number in a Strong's lexicon (see
// _UpdateShowHitsButtonState()) -- an ordinary lexicon entry (AmTract,
// Hitchcock, ...) has no Bible occurrences to chart.
#define DICT_SHOW_HITS		'DCsh'

// A small, single-purpose lookup window for installed Lexicon/Dictionary
// modules (#31) -- pick a module, type a key or word, see the rendered
// entry; a plain-text search across the whole module (SGModule::
// SearchEntries()) if the typed text isn't a key match by itself. Also
// the display target for a clicked Strong's number (#27, see
// ShowStrongsNumber()) -- that path bypasses the module picker entirely
// and looks across every installed lexicon via SwordBackend::
// LookupStrongsNumber() for whichever one actually declares the matching
// Feature=GreekDef/HebrewDef, the same as a manual lookup would find if
// the user picked the right module by hand.
class SGDictionaryWindow : public BWindow {
public:
							// Does not take ownership of backend; it must
							// outlive this window (same lifetime
							// relationship SGMainWindow already has with
							// its own SwordBackend). Takes ownership of
							// owner (deleted in the destructor, same as
							// SGSearchWindow's fMessenger) -- only actually
							// used on real app shutdown (SGMainWindow::
							// QuitRequested() calls Quit() on this window
							// directly); QuitRequested() below means the
							// user-facing close box never gets here at all.
							SGDictionaryWindow(BRect frame,
								SwordBackend* backend, BMessenger* owner);
	virtual					~SGDictionaryWindow();
	// Hides instead of actually closing -- same idiom as
	// SGSearchWindow::QuitRequested(). The previous approach let the
	// window really Quit()/self-delete and asynchronously told
	// SGMainWindow to null out its now-dangling fDictionaryWindow via
	// DICT_QUIT sent from the destructor; that notification race really
	// crashed (confirmed via a real crash report): closing this window
	// and triggering EnsureDictionaryWindow() again (e.g. a Strong's-
	// number click) before SGMainWindow's message loop had actually
	// processed the queued DICT_QUIT left fDictionaryWindow non-NULL but
	// pointing at an already-destructed BWindow, so EnsureDictionaryWindow()
	// skipped creating a new one and called Show()/Activate() straight
	// into freed memory. Never actually destructing on close removes the
	// race entirely -- the pointer stays valid for the app's whole
	// lifetime once created, exactly like fSearchWindow already does.
	virtual bool			QuitRequested();
	virtual void			MessageReceived(BMessage* message);

			// Looks up and displays `strongsNumber` (e.g. "G3056")
			// directly, activating this window. Safe to call from
			// another window's thread -- posts to itself rather than
			// touching any control directly, same rationale as
			// SGSearchWindow::RunSearch().
			void			ShowStrongsNumber(const char* strongsNumber);

private:
			void			_BuildGUI();
			void			_RebuildModuleMenu();
			void			_LookupKey(const char* key);
			// Looks up `key` directly (no exact-match-then-search
			// fallback -- the caller already knows this key exists,
			// e.g. from fAllKeysByLexicon or a result the user just
			// clicked) and records it as fCurrentKey.
			void			_ShowEntryForKey(const BString& key);
			void			_ShowEntry(const BString& rawEntry);
			// Fills fAllKeysByLexicon[fCurrentLexicon] if not already
			// populated for it -- see SGModule::AllKeys()'s own comment
			// on why the walk itself is expensive enough to cache
			// (measured live: 186ms for GerStrongsGreek's 5522 keys,
			// proportionally more for StrongsHebrew's 8675), and
			// _PopulateAllKeysList()'s own comment on why that cache is
			// keyed per lexicon rather than a single slot cleared on
			// every switch -- switching back to a lexicon already
			// visited this session should be instant, not a second
			// full walk.
			void			_EnsureAllKeys();
			// Replaces fResultList's contents with every key of
			// fCurrentLexicon (see _EnsureAllKeys()), via
			// DictionaryResultListView::AddKeyRows() (one bulk
			// BColumnListView::AddRows() call) rather than one row at a
			// time -- measured live against the old plain-BListView
			// design: 736ms for 5522 individual AddItem() calls vs.
			// 351ms for the same items via one AddList() call, and this
			// runs synchronously on the window's own thread, which is
			// what made the whole window appear to freeze/disappear for
			// a second or two on a lexicon switch. Also sets
			// plain-text search (see _LookupKey()'s fallback) replaces
			// it with matches instead, temporarily, and clears that
			// flag; the next entry actually shown calls this again via
			// _ShowEntryForKey(), so the sidebar always settles back to
			// "everything, with where you are highlighted". Guarded by
			// that flag in _ShowEntryForKey() rather than called
			// unconditionally -- reported live: rebuilding all ~2000+
			// items and re-scrolling on every single click, even when
			// the list already held exactly this content, visibly
			// jittered for an entry far down the list (barely noticeable
			// near the top, which is why it looked fine for "A" and
			// broken for "N").
			void			_PopulateAllKeysList();
			// Selects and scrolls to `key` inside fResultList if
			// present, else clears the selection.
			void			_SelectKeyInList(const BString& key);
			// Marks `lexicon`'s own item in fModuleField's menu --
			// used when a Strong's-number lookup (see DICT_SHOW_STRONGS)
			// switches fCurrentLexicon to whichever module actually
			// answered it, so the module picker doesn't silently drift
			// out of sync with what the sidebar/entry now show.
			void			_SelectModuleInMenu(SGModule* lexicon);
			// fBibleModuleField's own items -- one per
			// SwordBackend::SearchableModuleNames(), defaulting to the
			// saved "module" preference (same fallback SGSearchWindow's
			// constructor uses) if it's among them, else the first.
			void			_RebuildBibleModuleMenu();
			// Enabled only while fCurrentKey is a genuine Strong's
			// number in a Strong's lexicon (StrongsPrefixForLexicon()
			// != 0) and a Bible module is selected -- called wherever
			// either of those can change: _ShowEntryForKey(),
			// DICT_SELECT_MODULE, and DICT_SHOW_STRONGS.
			void			_UpdateShowHitsButtonState();
			// DICT_SHOW_HITS -- builds the fully-prefixed number (adding
			// StrongsPrefixForLexicon()'s own prefix if fCurrentKey
			// doesn't already carry one -- see fCurrentKey's own
			// comment for why it doesn't always), runs the same
			// SearchModule()-vs-FindStrongsWordsInText()-validated
			// search #83 already needed after real false positives
			// turned up (see LogosSearchWindow.cpp's own comment on
			// that), and opens/reuses fHitsWindow with the result.
			void			_ShowHitsChart();

			SwordBackend*	fBackend;
			SGModule*		fCurrentLexicon;
			BMessenger*		fOwner;

			BMenuField*		fModuleField;
			BTextControl*	fLookupField;
			DictionaryResultListView*	fResultList;
			DictionaryEntryView*	fEntryView;

			// #107: "Show Hits Chart". fBibleModuleName is fBibleModuleField's
			// current selection -- a plain BString, not an SGModule* index,
			// since SearchableModuleNames() (unlike LexiconAt()) has no
			// stable index to key a message on.
			BMenuField*		fBibleModuleField;
			BString			fBibleModuleName;
			BButton*		fShowHitsButton;
			// Lazily built the first time DICT_SHOW_HITS fires, then just
			// Show()n/Hide()n again -- same idiom as SGSearchWindow's own
			// fHitsWindow.
			SGSearchHitsWindow*	fHitsWindow;

			// One entry per lexicon ever visited this session -- see
			// _EnsureAllKeys()'s own comment on why a single slot
			// cleared on every switch (the original design) cost a
			// full re-walk every time the user went back to a lexicon
			// they'd already opened once.
			std::map<SGModule*, std::vector<BString> >	fAllKeysByLexicon;
			BString			fCurrentKey;
			// See _PopulateAllKeysList()'s own comment.
			bool			fListShowingAllKeys;
};

#endif // DICTIONARY_WINDOW_H

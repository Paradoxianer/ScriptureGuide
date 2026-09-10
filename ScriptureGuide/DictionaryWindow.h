/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef DICTIONARY_WINDOW_H
#define DICTIONARY_WINDOW_H

#include <vector>

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include "SwordBackend.h"

class BButton;
class BListView;
class BMenuField;
class BScrollView;
class BStringView;
class BTextControl;
// #32: a plain BTextView subclass, not the app's own TextDocumentView
// engine every OTHER reference-clickable surface (Bible columns, notes,
// the description field) is built on -- defined in DictionaryWindow.cpp
// only, next to the one place it's used. A smaller, self-contained
// addition was the point: this window's entry display had no rich-text
// machinery at all before this, so there was nothing bigger to extend.
class DictionaryEntryView;

#define DICT_SELECT_MODULE	'DCsm'
#define DICT_LOOKUP			'DClk'
#define DICT_SELECT_RESULT	'DCsr'
#define DICT_SHOW_STRONGS	'DCst'
#define DICT_BROWSE_ALL		'DCba'
#define DICT_PREV_ENTRY		'DCpv'
#define DICT_NEXT_ENTRY		'DCnx'
#define DICT_QUIT			'DCqu'

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
			// e.g. from fAllKeys or a result the user just clicked) and
			// records it as fCurrentKey, which Prev/Next step from.
			void			_ShowEntryForKey(const BString& key);
			void			_ShowEntry(const BString& rawEntry);
			// Shows or hides fResultsLabel/fResultScroll together --
			// kept hidden except right after a lookup that fell back to
			// a multi-match search (see _LookupKey()), or while
			// browsing the whole module (see DICT_BROWSE_ALL).
			void			_ShowResultsList(bool show);
			// Fills fAllKeys from fCurrentLexicon if not already
			// populated for it -- see SGModule::AllKeys()'s own comment
			// on why this is cached rather than re-walked every time.
			void			_EnsureAllKeys();
			void			_StepEntry(int32 direction);

			SwordBackend*	fBackend;
			SGModule*		fCurrentLexicon;
			BMessenger*		fOwner;

			BMenuField*		fModuleField;
			BTextControl*	fLookupField;
			BStringView*	fResultsLabel;
			BListView*		fResultList;
			BScrollView*	fResultScroll;
			BStringView*	fEntryLabel;
			BButton*		fPrevButton;
			BButton*		fNextButton;
			DictionaryEntryView*	fEntryView;

			// Cached for fCurrentLexicon specifically -- cleared on
			// every module switch (see DICT_SELECT_MODULE) so a stale
			// list from a different module is never paged through.
			std::vector<BString>	fAllKeys;
			BString			fCurrentKey;
};

#endif // DICTIONARY_WINDOW_H

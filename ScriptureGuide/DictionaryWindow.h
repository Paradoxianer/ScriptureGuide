/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef DICTIONARY_WINDOW_H
#define DICTIONARY_WINDOW_H

#include <String.h>
#include <Window.h>

#include "SwordBackend.h"

class BButton;
class BListView;
class BMenuField;
class BScrollView;
class BTextControl;
class BTextView;

#define DICT_SELECT_MODULE	'DCsm'
#define DICT_LOOKUP			'DClk'
#define DICT_SELECT_RESULT	'DCsr'
#define DICT_SHOW_STRONGS	'DCst'

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
							// its own SwordBackend).
							SGDictionaryWindow(BRect frame,
								SwordBackend* backend);
	virtual					~SGDictionaryWindow();
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
			void			_ShowEntry(const BString& rawEntry);

			SwordBackend*	fBackend;
			SGModule*		fCurrentLexicon;

			BMenuField*		fModuleField;
			BTextControl*	fLookupField;
			BListView*		fResultList;
			BScrollView*	fResultScroll;
			BTextView*		fEntryView;
};

#endif // DICTIONARY_WINDOW_H

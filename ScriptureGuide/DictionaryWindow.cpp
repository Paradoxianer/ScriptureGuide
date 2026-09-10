/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "DictionaryWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DictionaryWindow"


// Lexicon/dictionary entries carry whatever raw markup their own render
// filter left behind, unstripped for a "plain" target -- the same kind
// of leak already seen (and worked around narrowly) for Bible verses'
// <w>/<note> tags elsewhere in this app, but a dictionary entry's tag
// vocabulary (<entryFree>, <orth>, <pron>, <lb/>, ...) is much less
// predictable, so this strips *any* "<...>" run generically rather than
// naming specific tags. Not real markup rendering (no bold/italic from
// e.g. <hi>), just enough to read the definition instead of raw angle
// brackets.
static BString
StripTags(const BString& text)
{
	BString result;
	bool inTag = false;
	for (int32 i = 0; i < text.Length(); i++) {
		char c = text[i];
		if (c == '<') {
			inTag = true;
			continue;
		}
		if (c == '>') {
			inTag = false;
			continue;
		}
		if (!inTag)
			result.Append(&c, 1);
	}
	return result;
}


SGDictionaryWindow::SGDictionaryWindow(BRect frame, SwordBackend* backend,
	BMessenger* owner)
	:
	BWindow(frame, B_TRANSLATE("Dictionary"), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS),
	fBackend(backend),
	fCurrentLexicon(NULL),
	fOwner(owner)
{
	_BuildGUI();
}


SGDictionaryWindow::~SGDictionaryWindow()
{
	fOwner->SendMessage(DICT_QUIT);
	delete fOwner;
}


bool
SGDictionaryWindow::QuitRequested()
{
	Hide();
	return false;
}


void
SGDictionaryWindow::_BuildGUI()
{
	BPopUpMenu* moduleMenu = new BPopUpMenu("dictModuleChoice");
	fModuleField = new BMenuField("dictModule", B_TRANSLATE("Dictionary "),
		moduleMenu);

	fLookupField = new BTextControl("dictLookup", B_TRANSLATE("Look up: "),
		"", new BMessage(DICT_LOOKUP));
	fLookupField->SetDivider(
		fLookupField->StringWidth(B_TRANSLATE("Look up: ")) + 5);

	BButton* lookupButton = new BButton("dictLookupButton",
		B_TRANSLATE("Look up"), new BMessage(DICT_LOOKUP));

	// #84: a numbered index to page through, not just a jump-to-one-key
	// lookup -- fills the results list below with every key the current
	// module has (see SGModule::AllKeys()) instead of search matches.
	BButton* browseButton = new BButton("dictBrowseButton",
		B_TRANSLATE("Browse"), new BMessage(DICT_BROWSE_ALL));

	// Labeled explicitly (reported: with no label, it wasn't obvious
	// what the *first* of the two scroll areas below was even for --
	// it's normally empty, only ever holding something after a lookup
	// that *didn't* match a key directly falls back to a search). Also
	// hidden by default and only shown once it actually has candidates
	// to pick from (see _LookupKey()/_ShowResults()) -- effectively the
	// "expand only when relevant" behavior asked for, without a real
	// collapse/expand control to build and wire up.
	fResultsLabel = new BStringView("dictResultsLabel",
		B_TRANSLATE("Search results (only shown if a lookup finds more "
			"than one match):"));
	fResultList = new BListView("dictResults", B_SINGLE_SELECTION_LIST);
	fResultList->SetSelectionMessage(new BMessage(DICT_SELECT_RESULT));
	fResultScroll = new BScrollView("dictResultsScroll", fResultList,
		0, false, true);
	fResultScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 80.0f));
	fResultsLabel->Hide();
	fResultScroll->Hide();

	fEntryLabel = new BStringView("dictEntryLabel", B_TRANSLATE("Entry:"));

	// #84: steps through fAllKeys regardless of whether the results list
	// is currently showing it -- lazily built on first use (see
	// _EnsureAllKeys()), so this works right after a plain lookup too,
	// not only after clicking Browse.
	fPrevButton = new BButton("dictPrevButton", B_TRANSLATE("◀ Previous"),
		new BMessage(DICT_PREV_ENTRY));
	fNextButton = new BButton("dictNextButton", B_TRANSLATE("Next ▶"),
		new BMessage(DICT_NEXT_ENTRY));

	fEntryView = new BTextView("dictEntry");
	fEntryView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	fEntryView->MakeEditable(false);
	fEntryView->SetWordWrap(true);
	fEntryView->SetInsets(4.0f, 4.0f, 4.0f, 4.0f);
	BScrollView* entryScroll = new BScrollView("dictEntryScroll", fEntryView,
		0, false, true);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(fModuleField)
		.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
			.Add(fLookupField)
			.Add(lookupButton)
			.Add(browseButton)
		.End()
		.Add(fResultsLabel)
		.Add(fResultScroll)
		.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
			.Add(fEntryLabel)
			.AddGlue()
			.Add(fPrevButton)
			.Add(fNextButton)
		.End()
		.Add(entryScroll)
	.End();

	_RebuildModuleMenu();

	SetSizeLimits(300.0f, 4000.0f, 300.0f, 4000.0f);
}


void
SGDictionaryWindow::_ShowResultsList(bool show)
{
	if (fResultsLabel->IsHidden() == !show)
		return;
	if (show) {
		fResultsLabel->Show();
		fResultScroll->Show();
	} else {
		fResultsLabel->Hide();
		fResultScroll->Hide();
	}
}


void
SGDictionaryWindow::_RebuildModuleMenu()
{
	BMenu* menu = fModuleField->Menu();
	while (menu->CountItems() > 0)
		delete menu->RemoveItem((int32)0);

	int32 count = fBackend->CountLexicons();
	for (int32 i = 0; i < count; i++) {
		SGModule* lexicon = fBackend->LexiconAt(i);
		if (lexicon == NULL)
			continue;

		BMessage* select = new BMessage(DICT_SELECT_MODULE);
		select->AddInt32("index", i);
		BMenuItem* item = new BMenuItem(lexicon->Name(), select);
		menu->AddItem(item);

		if (fCurrentLexicon == NULL) {
			fCurrentLexicon = lexicon;
			item->SetMarked(true);
		}
	}

	if (fModuleField->MenuItem() != NULL && fCurrentLexicon != NULL)
		fModuleField->MenuItem()->SetLabel(fCurrentLexicon->Name());
}


void
SGDictionaryWindow::_LookupKey(const char* key)
{
	if (fCurrentLexicon == NULL || key == NULL || key[0] == '\0')
		return;

	BString entry(fCurrentLexicon->GetEntry(key));
	if (!entry.IsEmpty()) {
		_ShowResultsList(false);
		fCurrentKey = key;
		_ShowEntry(entry);
		return;
	}

	// No exact key match -- fall back to a plain-text search across the
	// whole module (see SGModule::SearchEntries(), #31's "search
	// entries" requirement) and list the matching keys instead of
	// showing an entry directly; double-clicking one looks it up for
	// real via DICT_SELECT_RESULT.
	while (fResultList->CountItems() > 0)
		delete fResultList->RemoveItem((int32)0);

	std::vector<BString> matches = fCurrentLexicon->SearchEntries(key);
	for (size_t i = 0; i < matches.size(); i++)
		fResultList->AddItem(new BStringItem(matches[i].String()));

	if (matches.empty()) {
		_ShowResultsList(false);
		fEntryView->SetText(B_TRANSLATE("No matching entry found."));
	} else {
		_ShowResultsList(true);
		BString status;
		status.SetToFormat(
			B_TRANSLATE("%d matching entries -- pick one below."),
			(int)matches.size());
		fEntryView->SetText(status.String());
	}
}


void
SGDictionaryWindow::_ShowEntry(const BString& rawEntry)
{
	BString clean = StripTags(rawEntry);
	clean.Trim();
	fEntryView->SetText(clean.String());
}


void
SGDictionaryWindow::_ShowEntryForKey(const BString& key)
{
	if (fCurrentLexicon == NULL)
		return;
	fCurrentKey = key;
	_ShowEntry(fCurrentLexicon->GetEntry(key.String()));
}


void
SGDictionaryWindow::_EnsureAllKeys()
{
	if (!fAllKeys.empty() || fCurrentLexicon == NULL)
		return;
	fAllKeys = fCurrentLexicon->AllKeys();
}


void
SGDictionaryWindow::_StepEntry(int32 direction)
{
	if (fCurrentLexicon == NULL)
		return;

	_EnsureAllKeys();
	if (fAllKeys.empty())
		return;

	// Locate the current key rather than tracking a bare index: the
	// current entry could have been reached via a plain lookup, a search
	// result, or a Strong's-number click, none of which update an index
	// into fAllKeys directly -- only the key itself is known for certain.
	// A linear scan over up to ~8700 entries (StrongsHebrew, measured) is
	// cheap next to the whole-module walk that already happened once to
	// build the list.
	int32 index = -1;
	for (size_t i = 0; i < fAllKeys.size(); i++) {
		if (fAllKeys[i] == fCurrentKey) {
			index = (int32)i;
			break;
		}
	}

	// Not found (nothing looked up yet, or the current entry came from a
	// module AllKeys() doesn't agree came from fCurrentLexicon -- e.g. a
	// Strong's-number click, which never sets fCurrentLexicon to whichever
	// dictionary actually answered it) -- start from an end rather than
	// doing nothing.
	if (index < 0)
		index = direction > 0 ? -1 : (int32)fAllKeys.size();

	index += direction;
	if (index < 0 || (size_t)index >= fAllKeys.size())
		return;

	_ShowResultsList(false);
	_ShowEntryForKey(fAllKeys[index]);
}


void
SGDictionaryWindow::ShowStrongsNumber(const char* strongsNumber)
{
	BMessage msg(DICT_SHOW_STRONGS);
	msg.AddString("number", strongsNumber);
	BMessenger(this).SendMessage(&msg);
}


void
SGDictionaryWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case DICT_SELECT_MODULE:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				SGModule* lexicon = fBackend->LexiconAt(index);
				if (lexicon != NULL)
					fCurrentLexicon = lexicon;
			}
			while (fResultList->CountItems() > 0)
				delete fResultList->RemoveItem((int32)0);
			_ShowResultsList(false);
			// A different module: the cached index and the entry it
			// pointed at both belong to whatever module was current
			// before.
			fAllKeys.clear();
			fCurrentKey = "";
			break;
		}

		case DICT_LOOKUP:
		{
			_LookupKey(fLookupField->Text());
			break;
		}

		case DICT_SELECT_RESULT:
		{
			int32 selected = fResultList->CurrentSelection();
			if (selected >= 0 && fCurrentLexicon != NULL) {
				BStringItem* item
					= (BStringItem*)fResultList->ItemAt(selected);
				_ShowEntryForKey(item->Text());
			}
			break;
		}

		case DICT_BROWSE_ALL:
		{
			if (fCurrentLexicon == NULL)
				break;
			_EnsureAllKeys();
			while (fResultList->CountItems() > 0)
				delete fResultList->RemoveItem((int32)0);
			for (size_t i = 0; i < fAllKeys.size(); i++)
				fResultList->AddItem(new BStringItem(fAllKeys[i].String()));
			_ShowResultsList(true);
			if (!fAllKeys.empty() && fCurrentKey.IsEmpty())
				_ShowEntryForKey(fAllKeys[0]);
			break;
		}

		case DICT_PREV_ENTRY:
		{
			_StepEntry(-1);
			break;
		}

		case DICT_NEXT_ENTRY:
		{
			_StepEntry(1);
			break;
		}

		case DICT_SHOW_STRONGS:
		{
			BString number;
			if (message->FindString("number", &number) == B_OK) {
				_ShowResultsList(false);
				BString entry = fBackend->LookupStrongsNumber(
					number.String());
				if (entry.IsEmpty()) {
					// Two genuinely different situations, and telling
					// them apart is the difference between a message the
					// user can act on and one they can only shrug at:
					// either the whole dictionary for this language is
					// missing (install it), or it is present and simply
					// has no entry for this number (nothing to do).
					char prefix = number.Length() > 0
						? number.ByteAt(0) : '\0';
					const char* moduleName
						= SwordBackend::StrongsDictionaryNameFor(prefix);
					if (moduleName != NULL
						&& !fBackend->HasStrongsDictionary(prefix)) {
						BString missing(B_TRANSLATE(
							"No Strong's dictionary for these numbers is "
							"installed. Install the %module% module via "
							"Program > Book Manager to look up %number%."));
						missing.ReplaceFirst("%module%", moduleName);
						missing.ReplaceFirst("%number%", number);
						fEntryView->SetText(missing.String());
					} else {
						BString missing(B_TRANSLATE(
							"No entry for %number% in the installed "
							"Strong's dictionary."));
						missing.ReplaceFirst("%number%", number);
						fEntryView->SetText(missing.String());
					}
				} else {
					_ShowEntry(entry);
				}
			}
			Activate(true);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}

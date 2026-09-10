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
#include <Window.h>

#include "constants.h"

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


// #32: reference recognition already covers notes, commentary and the
// verse-list description field -- the one surface it never reached is a
// dictionary/lexicon entry, because fEntryView was a plain BTextView
// with no notion of a clickable span at all. This is deliberately NOT
// the app's own TextDocumentView engine those other surfaces share:
// this window's entry display had no rich-text machinery to extend in
// the first place, so a self-contained BTextView subclass -- SetText()
// plus a text_run_array for colour/underline, MouseDown() overridden to
// check a click against the same ranges -- is the smaller addition, not
// a rewrite of the window around a different view type.
class DictionaryEntryView : public BTextView {
public:
	DictionaryEntryView(const char* name, BMessenger* owner)
		:
		BTextView(name),
		fOwner(owner)
	{
		// Off by default -- confirmed empirically (a standalone probe
		// read back plain black/regular at an offset a text_run_array
		// had explicitly coloured/underlined) that SetText(text, runs)
		// silently ignores every run and renders everything in the
		// view's single uniform font/colour without this. Not
		// documented as a precondition on SetText() itself, only
		// discoverable by noticing IsStylable()/SetStylable() exist at
		// all and testing the theory.
		SetStylable(true);
	}

	// Replaces plain SetText(): detects references in `text` (the same
	// FindReferencesInText() every other surface uses, so what counts
	// as a reference here is never a second, drifting definition of it)
	// and colours/underlines each one via a text_run_array, remembering
	// their ranges so MouseDown() can tell a click on one from an
	// ordinary click landing elsewhere in the entry.
	void SetEntryText(const BString& text)
	{
		// FindReferencesInText() can return overlapping candidates --
		// the same "skip if this match starts before the previous one
		// ended" filter SGVerseListWindow's own description-field
		// restyling already needs for exactly this reason.
		std::vector<TextReference> allMatches
			= FindReferencesInText(text.String());
		fReferences.clear();
		int32 matchCursor = 0;
		for (size_t i = 0; i < allMatches.size(); i++) {
			if (allMatches[i].start < matchCursor)
				continue;
			fReferences.push_back(allMatches[i]);
			matchCursor = allMatches[i].start + allMatches[i].length;
		}

		if (fReferences.empty()) {
			SetText(text.String());
			return;
		}

		// Read back rather than assume black: whatever this view's own
		// default text colour actually is (theme-dependent), not a
		// hard-coded guess at it.
		BFont plainFont;
		rgb_color plainColor;
		GetFontAndColor(0, &plainFont, &plainColor);
		BFont linkFont(plainFont);
		linkFont.SetFace(B_UNDERSCORE_FACE);
		// Same colour as fReferenceLinkStyle elsewhere in this app
		// (BibleTextDocument.cpp) -- one reference-link colour, not a
		// second one invented for this window alone.
		rgb_color linkColor = { 0, 0, 200, 255 };

		int32 count = 1 + 2 * (int32)fReferences.size();
		size_t size = sizeof(text_run_array)
			+ (count - 1) * sizeof(text_run);
		text_run_array* runs = (text_run_array*)malloc(size);
		runs->count = count;
		int32 run = 0;
		runs->runs[run].offset = 0;
		runs->runs[run].font = plainFont;
		runs->runs[run].color = plainColor;
		run++;
		for (size_t i = 0; i < fReferences.size(); i++) {
			runs->runs[run].offset = fReferences[i].start;
			runs->runs[run].font = linkFont;
			runs->runs[run].color = linkColor;
			run++;
			runs->runs[run].offset = fReferences[i].start
				+ fReferences[i].length;
			runs->runs[run].font = plainFont;
			runs->runs[run].color = plainColor;
			run++;
		}

		SetText(text.String(), runs);
		free(runs);
	}

	virtual void MouseDown(BPoint where)
	{
		int32 offset = OffsetAt(where);
		for (size_t i = 0; i < fReferences.size(); i++) {
			if (offset < fReferences[i].start
				|| offset >= fReferences[i].start + fReferences[i].length) {
				continue;
			}
			if (fOwner != NULL) {
				BMessage jump(SG_BIBLE);
				jump.AddString("key", fReferences[i].normalizedKey);
				fOwner->SendMessage(&jump);
			}
			return;
		}
		BTextView::MouseDown(where);
	}

private:
	BMessenger*					fOwner;
	std::vector<TextReference>	fReferences;
};


SGDictionaryWindow::SGDictionaryWindow(BRect frame, SwordBackend* backend,
	BMessenger* owner)
	:
	BWindow(frame, B_TRANSLATE("Dictionary"), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS),
	fBackend(backend),
	fCurrentLexicon(NULL),
	fOwner(owner),
	fListShowingAllKeys(false)
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

	// Reported: a separate "Browse" button that expanded a normally-
	// hidden results list was less usable than just always having the
	// whole module's key list sitting there to click through. So there
	// is no more toggle: fResultList is a permanent sidebar, always
	// populated with every key of fCurrentLexicon (see
	// _PopulateAllKeysList()) except while a plain-text search's
	// matches are being shown instead (see _LookupKey()'s fallback) --
	// the next entry actually shown restores it via _ShowEntryForKey().
	// Also replaces Prev/Next (#84) entirely, since the list makes them
	// redundant -- browsing forward/backward through the module is just
	// clicking the next row.
	fResultsLabel = new BStringView("dictResultsLabel",
		B_TRANSLATE("Entries:"));
	fResultList = new BListView("dictResults", B_SINGLE_SELECTION_LIST);
	fResultList->SetSelectionMessage(new BMessage(DICT_SELECT_RESULT));
	fResultScroll = new BScrollView("dictResultsScroll", fResultList,
		0, false, true);
	fResultScroll->SetExplicitMinSize(BSize(150.0f, B_SIZE_UNSET));

	fEntryLabel = new BStringView("dictEntryLabel", B_TRANSLATE("Entry:"));

	fEntryView = new DictionaryEntryView("dictEntry", fOwner);
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
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
			.AddGroup(B_VERTICAL, B_USE_HALF_ITEM_SPACING)
				.Add(fResultsLabel)
				.Add(fResultScroll)
			.End()
			.AddGroup(B_VERTICAL, B_USE_HALF_ITEM_SPACING)
				.Add(fEntryLabel)
				.Add(entryScroll)
			.End()
		.End()
	.End();

	_RebuildModuleMenu();
	_PopulateAllKeysList();

	SetSizeLimits(300.0f, 4000.0f, 300.0f, 4000.0f);
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
		// _ShowEntryForKey() re-reads the module's own canonical key
		// text rather than trusting `key` as typed -- see its own
		// comment on why that match matters for the sidebar.
		_ShowEntryForKey(key);
		return;
	}

	// No exact key match -- fall back to a plain-text search across the
	// whole module (see SGModule::SearchEntries(), #31's "search
	// entries" requirement) and show the matches in the sidebar instead
	// of every key, temporarily; clicking one looks it up for real via
	// DICT_SELECT_RESULT, which restores the full list afterwards (see
	// _ShowEntryForKey()). Not any one key any more -- the label goes
	// back to plain "Entry:" until a result is actually picked.
	fCurrentKey = "";
	_UpdateEntryLabel();
	while (fResultList->CountItems() > 0)
		delete fResultList->RemoveItem((int32)0);
	fListShowingAllKeys = false;

	std::vector<BString> matches = fCurrentLexicon->SearchEntries(key);
	for (size_t i = 0; i < matches.size(); i++)
		fResultList->AddItem(new BStringItem(matches[i].String()));

	if (matches.empty()) {
		fEntryView->SetText(B_TRANSLATE("No matching entry found."));
	} else {
		BString status;
		status.SetToFormat(
			B_TRANSLATE("%d matching entries -- pick one from the list."),
			(int)matches.size());
		fEntryView->SetText(status.String());
	}
}


void
SGDictionaryWindow::_ShowEntry(const BString& rawEntry)
{
	BString clean = StripTags(rawEntry);
	clean.Trim();
	fEntryView->SetEntryText(clean);
}


void
SGDictionaryWindow::_UpdateEntryLabel()
{
	if (fCurrentKey.IsEmpty()) {
		fEntryLabel->SetText(B_TRANSLATE("Entry:"));
		return;
	}
	BString label(B_TRANSLATE("Entry: %key%"));
	label.ReplaceFirst("%key%", fCurrentKey);
	fEntryLabel->SetText(label.String());
}


void
SGDictionaryWindow::_ShowEntryForKey(const BString& key)
{
	if (fCurrentLexicon == NULL)
		return;
	BString entry = fCurrentLexicon->GetEntry(key.String());
	// The module's own canonical key text, not necessarily `key` as
	// passed in -- see _LookupKey()'s own comment on why this matters
	// for the sidebar-selection lookup just below. `key` here already
	// comes from fAllKeys/a search result, so it should already BE
	// canonical, but reading it back after GetEntry() actually resolved
	// it is one less thing to keep in sync by hand.
	fCurrentKey = fCurrentLexicon->GetModule()->getKeyText();
	_UpdateEntryLabel();

	// Only actually rebuild the sidebar if it isn't already showing
	// every key -- reported live: doing this unconditionally on every
	// single selection (tearing down and re-adding ~2000+ items just to
	// reselect one that was already there) visibly jittered for an
	// entry far down the list, since the rebuild's own re-scroll
	// fights the list's normal, immediate scroll-to-click behavior. A
	// search's matches (see _LookupKey()'s fallback) DO need replacing.
	if (!fListShowingAllKeys)
		_PopulateAllKeysList();
	_SelectKeyInList(fCurrentKey);

	_ShowEntry(entry);
}


void
SGDictionaryWindow::_EnsureAllKeys()
{
	if (!fAllKeys.empty() || fCurrentLexicon == NULL)
		return;
	fAllKeys = fCurrentLexicon->AllKeys();
}


void
SGDictionaryWindow::_PopulateAllKeysList()
{
	if (fCurrentLexicon == NULL)
		return;
	_EnsureAllKeys();
	while (fResultList->CountItems() > 0)
		delete fResultList->RemoveItem((int32)0);
	for (size_t i = 0; i < fAllKeys.size(); i++)
		fResultList->AddItem(new BStringItem(fAllKeys[i].String()));
	fListShowingAllKeys = true;
}


void
SGDictionaryWindow::_SelectKeyInList(const BString& key)
{
	for (int32 i = 0; i < fResultList->CountItems(); i++) {
		BStringItem* item = (BStringItem*)fResultList->ItemAt(i);
		if (key == item->Text()) {
			fResultList->Select(i);
			fResultList->ScrollToSelection();
			return;
		}
	}
	fResultList->DeselectAll();
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
			// A different module: the cached key list and the entry it
			// pointed at both belong to whatever module was current
			// before.
			fAllKeys.clear();
			fCurrentKey = "";
			_UpdateEntryLabel();
			_PopulateAllKeysList();
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
				// _ShowEntryForKey() re-selects this same list to sync
				// the sidebar after showing any entry, which sends this
				// same message right back -- asynchronously, through
				// this BLooper's own queue, so by the time it is
				// actually handled fCurrentKey already IS this item's
				// text and there is nothing left to do. Breaks that
				// cycle; see _ShowEntryForKey()'s own comment for why a
				// plain re-entrancy bool does not (confirmed live: it
				// does not, this recursed with one in place).
				if (item->Text() != fCurrentKey)
					_ShowEntryForKey(item->Text());
			}
			break;
		}

		case DICT_SHOW_STRONGS:
		{
			BString number;
			if (message->FindString("number", &number) == B_OK) {
				// A Strong's-number click never touches fLookupField --
				// this is the only place the number itself would ever
				// be visible anywhere in the window, found or not.
				fCurrentKey = number;
				_UpdateEntryLabel();
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

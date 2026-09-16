/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "DictionaryWindow.h"

#include <algorithm>

#include <Button.h>
#include <Catalog.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <LayoutBuilder.h>
#include <List.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

#include "constants.h"
#include "LogosSearchHitsWindow.h"
#include "Preferences.h"

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
	// PLUS, for a Strong's-numbered lexicon, in-entry cross-references
	// to other Strong's numbers (#110, e.g. GerStrongsGreek's own "von
	// 25" or StrongsGreek's "see GREEK for 25") via
	// FindStrongsCrossReferencesInText(). Colours/underlines both kinds
	// alike via a text_run_array, remembering their ranges (and which
	// kind each is) so MouseDown() can tell a click on one from an
	// ordinary click landing elsewhere in the entry.
	//
	// `strongsPrefix` is 0 for a non-Strong's lexicon (AmTract,
	// Hitchcock, ...) -- skips the second detector entirely rather than
	// scanning word-keyed prose for a pattern that can only ever exist
	// in a number-keyed one.
	void SetEntryText(const BString& text, char strongsPrefix)
	{
		// Both detectors can return overlapping candidates, and now
		// each other's matches too (unlikely given how structurally
		// different a Bible reference and a bare-number cross-reference
		// are, but merged and de-overlapped the same way regardless,
		// rather than trusting that by construction) -- the same "skip
		// if this match starts before the previous one ended" filter
		// SGVerseListWindow's own description-field restyling already
		// needs for exactly this reason.
		std::vector<TextReference> bibleMatches
			= FindReferencesInText(text.String());
		std::vector<StrongsCrossReference> strongsMatches;
		if (strongsPrefix != 0) {
			strongsMatches = FindStrongsCrossReferencesInText(
				text.String(), strongsPrefix);
		}

		std::vector<EntryLink> allMatches;
		for (size_t i = 0; i < bibleMatches.size(); i++) {
			EntryLink link;
			link.start = bibleMatches[i].start;
			link.length = bibleMatches[i].length;
			link.isStrongsNumber = false;
			link.target = bibleMatches[i].normalizedKey;
			allMatches.push_back(link);
		}
		for (size_t i = 0; i < strongsMatches.size(); i++) {
			EntryLink link;
			link.start = strongsMatches[i].start;
			link.length = strongsMatches[i].length;
			link.isStrongsNumber = true;
			link.target.SetToFormat("%c%s", strongsMatches[i].language,
				strongsMatches[i].number.String());
			allMatches.push_back(link);
		}
		std::sort(allMatches.begin(), allMatches.end(), &EntryLink::Before);

		fLinks.clear();
		int32 matchCursor = 0;
		for (size_t i = 0; i < allMatches.size(); i++) {
			if (allMatches[i].start < matchCursor)
				continue;
			fLinks.push_back(allMatches[i]);
			matchCursor = allMatches[i].start + allMatches[i].length;
		}

		if (fLinks.empty()) {
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
		// (BibleTextDocument.cpp) -- one reference-link colour for both
		// kinds, not a second one invented for this window alone.
		rgb_color linkColor = { 0, 0, 200, 255 };

		int32 count = 1 + 2 * (int32)fLinks.size();
		size_t size = sizeof(text_run_array)
			+ (count - 1) * sizeof(text_run);
		text_run_array* runs = (text_run_array*)malloc(size);
		runs->count = count;
		int32 run = 0;
		runs->runs[run].offset = 0;
		runs->runs[run].font = plainFont;
		runs->runs[run].color = plainColor;
		run++;
		for (size_t i = 0; i < fLinks.size(); i++) {
			runs->runs[run].offset = fLinks[i].start;
			runs->runs[run].font = linkFont;
			runs->runs[run].color = linkColor;
			run++;
			runs->runs[run].offset = fLinks[i].start + fLinks[i].length;
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
		for (size_t i = 0; i < fLinks.size(); i++) {
			if (offset < fLinks[i].start
				|| offset >= fLinks[i].start + fLinks[i].length) {
				continue;
			}
			if (fLinks[i].isStrongsNumber) {
				// Same message DICT_SHOW_STRONGS already handles for a
				// Strong's-number click from Bible text -- posted to
				// this window itself (Window(), not fOwner, which
				// targets SGMainWindow) so it gets the exact same
				// module-switch + sidebar-selection behaviour for free.
				BMessage lookup(DICT_SHOW_STRONGS);
				lookup.AddString("number", fLinks[i].target);
				if (Window() != NULL)
					Window()->PostMessage(&lookup);
			} else if (fOwner != NULL) {
				BMessage jump(SG_BIBLE);
				jump.AddString("key", fLinks[i].target);
				fOwner->SendMessage(&jump);
			}
			return;
		}
		BTextView::MouseDown(where);
	}

private:
	// One clickable span inside the entry, either kind -- see
	// SetEntryText()'s own comment on why both live in one merged,
	// de-overlapped list rather than two separate ones.
	struct EntryLink {
		int32	start;
		int32	length;
		bool	isStrongsNumber;
		BString	target;	// normalizedKey, or "G1234"/"H1234"

		static bool Before(const EntryLink& a, const EntryLink& b)
		{
			return a.start < b.start;
		}
	};

	BMessenger*				fOwner;
	std::vector<EntryLink>	fLinks;
};


// The sidebar's own list widget -- a plain BListView until it turned out
// not to scale to a Strong's-numbered lexicon's several thousand keys.
// Confirmed by reading Haiku's own ListView.cpp: BListView::FrameResized()
// unconditionally calls _UpdateItems(), which re-measures EVERY item on
// EVERY resize tick, regardless of whether it's actually visible -- this
// is what made dragging the sidebar/entry divider (see _BuildGUI()) laggy
// at that item count. BColumnListView's own OutlineView::FrameResized()
// only updates its own tracked visible-rect bookkeeping; no per-item
// work at all. Modeled directly on LogosVerseListWindow.cpp's own
// VerseListRowListView -- same CountItems()/CurrentSelection(int32)/
// Select(int32)/MakeEmpty() position-based wrappers, so every *caller*
// here keeps the same BListView-shaped mental model it already had.
class DictionaryResultListView : public BColumnListView {
public:
	DictionaryResultListView(const char* name)
		:
		BColumnListView(name, B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS,
			B_FANCY_BORDER, true)
	{
		SetSelectionMode(B_SINGLE_SELECTION_LIST);
		// Off: these rows reflect a lexicon's own natural key order --
		// numeric for a Strong's-numbered module, alphabetic for a
		// word-keyed one -- and a header-click re-sort as a plain
		// string would put "00010" before "00002".
		SetSortingEnabled(false);
		// No latch column -- these rows are always flat, never nested,
		// so the space BColumnListView normally reserves on the left
		// for an expand/collapse arrow is just wasted width here.
		SetLatchWidth(0);
		AddColumn(new BStringColumn("", 200, 40, 1000, B_TRUNCATE_END), 0);
	}

	int32 CountItems() { return CountRows(); }

	// Position-based, matching fCurrentKey/fAllKeysByLexicon's own
	// index-free bookkeeping. CurrentSelection(BRow*) walks a linked
	// chain of *pointers*, not indices -- see VerseListRowListView's own
	// comment on the same pattern.
	int32 CurrentSelection(int32 index = 0)
	{
		BRow* row = BColumnListView::CurrentSelection();
		for (int32 i = 0; i < index && row != NULL; i++)
			row = BColumnListView::CurrentSelection(row);
		return row != NULL ? IndexOf(row) : -1;
	}

	void Select(int32 index)
	{
		DeselectAll();
		BRow* row = RowAt(index);
		if (row != NULL)
			AddToSelection(row);
	}

	void MakeEmpty() { Clear(); }

	// One BColumnListView::AddRows() bulk call rather than one AddRow()
	// per key -- measured live against the plain-BListView design this
	// replaced: 736ms for 5522 individual BListView::AddItem() calls
	// vs. 351ms for the same items via one BListView::AddList() call;
	// BColumnListView::AddRows() batches its own relayout/invalidate the
	// same way (confirmed by reading ColumnListView.cpp), so this keeps
	// that win rather than trading it away for the resize-lag fix.
	void AddKeyRows(const std::vector<BString>& keys)
	{
		BList rows((int32)keys.size());
		for (size_t i = 0; i < keys.size(); i++) {
			BRow* row = new BRow();
			row->SetField(new BStringField(keys[i].String()), 0);
			rows.AddItem(row);
		}
		AddRows(&rows, -1, NULL);
	}

	const char* TextAt(int32 index)
	{
		BRow* row = RowAt(index);
		if (row == NULL)
			return NULL;
		BStringField* field = (BStringField*)row->GetField(0);
		return field != NULL ? field->String() : NULL;
	}
};


SGDictionaryWindow::SGDictionaryWindow(BRect frame, SwordBackend* backend,
	BMessenger* owner)
	:
	// B_FLOATING_APP_WINDOW_FEEL, not B_TITLED_WINDOW's own
	// B_NORMAL_WINDOW_FEEL -- asked for: repeated Strong's-number clicks
	// from Bible text kept needing this window re-activated/re-found
	// rather than just staying visible above the main window it serves,
	// the way a reference/tool window is expected to.
	BWindow(frame, B_TRANSLATE("Dictionary"), B_TITLED_WINDOW_LOOK,
		B_FLOATING_APP_WINDOW_FEEL, B_ASYNCHRONOUS_CONTROLS),
	fBackend(backend),
	fCurrentLexicon(NULL),
	fOwner(owner),
	fHitsWindow(NULL),
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

	// #107: which Bible to search when "Show Hits Chart" runs -- this
	// window (unlike SGSearchWindow) has no reading-pane columns of its
	// own to offer, so SearchableModuleNames() (every installed Bible/
	// Commentary) fills the same role SGSearchWindow's own "Search in"
	// field does.
	BPopUpMenu* bibleModuleMenu = new BPopUpMenu("dictBibleModuleChoice");
	fBibleModuleField = new BMenuField("dictBibleModule",
		B_TRANSLATE("Search in "), bibleModuleMenu);
	fShowHitsButton = new BButton("dictShowHitsButton",
		B_TRANSLATE("Show Hits Chart"), new BMessage(DICT_SHOW_HITS));
	fShowHitsButton->SetEnabled(false);

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
	//
	// No "Entries:"/"Entry: <key>" labels above either side any more,
	// either -- reported: once the sidebar always shows every key with
	// the current one highlighted, a label repeating that same key in
	// words next to it was pure redundancy.
	// DictionaryResultListView (BColumnListView-backed, see its own
	// comment) manages its own scrolling -- no separate BScrollView
	// wrapper needed the way a plain BListView required.
	fResultList = new DictionaryResultListView("dictResults");
	fResultList->SetSelectionMessage(new BMessage(DICT_SELECT_RESULT));
	// Reported: 150px made the collapse-or-snap-to-minimum zone (see
	// _BuildGUI()'s own comment on the split below) feel too aggressive
	// -- collapsing the sidebar entirely is a real, wanted feature, just
	// not one that should trigger this early. A narrower floor still
	// shows a short key before truncating, while leaving much more room
	// to drag before the collapse threshold kicks in.
	fResultList->SetExplicitMinSize(BSize(77.0f, B_SIZE_UNSET));

	fEntryView = new DictionaryEntryView("dictEntry", fOwner);
	fEntryView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	fEntryView->MakeEditable(false);
	fEntryView->SetWordWrap(true);
	fEntryView->SetInsets(4.0f, 4.0f, 4.0f, 4.0f);
	BScrollView* entryScroll = new BScrollView("dictEntryScroll", fEntryView,
		0, false, true);

	// A plain horizontal Group gave the sidebar a fixed width -- asked
	// for a draggable divider instead, so the sidebar can be widened for
	// a lexicon with long keys (or narrowed out of the way) without
	// resizing the whole window. BSplitView (via BLayoutBuilder::Split)
	// is exactly this, and is otherwise unused elsewhere in this app.
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(fModuleField)
		.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
			.Add(fLookupField)
			.Add(lookupButton)
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
			.Add(fBibleModuleField)
			.Add(fShowHitsButton)
		.End()
		// Weighted 1:2 for its initial size only -- the divider drags
		// freely from there, fResultList's own min-width (above) is
		// the only hard floor. Collapsible left at its default (true):
		// BSplitLayout snaps a pane shut once it's dragged past half its
		// minimum size (confirmed by reading Haiku's own
		// SplitLayout.cpp) -- initially turned off here after that felt
		// like an on/off toggle at a 150px minimum, but collapsing the
		// sidebar away entirely is a real, wanted feature on its own;
		// the actual fix was narrowing fResultList's own minimum
		// (above) so the collapse threshold sits much closer to fully
		// closed instead of eating most of the usable drag range.
		.AddSplit(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
			.Add(fResultList, 1.0f)
			.Add(entryScroll, 2.0f)
		.End()
	.End();

	_RebuildModuleMenu();
	_PopulateAllKeysList();
	_RebuildBibleModuleMenu();

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
SGDictionaryWindow::_SelectModuleInMenu(SGModule* lexicon)
{
	if (lexicon == NULL)
		return;
	BMenu* menu = fModuleField->Menu();
	if (fModuleField->MenuItem() != NULL)
		fModuleField->MenuItem()->SetMarked(false);
	BMenuItem* item = menu->FindItem(lexicon->Name());
	if (item != NULL)
		item->SetMarked(true);
	if (fModuleField->MenuItem() != NULL)
		fModuleField->MenuItem()->SetLabel(lexicon->Name());
}


void
SGDictionaryWindow::_RebuildBibleModuleMenu()
{
	BMenu* menu = fBibleModuleField->Menu();
	while (menu->CountItems() > 0)
		delete menu->RemoveItem((int32)0);

	std::vector<BString> names = fBackend->SearchableModuleNames();

	// The saved "module" preference (same fallback SGSearchWindow's own
	// constructor uses when it has no reading-pane columns to offer
	// either) if it's actually installed, else whatever sorts first --
	// SearchableModuleNames() itself makes no promise about order.
	BString preferred;
	prefsLock.Lock();
	if (preferences.FindString("module", &preferred) != B_OK)
		preferred = "";
	prefsLock.Unlock();

	std::sort(names.begin(), names.end());

	// Keeps whatever was already selected (a previous rebuild, or the
	// user's own pick via DICT_SELECT_BIBLE_MODULE) if it's still
	// installed; otherwise falls back to `preferred`, then to whatever
	// sorts first.
	BString keep = fBibleModuleName;
	fBibleModuleName = "";
	int32 markIndex = -1;
	for (size_t i = 0; i < names.size(); i++) {
		if (names[i] == keep)
			markIndex = (int32)i;
		else if (markIndex < 0 && names[i] == preferred)
			markIndex = (int32)i;
	}
	if (markIndex < 0 && !names.empty())
		markIndex = 0;

	for (size_t i = 0; i < names.size(); i++) {
		BMessage* select = new BMessage(DICT_SELECT_BIBLE_MODULE);
		select->AddString("module", names[i]);
		BMenuItem* item = new BMenuItem(names[i].String(), select);
		menu->AddItem(item);
		if ((int32)i == markIndex) {
			item->SetMarked(true);
			fBibleModuleName = names[i];
		}
	}
	if (fBibleModuleField->MenuItem() != NULL)
		fBibleModuleField->MenuItem()->SetLabel(fBibleModuleName.String());
}


void
SGDictionaryWindow::_UpdateShowHitsButtonState()
{
	bool isStrongsNumber = fCurrentLexicon != NULL
		&& SwordBackend::StrongsPrefixForLexicon(fCurrentLexicon) != 0
		&& !fCurrentKey.IsEmpty();
	fShowHitsButton->SetEnabled(isStrongsNumber && !fBibleModuleName.IsEmpty());
}


void
SGDictionaryWindow::_ShowHitsChart()
{
	if (fCurrentLexicon == NULL || fCurrentKey.IsEmpty()
		|| fBibleModuleName.IsEmpty()) {
		return;
	}

	char prefix = SwordBackend::StrongsPrefixForLexicon(fCurrentLexicon);
	if (prefix == 0)
		return;

	// fCurrentKey already carries the prefix when it came from a Bible-
	// text click (DICT_SHOW_STRONGS), but not when it came from browsing
	// the sidebar list directly (_ShowEntryForKey() sets it to the raw
	// module key) -- see fCurrentKey's own comment.
	BString fullNumber = fCurrentKey;
	if (fullNumber.ByteAt(0) != prefix) {
		// The module's own raw key text is zero-padded ("00026", not
		// "26" -- confirmed live: this produced "G00026", a number
		// SEARCHTYPE_ENTRYATTR's "Word//Lemma./<number>/" never matches
		// anything with, silently returning zero hits instead of an
		// error). Strip the padding before combining with the prefix.
		int32 firstNonZero = 0;
		while (firstNonZero < fullNumber.Length() - 1
			&& fullNumber.ByteAt(firstNonZero) == '0') {
			firstNonZero++;
		}
		fullNumber.Remove(0, firstNonZero);
		BString withPrefix;
		withPrefix << prefix << fullNumber;
		fullNumber = withPrefix;
	}

	SGModule* bibleModule = fBackend->FindModule(fBibleModuleName.String());
	if (bibleModule == NULL)
		return;

	// Same "Word//Lemma./<number>/" SEARCHTYPE_ENTRYATTR wrapping and
	// FindStrongsWordsInText() re-validation LogosSearchWindow.cpp's own
	// #83 handler uses, and for the same reason: SWORD's own search
	// index can report a hit the module's own attribute data does not
	// actually back up (confirmed live against a real installed module --
	// see that handler's own comment). No book-range/case-sensitivity
	// controls here (this window has none), so the whole Bible,
	// case-insensitive -- the same defaults the search window itself
	// starts with.
	BString wrapped("Word//Lemma./");
	wrapped << fullNumber << "/";
	std::vector<const char*> books = GetBookNames();
	// -3 == SWORD's own SEARCHTYPE_ENTRYATTR (swmodule.h's search() doc
	// comment) -- LogosSearchWindow.h names this SEARCH_STRONGS, but
	// that enum is window-local (its own comment explains why), not
	// worth pulling in that whole header just for one constant.
	std::vector<BString> rawHits = bibleModule->SearchModule(
		-3, REG_ICASE, wrapped.String(), books.front(),
		books.back(), NULL);

	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	std::vector<BString> verseList;
	for (size_t i = 0; i < rawHits.size(); i++) {
		sword::VerseKey key(rawHits[i].String());
		key.setLocale(language.Code());
		BString verseText(bibleModule->GetVerse(rawHits[i].String()));
		std::vector<StrongsWord> words
			= FindStrongsWordsInText(bibleModule->GetModule(), verseText);
		bool confirmed = false;
		for (size_t w = 0; w < words.size() && !confirmed; w++) {
			if (words[w].strongsNumber == fullNumber)
				confirmed = true;
		}
		if (confirmed)
			verseList.push_back(rawHits[i]);
	}

	std::vector<SearchHit> hits = BuildSearchHits(bibleModule, verseList);

	BString title;
	title.SetToFormat(
		B_TRANSLATE("%d occurrences of %s in %s"),
		(int)hits.size(), fullNumber.String(), bibleModule->FullName());

	if (fHitsWindow == NULL) {
		BRect r(Frame());
		r.OffsetBy(30, 30);
		r.right = r.left + 520;
		r.bottom = r.top + 420;
		fHitsWindow = new SGSearchHitsWindow(r, hits, title.String(),
			new BMessenger(*fOwner));
	} else {
		fHitsWindow->SetHits(hits, title.String());
	}
	fHitsWindow->Show();
	fHitsWindow->Activate(true);
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
	// _ShowEntryForKey()). Not any one key any more until a result is
	// actually picked.
	fCurrentKey = "";
	fResultList->MakeEmpty();
	fListShowingAllKeys = false;

	std::vector<BString> matches = fCurrentLexicon->SearchEntries(key);
	fResultList->AddKeyRows(matches);

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
	// 0 for a non-Strong's lexicon (AmTract, Hitchcock, ...) -- see
	// SetEntryText()'s own comment on why that skips in-entry
	// cross-reference detection (#110) entirely rather than scanning
	// word-keyed prose for a pattern that only exists in a
	// number-keyed one.
	char strongsPrefix = SwordBackend::StrongsPrefixForLexicon(fCurrentLexicon);
	fEntryView->SetEntryText(clean, strongsPrefix);
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
	// comes from fAllKeysByLexicon/a search result, so it should already BE
	// canonical, but reading it back after GetEntry() actually resolved
	// it is one less thing to keep in sync by hand.
	fCurrentKey = fCurrentLexicon->GetModule()->getKeyText();

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
	_UpdateShowHitsButtonState();
}


void
SGDictionaryWindow::_EnsureAllKeys()
{
	if (fCurrentLexicon == NULL)
		return;
	if (fAllKeysByLexicon.find(fCurrentLexicon) != fAllKeysByLexicon.end())
		return;
	fAllKeysByLexicon[fCurrentLexicon] = fCurrentLexicon->AllKeys();
}


void
SGDictionaryWindow::_PopulateAllKeysList()
{
	if (fCurrentLexicon == NULL)
		return;
	_EnsureAllKeys();
	fResultList->MakeEmpty();
	fResultList->AddKeyRows(fAllKeysByLexicon[fCurrentLexicon]);
	fListShowingAllKeys = true;
}


void
SGDictionaryWindow::_SelectKeyInList(const BString& key)
{
	for (int32 i = 0; i < fResultList->CountItems(); i++) {
		if (key == fResultList->TextAt(i)) {
			fResultList->Select(i);
			fResultList->ScrollTo(fResultList->RowAt(i));
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
			// A different module: the entry the old key pointed to
			// belongs to whatever module was current before -- the key
			// list itself stays cached per lexicon (fAllKeysByLexicon),
			// so switching back to this module later costs nothing.
			fCurrentKey = "";
			_PopulateAllKeysList();
			_UpdateShowHitsButtonState();
			break;
		}

		case DICT_SELECT_BIBLE_MODULE:
		{
			BString module;
			if (message->FindString("module", &module) == B_OK) {
				fBibleModuleName = module;
				if (fBibleModuleField->MenuItem() != NULL)
					fBibleModuleField->MenuItem()->SetLabel(module.String());
				_UpdateShowHitsButtonState();
			}
			break;
		}

		case DICT_SHOW_HITS:
		{
			_ShowHitsChart();
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
			const char* text = selected >= 0
				? fResultList->TextAt(selected) : NULL;
			if (text != NULL && fCurrentLexicon != NULL) {
				// _ShowEntryForKey() re-selects this same list to sync
				// the sidebar after showing any entry, which sends this
				// same message right back -- asynchronously, through
				// this BLooper's own queue, so by the time it is
				// actually handled fCurrentKey already IS this item's
				// text and there is nothing left to do. Breaks that
				// cycle; see _ShowEntryForKey()'s own comment for why a
				// plain re-entrancy bool does not (confirmed live: it
				// does not, this recursed with one in place).
				if (fCurrentKey != text)
					_ShowEntryForKey(text);
			}
			break;
		}

		case DICT_SHOW_STRONGS:
		{
			BString number;
			if (message->FindString("number", &number) == B_OK) {
				SGModule* lexicon = NULL;
				BString entry = fBackend->LookupStrongsNumber(
					number.String(), &lexicon);
				if (entry.IsEmpty()) {
					// Two genuinely different situations, and telling
					// them apart is the difference between a message the
					// user can act on and one they can only shrug at:
					// either the whole dictionary for this language is
					// missing (install it), or it is present and simply
					// has no entry for this number (nothing to do).
					fCurrentKey = number;
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
					// Reported: nothing in the window showed which
					// number this was once the old "Entry: <key>" label
					// (its only home) was removed. Rather than re-adding
					// a label, route through the exact same
					// module-switch + sidebar-selection plumbing a
					// manual pick-a-module-then-look-up-a-key already
					// gets -- LookupStrongsNumber() now reports which
					// lexicon actually answered, so this can adopt it as
					// fCurrentLexicon instead of leaving the module
					// picker and sidebar showing something unrelated.
					if (lexicon != fCurrentLexicon) {
						fCurrentLexicon = lexicon;
						fListShowingAllKeys = false;
						_SelectModuleInMenu(lexicon);
					}
					// LookupStrongsNumber() strips the "G"/"H" prefix
					// before calling GetEntry() on `lexicon` -- passing
					// the prefixed form to _ShowEntryForKey() here would
					// just fail to resolve against this same lexicon's
					// real (unprefixed) keys.
					BString bareNumber(number);
					bareNumber.Remove(0, 1);
					_ShowEntryForKey(bareNumber);
				}
			}
			_UpdateShowHitsButtonState();
			Activate(true);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}

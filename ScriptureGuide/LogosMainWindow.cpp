#include "LogosMainWindow.h"

#include <Alert.h>
#include <Application.h>
#include <AboutWindow.h>
#include <Button.h>
#include <Bitmap.h>
#include <Box.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <String.h>
#include <ControlLook.h>
#include <GroupView.h>
#include <IconUtils.h>
#include <Resources.h>
#include <ToolBar.h>



#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>

#include "constants.h"
#include "ExportFormats.h"
#include "FontPanel.h"
#include "LogosApp.h"
#include "Preferences.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"


// Renders one of this app's own #'VICN' resources (see
// ScriptureGuide.rdef) into a bitmap at the size the current control look
// wants for a toolbar icon, so the buttons scale with the user's font
// size instead of being pinned to one pixel size. NULL if the resource is
// missing -- BToolBar::AddAction() accepts a NULL icon and simply draws a
// text-only button, so a failure here costs the picture, not the button.
// Caller owns the returned bitmap; AddAction() copies it.
static BBitmap*
_LoadVectorIcon(const char* name)
{
	app_info info;
	if (be_app->GetAppInfo(&info) != B_OK)
		return NULL;

	BFile file(&info.ref, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return NULL;

	BResources resources(&file);
	if (resources.InitCheck() != B_OK)
		return NULL;

	size_t dataSize = 0;
	const void* data = resources.LoadResource(B_VECTOR_ICON_TYPE, name,
		&dataSize);
	if (data == NULL || dataSize == 0)
		return NULL;

	BSize iconSize = BControlLook::ComposeIconSize(B_MINI_ICON);
	BBitmap* bitmap = new(std::nothrow) BBitmap(
		BRect(BPoint(0, 0), iconSize), 0, B_RGBA32);
	if (bitmap == NULL || bitmap->InitCheck() != B_OK
		|| BIconUtils::GetVectorIcon((const uint8*)data, dataSize, bitmap)
			!= B_OK) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}

SGMainWindow::SGMainWindow(BRect frame, const char* module, const char* key,
		uint16 selectVers, uint16 selectVersEnd )
 :	BWindow(frame, "Scripture Guide", B_DOCUMENT_WINDOW, 0),
 	// Starts true so nothing the constructor itself does -- restoring the
	// saved column layout, applying the startup key -- lands in the
	// history. Cleared at the end of the constructor, once the window is
	// actually sitting where the user left it. Otherwise Back came up
	// enabled on a fresh launch and led somewhere the user had never been.
 	fRestoringHistory(true),
	fBackItem(NULL),
	fForwardItem(NULL),
	fToolBar(NULL),
	fSearchWindow(NULL),
	fDictionaryWindow(NULL),
 	fModManager(NULL),
 	fCurrentModule(NULL),
 	fCurrentChapter(1),
	fFontPanel(NULL),
 	fFindMessenger(NULL)
{
	fCurrentVerse = selectVers;
	fCurrentVerseEnd = selectVersEnd;

	// BuildGUI() below marks fShowVerseNumItem from this; the real value
	// (if any was saved) isn't loaded until LoadPrefsForModule() runs,
	// well after BuildGUI() -- default to the same "on" LoadPrefsForModule()
	// itself falls back to when there's nothing saved yet.
	fShowVerseNumbers = true;
	fShowStrongsNumbers = true;
	fShowCrossReferences = true;

	fModManager = new SwordBackend();
	BuildGUI();

	// BLayout invalidation is asynchronous (see BWindow::
	// UpdateSizeLimits()'s own doc comment: layout normally only
	// resolves once the deferred B_LAYOUT_WINDOW message reaches this
	// window's own message loop) -- but RestoreColumnLayout() below adds
	// every saved column (each triggering ParallelBibleView::
	// _RebuildLayout()/_PositionColumns()) while this window hasn't even
	// been Show()n yet, let alone pumped that deferred message. Without
	// forcing it now, fParallelView's Bounds() stays the canonical
	// degenerate placeholder (Height() == -1) through the entire restore
	// -- and, confirmed via live debug logging, can keep reading that
	// way through any number of further structural changes after Show()
	// too, since each one can itself re-invalidate the layout before the
	// original deferred message ever gets its turn. A notes column's
	// scrollbar range in particular has no other self-healing path for
	// this (unlike a Bible column's own TextDocumentView, which reacts
	// to its own later, real FrameResized()) -- it silently looked
	// present but had ~zero usable range until some unrelated later
	// interaction (any navigation, e.g. a search) finally forced a fresh
	// layout pass. Forcing it here, before RestoreColumnLayout() runs,
	// gives fParallelView a real Bounds() from the very first column it
	// restores.
	Layout(true);


	// More voodoo hackerdom to work around a bug. :)
	AddCommonFilter(new EndKeyFilter);
	AddCommonFilter(new UniversalSearchEnterFilter(fUniversalSearchBox));

	if (fModManager->CountModules()==0)
	{
		// TODO: fail
		return;
	}
	
	SetModuleFromString(module);
	if (!fCurrentModule)
	{
		// It's possible for this call to fail, so we'll handle it as best we can. 
		// Seeing how we managed to get this far, there has to be *some* kind 
		// of module available. As a result, we'll just select the first module available.
		if (fModManager->CountBibles() > 0)
		{
			SGModule* mod = fModManager->BibleAt(0);
			if (mod)
			{
				fModManager->SetModule(mod);
				fCurrentModule = mod;
			}
		} else
		if (fModManager->CountCommentaries() > 0)
		{
			SGModule* mod = fModManager->CommentaryAt(0);
			if (mod)
			{
				fModManager->SetModule(mod);
				fCurrentModule = mod;
			}
		} else if (fModManager->CountLexicons() > 0)
		{
			SGModule* mod = fModManager->LexiconAt(0);
			if (mod)
			{
				fModManager->SetModule(mod);
				fCurrentModule = mod;
			}
		} else if (fModManager->CountGeneralTexts() > 0)
		{
			SGModule* mod = fModManager->GeneralTextAt(0);
			if (mod)
			{
				fModManager->SetModule(mod);
				fCurrentModule = mod;
			}
		} else
			return; // Shoud never happen.
	}

	// SetModuleFromString() above already added the parallel view's first
	// column via SetModule() when it found a match; the fallback branches
	// just above it set fCurrentModule directly instead, so make sure a
	// column still exists either way.
	if (fParallelView->CountColumns() == 0 && fCurrentModule != NULL)
		fParallelView->AddColumn(fCurrentModule->Name());

	// A saved column layout (see #9) takes over from whatever single
	// column SetModuleFromString()/the fallback above just built --
	// replacing it with the exact set of columns (Bible/Commentary
	// modules and the notes column, in position) the window had the
	// last time it was closed. Absent one (fresh install, or a
	// preferences file from before this existed), the single column
	// already built stands as-is.
	RestoreColumnLayout();

	// Load the preferences for the individual module
	LoadPrefsForModule();


	BMenuItem* item = fBookMenu->FindItem(BookFromKey(key));
	if (item)
		item->SetMarked(true);

	if (key)
	{
		fCurrentChapter = ChapterFromKey(key);
		fCurrentVerse = VerseFromKey(key);
		SetChapter(fCurrentChapter);
		SetVerse(fCurrentVerse);
	}

	// Last, for the reason given on the definition: the position restore
	// just above would otherwise undo it.
	RestoreVerseLists();

	// Everything above is "where the window opens", not navigation the
	// user performed -- see fRestoringHistory's initializer.
	fRestoringHistory = false;
	UpdateHistoryControls();
}


SGMainWindow::~SGMainWindow()
{
	delete fModManager;
}


void SGMainWindow::BuildGUI(void) 
{
	BMenu* menu;
	fMenuBar = new BMenuBar("menubar");
	
	menu = new BMenu(B_TRANSLATE("Program"));
	menu->AddItem(new BMenuItem(B_TRANSLATE("About Scripture Guide…"),
		new BMessage(MENU_HELP_ABOUT)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Book Manager…"),
		new BMessage(MENU_PROGRAM_BOOKMANAGER)));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Dictionary…"),
		new BMessage(MENU_PROGRAM_DICTIONARY)));
	menu->AddSeparatorItem();

	// One verse-aligned table of every open column (+ notes, if any),
	// copied to the clipboard ready to paste into a spreadsheet/Markdown
	// doc/etc. (#8) -- four formats rather than a file-save panel, since
	// "paste into another tool" is the actual use case the issue asks
	// for, not archiving a file.
	BMenu* exportMenu = new BMenu(B_TRANSLATE("Copy Comparison"));
	exportMenu->AddItem(new BMenuItem(B_TRANSLATE("As Plain Text"),
		new BMessage(MENU_PROGRAM_EXPORT_PLAIN)));
	exportMenu->AddItem(new BMenuItem(B_TRANSLATE("As Tab-Separated"),
		new BMessage(MENU_PROGRAM_EXPORT_TSV)));
	exportMenu->AddItem(new BMenuItem(B_TRANSLATE("As Markdown Table"),
		new BMessage(MENU_PROGRAM_EXPORT_MARKDOWN)));
	exportMenu->AddItem(new BMenuItem(B_TRANSLATE("As HTML Table"),
		new BMessage(MENU_PROGRAM_EXPORT_HTML)));
	menu->AddItem(exportMenu);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Duplicate This Window…"),
		new BMessage(MENU_FILE_NEW), 'D'));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Close This Window"),
		new BMessage(B_QUIT_REQUESTED), 'W'));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(MENU_FILE_QUIT), 'Q'));
	fMenuBar->AddItem(menu);

	menu = new BMenu(B_TRANSLATE("Navigation"));
	// Deliberately first, and on the shortcut a browser would use:
	// following a cross-reference navigates the chain you were reading
	// in, so without a way back a link costs you your place (see
	// RecordHistory()/GoBack()).
	fBackItem = new BMenuItem(B_TRANSLATE("Back"),
		new BMessage(MENU_NAVIGATION_BACK), '[');
	fBackItem->SetEnabled(false);
	menu->AddItem(fBackItem);
	fForwardItem = new BMenuItem(B_TRANSLATE("Forward"),
		new BMessage(MENU_NAVIGATION_FORWARD), ']');
	fForwardItem->SetEnabled(false);
	menu->AddItem(fForwardItem);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Find Verse…"),
		new BMessage(MENU_EDIT_FIND), 'F'));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Next Book"),
		new BMessage(NEXT_BOOK), B_RIGHT_ARROW, B_OPTION_KEY));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Previous Book"),
		new BMessage(PREV_BOOK), B_LEFT_ARROW, B_OPTION_KEY));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Next Chapter"),
		new BMessage(NEXT_CHAPTER), B_RIGHT_ARROW));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Previous Chapter"),
		new BMessage(PREV_CHAPTER), B_LEFT_ARROW));
	fMenuBar->AddItem(menu);

	menu = new BMenu(B_TRANSLATE("Options"));
	fShowVerseNumItem = new BMenuItem(B_TRANSLATE("Show Verse Numbers"),
		new BMessage(MENU_OPTIONS_VERSENUMBERS));
	fShowVerseNumItem->SetMarked(fShowVerseNumbers);
	menu->AddItem(fShowVerseNumItem);
	fShowStrongsNumItem = new BMenuItem(B_TRANSLATE("Show Strong's Numbers"),
		new BMessage(MENU_OPTIONS_STRONGS));
	fShowStrongsNumItem->SetMarked(fShowStrongsNumbers);
	menu->AddItem(fShowStrongsNumItem);
	fShowCrossRefItem = new BMenuItem(B_TRANSLATE("Show Cross-References"),
		new BMessage(MENU_OPTIONS_CROSSREF));
	fShowCrossRefItem->SetMarked(fShowCrossReferences);
	menu->AddItem(fShowCrossRefItem);
	menu->AddItem(new BMenuItem(B_TRANSLATE("Choose Font…"),
		new BMessage(MENU_OPTIONS_FONT)));
	fMenuBar->AddItem(menu);

	// Prepare the book menu
	fBookMenu = new BMenu("book");
	BMenuField* bookfield = new BMenuField("bookfield", B_TRANSLATE("Book:"),
		fBookMenu);
	bookfield->SetDivider(be_plain_font->StringWidth("Book:") + 5);
	
	fBookMenu->SetLabelFromMarked(true);
	// Without radio mode, clicking an item doesn't mark it automatically --
	// the app has to call BMenuItem::SetMarked() itself. SELECT_BOOK's
	// handler never did that, so FindMarked() (which UpdateParallelKey()
	// relies on to know which book to build the key from) kept returning
	// whatever was marked before, regardless of which book was actually
	// clicked in the dropdown.
	fBookMenu->SetRadioMode(true);

	// TODO: needs to be reworked for to make
	// a dvision between Old Testament and New Testament
	vector<const char *> booknames = GetBookNames();
	for (uint32 i = 0; i < booknames.size(); i++)
		fBookMenu->AddItem(new BMenuItem(booknames[i], new BMessage(SELECT_BOOK)));
	fBookMenu->ItemAt(0)->SetMarked(true);
	
	// Toggles the per-verse notes column (see ParallelBibleView) on and
	// off -- superseded the old button's behavior of opening a single,
	// unstructured Notes.txt file externally, now that there's a proper
	// per-verse notes column built into the reading pane itself.
	BButton* fNoteButton = new BButton("note_button", B_TRANSLATE("Notes"), new BMessage(MENU_EDIT_NOTE));
	
	// Prepare the Chapter intput box
	BString alphaChars("qwertyuiop[]\\asdfghjkl;'zxcvbnm,./QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?`~!@#$%^&*()-_=+");
	fChapterBox = new BTextControl("chapter_choice", B_TRANSLATE("Chapter"), NULL,
									new BMessage(SELECT_CHAPTER));
	fVerseBox = new BTextControl("verse_choice", B_TRANSLATE("Verse"), NULL,
									new BMessage(SELECT_VERSE));
	BTextView* verseView = fVerseBox->TextView();
	BTextView* chapterView = fChapterBox->TextView();
	
	for (int32 i = 0; i < alphaChars.CountChars(); i++)
	{
		char c = alphaChars.ByteAt(i);
		chapterView->DisallowChar(c);
		verseView->DisallowChar(c);
	}

	// Universal search/goto box (#7): a reference like "Joh 3:16" or "1
	// Kor 13" navigates directly (see ParseVerseReference()), anything
	// else runs a text search in the same Find window "Find Verse..."
	// opens (see UNIVERSAL_SEARCH in MessageReceived()).
	fUniversalSearchBox = new BTextControl("universal_search",
		B_TRANSLATE("Go to / Search:"), NULL, new BMessage(UNIVERSAL_SEARCH));

	// Parallel View is now what the main window's own reading pane is --
	// with a single column it reads just like the old BTextView pane did,
	// and the "+" button in its header is how the user adds more columns
	// (see issue #11), instead of a separate window (see the "keep it
	// simple" discussion that led to this merge).
	fParallelView = new ParallelBibleView("parallelView",
		fModManager->Manager(), Frame().Width());
	// Vertical scrolling is now per-column (see the class comment on
	// ParallelBibleView, issue #12) -- this outer BScrollView only ever
	// needs its horizontal bar any more.
	fScrollView = new BScrollView("scroll_view", fParallelView,
		0, true, false, B_NO_BORDER);

	fToolBar = new BToolBar();
	BToolBar* toolBar = fToolBar;

	// Leading, in the order and with the artwork every Tracker window
	// already trains users to expect (see ScriptureGuide.rdef) -- the
	// menu entries stay as the discoverable/keyboard route, these are
	// the reachable one. Both start disabled; UpdateHistoryControls()
	// owns their state from here on.
	BBitmap* backIcon = _LoadVectorIcon("back_nav");
	BBitmap* forwardIcon = _LoadVectorIcon("forward_nav");
	toolBar->AddAction(MENU_NAVIGATION_BACK, this, backIcon,
		B_TRANSLATE("Back"));
	toolBar->AddAction(MENU_NAVIGATION_FORWARD, this, forwardIcon,
		B_TRANSLATE("Forward"));
	// AddAction() copies the bitmap into its own button.
	delete backIcon;
	delete forwardIcon;
	toolBar->SetActionEnabled(MENU_NAVIGATION_BACK, false);
	toolBar->SetActionEnabled(MENU_NAVIGATION_FORWARD, false);
	toolBar->AddSeparator();

	// Book/chapter/verse get the same tinted band the active chain's own
	// header cells carry (see ParallelHeaderView::Draw()). These three
	// fields drive exactly that chain and nothing else, so wearing its
	// colour is what says so -- otherwise, with several chains open,
	// nothing visually ties the fields to the one they move.
	//
	// SetViewUIColor() with a tint rather than a fixed rgb_color, so it
	// tracks the user's colour scheme the same way the header band does
	// (which tints ViewColor() at draw time). The tint has to go on the
	// controls as well as the group: a BMenuField and a BTextControl
	// paint their own label area in their OWN view colour, so tinting
	// only the group behind them would leave pale rectangles around each
	// label.
	BGroupView* verseFields = new BGroupView(B_HORIZONTAL,
		B_USE_SMALL_SPACING);
	verseFields->SetViewUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_1_TINT);
	bookfield->SetViewUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_1_TINT);
	fChapterBox->SetViewUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_1_TINT);
	fVerseBox->SetViewUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_1_TINT);
	BLayoutBuilder::Group<>(verseFields)
		.Add(bookfield)
		.Add(fChapterBox)
		.Add(fVerseBox)
		.SetInsets(B_USE_SMALL_INSETS, 0, B_USE_SMALL_INSETS, 0);

	toolBar->AddView(verseFields);
	// Deliberately OUTSIDE the band: the search field jumps to whatever
	// is typed and is not tied to one chain.
	toolBar->AddView(fUniversalSearchBox);
	toolBar->AddGlue();
	toolBar->AddView(fNoteButton);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar, B_USE_DEFAULT_SPACING)
		.Add(toolBar)
		.AddGroup(B_HORIZONTAL, 0)
			.Add(fParallelView->HeaderView())
			// fScrollView is horizontal-only now (see above) -- it no
			// longer reserves any width for a vertical BScrollBar of its
			// own, so fParallelView->HeaderView() no longer needs a
			// compensating strut to keep its row the same width as
			// fScrollView's actual content area.
		.End()
		.Add(fScrollView)
	.End();
}


void SGMainWindow::LoadPrefsForModule(void)
{
	modPrefsLock.Lock();
	BMessage msg;
	
	BFont font;
	status_t status;
	font_family fam;
	font_style sty;
	BString modname;
	
	status = LoadModulePreferences(fCurrentModule->Name(),&msg);
	
	if (status != B_OK)
	{
		// We couldn't load the module-specific preferences, so generate them,
		// save them to disk, and return
		
		// We default to whatever font the system has for be_plain_font and detect the
		// need for linebreaks for the particular module
		
		msg.MakeEmpty();
		
		fFontSize = (int16)font.Size();
		
		// The fDisplayFont object initializes to be_plain_font, which is what we
		// want, but we'll need to save the family and style to preferences
		font.GetFamilyAndStyle(&fam,&sty);
		
		// Detect need for linebreak insertion
		fIsLineBreak = NeedsLineBreaks();
		
		// Normally show verse numbers, Strong's numbers, and cross-references
		fShowVerseNumbers = true;
		fShowStrongsNumbers = true;
		fShowCrossReferences = true;

		msg.AddInt16("fontsize",fFontSize);
		msg.AddString("family",fam);
		msg.AddString("style",sty);
		msg.AddBool("linebreaks",fIsLineBreak);
		msg.AddBool("versenumbers",fShowVerseNumbers);
		msg.AddBool("strongsnumbers",fShowStrongsNumbers);
		msg.AddBool("crossreferences",fShowCrossReferences);

		SaveModulePreferences(fCurrentModule->Name(),&msg);
	} else
	{
		// It's possible that something was left out, so detect individually
		// and compensate even though the preferences are likely to be an all-or-nothing thing
		
		bool saveprefs = false;
		
		if (msg.FindInt16("fontsize",&fFontSize)!=B_OK)
		{
			fFontSize = (int16)be_plain_font->Size();
			msg.AddInt16("fontsize",fFontSize);
			saveprefs = true;
		}
		
		if (msg.FindBool("linebreaks",&fIsLineBreak)!=B_OK)
		{
			fIsLineBreak = NeedsLineBreaks();
			msg.AddBool("linebreaks",fIsLineBreak);
			saveprefs = true;
		}
		
		if (msg.FindBool("versenumbers",&fShowVerseNumbers)!=B_OK)
		{
			fShowVerseNumbers = true;
			msg.AddBool("versenumbers",fShowVerseNumbers);
			saveprefs = true;
		}

		if (msg.FindBool("strongsnumbers",&fShowStrongsNumbers)!=B_OK)
		{
			fShowStrongsNumbers = true;
			msg.AddBool("strongsnumbers",fShowStrongsNumbers);
			saveprefs = true;
		}

		if (msg.FindBool("crossreferences",&fShowCrossReferences)!=B_OK)
		{
			fShowCrossReferences = true;
			msg.AddBool("crossreferences",fShowCrossReferences);
			saveprefs = true;
		}

		fDisplayFont = font;
		
		BString sfam, ssty;
		if ( msg.FindString("family", &sfam) != B_OK
				|| msg.FindString("style", &ssty) != B_OK )
		{
			font.GetFamilyAndStyle(&fam,&sty);
			msg.AddString("family",fam);
			msg.AddString("style",sty);
			saveprefs = true;
		} else
		{
			fDisplayFont.SetFamilyAndStyle(sfam.String(),ssty.String());
			fDisplayFont.SetSize(fFontSize);
		}
		
		if (saveprefs)
			SaveModulePreferences(fCurrentModule->Name(),&msg);
	}
	modPrefsLock.Unlock();

	// fShowVerseNumbers was just (re)loaded above -- keep the menu mark
	// and the actual columns in sync with it. Harmless/no-op if nothing
	// has actually changed (see BibleTextDocument::SetShowVerseNumbers()).
	fShowVerseNumItem->SetMarked(fShowVerseNumbers);
	fParallelView->SetShowVerseNumbers(fShowVerseNumbers);
	fShowStrongsNumItem->SetMarked(fShowStrongsNumbers);
	fParallelView->SetShowStrongsNumbers(fShowStrongsNumbers);
	fShowCrossRefItem->SetMarked(fShowCrossReferences);
	fParallelView->SetShowCrossReferences(fShowCrossReferences);

	// Same idea for fDisplayFont -- was dead state before this (see #21):
	// loaded/saved but never actually applied to the reading pane.
	fParallelView->SetBaseFont(fDisplayFont);
}


void SGMainWindow::SavePrefsForModule(void)
{
	if (!fCurrentModule)
		return;
	
	BMessage msg;

	font_family fam;
	font_style sty;
	fDisplayFont.GetFamilyAndStyle(&fam,&sty);
	
	msg.AddBool("linebreaks",fIsLineBreak);
	msg.AddInt16("fontsize",fDisplayFont.Size());
	msg.AddString("family",fam);
	msg.AddString("style",sty);
	msg.AddBool("versenumbers",fShowVerseNumbers);
	msg.AddBool("strongsnumbers",fShowStrongsNumbers);
	msg.AddBool("crossreferences",fShowCrossReferences);
	SaveModulePreferences(fCurrentModule->Name(),&msg);
	
	// We also need to write to the application's main preferences so that the last
	// module used is the one to come up on the app's next execution
		
	prefsLock.Lock();
	preferences.RemoveData("windowframe");
	preferences.AddRect("windowframe",Frame());
	preferences.RemoveData("module");
	preferences.AddString("module",fCurrentModule->Name());
	preferences.RemoveData("key");
	// The ACTIVE CHAIN's own position, not fCurrentModule's (#24).
	// Nothing ever calls fCurrentModule->SetKey() -- confirmed, there is
	// no such call anywhere in this file -- so its key was only ever
	// whatever the underlying shared SWModule happened to be left at by
	// the last BibleTextDocument that rebuilt from it. It does not track
	// what the user is reading: navigating to Psalmen 119:5 and quitting
	// saved "1. Mose 1:1" (reproduced). ChainKey() is the value the
	// history feature already relies on for exactly this question.
	BString savedKey;
	if (fParallelView != NULL) {
		int32 column = fParallelView->ActiveColumn();
		if (column >= 0)
			savedKey = fParallelView->ChainKey(column);
	}
	if (savedKey.IsEmpty() && fCurrentModule != NULL)
		savedKey = fCurrentModule->GetKey();
	preferences.AddString("key",savedKey);

	// Full column layout (#9): every column's type and, for Bible/
	// Commentary columns, its module -- in on-screen order, so a saved
	// workspace restores the notes column at the position it was
	// actually in rather than always appending it last. Only one
	// layout is ever persisted app-wide (matching how "module"/"key"
	// above already work): whichever window last saved wins, same
	// simplification the pre-existing single-module/key persistence
	// already made for multiple open windows.
	// RemoveData() (used just above for the single-value fields) only
	// clears the entry at its default index 0, not the whole array --
	// harmless there since those fields only ever hold one value, but
	// these two are genuine arrays (one pair per column). RemoveName()
	// is what actually clears all of them; without it, every save
	// appended another copy of the old layout in front of the new one
	// instead of replacing it (confirmed empirically: two saves in a
	// row produced a 5-entry array from what should've stayed 3).
	preferences.RemoveName("columnIsNotes");
	preferences.RemoveName("columnModule");
	preferences.RemoveName("columnLinkedToNext");
	preferences.RemoveName("columnVerseListPath");
	std::vector<ParallelBibleView::ColumnDescription> columns
		= fParallelView->ColumnLayout();
	for (size_t i = 0; i < columns.size(); i++) {
		preferences.AddBool("columnIsNotes", columns[i].isNotes);
		preferences.AddString("columnModule", columns[i].moduleName);
		preferences.AddBool("columnLinkedToNext", columns[i].linkedToNext);
		preferences.AddString("columnVerseListPath", columns[i].verseListPath);
	}
	prefsLock.Unlock();
}


// Separate from RestoreColumnLayout(), and called after it rather than
// inside it, because the constructor restores the saved book/chapter
// between the two -- and naming a chapter deliberately leaves list mode
// (see BibleTextDocument::SetKey()). Applied inside the layout restore,
// a saved verse list was put in place and then immediately thrown away
// again by the position restore, with nothing to show for it.
void SGMainWindow::RestoreVerseLists(void)
{
	prefsLock.Lock();
	type_code type = B_ANY_TYPE;
	int32 count = 0;
	std::vector<BString> paths;
	if (preferences.GetInfo("columnVerseListPath", &type, &count) == B_OK) {
		for (int32 i = 0; i < count; i++) {
			BString path;
			// Absent for anything saved before verse lists existed,
			// which simply means "this column shows a chapter".
			preferences.FindString("columnVerseListPath", i, &path);
			paths.push_back(path);
		}
	}
	prefsLock.Unlock();

	// The FILE path is what was saved, not the list's text -- restoring
	// re-reads it fresh via SetColumnVerseListFile(), which is also what
	// gives the band its actual name (see BibleTextDocument::
	// SetVerseListOrigin()) instead of leaving it to fall back to the
	// list's raw first line the way it silently did before this existed.
	for (size_t i = 0; i < paths.size(); i++) {
		if (!paths[i].IsEmpty())
			fParallelView->SetColumnVerseListFile((int32)i, paths[i].String());
	}

	// SetColumnVerseList() alone does not touch which chain is active or
	// notify this window (RestoreColumnLayout()'s own AddColumn() calls
	// already settled that earlier) -- so if a restored list landed on
	// the chain that is already active, Book/Chapter/Verse would come up
	// enabled and showing a stale chapter, the same bug fixed live for
	// the band's own "New list"/selecting a list. Same fix, startup
	// side: resync explicitly rather than relying on a notification
	// nothing here posts.
	SyncToolbarToActiveChain();
}


void SGMainWindow::RestoreColumnLayout(void)
{
	prefsLock.Lock();

	int32 count = 0;
	type_code type;
	bool haveSavedLayout
		= preferences.GetInfo("columnIsNotes", &type, &count) == B_OK;

	std::vector<bool> isNotes;
	std::vector<BString> moduleNames;
	std::vector<bool> linkedToNext;
	if (haveSavedLayout) {
		for (int32 i = 0; i < count; i++) {
			bool notesFlag = false;
			BString moduleName;
			preferences.FindBool("columnIsNotes", i, &notesFlag);
			preferences.FindString("columnModule", i, &moduleName);
			// Absent for a preferences file saved before #12's linking
			// existed -- defaults to true, matching every column's
			// starting state (see the class comment on ParallelBibleView).
			// Checked explicitly rather than relying on FindBool() to
			// leave the out-param untouched on B_NAME_NOT_FOUND -- it
			// doesn't (confirmed empirically: every restored gap came
			// back false instead of the intended true default, splitting
			// every column into its own single-member chain).
			bool linkedFlag = true;
			if (preferences.FindBool("columnLinkedToNext", i, &linkedFlag)
					!= B_OK) {
				linkedFlag = true;
			}
			isNotes.push_back(notesFlag);
			moduleNames.push_back(moduleName);
			linkedToNext.push_back(linkedFlag);
		}
	}
	prefsLock.Unlock();

	// No saved layout at all (fresh install, or a preferences file from
	// before this existed) -- leave the single column
	// SetModuleFromString()/the fallback in the constructor already
	// built alone.
	if (!haveSavedLayout || count == 0)
		return;

	while (fParallelView->CountColumns() > 0)
		fParallelView->RemoveColumn(0);

	for (int32 i = 0; i < count; i++) {
		// A notes column that recorded a module name is an editable
		// column on a module the user picked (see
		// IsEditableVerseModule()); AddColumn() routes it back there by
		// itself. Only our own notes module has no name to record.
		if (isNotes[i] && moduleNames[i].IsEmpty())
			// AddNotesColumn(), not SetNotesEnabled(true) -- the latter
			// is a coarse "is there one anywhere yet" toggle (see the
			// class comment on ParallelBibleView) that would silently
			// skip every notes column after the first one a saved
			// layout had more than one of.
			fParallelView->AddNotesColumn();
		else if (!moduleNames[i].IsEmpty())
			fParallelView->AddColumn(moduleNames[i].String());
	}

	// Every AddColumn()/AddNotesColumn() call above appended its column
	// fully linked to whatever was already last (see the class comment)
	// -- restore whichever gaps the saved layout actually had broken.
	for (int32 i = 0; i + 1 < count; i++)
		fParallelView->SetColumnLinked(i, linkedToNext[i]);

}


bool SGMainWindow::NeedsLineBreaks(void)
{
	// This function detects the need to manually insert newlines after 
	// each verse by getting the entire current chapter. While not perfect,
	// it is unlikely that an entire chapter should go by without a new paragraph.
	
	// Get the verse for processing
	BString text;
	
	BString currentbook = fBookMenu->FindMarked()->Label();
	uint16 chaptercount = VersesInChapter(currentbook.String(), fCurrentChapter);
	for (uint16 currentverse = 1; currentverse <= chaptercount; currentverse++)
			text += fCurrentModule->GetVerse(currentbook.String(),
						fCurrentChapter, currentverse);
	
	text.RemoveAll("\xc2\xb6 ");
	text.RemoveAll("<P> ");
	
	if (text.FindFirst("\n") == B_ERROR)
		return true;
	
	return false;
}


void SGMainWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what)
	{
		case NEXT_BOOK:
		{
			// We'll figure this out by using the books menu -- figuring it all out using
			// the STL vector class is a pain, not to mention slower.
			
			int32 index = fBookMenu->IndexOf(fBookMenu->FindMarked());
			BMenuItem* currentItem = fBookMenu->ItemAt(index);
			
			if (index < fBookMenu->CountItems() - 1)
			{
				currentItem->SetMarked(false);

				BMenuItem* newItem = fBookMenu->ItemAt(++index);
				newItem->SetMarked(true);

				fCurrentChapter = 1;
				fCurrentVerse = 1;
				UpdateParallelKey();

				fChapterBox->SetText("1");
			}
			break;
		}
		case PREV_BOOK:
		{
			// We'll figure this out by using the books menu. Figuring it all out using
			// the STL vector class is a pain.
			
			int32 index = fBookMenu->IndexOf(fBookMenu->FindMarked());
			BMenuItem* currentItem = fBookMenu->ItemAt(index);
			
			if (index > 0)
			{
				currentItem->SetMarked(false);

				BMenuItem* newItem = fBookMenu->ItemAt(--index);
				newItem->SetMarked(true);

				fCurrentChapter = 1;
				fCurrentVerse = 1;
				UpdateParallelKey();

				fChapterBox->SetText("1");
			}
			break;
		}
		case SELECT_BOOK:
		{
			fCurrentChapter = 1;
			fCurrentVerse = 1;
			UpdateParallelKey();

			fChapterBox->SetText("1");
			fVerseBox->SetText("1");
			break;
		}
		case NEXT_CHAPTER:
		{
			SetChapter(++fCurrentChapter);
			break;
		}
		case PREV_CHAPTER:
		{
			SetChapter(--fCurrentChapter);
			break;
		}
		case SELECT_CHAPTER:
		{
			int num = atoi(fChapterBox->Text());
			SetChapter(num);
			break;
		}
		
		case MENU_FILE_NEW:
		{
			SGMainWindow* win = new SGMainWindow(Frame().OffsetByCopy(20,20),
												fCurrentModule->Name(), fCurrentModule->GetKey());
			win->Show();
			break;
		}
		case MENU_FILE_QUIT:
		{
			
			PostMessage(B_QUIT_REQUESTED);
			break;
		}
		case MENU_HELP_ABOUT:
		{
			BAboutWindow* window = new BAboutWindow("ScriptureGuide", 
						"application/x-vnd.Scripture-Guide");
			const char* authors[] = {
				"Jan Bungeroth (jan__64)",
				"Augustin Cavalier (waddlesplash)",
				"Kevin Field",
				"Brian Jennings",
				"Matthias Linder (Paradoxianer)",
				"Jon Yoder (DarkWyrm)",
				NULL
			};
			const char* specialThanks[] = {
				"al-popa (ru, ro)",
				"Begasus",
				"Briseur (fr)",
				"extrowerk",
				"korli",
				"sword lib team",
				"unspacyar (es)",
				"zvacet (hr)",
				NULL
			};
			const char* extraCopyrights[] = {
				"2004 Jan Bungeroth",
				NULL
			};
			window->AddCopyright(2005, "Scripture Guide Team",extraCopyrights);
			window->AddAuthors(authors);
			window->AddSpecialThanks(specialThanks);

			window->Show();
			break;
		}
		case MENU_PROGRAM_BOOKMANAGER:
		{
			be_roster->Launch(SG_MANAGER_SIGNATURE);
			break;
		}
		case MENU_PROGRAM_DICTIONARY:
		{
			EnsureDictionaryWindow();
			break;
		}
		case MENU_PROGRAM_EXPORT_PLAIN:
		case MENU_PROGRAM_EXPORT_TSV:
		case MENU_PROGRAM_EXPORT_MARKDOWN:
		case MENU_PROGRAM_EXPORT_HTML:
		{
			ExportFormat format = EXPORT_PLAIN_TEXT;
			if (msg->what == MENU_PROGRAM_EXPORT_TSV)
				format = EXPORT_TAB_SEPARATED;
			else if (msg->what == MENU_PROGRAM_EXPORT_MARKDOWN)
				format = EXPORT_MARKDOWN_TABLE;
			else if (msg->what == MENU_PROGRAM_EXPORT_HTML)
				format = EXPORT_HTML_TABLE;

			std::vector<BString> columnNames = fParallelView->ColumnModuleNames();
			bool hasNotes = fParallelView->NotesEnabled();
			std::vector<ParallelBibleView::ExportRow> rows
				= fParallelView->BuildExportRows();
			BString exportText = FormatExport(format, columnNames, hasNotes,
				rows);

			if (be_clipboard->Lock())
			{
				be_clipboard->Clear();
				BMessage* clip = be_clipboard->Data();
				if (clip != NULL)
				{
					clip->AddData("text/plain", B_MIME_TYPE,
						exportText.String(), exportText.Length());
					be_clipboard->Commit();
				}
				be_clipboard->Unlock();
			}
			break;
		}
		case FIND_QUIT:
		{
			// This message is received whenever the child find window quits
			delete fFindMessenger;
			fFindMessenger = NULL;
			break;
		}
		case MENU_NAVIGATION_BACK:
		{
			GoBack();
			break;
		}
		case MENU_NAVIGATION_FORWARD:
		{
			GoForward();
			break;
		}
		case MENU_EDIT_FIND:
		{
			EnsureSearchWindow();
			break;
		}
		case MENU_EDIT_NOTE:
		{
			fParallelView->SetNotesEnabled(!fParallelView->NotesEnabled());
			break;
		}
		case PARALLEL_ACTIVE_COLUMN_CHANGED:
		{
			SyncToolbarToActiveChain();
			break;
		}
		case MENU_OPTIONS_VERSENUMBERS:
		{
			fShowVerseNumbers = !fShowVerseNumbers;
			fShowVerseNumItem->SetMarked(fShowVerseNumbers);
			fParallelView->SetShowVerseNumbers(fShowVerseNumbers);
			SavePrefsForModule();
			break;
		}

		case MENU_OPTIONS_STRONGS:
		{
			fShowStrongsNumbers = !fShowStrongsNumbers;
			fShowStrongsNumItem->SetMarked(fShowStrongsNumbers);
			fParallelView->SetShowStrongsNumbers(fShowStrongsNumbers);
			SavePrefsForModule();
			break;
		}

		case MENU_OPTIONS_CROSSREF:
		{
			fShowCrossReferences = !fShowCrossReferences;
			fShowCrossRefItem->SetMarked(fShowCrossReferences);
			fParallelView->SetShowCrossReferences(fShowCrossReferences);
			SavePrefsForModule();
			break;
		}

		case MENU_OPTIONS_FONT:
		{
			if (fFontPanel)
				delete fFontPanel;

			fFontPanel = new FontPanel(this, NULL, fFontSize);
			fFontPanel->SelectFont(fDisplayFont);
			fFontPanel->Show();
			break;
		}

		// Sent by FontPanel once the user picks OK.
		case M_FONT_SELECTED:
		{
			BString family;
			BString style;
			float size;

			if (msg->FindString("family", &family) != B_OK
				|| msg->FindString("style", &style) != B_OK
				|| msg->FindFloat("size", &size) != B_OK)
				break;

			fFontSize = (int16)size;
			fDisplayFont.SetSize(size);
			fDisplayFont.SetFamilyAndStyle(family.String(), style.String());

			fParallelView->SetBaseFont(fDisplayFont);
			SavePrefsForModule();
			break;
		}

		case SELECT_VERSE :
		{
			int num = atoi(fVerseBox->Text());
			SetVerse(num);
			break;
		}
		case SG_BIBLE:
		{
			BString key;
			if (msg->FindString("key",&key) == B_OK)
				JumpToKey(key.String());
			break;
		}
		case SG_STRONGS_LOOKUP:
		{
			BString number;
			if (msg->FindString("number", &number) == B_OK) {
				EnsureDictionaryWindow();
				fDictionaryWindow->ShowStrongsNumber(number.String());
			}
			break;
		}
		case DICT_QUIT:
		{
			// fDictionaryWindow is about to be destroyed (sent from its
			// own destructor) -- null it out so EnsureDictionaryWindow()
			// creates a fresh one next time instead of calling Show()/
			// Activate() on a deleted BWindow (reported: reopening after
			// closing behaved oddly, exactly this class of bug).
			fDictionaryWindow = NULL;
			break;
		}
		case UNIVERSAL_SEARCH:
		{
			BString input(fUniversalSearchBox->Text());
			input.Trim();
			if (input.IsEmpty())
				break;

			BString normalizedKey;
			if (ParseVerseReference(input.String(), normalizedKey))
			{
				JumpToKey(normalizedKey.String());
				fUniversalSearchBox->SetText("");
			} else
			{
				EnsureSearchWindow();
				fSearchWindow->RunSearch(input.String());
			}
			break;
		}
		default:
		{
			BWindow::MessageReceived(msg);
			break;
		}
	}
}


void SGMainWindow::SetModuleFromString(const char* name)
{
	if (!name)
		return;
	
	SGModule* current = NULL;
	for (int32 i = 0; i < fModManager->CountBibles(); i++)
	{
		current = fModManager->BibleAt(i);
		if ( strcmp(name, current->Name()) == 0
			|| strcmp(name, current->FullName()) == 0 )
		{
			SetModule(TEXT_BIBLE, i);
			return;
		}
	}
	
	for (int32 i = 0; i < fModManager->CountCommentaries(); i++)
	{
		current = fModManager->CommentaryAt(i);
		if ( strcmp(name, current->Name()) == 0
			|| strcmp(name, current->FullName()) == 0 )
		{
			SetModule(TEXT_COMMENTARY, i);
			return;
		}
	}
	
	// If we got here, something is wrong
	SetModule(TEXT_BIBLE, 0);
}


void SGMainWindow::SetModule(const TextType &module, const int32 &index)
{
	SavePrefsForModule();
	
	SGModule* sgmod;
	if (module == TEXT_BIBLE)
		sgmod = fModManager->BibleAt(index);
	else
	if (module == TEXT_COMMENTARY)
		sgmod = fModManager->CommentaryAt(index);
	else
	{
		// Currently-unsupported module. Should *never* happen.
		return;
	}
	
	if (!sgmod)
		return;
	
	fModManager->SetModule(sgmod);
	fCurrentModule = sgmod;

	// make sure only the books available can be selected
	BMenuItem* currentbook;
	
	bool onvalue = sgmod->HasOT();
	for (int32 i = 0; i < 39; i++)
	{
		currentbook = fBookMenu->ItemAt(i);
		if (!currentbook)
			break;
		currentbook->SetEnabled(onvalue);
	}
	
	onvalue = sgmod->HasNT();
	for (int32 i = 39; i < 66; i++)
	{
		currentbook = fBookMenu->ItemAt(i);
		if (!currentbook)
			break;
		currentbook->SetEnabled(onvalue);
	}
	
	BString title("Scripture Guide: ");
	title << fCurrentModule->FullName();
	SetTitle(title.String());

	LoadPrefsForModule();

	int32 firstBiblePosition = fParallelView->FirstBibleColumnPosition();
	if (firstBiblePosition < 0)
		fParallelView->AddColumn(sgmod->Name());
	else
		fParallelView->ReplaceColumn(firstBiblePosition, sgmod->Name());

	BString chapterString;
	chapterString << fCurrentChapter;
	fChapterBox->SetText(chapterString.String());

	BString verseString;
	verseString << fCurrentChapter;
	fVerseBox->SetText(verseString.String());

	UpdateParallelKey();
}

void SGMainWindow::SetBook(const char* book, bool updateParallelView)
{
	BMenuItem *bookItem=fBookMenu->FindItem(book);
	if (bookItem != NULL)
	{
		BMenuItem *tmpItem = fBookMenu->FindMarked();
		while (tmpItem != NULL)
		{
			tmpItem->SetMarked(false);
			tmpItem = fBookMenu->FindMarked();
		}
		bookItem->SetMarked(true);
	}
	if (updateParallelView)
		UpdateParallelKey();
}


void
SGMainWindow::UpdateParallelKey(void)
{
	if (fParallelView == NULL || fBookMenu->FindMarked() == NULL)
		return;

	uint16 verse = fCurrentVerse != 0 ? fCurrentVerse : 1;
	BString key;
	key << fBookMenu->FindMarked()->Label() << " " << fCurrentChapter << ":"
		<< verse;

	// The single funnel every navigation passes through -- the toolbar
	// fields, next/previous book and chapter, a dropped or searched-for
	// reference, and a followed cross-reference alike -- which is why
	// recording here covers all of them rather than just the one that
	// prompted this.
	RecordHistory();

	fParallelView->SetKey(key.String());
}


// Remembers where the chain that is about to move currently is. Must run
// BEFORE the move, and records the CHAIN's own key rather than the
// toolbar's, because SetKey() only ever moves the active chain (see
// issue #12) and the toolbar may already have been updated to the
// destination by the caller.
void
SGMainWindow::RecordHistory(void)
{
	if (fParallelView == NULL || fRestoringHistory)
		return;

	int32 column = fParallelView->ActiveColumn();
	if (column < 0)
		return;

	BString current = fParallelView->ChainKey(column);
	if (current.IsEmpty())
		return;

	// Re-navigating a chain to where it already is (the toolbar re-emits
	// the same key in a few paths) is not a step worth being able to undo.
	if (!fBackStack.empty() && fBackStack.back().column == column
		&& fBackStack.back().key == current) {
		return;
	}

	HistoryEntry entry;
	entry.column = column;
	entry.key = current;
	fBackStack.push_back(entry);

	// Going somewhere new abandons whatever trail Back had opened up --
	// the same rule a browser's address bar follows.
	fForwardStack.clear();

	// Bounded: this exists to undo the last few jumps, not to log a
	// session. Dropping from the front keeps the most recent entries,
	// which are the ones Back actually reaches.
	const size_t kMaxHistoryEntries = 50;
	if (fBackStack.size() > kMaxHistoryEntries)
		fBackStack.erase(fBackStack.begin());

	UpdateHistoryControls();
}


void
SGMainWindow::GoBack(void)
{
	if (fBackStack.empty())
		return;

	HistoryEntry entry = fBackStack.back();
	fBackStack.pop_back();
	GoToHistoryEntry(entry, fForwardStack);
}


void
SGMainWindow::GoForward(void)
{
	if (fForwardStack.empty())
		return;

	HistoryEntry entry = fForwardStack.back();
	fForwardStack.pop_back();
	GoToHistoryEntry(entry, fBackStack);
}


void
SGMainWindow::GoToHistoryEntry(const HistoryEntry& entry,
	std::vector<HistoryEntry>& opposite)
{
	if (fParallelView == NULL)
		return;

	// Where we are right now becomes the far side's return point, so Back
	// and Forward stay each other's inverse however far the user walks.
	int32 column = fParallelView->ActiveColumn();
	if (column >= 0) {
		BString current = fParallelView->ChainKey(column);
		if (!current.IsEmpty()) {
			HistoryEntry here;
			here.column = column;
			here.key = current;
			opposite.push_back(here);
		}
	}

	// The chain this entry was recorded for may have been removed or
	// reordered since. Falling back to the active chain still puts the
	// user at the passage they asked for, which is what Back and Forward
	// actually promise -- better than doing nothing.
	if (entry.column >= 0 && entry.column < fParallelView->CountColumns())
		fParallelView->SetActiveColumn(entry.column);

	// Guarded so the navigation below isn't itself recorded -- otherwise
	// it would clear the very forward trail the user is walking along.
	fRestoringHistory = true;
	JumpToKey(entry.key.String());
	fRestoringHistory = false;

	UpdateHistoryControls();
}


void
SGMainWindow::UpdateHistoryControls(void)
{
	bool canGoBack = !fBackStack.empty();
	bool canGoForward = !fForwardStack.empty();

	if (fBackItem != NULL)
		fBackItem->SetEnabled(canGoBack);
	if (fForwardItem != NULL)
		fForwardItem->SetEnabled(canGoForward);
	if (fToolBar != NULL) {
		fToolBar->SetActionEnabled(MENU_NAVIGATION_BACK, canGoBack);
		fToolBar->SetActionEnabled(MENU_NAVIGATION_FORWARD, canGoForward);
	}
}



void SGMainWindow::SetChapter(const int16 &chapter)
{
	BString currentbook;

	int16 maxchapters = ChaptersInBook(fBookMenu->FindMarked()->Label());
	if (chapter > maxchapters)
	{
		int16 index = fBookMenu->IndexOf(fBookMenu->FindMarked());
		if (index < fBookMenu->CountItems()-1)
		{
			BMenuItem* item = fBookMenu->ItemAt(index);
			item->SetMarked(false);
			index++;
			item = fBookMenu->ItemAt(index);
			item->SetMarked(true);
			
			currentbook = fBookMenu->ItemAt(index)->Label();
			fCurrentChapter = 1;
			fCurrentVerse	= 0;
		} else
		{
			fCurrentChapter = maxchapters;
			return;
		}
	} else if (chapter < 1)
	{
		// we are at the first chapter of the book.
		// go to the first verse of the last chapter of the previous book
		// unless there isn't another one
		
		int16 index = fBookMenu->IndexOf(fBookMenu->FindMarked());
		if (index > 0)
		{
			BMenuItem* item = fBookMenu->ItemAt(index);
			item->SetMarked(false);
			index--;
			item = fBookMenu->ItemAt(index);
			item->SetMarked(true);
			
			currentbook = fBookMenu->ItemAt(index)->Label();
			fCurrentChapter = ChaptersInBook(fBookMenu->FindMarked()->Label());
		} else
		{
			fCurrentChapter = 1;
			return;
		}
	} else
	{
		fCurrentChapter = chapter;
	}
	
	UpdateParallelKey();

	BString cText;
	cText << fCurrentChapter;
	fChapterBox->SetText(cText.String());
	BString vText;
	vText << fCurrentVerse;
	fVerseBox->SetText(vText.String());
}


void SGMainWindow::SetVerse(const int16 &verse)
{
	fCurrentVerse = verse;
	UpdateParallelKey();
	BString vText;
	vText << fCurrentVerse;
	fVerseBox->SetText(vText.String());
}


void
SGMainWindow::JumpToKey(const char* key)
{
	// fCurrentChapter/fCurrentVerse have to already hold the target
	// chapter/verse before SetBook() runs -- SetBook() itself calls
	// UpdateParallelKey(), which builds "<book> <fCurrentChapter>:
	// <fCurrentVerse>" from whatever they currently hold. Setting the
	// book first (the previous order here) pushed a reference built
	// from the *old* book's leftover chapter/verse -- out of range for
	// the new book more often than not, which SWORD's VerseKey resolves
	// by rolling over into subsequent books/chapters rather than
	// failing, occasionally landing pages away from the intended verse
	// before the second and third calls below ever got a chance to
	// correct it.
	fCurrentChapter = ChapterFromKey(key);
	fCurrentVerse = VerseFromKey(key);
	SetBook(BookFromKey(key));

	BString cText;
	cText << fCurrentChapter;
	fChapterBox->SetText(cText.String());
	BString vText;
	vText << fCurrentVerse;
	fVerseBox->SetText(vText.String());

	fParallelView->HighlightVerse(fCurrentVerse, fCurrentVerse);
}


void
SGMainWindow::SyncToolbarToActiveChain(void)
{
	if (fParallelView == NULL)
		return;

	int32 active = fParallelView->ActiveColumn();
	if (active < 0)
		return;

	// A chain on a verse list has no single book/chapter/verse to show
	// (#47) -- and BibleTextDocument::SetKey() deliberately treats
	// naming one as leaving list mode, so pushing whatever these fields
	// last held back into the chain (which typing in them, or Next/Prev
	// Chapter, ultimately does) would silently exit the list. Disabled
	// instead, like any control that does not currently apply; a
	// section heading is the way back. Confirmed live as a real bug,
	// not a hypothetical: applying a list left these fields showing the
	// chain's previous chapter, unchanged and still editable.
	bool onList = !fParallelView->ChainVerseList().IsEmpty();
	fBookMenu->SetEnabled(!onList);
	fChapterBox->SetEnabled(!onList);
	fVerseBox->SetEnabled(!onList);
	if (onList)
		return;

	BString key = fParallelView->ChainKey(active);
	if (key.IsEmpty())
		return;

	// updateParallelView = false: this is purely a display sync, not a
	// navigation -- the active chain is already showing `key`, only the
	// toolbar's own fields were stale, so fParallelView must NOT be
	// touched (contrast JumpToKey(), which calls SetBook(book, true)).
	fCurrentChapter = ChapterFromKey(key.String());
	fCurrentVerse = VerseFromKey(key.String());
	SetBook(BookFromKey(key.String()), false);

	BString cText;
	cText << fCurrentChapter;
	fChapterBox->SetText(cText.String());
	BString vText;
	vText << fCurrentVerse;
	fVerseBox->SetText(vText.String());
}


void
SGMainWindow::EnsureSearchWindow(void)
{
	if (fFindMessenger)
	{
		// The module list was otherwise only ever built once, when this
		// window was first created -- switching a column to a different
		// translation afterward left it stale (reported: changed a
		// column to a different translation, but the already-open
		// window's "Search in" field still only offered the old one).
		fSearchWindow->RefreshModuleList(fParallelView->ColumnModuleNames());
		fFindMessenger->SendMessage(M_ACTIVATE_WINDOW);
		return;
	}

	BRect r(Frame().OffsetByCopy(5, 23));
	r.right = r.left + 325;
	r.bottom = r.top + 410;
	if (!fSearchWindow)
	{
		// The reading pane's own open columns, not fCurrentModule --
		// that field is a leftover from before ParallelBibleView could
		// hold more than one column and had drifted out of sync with
		// whatever the columns actually show (reported: search always
		// used the first-ever-loaded translation regardless of what was
		// open).
		fSearchWindow = new SGSearchWindow(r,
								fParallelView->ColumnModuleNames(),
								new BMessenger(this));
		fFindMessenger = new BMessenger(fSearchWindow);
	}
	fSearchWindow->Show();
}


void
SGMainWindow::EnsureDictionaryWindow(void)
{
	if (!fDictionaryWindow)
	{
		BRect r(Frame().OffsetByCopy(30, 40));
		r.right = r.left + 400;
		r.bottom = r.top + 350;
		fDictionaryWindow = new SGDictionaryWindow(r, fModManager,
			new BMessenger(this));
	}
	fDictionaryWindow->Show();
	fDictionaryWindow->Activate(true);
}


bool SGMainWindow::QuitRequested()
{
	if (fFindMessenger)
	{
		fFindMessenger->SendMessage(B_QUIT_REQUESTED);
		delete fFindMessenger;
		fFindMessenger = NULL;
	}
	if(fSearchWindow)
	{
		if (fSearchWindow->LockLooper())
			fSearchWindow->Quit();
	}
	if (fDictionaryWindow)
	{
		if (fDictionaryWindow->LockLooper())
			fDictionaryWindow->Quit();
	}
	if (fFontPanel)
	{
		if (fFontPanel->Window()->LockLooper())
			fFontPanel->Window()->Quit();
	}
	SavePrefsForModule();
	be_app_messenger.SendMessage(new BMessage(M_WINDOW_CLOSED));
	return true;
}


EndKeyFilter::EndKeyFilter(void)
 : BMessageFilter(B_PROGRAMMED_DELIVERY, B_ANY_SOURCE, B_KEY_DOWN)
{
}


EndKeyFilter::~EndKeyFilter(void)
{
}


filter_result EndKeyFilter::Filter(BMessage* msg, BHandler **target)
{
	int32 c;
	msg->FindInt32("raw_char",&c);
	if (c == B_END || c == B_HOME)
	{
		BTextView* text = dynamic_cast<BTextView*>(*target);
		if (text && text->IsFocus())
		{
			BScrollBar* sb = text->ScrollBar(B_VERTICAL);
			
			// We have to include this check, because each
			// BTextControl has a BTextView inside it, but
			// those ones don't have scrollbars
			if (!sb)
				return B_DISPATCH_MESSAGE;
			float min, max;
			sb->GetRange(&min,&max);
			
			if (c == B_HOME)
				sb->SetValue(min);
			else
				sb->SetValue(max);
			return B_SKIP_MESSAGE;
		}
	}
	return B_DISPATCH_MESSAGE;
}


UniversalSearchEnterFilter::UniversalSearchEnterFilter(BTextControl* searchBox)
 :	BMessageFilter(B_PROGRAMMED_DELIVERY, B_ANY_SOURCE, B_KEY_DOWN),
 	fSearchBox(searchBox)
{
}


UniversalSearchEnterFilter::~UniversalSearchEnterFilter(void)
{
}


filter_result UniversalSearchEnterFilter::Filter(BMessage* msg, BHandler **target)
{
	int32 c;
	msg->FindInt32("raw_char", &c);
	if (c == B_ENTER && *target == fSearchBox->TextView())
	{
		// Invoke() unconditionally (unlike the box's own KeyDown(), which
		// only calls it when the text changed) then skip the message
		// entirely, so that conditional KeyDown() never runs and can't
		// double-invoke on top of this when the text did change.
		fSearchBox->Invoke();
		fSearchBox->TextView()->SelectAll();
		return B_SKIP_MESSAGE;
	}
	return B_DISPATCH_MESSAGE;
}


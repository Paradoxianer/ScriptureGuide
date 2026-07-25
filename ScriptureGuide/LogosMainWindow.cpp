#include "LogosMainWindow.h"

#include <Alert.h>
#include <Application.h>
#include <AboutWindow.h>
#include <Button.h>
#include <Box.h>
#include <Catalog.h>
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
#include <ToolBar.h>



#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>

#include "constants.h"
#include "FontPanel.h"
#include "LogosApp.h"
#include "Preferences.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"

SGMainWindow::SGMainWindow(BRect frame, const char* module, const char* key,
		uint16 selectVers, uint16 selectVersEnd )
 :	BWindow(frame, "Scripture Guide", B_DOCUMENT_WINDOW, 0),
 	fModManager(NULL),
 	fCurrentModule(NULL),
 	fCurrentChapter(1),
 	fFindMessenger(NULL),
	fSearchWindow(NULL),
	fFontPanel(NULL)
{
	fCurrentVerse = selectVers;
	fCurrentVerseEnd = selectVersEnd;

	// BuildGUI() below marks fShowVerseNumItem from this; the real value
	// (if any was saved) isn't loaded until LoadPrefsForModule() runs,
	// well after BuildGUI() -- default to the same "on" LoadPrefsForModule()
	// itself falls back to when there's nothing saved yet.
	fShowVerseNumbers = true;

	fModManager = new SwordBackend();
	BuildGUI();
	
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
	fScrollView = new BScrollView("scroll_view", fParallelView,
		0, true, true, B_NO_BORDER);

	BToolBar *toolBar = new BToolBar();
	toolBar->AddView(bookfield);
	toolBar->AddView(fChapterBox);
	toolBar->AddView(fVerseBox);
	toolBar->AddView(fUniversalSearchBox);
	toolBar->AddGlue();
	toolBar->AddView(fNoteButton);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar, B_USE_DEFAULT_SPACING)
		.Add(toolBar)
		.AddGroup(B_HORIZONTAL, 0)
			.Add(fParallelView->HeaderView())
			// fScrollView reserves this much width on its right edge for
			// its own vertical BScrollBar; fParallelView->HeaderView() is
			// a plain sibling BView with no such reservation, so without
			// this strut its row would be wider than fScrollView's actual
			// content area and end up misaligned with it.
			.AddStrut(B_V_SCROLL_BAR_WIDTH)
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
		
		// Normally show verse numbers
		fShowVerseNumbers = true;
		
		msg.AddInt16("fontsize",fFontSize);
		msg.AddString("family",fam);
		msg.AddString("style",sty);
		msg.AddBool("linebreaks",fIsLineBreak);
		msg.AddBool("versenumbers",fShowVerseNumbers);
		
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
	SaveModulePreferences(fCurrentModule->Name(),&msg);
	
	// We also need to write to the application's main preferences so that the last
	// module used is the one to come up on the app's next execution
		
	prefsLock.Lock();
	preferences.RemoveData("windowframe");
	preferences.AddRect("windowframe",Frame());
	preferences.RemoveData("module");
	preferences.AddString("module",fCurrentModule->Name());
	preferences.RemoveData("key");
	preferences.AddString("key",fCurrentModule->GetKey());
	prefsLock.Unlock();
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
		case FIND_QUIT:
		{
			// This message is received whenever the child find window quits
			delete fFindMessenger;
			fFindMessenger = NULL;
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
		case MENU_OPTIONS_VERSENUMBERS:
		{
			fShowVerseNumbers = !fShowVerseNumbers;
			fShowVerseNumItem->SetMarked(fShowVerseNumbers);
			fParallelView->SetShowVerseNumbers(fShowVerseNumbers);
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

void SGMainWindow::SetBook(const char* book)
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
	fParallelView->SetKey(key.String());
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


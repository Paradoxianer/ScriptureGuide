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

#include <gbfplain.h>
#include <gbfstrongs.h>
#include <markupfiltmgr.h>

#include "constants.h"
#include "LogosApp.h"
#include "FontPanel.h"
#include "Preferences.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"

#define CONFIGPATH MODULES_PATH

//@ToDo: needs a massiv Cleanup since nearly the whole funktionality is already in BibleTextColumnListView

SGMainWindow::SGMainWindow(BRect frame, const char* module, const char* key,
		uint16 selectVers, uint16 selectVersEnd )
 :	BWindow(frame, "Scripture Guide", B_DOCUMENT_WINDOW, 0),
 	fFontPanel(NULL),
 	fModManager(NULL),
 	fCurrentModule(NULL),
 	fCurrentChapter(1),
 	fCurrentFont(NULL),
 	fFindMessenger(NULL),
	fSearchWindow(NULL)
{
	fCurrentVerse = selectVers;
	fCurrentVerseEnd = selectVersEnd;
		
	fModManager = new SwordBackend();
	BuildGUI();
	
	// More voodoo hackerdom to work around a bug. :)
	AddCommonFilter(new EndKeyFilter);
	
	if (fModManager->CountModules()==0)
	{
		// TODO: Alter or run Installer
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
	
	fCurrentFont = &fDisplayFont;
	
	fRomanFont.SetSize(fFontSize);
	fGreekFont.SetSize(fFontSize);
	fHebrewFont.SetSize(fFontSize);
	
	// Attempt to set the supported Greek and Hebrew fonts. 
	// In this case, the BeBook is wrong -- SetFamilyandFace 
	// is declared in Font.h as returning a status_t.
	// Ideally, there should be a better way of choosing non-Latin fonts, 
	// but we'll live with this for now. :)
	font_family fam;
	sprintf((char*)fam, "%s", GREEK);
	if (fGreekFont.SetFamilyAndFace(fam, B_REGULAR_FACE)!=B_OK)
		fGreekFont = *be_plain_font;
	
	sprintf((char*)fam,"%s",HEBREW);
	if (fHebrewFont.SetFamilyAndFace(fam, B_REGULAR_FACE)!=B_OK)
		fHebrewFont=*be_plain_font;
}


SGMainWindow::~SGMainWindow() 
{
	delete fModManager;
	delete fFontPanel;
}


void SGMainWindow::BuildGUI(void) 
{
	BMenu* menu;
	fMenuBar = new BMenuBar("menubar");
	
	menu = new BMenu(B_TRANSLATE("Program"));
	menu->AddItem(new BMenuItem(B_TRANSLATE("About Scripture Guide…"),
		new BMessage(MENU_HELP_ABOUT)));
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
	menu->AddSeparatorItem();
	
	BMenuItem* copyitem = new BMenuItem(B_TRANSLATE("Copy Verses"),
		new BMessage(B_COPY), 'C');
	menu->AddItem(copyitem);
	
	BMenuItem* selectallitem = new BMenuItem(B_TRANSLATE("Select All"),
		new BMessage(B_SELECT_ALL), 'A');
	menu->AddItem(selectallitem);
	fMenuBar->AddItem(menu);
	
	menu = new BMenu(B_TRANSLATE("Options"));
	
	menu->AddItem(new BMenuItem(B_TRANSLATE("Choose Font..."),
		new BMessage(SELECT_FONT)));
	menu->AddSeparatorItem();
	
	fShowVerseNumItem = new BMenuItem(B_TRANSLATE("Show Verse Numbers"),
		new BMessage(MENU_OPTIONS_VERSENUMBERS));
	if (fShowVerseNumbers)
		fShowVerseNumItem->SetMarked(true);
	menu->AddItem(fShowVerseNumItem);
	
	fMenuBar->AddItem(menu);
	
	// Add the toolbar view	
	BBox* toolbar = new BBox("toolbar_view");
	
	/*fModuleField = new BMenuField("modulefield", B_TRANSLATE("Text:"), modulemenu);
	fModuleField->SetDivider(be_plain_font->StringWidth("Text:") + 5);*/
	
	// Prepare the book menu
	fBookMenu = new BMenu("book");
	BMenuField* bookfield = new BMenuField("bookfield", B_TRANSLATE("Book:"),
		fBookMenu);
	bookfield->SetDivider(be_plain_font->StringWidth("Book:") + 5);
	
	fBookMenu->SetLabelFromMarked(true);
	
	// TODO: needs to be reworked for to make 
	// a dvision between Old Testament and New Testament
	vector<const char *> booknames = GetBookNames();
	for (uint32 i = 0; i < booknames.size(); i++)
		fBookMenu->AddItem(new BMenuItem(booknames[i], new BMessage(SELECT_BOOK)));
	fBookMenu->ItemAt(0)->SetMarked(true);
	
	// Prepare the notes button
	BButton* fNoteButton = new BButton("note_button", B_TRANSLATE("Notes…"), new BMessage(MENU_EDIT_NOTE));
	
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
	
	// prepare the TextFrame
	BRect textframe = Bounds();
	textframe.top = toolbar->Frame().bottom + 1;
	textframe.right -= B_V_SCROLL_BAR_WIDTH;
	BRect textrect = textframe;
	textrect.OffsetTo(B_ORIGIN);
	
	BToolBar *toolBar = new BToolBar();
	toolBar->AddView(bookfield);
	toolBar->AddView(fChapterBox);
	toolBar->AddView(fVerseBox);
	toolBar->AddGlue();
	toolBar->AddView(fNoteButton);
	bibleTextView = new BibleTextColumnView("bibletextview",
		new SWMgr(CONFIGPATH, true, new MarkupFilterMgr(FMT_GBF, ENC_UTF8)),new VerseKey("John 1:10-12"));
	
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar, B_USE_DEFAULT_SPACING)
		.Add(toolBar)
		.AddSplit(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(bibleTextView)
		.End()
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



void SGMainWindow::FrameResized(float width, float height) 
{
}


void SGMainWindow::MessageReceived(BMessage* msg) 
{
	switch (msg->what) 
	{
		case SELECT_BIBLE:
		{
			int32 index = 0;
			if (msg->FindInt32("index", &index) != B_OK)
				break;
			SetModule(TEXT_BIBLE, index);
			break;
		}
		case SELECT_COMMENTARY:
		{
			int32 index = 0;
			if (msg->FindInt32("index", &index) != B_OK)
				break;
			SetModule(TEXT_COMMENTARY, index);
			break;
		}
		
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

				//ToDo implement SetBook as (const charÜ)
				bibleTextView->SetBook((char *)currentItem->Label());
				
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
				fChapterBox->SetText("1");
			}
			break;
		}
		case SELECT_BOOK:
		{
			fCurrentChapter = 1;
			fCurrentVerse = 1;

			bibleTextView->SetBook((char *)fBookMenu->FindMarked()->Label());

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
		case MENU_OPTIONS_VERSENUMBERS:
		{
			fShowVerseNumbers = (fShowVerseNumbers) ? false : true;			
			fShowVerseNumItem->SetMarked(fShowVerseNumbers);
			bibleTextView->SetShowVerseNumber(fShowVerseNumbers);
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
		case FIND_QUIT:
		{
			// This message is received whenever the child find window quits
			delete fFindMessenger;
			fFindMessenger = NULL;
			break;
		}
		case MENU_EDIT_FIND:
		{
			if (fFindMessenger)
			{
				fFindMessenger->SendMessage(M_ACTIVATE_WINDOW);
				break;
			}
			
			BRect r(Frame().OffsetByCopy(5, 23));
			r.right = r.left + 325;
			r.bottom = r.top + 410;
			if (!fSearchWindow)
			{
				fSearchWindow = new SGSearchWindow(r, fCurrentModule->Name(),
										new BMessenger(this));
				fFindMessenger = new BMessenger(fSearchWindow);
			}
			fSearchWindow->Show();
			break;
		}
		case MENU_EDIT_NOTE:
		{
			BString notespath(NOTESPATH);
			notespath += "Notes.txt";
			
			BEntry entry;
			if (entry.SetTo(notespath.String())!=B_OK)
			{
				// Apparently the notes were blown away while the app was running, so 
				// re-create them
				create_directory(notespath.String(),0777);
				
				notespath += "Notes.txt";
				BFile file(notespath.String(), B_READ_WRITE | B_CREATE_FILE);
				const char notes[]="Scripture Guide Study Notes\n-------------------------------\n";
				file.Write(notes, strlen(notes));				
				file.Unset();
				
				break;
			}
			
			entry_ref ref;
			entry.GetRef(&ref);
			be_roster->Launch(&ref);
			break;
		}

		// Called when the user selects 'Choose Font...' from menu
		case SELECT_FONT:
		{
			if (fFontPanel)
				delete fFontPanel;
							
			fFontPanel = new FontPanel(this, NULL, fFontSize);
			fFontPanel->SelectFont(fDisplayFont);
			fFontPanel->Show();
			break;
		}
		
		// called when the user selects a font from the panel
		case M_FONT_SELECTED:
		{
			BString family;
			BString style;
			float size;
			
			if (msg->FindString("family",&family) != B_OK
				|| msg->FindString("style",&style) != B_OK
				|| msg->FindFloat("size",&size) != B_OK)
				break;
			
			fFontSize = (int)size;
			fCurrentFont->SetSize(size);
			fCurrentFont->SetFamilyAndStyle(family.String(),style.String());
			
			fDisplayFont.SetSize(size);
			fDisplayFont.SetFamilyAndStyle(family.String(),style.String());
			
			//@ToDo Set Font and Color from the bibleTextColumnListView
			
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
			{
				SetBook(BookFromKey(key.String()));
				SetChapter(ChapterFromKey(key.String()));
				SetVerse(VerseFromKey(key.String()));
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
	
	/*SGModule* current = NULL;
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
	SetModule(TEXT_BIBLE, 0);*/
}


void SGMainWindow::SetModule(const TextType &module, const int32 &index)
{
	SavePrefsForModule();
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
	//@ToDo change bibleTExtColumnView
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
	BString vText;
	vText << fCurrentVerse;
	fVerseBox->SetText(vText.String());
	bibleTextView->SetVerse(verse);
}


bool SGMainWindow::QuitRequested() 
{
	if (fFindMessenger)
	{
		fFindMessenger->SendMessage(B_QUIT_REQUESTED);
		delete fFindMessenger;
		fFindMessenger = NULL;
	}
	if (fFontPanel)
	{
		if (fFontPanel->Window()->LockLooper())
			fFontPanel->Window()->Quit();
	}
	if(fSearchWindow)
	{
		if (fSearchWindow->LockLooper())
			fSearchWindow->Quit();
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


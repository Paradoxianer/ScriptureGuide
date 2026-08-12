#include <Application.h>
#include <LayoutBuilder.h>

#include <stdlib.h>
#include <ColumnTypes.h>

#include "MainWindow.h"
#include "ModUtils.h"
#include "BookRow.h"
#include "DownloadLocations.h"

extern BList gFileNameList;
extern BList fConfFileList;

enum
{
	M_SELECT_MODULE='slmd',
	M_MARK_MODULE,
	M_SET_PACKAGES,
	M_INSTALL_SELECTION,
	M_REMOVE_SELECTION
};


// What a row's status column says, and what it means for the pending
// lists. Replaces the single characters this column used to hold (" ",
// "*", "i", "X") -- those were unreadable without the source open, which
// is half of why marking modules felt like guesswork (see issues #37,
// #42).
enum
{
	STATE_AVAILABLE = 0,	// not installed, nothing pending
	STATE_INSTALLED,		// installed, nothing pending
	STATE_WILL_INSTALL,		// not installed, marked to install
	STATE_WILL_REMOVE		// installed, marked to remove
};

static const char*
state_text(int32 state)
{
	switch(state)
	{
		case STATE_INSTALLED:		return "Installed";
		case STATE_WILL_INSTALL:	return "Will install";
		case STATE_WILL_REMOVE:		return "Will remove";
		default:					return "";
	}
}


// Column indices, named because there are now four of them and the
// status column is read back by index in several places.
enum
{
	COLUMN_STATUS = 0,
	COLUMN_BOOK,
	COLUMN_TYPE,
	COLUMN_LANGUAGE
};

MainWindow::MainWindow(BRect frame)
	: BWindow(frame, "ScriptureGuide Book Manager",B_DOCUMENT_WINDOW_LOOK,
 		B_NORMAL_WINDOW_FEEL, 0)
{
	fApplyThread=-1;
	
	// Set up menu
	BMenuBar *mbar=new BMenuBar("menu_bar");
	
	BMenu *menu=new BMenu("Program");
	menu->AddItem(new BMenuItem("Quit",new BMessage(B_QUIT_REQUESTED),'Q',0));
	mbar->AddItem(menu);
	
	// Set up the module list
	fBookListView = new BColumnListView("booklist",0);
	// Wide enough for "Will install" -- the old 25px fitted one character
	// and nothing more.
	BStringColumn *installed_column = new BStringColumn("Status",90,50,150,0);
	BStringColumn *book_column = new BStringColumn("Book",200,50,1000,0);
	// SWORD's own module vocabulary, so this column says the same thing
	// the reading app's module menu does (see ReadConfigFile()). Sortable
	// like every other column, which is what issue #41 asks for.
	BStringColumn *type_column = new BStringColumn("Type",150,50,400,0);
	BStringColumn *language_column = new BStringColumn("Language",75,50,1000,0);
	fBookListView->AddColumn(installed_column,COLUMN_STATUS);
	fBookListView->AddColumn(book_column,COLUMN_BOOK);
	fBookListView->AddColumn(type_column,COLUMN_TYPE);
	fBookListView->AddColumn(language_column,COLUMN_LANGUAGE);
	// The point of issue #37: mark a whole batch at once instead of
	// double-clicking every entry in turn.
	fBookListView->SetSelectionMode(B_MULTIPLE_SELECTION_LIST);
	fBookListView->SetSelectionMessage(new BMessage(M_SELECT_MODULE));
	// Double-click still works as a shortcut for "install this one" on a
	// single row, but it is no longer the ONLY way to mark anything.
	fBookListView->SetInvocationMessage(new BMessage(M_MARK_MODULE));	
	for(int32 i=0; i<fConfFileList.CountItems(); i++)
	{
		ConfigFile *cfile=(ConfigFile*)fConfFileList.ItemAt(i);
		if(cfile)
		{
			BookRow *row = new BookRow(cfile);
			bool installed = IsInstalled(cfile->fFileName.String());
			row->SetField(new BStringField(state_text(installed
					? STATE_INSTALLED : STATE_AVAILABLE)),
				COLUMN_STATUS);
			row->SetField(new BStringField(cfile->fDescription.String()),
				COLUMN_BOOK);
			row->SetField(new BStringField(cfile->fType.String()),
				COLUMN_TYPE);
			row->SetField(new BStringField(cfile->fLanguage.String()),
				COLUMN_LANGUAGE);
			fBookListView->AddRow(row);
		}
	}
	
	// Add the box we use for descriptions
	fTextView=new BTextView("descriptionview");
	fTextView->MakeEditable(false);
	fTextScrollView=new BScrollView("textscrollview",fTextView,0,false,true);
	fTextScrollView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	fInstallButton=new BButton("install button", "Install",
			new BMessage(M_INSTALL_SELECTION));
	fInstallButton->SetEnabled(false);
	fRemoveButton=new BButton("remove button", "Remove",
			new BMessage(M_REMOVE_SELECTION));
	fRemoveButton->SetEnabled(false);
	fApplyButton=new BButton("apply button", "Apply",
			new BMessage(M_SET_PACKAGES));
	fApplyButton->SetEnabled(false);
	
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(mbar)
		// 2:1 rather than 1:1 -- the list carries four columns now and is
		// the thing being worked in; the description pane only has to
		// hold a paragraph.
		.AddSplit(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
			.Add(fBookListView, 2)
			.AddGroup(B_VERTICAL, B_USE_HALF_ITEM_SPACING, 1)
				.Add(fTextScrollView)
				.AddGroup(B_HORIZONTAL)
					.Add(fInstallButton)
					.Add(fRemoveButton)
					.AddGlue()
					.Add(fApplyButton)
				.End()
			.End()
		.End()
	.End();
}

bool MainWindow::QuitRequested(void)
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

void MainWindow::MessageReceived(BMessage *msg)
{
	switch(msg->what)
	{
		case M_SELECT_MODULE:
		{
			if(fApplyThread!=-1)
				break;
			
			// With multiple selection there may be no single module to
			// describe. One row still shows its description; several show
			// how many are selected, so the pane never looks stale by
			// describing whichever row happened to be clicked first.
			BookRow *first=(BookRow*)fBookListView->CurrentSelection();
			int32 count=0;
			for(BRow *r=first; r!=NULL; r=fBookListView->CurrentSelection(r))
				count++;
			
			if(count==1 && first && first->File())
			{
				BString str(first->File()->fAbout);
				str << "\n\n" << "Archive Size: " << first->File()->fFileSize
					<< " K";
				fTextView->SetText(str.String());
			}
			else if(count>1)
			{
				BString str;
				str << count << " modules selected";
				fTextView->SetText(str.String());
			}
			UpdateButtons();
			break;
		}
		case M_INSTALL_SELECTION:
		{
			if(fApplyThread!=-1)
				break;
			MarkSelection(true);
			break;
		}
		case M_REMOVE_SELECTION:
		{
			if(fApplyThread!=-1)
				break;
			MarkSelection(false);
			break;
		}
		case M_MARK_MODULE:
		{
			if(fApplyThread!=-1)
				break;
			// Double-click means "move this one toward installed", the
			// same thing the Install button does for a whole selection.
			MarkSelection(true);
			break;
		}
		case M_SET_PACKAGES:
		{
			if(fApplyThread!=-1)
				break;
			
			fApplyThread=spawn_thread(ApplyThread,"applythread",B_NORMAL_PRIORITY,this);
			resume_thread(fApplyThread);
			
			break;
		}
		default:
			BWindow::MessageReceived(msg);
	}
}

// `install` reads as "move toward installed": it marks an available
// module for installation, and it also CANCELS a pending removal, which
// is what lets two buttons cover marking and unmarking both ways without
// a third control.
void MainWindow::MarkSelection(bool install)
{
	for(BRow *r=fBookListView->CurrentSelection(); r!=NULL;
			r=fBookListView->CurrentSelection(r))
	{
		BookRow *row=(BookRow*)r;
		if(row->File()==NULL)
			continue;
		
		bool installed=IsInstalled(row->File()->fFileName.String());
		int32 state;
		if(install)
			state = installed ? STATE_INSTALLED : STATE_WILL_INSTALL;
		else
			state = installed ? STATE_WILL_REMOVE : STATE_AVAILABLE;
		SetRowState(row,state);
	}
	UpdateButtons();
}


void MainWindow::SetRowState(BookRow *row, int32 state)
{
	BStringField *field
		= dynamic_cast<BStringField*>(row->GetField(COLUMN_STATUS));
	if(field==NULL)
		return;
	field->SetString(state_text(state));
	
	// The pending lists are keyed by zip file name and are what
	// ApplyThread() works from; keep them in step with what the row now
	// says rather than deriving one from the other later.
	const BString &zip=row->File()->fZipFileName;
	fInstallList.Remove(zip);
	fUninstallList.Remove(zip);
	if(state==STATE_WILL_INSTALL)
		fInstallList.Add(zip);
	else if(state==STATE_WILL_REMOVE)
		fUninstallList.Add(zip);
	
	row->Invalidate();
	fBookListView->InvalidateRow(row);
}


void MainWindow::UpdateButtons(void)
{
	// A button is offered only when it would actually do something to
	// what is selected -- an Install that silently no-ops on an already
	// installed module is the same kind of lie as a link that leads
	// nowhere.
	bool canInstall=false, canRemove=false;
	for(BRow *r=fBookListView->CurrentSelection(); r!=NULL;
			r=fBookListView->CurrentSelection(r))
	{
		BookRow *row=(BookRow*)r;
		if(row->File()==NULL)
			continue;
		const BString &zip=row->File()->fZipFileName;
		if(IsInstalled(row->File()->fFileName.String()))
		{
			if(!fUninstallList.HasString(zip))
				canRemove=true;
			else
				canInstall=true;	// cancels the pending removal
		}
		else
		{
			if(!fInstallList.HasString(zip))
				canInstall=true;
			else
				canRemove=true;		// cancels the pending installation
		}
	}
	
	fInstallButton->SetEnabled(canInstall && fApplyThread==-1);
	fRemoveButton->SetEnabled(canRemove && fApplyThread==-1);
	fApplyButton->SetEnabled(fApplyThread==-1
		&& (fInstallList.CountStrings()>0 || fUninstallList.CountStrings()>0));
}


int32 MainWindow::ApplyThread(void *data)
{
	MainWindow *win=(MainWindow*)data;
	
	int32 i, installcount,removecount;

	BString zipfilename, configpath, syscmd, displaystring;

	win->Lock();
	win->fApplyButton->SetEnabled(false);
	win->fTextView->SetText("");
	
	installcount=win->fInstallList.CountStrings();
	removecount=win->fUninstallList.CountStrings();
	
	win->Unlock();
	
	for(i=0; i<installcount; i++)
	{
		win->Lock();

		// A fresh ConfigFile every iteration -- see the same fix in the
		// uninstall loop below for why sharing one across iterations is
		// dangerous (a failed ReadConfigFile() here just means a
		// download runs with a blank description, not a wrong
		// deletion, so this loop was never the *unsafe* half of the
		// bug, but reusing state across unrelated iterations was still
		// the wrong instinct here too).
		ConfigFile cfile;

		zipfilename = win->fInstallList.StringAt(i);
		configpath=zipfilename;
		configpath.ToLower();
		configpath+=".conf";
		configpath.Prepend(SG_PKGINFO_PATH "configfiles/");
		ReadConfigFile(configpath.String(),cfile);

		displaystring="Downloading ";
		displaystring << cfile.fDescription << "\n";
				
		win->fTextView->Insert(displaystring.String());
		win->Unlock();
		
		syscmd="wget -P " SG_PKGCACHE_PATH " " SG_DOWNLOAD_PKGS;
		syscmd+=zipfilename;
		syscmd+=".zip";
		
		system(syscmd.String());
		
		win->Lock();
		displaystring="Installing ";
		displaystring << cfile.fDescription << "\n";
		win->fTextView->Insert(displaystring.String());
		win->Unlock();
		
		syscmd="unzip -o ";
		syscmd << SG_PKGCACHE_PATH << zipfilename << ".zip -d " << SG_MODULEBASE_PATH;
		
		system(syscmd.String());
		
		win->Lock();
		win->fTextView->Insert("Done\n");
		win->Unlock();
	}
	for(i=0; i<removecount; i++)
	{
		printf("removing %s\n ",win->fUninstallList.StringAt(i).String());
		win->Lock();

		// A fresh ConfigFile every iteration, not one shared across the
		// whole loop -- if ReadConfigFile() below fails (missing file,
		// or a DataPath= line it didn't find) it just leaves every
		// field untouched rather than erroring the fields out, so a
		// shared cfile would silently keep the *previous* iteration's
		// fDataPath, and the very first iteration would keep its
		// default-constructed *empty* one.
		ConfigFile cfile;

		zipfilename=win->fUninstallList.StringAt(i);
		configpath=zipfilename;
		configpath.ToLower();
		configpath+=".conf";
		configpath.Prepend(SG_PKGINFO_PATH "configfiles/");
		status_t readStatus = ReadConfigFile(configpath.String(),cfile);

		// This is the actual data-loss bug (#36): with an empty
		// fDataPath (ReadConfigFile() failed, or a config file with no
		// DataPath= line at all), "rm -r SG_MODULEBASE_PATH*" -- the
		// datapath fragment collapsing to nothing -- deletes every
		// installed module's data, not just this one's. Confirmed by
		// the very TODO this replaces ("one error and all modules are
		// gone.. like it happens now"): that's not hypothetical, it's
		// already happened at least once (see commit 4d8b6db). Skip
		// the whole removal for this entry rather than run any of the
		// three commands below with a path we can't trust.
		if (readStatus != B_OK || cfile.fDataPath.IsEmpty())
		{
			displaystring="Error: couldn't read the config file for ";
			displaystring << zipfilename
				<< " -- skipping its removal rather than guessing.\n";
			win->fTextView->Insert(displaystring.String());
			win->Unlock();
			continue;
		}

		displaystring="Removing ";
		displaystring << cfile.fDescription << "\n";

		win->fTextView->Insert(displaystring.String());
		win->Unlock();

		// The two things needed to remove a module:
		// delete mods.d/modulename.conf
		// delete datapath/*
		// remove datapath

		configpath=zipfilename;
		configpath.ToLower();

		syscmd="rm ";
		syscmd << SG_MODULEBASE_PATH << "mods.d/" << configpath << ".conf";
		printf("%s\n",syscmd.String());
		system(syscmd.String());

		syscmd="rm -r ";
		syscmd << SG_MODULEBASE_PATH << cfile.fDataPath << "*";
		printf("%s\n",syscmd.String());
		system(syscmd.String());

		syscmd="rmdir ";
		syscmd << SG_MODULEBASE_PATH << cfile.fDataPath;
		printf("%s\n",syscmd.String());
		system(syscmd.String());
		
		win->Lock();
		win->fTextView->Insert("Done\n");
		win->Unlock();
	}
	
	win->Lock();
	win->fTextView->Insert("=====Finished====\n");
	win->Unlock();
	win->Lock();
	win->fApplyThread=-1;
	win->fInstallList.MakeEmpty();
	win->fUninstallList.MakeEmpty();

	// Every row's status column still says what was PENDING; the pending
	// lists have just been emptied, so rewrite each row from what is now
	// actually on disk. Without this a just-installed module kept saying
	// "Will install" until the window was reopened.
	for(int32 i=0; i<win->fBookListView->CountRows(); i++)
	{
		BookRow *row=(BookRow*)win->fBookListView->RowAt(i);
		if(row==NULL || row->File()==NULL)
			continue;
		win->SetRowState(row, IsInstalled(row->File()->fFileName.String())
			? STATE_INSTALLED : STATE_AVAILABLE);
	}
	// Drives Install/Remove/Apply together -- setting fApplyButton alone
	// here would leave the two new buttons stuck disabled after an apply.
	win->UpdateButtons();
	win->Unlock();

	exit_thread(B_OK);
	return 0;
}

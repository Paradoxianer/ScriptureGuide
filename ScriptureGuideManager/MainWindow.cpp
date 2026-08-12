#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <GroupView.h>
#include <List.h>
#include <StringView.h>

#include <stdlib.h>
#include <ColumnTypes.h>

#include "MainWindow.h"
#include "ModUtils.h"
#include "BookRow.h"
#include "DownloadLocations.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BookManager"

extern BList gFileNameList;
extern BList fConfFileList;

enum
{
	M_SELECT_MODULE='slmd',
	M_MARK_MODULE,
	M_SET_PACKAGES,
	M_INSTALL_SELECTION,
	M_REMOVE_SELECTION,
	M_SELECT_AVAILABLE,
	M_SELECT_INSTALLED
};


// What a row's status column says.
//
// The settled states say NOTHING: once a module is listed under
// "Installed" or "Available", printing "Installed" beside it only repeats
// which list the reader is already looking at. The column earns its place
// during the work, where the side a row sits on is what it WILL be and
// the text is what is happening to get there.
enum
{
	STATE_AVAILABLE = 0,	// not installed, nothing pending
	STATE_INSTALLED,		// installed, nothing pending
	STATE_WILL_INSTALL,		// queued to install
	STATE_WILL_REMOVE,		// queued to remove
	STATE_INSTALLING,		// being downloaded/unpacked right now
	STATE_REMOVING			// being deleted right now
};

static const char*
state_text(int32 state)
{
	switch(state)
	{
		case STATE_WILL_INSTALL:
		case STATE_WILL_REMOVE:		return B_TRANSLATE("Waiting…");
		case STATE_INSTALLING:		return B_TRANSLATE("Installing…");
		case STATE_REMOVING:		return B_TRANSLATE("Removing…");
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
	// B_TITLED_WINDOW_LOOK, not B_DOCUMENT_WINDOW_LOOK: per the Interface
	// Kit docs the two differ in exactly one thing -- a document window
	// gets a "draggable resize corner THUMB", a titled window "a resize
	// CORNER instead". The thumb sits inside the content area, which is
	// the BeOS arrangement for a window carrying BOTH scrollbars, where
	// it tucks into the square between them. This window has no such
	// square, so the thumb landed on top of the description pane's
	// vertical scrollbar (reported). A titled window puts the resize
	// affordance in the border where it belongs and gives the content
	// its full height back.
	: BWindow(frame, B_TRANSLATE_SYSTEM_NAME("ScriptureGuide Book Manager"),B_TITLED_WINDOW_LOOK,
 		B_NORMAL_WINDOW_FEEL, 0)
{
	fApplyThread=-1;
	
	// Set up menu
	BMenuBar *mbar=new BMenuBar("menu_bar");
	
	BMenu *menu=new BMenu(B_TRANSLATE("Program"));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),new BMessage(B_QUIT_REQUESTED),'Q',0));
	mbar->AddItem(menu);
	
	// Set up the two module lists. Which side a row is on is its state
	// (see MainWindow.h); the Status column carries only the pending part.
	fAvailableList = MakeModuleList("availablelist", M_SELECT_AVAILABLE);
	fInstalledList = MakeModuleList("installedlist", M_SELECT_INSTALLED);

	for(int32 i=0; i<fConfFileList.CountItems(); i++)
	{
		ConfigFile *cfile=(ConfigFile*)fConfFileList.ItemAt(i);
		if(cfile==NULL)
			continue;

		BookRow *row = new BookRow(cfile);
		row->SetField(new BStringField(""),COLUMN_STATUS);
		row->SetField(new BStringField(cfile->fDescription.String()),
			COLUMN_BOOK);
		row->SetField(new BStringField(cfile->fType.String()),COLUMN_TYPE);
		row->SetField(new BStringField(cfile->fLanguage.String()),
			COLUMN_LANGUAGE);

		if(IsInstalled(cfile->fFileName.String()))
			fInstalledList->AddRow(row);
		else
			fAvailableList->AddRow(row);
	}
	
	// Add the box we use for descriptions
	fTextView=new BTextView("descriptionview");
	fTextView->MakeEditable(false);
	fTextScrollView=new BScrollView("textscrollview",fTextView,0,false,true);
	fTextScrollView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	// A floor, not a fixed height -- the split above it stays draggable.
	// Without one the pane collapsed towards a single line, which is too
	// little for either a module's About text or a run of installation
	// output, the two things it exists to show.
	font_height fontHeight;
	be_plain_font->GetHeight(&fontHeight);
	float lineHeight=fontHeight.ascent+fontHeight.descent+fontHeight.leading;
	fTextScrollView->SetExplicitMinSize(
		BSize(B_SIZE_UNSET, lineHeight*8.0f));
	
	// Arrows rather than words: they say WHICH WAY a module moves, which
	// is the whole point of the two-list layout. The tooltip carries the
	// verb for anyone who wants it spelled out.
	fInstallButton=new BButton("install button", "\xE2\x86\x92",
			new BMessage(M_INSTALL_SELECTION));
	fInstallButton->SetToolTip(B_TRANSLATE("Install the modules selected on the left"));
	fInstallButton->SetEnabled(false);
	fRemoveButton=new BButton("remove button", "\xE2\x86\x90",
			new BMessage(M_REMOVE_SELECTION));
	fRemoveButton->SetToolTip(B_TRANSLATE("Remove the modules selected on the right"));
	fRemoveButton->SetEnabled(false);
	BStringView *availableLabel=new BStringView("availablelabel",B_TRANSLATE("Available"));
	BStringView *installedLabel=new BStringView("installedlabel",B_TRANSLATE("Installed"));
	// Without this the labels were what pinned the lists to their minimum
	// width, and every spare pixel went to the arrows between them:
	// BStringView::MaxSize() reports its PREFERRED width unless an
	// explicit maximum is set, and a vertical group is only as wide as
	// its narrowest-capped child allows.
	availableLabel->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,B_SIZE_UNSET));
	installedLabel->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,B_SIZE_UNSET));

	// Its own view with a pinned maximum width. Left as a plain group
	// with weight 0 it still absorbed a third of the window: weight only
	// governs how SPARE space is shared, and nothing else was capping how
	// wide the column could grow, so the two lists -- the things actually
	// being read -- ended up narrower than the gap between them.
	// Capped on the BUTTONS as well as the group. Neither weight 0 nor a
	// max size on the group alone was enough: a group whose children can
	// grow without bound reports an unbounded maximum itself, so the
	// column kept claiming a third of the window while the two lists --
	// the things actually being read -- were squeezed.
	const float kArrowWidth=28.0f;
	fInstallButton->SetExplicitMaxSize(BSize(kArrowWidth,B_SIZE_UNSET));
	fRemoveButton->SetExplicitMaxSize(BSize(kArrowWidth,B_SIZE_UNSET));

	BGroupView *moveButtons=new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(moveButtons)
		.AddGlue()
		.Add(fInstallButton)
		.Add(fRemoveButton)
		.AddGlue();
	moveButtons->SetExplicitMaxSize(BSize(kArrowWidth,B_SIZE_UNLIMITED));
	
	// The description/progress pane spans the full width UNDER both
	// lists, in its own half of a vertical split, so a long About text or
	// a run of installation output has room without stealing width from
	// the lists.
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(mbar)
		.AddSplit(B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			// Spacing 0 across the top row: the arrow column is meant to
			// read as a seam between the two lists, and item spacing on
			// both sides of it made that seam three times wider than the
			// buttons themselves.
			.AddGroup(B_HORIZONTAL, 0, 2.0f)
				.AddGroup(B_VERTICAL, 0)
					.Add(availableLabel)
					.Add(fAvailableList)
				.End()
				// Weight 0: .Add() hands out 1.0 by default, which had this
				// narrow column claiming an equal share of the width
				// alongside the two lists.
				.Add(moveButtons, 0.0f)
				.AddGroup(B_VERTICAL, 0)
					.Add(installedLabel)
					.Add(fInstalledList)
				.End()
			.End()
			// No insets: the pane is meant to span the window edge to
			// edge, the same way the lists above it do. Insets here were
			// what kept it short of both.
			.Add(fTextScrollView, 1.0f)
		.End()
	.End();
}


BColumnListView*
MainWindow::MakeModuleList(const char *name, uint32 selectionMessage)
{
	BColumnListView *list = new BColumnListView(name,0);
	list->AddColumn(new BStringColumn(B_TRANSLATE("Status"),80,40,150,0),
		COLUMN_STATUS);
	list->AddColumn(new BStringColumn(B_TRANSLATE("Book"),200,50,1000,0),COLUMN_BOOK);
	// SWORD's own module vocabulary, so this says the same thing the
	// reading app's module menu does (see ReadConfigFile()). Sortable
	// like every other column, which is what issue #41 asks for.
	list->AddColumn(new BStringColumn(B_TRANSLATE("Type"),150,50,400,0),COLUMN_TYPE);
	list->AddColumn(new BStringColumn(B_TRANSLATE("Language"),75,50,1000,0),
		COLUMN_LANGUAGE);
	// The point of issue #37: mark a whole batch at once instead of
	// double-clicking every entry in turn.
	list->SetSelectionMode(B_MULTIPLE_SELECTION_LIST);
	list->SetSelectionMessage(new BMessage(selectionMessage));
	list->SetInvocationMessage(new BMessage(M_MARK_MODULE));
	return list;
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
		case M_SELECT_AVAILABLE:
		{
			if(fApplyThread!=-1)
				break;
			// Selecting in one list clears the other's selection, so the
			// two move buttons can never both look armed and leave it
			// ambiguous which way a click would go.
			fInstalledList->DeselectAll();
			ShowSelectionInfo(fAvailableList);
			UpdateButtons();
			break;
		}
		case M_SELECT_INSTALLED:
		{
			if(fApplyThread!=-1)
				break;
			fAvailableList->DeselectAll();
			ShowSelectionInfo(fInstalledList);
			UpdateButtons();
			break;
		}
		case M_INSTALL_SELECTION:
		{
			if(fApplyThread!=-1)
				break;
			MoveSelection(true);
			break;
		}
		case M_REMOVE_SELECTION:
		{
			if(fApplyThread!=-1)
				break;
			MoveSelection(false);
			break;
		}
		case M_MARK_MODULE:
		{
			if(fApplyThread!=-1)
				break;
			// Double-click moves the row to the other side, the same as
			// the arrow button for whichever list it lives in.
			MoveSelection(fAvailableList->CurrentSelection()!=NULL);
			break;
		}
		default:
			BWindow::MessageReceived(msg);
	}
}

// Moves everything selected in the source list across to the other one.
// A row always sits in the list it will be in AFTER Apply, so moving it
// is both the gesture and the feedback.
//
// Moving a row BACK cancels the pending mark instead of adding an
// opposite one, which is why the two buttons need no third "unmark"
// companion: the state is derived from where the row now is versus what
// is actually on disk.
void MainWindow::MoveSelection(bool toInstalled)
{
	BColumnListView *source = toInstalled ? fAvailableList : fInstalledList;

	// Collected first, then moved: RemoveRow() while iterating the
	// selection would pull the row the iteration is standing on out from
	// under it.
	BList selected;
	for(BRow *r=source->CurrentSelection(); r!=NULL;
			r=source->CurrentSelection(r))
	{
		selected.AddItem(r);
	}

	for(int32 i=0; i<selected.CountItems(); i++)
	{
		BookRow *row=(BookRow*)selected.ItemAt(i);
		if(row->File()==NULL)
			continue;

		bool installed=IsInstalled(row->File()->fFileName.String());
		int32 state;
		if(toInstalled)
			state = installed ? STATE_INSTALLED : STATE_WILL_INSTALL;
		else
			state = installed ? STATE_WILL_REMOVE : STATE_AVAILABLE;
		SetRowState(row,state);
	}
	UpdateButtons();

	// Moving a module IS the instruction -- there is no separate Apply
	// step any more, so the work starts here.
	StartApply();
}


// Confirms first when anything is about to be REMOVED, and only then.
//
// Installing is additive, slow but harmless, and undone by moving the
// module back. Removing deletes a module's data directory outright, and
// this is the code path with a documented history of doing that too
// broadly (see ApplyThread()'s own comment on the empty-fDataPath bug
// that once wiped every installed module). An accidental drag of the
// wrong row is a plausible mistake with an expensive, download-sized
// undo, so it gets one question. Installing gets none.
void MainWindow::StartApply(void)
{
	if(fApplyThread!=-1)
		return;
	if(fInstallList.CountStrings()==0 && fUninstallList.CountStrings()==0)
		return;

	if(fUninstallList.CountStrings()>0)
	{
		// Placeholders rather than concatenation, so a translation can
		// put the name or the number wherever its language needs it.
		BString message;
		if(fUninstallList.CountStrings()==1)
		{
			message=B_TRANSLATE("Remove %module%?");
			message.ReplaceFirst("%module%",fUninstallList.StringAt(0));
		}
		else
		{
			BString countText;
			countText << fUninstallList.CountStrings();
			message=B_TRANSLATE("Remove %count% modules?");
			message.ReplaceFirst("%count%",countText);
		}
		message << "\n\n"
			<< B_TRANSLATE("Their downloaded data will be deleted.");

		BAlert *alert=new BAlert(B_TRANSLATE("Remove modules"),
			message.String(),B_TRANSLATE("Cancel"),B_TRANSLATE("Remove"),
			NULL,B_WIDTH_AS_USUAL,B_WARNING_ALERT);
		alert->SetShortcut(0,B_ESCAPE);
		if(alert->Go()==0)
		{
			// Put every pending row back where it actually is on disk,
			// so a cancelled removal leaves no row stranded on the wrong
			// side saying "Will remove".
			RevertPending();
			return;
		}
	}

	fApplyThread=spawn_thread(ApplyThread,"applythread",B_NORMAL_PRIORITY,
		this);
	resume_thread(fApplyThread);
}


// Re-derives every row's side and status from what is installed right
// now, discarding anything pending.
void MainWindow::RevertPending(void)
{
	fInstallList.MakeEmpty();
	fUninstallList.MakeEmpty();

	BList rows;
	for(int32 i=0; i<fAvailableList->CountRows(); i++)
		rows.AddItem(fAvailableList->RowAt(i));
	for(int32 i=0; i<fInstalledList->CountRows(); i++)
		rows.AddItem(fInstalledList->RowAt(i));

	for(int32 i=0; i<rows.CountItems(); i++)
	{
		BookRow *row=(BookRow*)rows.ItemAt(i);
		if(row==NULL || row->File()==NULL)
			continue;
		SetRowState(row, IsInstalled(row->File()->fFileName.String())
			? STATE_INSTALLED : STATE_AVAILABLE);
	}
	UpdateButtons();
}


void MainWindow::SetRowState(BookRow *row, int32 state)
{
	BStringField *field
		= dynamic_cast<BStringField*>(row->GetField(COLUMN_STATUS));
	if(field!=NULL)
		field->SetString(state_text(state));

	// The pending lists are keyed by zip file name and are what
	// ApplyThread() works from; keep them in step with what the row now
	// says rather than deriving one from the other later.
	const BString &zip=row->File()->fZipFileName;
	// Only the QUEUED states belong in the pending lists. The in-progress
	// ones are already being worked on by ApplyThread(), which owns those
	// lists for the duration -- re-adding an entry there would hand it to
	// the next run as well.
	if(state!=STATE_INSTALLING && state!=STATE_REMOVING)
	{
		fInstallList.Remove(zip);
		fUninstallList.Remove(zip);
		if(state==STATE_WILL_INSTALL)
			fInstallList.Add(zip);
		else if(state==STATE_WILL_REMOVE)
			fUninstallList.Add(zip);
	}

	// "Installed", "will be installed" and "installing" share the
	// right-hand list; "available", "will be removed" and "removing"
	// share the left. A row is always on the side it will END on, so the
	// in-progress states move it nowhere -- it is already there.
	BColumnListView *wanted
		= (state==STATE_INSTALLED || state==STATE_WILL_INSTALL
			|| state==STATE_INSTALLING)
			? fInstalledList : fAvailableList;
	BColumnListView *current
		= (fInstalledList->IndexOf(row)>=0) ? fInstalledList : fAvailableList;

	if(current!=wanted)
	{
		current->RemoveRow(row);	// detaches only, does not delete
		wanted->AddRow(row);
	}
	else
		wanted->UpdateRow(row);
}


// The row for a module, wherever it currently sits. ApplyThread() works
// from the pending lists (plain strings), but needs the row back to show
// progress on it.
BookRow* MainWindow::FindRow(const BString &zipFileName)
{
	BColumnListView *lists[2]={fAvailableList,fInstalledList};
	for(int32 l=0; l<2; l++)
	{
		for(int32 i=0; i<lists[l]->CountRows(); i++)
		{
			BookRow *row=(BookRow*)lists[l]->RowAt(i);
			if(row!=NULL && row->File()!=NULL
				&& row->File()->fZipFileName==zipFileName)
			{
				return row;
			}
		}
	}
	return NULL;
}


void MainWindow::ShowSelectionInfo(BColumnListView *list)
{
	BookRow *first=(BookRow*)list->CurrentSelection();
	int32 count=0;
	for(BRow *r=first; r!=NULL; r=list->CurrentSelection(r))
		count++;

	// With multiple selection there may be no single module to describe.
	// One row shows its description; several show how many, so the pane
	// never looks stale by describing whichever row was clicked first.
	if(count==1 && first!=NULL && first->File()!=NULL)
	{
		BString str(first->File()->fAbout);
		str << "\n\n" << B_TRANSLATE("Archive size:") << " "
			<< first->File()->fFileSize << " K";
		fTextView->SetText(str.String());
	}
	else if(count>1)
	{
		BString str;
		BString countText;
		countText << count;
		str=B_TRANSLATE("%count% modules selected");
		str.ReplaceFirst("%count%",countText);
		fTextView->SetText(str.String());
	}
}


void MainWindow::UpdateButtons(void)
{
	// Each arrow is offered only when there is something on its own side
	// to move -- an arrow that silently does nothing is the same kind of
	// lie as a link that leads nowhere.
	bool idle = fApplyThread==-1;
	fInstallButton->SetEnabled(idle
		&& fAvailableList->CurrentSelection()!=NULL);
	fRemoveButton->SetEnabled(idle
		&& fInstalledList->CurrentSelection()!=NULL);
}


int32 MainWindow::ApplyThread(void *data)
{
	MainWindow *win=(MainWindow*)data;
	
	int32 i, installcount,removecount;

	BString zipfilename, configpath, syscmd, displaystring;

	win->Lock();
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

		// One whole sentence with a placeholder, not a prefix the module
		// name is glued onto: a translation has to be free to put the name
		// elsewhere, and a fragment ending in a space is easy to get wrong.
		displaystring=B_TRANSLATE("Downloading %module%…");
		displaystring.ReplaceFirst("%module%",cfile.fDescription);
		displaystring << "\n";
				
		win->fTextView->Insert(displaystring.String());
		// Which of the queued modules is actually being worked on right
		// now -- the description pane scrolls, the row does not.
		BookRow *row=win->FindRow(zipfilename);
		if(row!=NULL)
			win->SetRowState(row,STATE_INSTALLING);
		win->Unlock();
		
		syscmd="wget -P " SG_PKGCACHE_PATH " " SG_DOWNLOAD_PKGS;
		syscmd+=zipfilename;
		syscmd+=".zip";
		
		system(syscmd.String());
		
		win->Lock();
		displaystring=B_TRANSLATE("Installing %module%…");
		displaystring.ReplaceFirst("%module%",cfile.fDescription);
		displaystring << "\n";
		win->fTextView->Insert(displaystring.String());
		win->Unlock();
		
		syscmd="unzip -o ";
		syscmd << SG_PKGCACHE_PATH << zipfilename << ".zip -d " << SG_MODULEBASE_PATH;
		
		system(syscmd.String());
		
		win->Lock();
		win->fTextView->Insert(B_TRANSLATE("Done"));
		win->fTextView->Insert("\n");
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
			displaystring=B_TRANSLATE("Error: couldn't read the config file for %module% -- skipping its removal rather than guessing.");
			displaystring.ReplaceFirst("%module%",zipfilename);
			displaystring << "\n";
			win->fTextView->Insert(displaystring.String());
			win->Unlock();
			continue;
		}

		displaystring=B_TRANSLATE("Removing %module%…");
		displaystring.ReplaceFirst("%module%",cfile.fDescription);
		displaystring << "\n";

		win->fTextView->Insert(displaystring.String());
		BookRow *row=win->FindRow(zipfilename);
		if(row!=NULL)
			win->SetRowState(row,STATE_REMOVING);
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
	win->fTextView->Insert("===== ");
	win->fTextView->Insert(B_TRANSLATE("Finished"));
	win->fTextView->Insert(" =====\n");
	win->Unlock();
	win->Lock();
	win->fApplyThread=-1;
	win->fInstallList.MakeEmpty();
	win->fUninstallList.MakeEmpty();

	// Every row's status column still says what was PENDING; the pending
	// lists have just been emptied, so rewrite each row from what is now
	// actually on disk. Without this a just-installed module kept saying
	// "Will install" until the window was reopened.
	// Every row's status still says what was PENDING and may be sitting on
	// the side it was heading for; the pending lists have just been
	// emptied, so put each row back where what is actually on disk says
	// it belongs. Collected first because SetRowState() moves rows
	// between the two lists, which would disturb an iteration over either.
	BList rows;
	for(int32 i=0; i<win->fAvailableList->CountRows(); i++)
		rows.AddItem(win->fAvailableList->RowAt(i));
	for(int32 i=0; i<win->fInstalledList->CountRows(); i++)
		rows.AddItem(win->fInstalledList->RowAt(i));

	for(int32 i=0; i<rows.CountItems(); i++)
	{
		BookRow *row=(BookRow*)rows.ItemAt(i);
		if(row==NULL || row->File()==NULL)
			continue;
		win->SetRowState(row, IsInstalled(row->File()->fFileName.String())
			? STATE_INSTALLED : STATE_AVAILABLE);
	}
	// Re-arms the two arrows now that the thread is done.
	win->UpdateButtons();
	win->Unlock();

	exit_thread(B_OK);
	return 0;
}

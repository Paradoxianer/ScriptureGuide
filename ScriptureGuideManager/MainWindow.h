#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <Button.h>
#include <ColumnListView.h>
#include <ListView.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <ScrollView.h>
#include <StringList.h>
#include <TextView.h>
#include <Window.h>


class MainWindow : public BWindow
{
public:
	MainWindow(BRect frame);
	bool QuitRequested(void);
	void MessageReceived(BMessage *msg);
private:
	static int32 ApplyThread(void *data);
	static int32 RefreshThread(void *data);
	// Two lists rather than one with a status column: which side a module
	// is on IS its state, so "what do I have" and "what could I get" are
	// answerable at a glance instead of by reading a column. A row moves
	// to the side it will be on AFTER Apply, and the Status column then
	// only has to explain the pending part ("Will install"/"Will
	// remove").
	BColumnListView	*fAvailableList;
	BColumnListView	*fInstalledList;
	BScrollView *fTextScrollView;
	BTextView *fTextView;
	BButton *fInstallButton;
	BButton *fRemoveButton;
	
	thread_id fApplyThread;
	thread_id fRefreshThread;
	BStringList fInstallList, fUninstallList;

	// Moves every row selected in the source list across to the other
	// one, marking it pending. Moving a row back cancels the mark rather
	// than stacking a second one, so the two buttons cover marking and
	// unmarking without needing a third control.
	void MoveSelection(bool toInstalled);
	// Puts `row` in the list its state belongs to and rewrites its status
	// text and its entry in the pending lists. Safe to call for a row
	// that is already in the right list.
	void SetRowState(class BookRow *row, int32 state);
	// Enables/disables the two move buttons and Apply for what is
	// currently selected and pending.
	void UpdateButtons(void);
	// Starts the install/remove work for whatever is pending. Asks first
	// if anything is being removed -- see the definition.
	void StartApply(void);
	// Discards everything pending, putting each row back on the side that
	// matches what is installed right now.
	void RevertPending(void);
	// Builds one of the two lists with identical columns.
	BColumnListView* MakeModuleList(const char *name, uint32 selectionMessage);
	// Description pane contents for whichever list was last clicked in.
	void ShowSelectionInfo(BColumnListView *list);
	// The row for a module, wherever it currently sits.
	class BookRow* FindRow(const BString &zipFileName);
	// Empties both lists and refills them from fConfFileList. Separate from
	// the constructor because a refresh has to be able to do it again.
	void PopulateLists(void);
	// Fetches the module list from crosswire.org in the background and
	// repopulates when it lands. The only way to obtain a list after
	// declining the network warning at startup, which otherwise left a
	// permanently empty window (reported).
	void StartRefresh(void);
	// Explains an empty Available list instead of leaving the window
	// looking broken. Does nothing once there is anything to show.
	void ShowEmptyListHint(void);
};

#endif


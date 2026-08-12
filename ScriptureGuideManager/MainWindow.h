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
	BListView *fListView;
	BColumnListView	*fBookListView;
	BScrollView *fTextScrollView;
	BTextView *fTextView;
	BButton *fInstallButton;
	BButton *fRemoveButton;
	BButton *fApplyButton;
	
	thread_id fApplyThread;
	BStringList fInstallList, fUninstallList;

	// Applies `install` (true = "move toward installed", false = "move
	// toward removed") to every selected row, and is what the Install and
	// Remove buttons call. Each button also UNDOES a pending mark in the
	// other direction, so the pair covers marking and unmarking without
	// needing a third control.
	void MarkSelection(bool install);
	// Rewrites one row's status text and its entry in the pending lists.
	void SetRowState(class BookRow *row, int32 state);
	// Enables/disables Install, Remove and Apply for what is currently
	// selected and pending.
	void UpdateButtons(void);
};

#endif


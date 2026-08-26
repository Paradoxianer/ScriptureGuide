#ifndef SEARCH_WINDOW_H
#define SEARCH_WINDOW_H

#include <CheckBox.h>
#include <StringView.h>
#include <ListView.h>
#include <vector>
#include <Button.h>
#include <Messenger.h>
#include <StatusBar.h>

#include <vector>

#include "SwordBackend.h"

#include "ResultListView.h"

class VersePreview;
class SGModule;

#define FIND_QUIT			'FTqu'
#define M_ACTIVATE_WINDOW	'ACwn'
#define FIND_RUN_SEARCH		'FRun'
#define FIND_SELECT_MODULE	'FSmd'
#define FIND_UPDATE_MODULES	'FUmd'

using namespace std;


enum
{
	SEARCH_WORDS = -2,
	SEARCH_PHRASE = -1,
	SEARCH_REGEX = 0
};

class SGSearchWindow : public BWindow
{
public:
					// moduleNames: every translation to offer in the
					// "Search in" field, in the order they should appear
					// -- normally the currently open reading-pane columns
					// (see SGMainWindow::EnsureSearchWindow()), so search
					// defaults to and only ever offers what's actually
					// visible rather than some separately tracked
					// "current module" disconnected from the columns.
					SGSearchWindow(BRect frame,
									const std::vector<BString>& moduleNames,
									BMessenger* owner);
					~SGSearchWindow();
	virtual bool	QuitRequested();
	virtual void	MessageReceived(BMessage* message);

					// For the universal search box (#7): fills the search
					// field and runs it exactly as if the user had typed
					// `term` in and pressed the Find button. Safe to call
					// from another window's thread (e.g. SGMainWindow's) --
					// posts a message to itself rather than touching
					// searchString directly, since that's not thread-safe
					// without holding this window's looper lock.
	void			RunSearch(const char* term);

					// Rebuilds the "Search in" field from a fresh list of
					// open columns -- called every time the search window
					// is reused (see SGMainWindow::EnsureSearchWindow()),
					// since the module list was otherwise only ever built
					// once at construction and went stale the moment a
					// column's translation changed while this window
					// stayed open. Same thread-safety note as RunSearch()
					// applies -- posts to itself rather than touching
					// moduleField directly.
	void			RefreshModuleList(
						const std::vector<BString>& moduleNames);

private:
	void			BuildGUI(void);
					// Rebuilds moduleField's menu items from
					// fModuleNames, marking index `markIndex`. Shared by
					// BuildGUI() (initial population) and
					// FIND_UPDATE_MODULES (see RefreshModuleList()).
	void			_RebuildModuleMenu(int32 markIndex);
					// Applies fModuleNames[selectIndex] (clamped) as
					// curModule/fCurrentModule and updates the window
					// title -- shared by BuildGUI(), FIND_SELECT_MODULE,
					// and FIND_UPDATE_MODULES.
	void			_ApplyModuleSelection(int32 selectIndex);

	vector<const char*>	books;
	SwordBackend		*myBible;
	BString				curModule;
	vector<BString>		fModuleNames;

	BMenuField			*moduleField;
	BMenuField			*bookField;
	BMenuField			*sndBookField;
	BTextControl		*searchString;
	ResultListView		*searchResults;
	VersePreview		*verseSelected;
	BCheckBox			*caseSensitiveCheckBox;
	BStatusBar			*searchStatus;
	BButton				*findButton;
	
	SGModule			*fCurrentModule;
	
	int					fSearchMode;
	int					fSearchFlags;
	int					fSearchStart;
	int					fSearchEnd;
	BString				fSearchString;
	vector<BString>	verseList;
	
	BMessenger			*fMessenger;
};

#endif

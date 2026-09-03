#ifndef SGMAINWINDOW_H
#define SGMAINWINDOW_H


#include <Menu.h>
#include <MenuItem.h>
#include <MenuField.h>
#include <MessageFilter.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <TextControl.h>
#include <TextView.h>
#include <String.h>
#include <Window.h>

#include <vector>

#include "DictionaryWindow.h"
#include "LogosVerseListWindow.h"
#include "LogosSearchWindow.h"
#include "SwordBackend.h"
#include "parallelbible/ParallelBibleView.h"


class SGModule;
class FontPanel;
namespace BPrivate { class BToolBar; }
using BPrivate::BToolBar;

#define M_WINDOW_CLOSED 'wcls'
using namespace std;


class EndKeyFilter : public BMessageFilter
{
public:
	EndKeyFilter(void);
	~EndKeyFilter(void);
	virtual filter_result Filter(BMessage* msg, BHandler **target);
};


// BTextControl only invokes on Enter if its text actually changed since
// the last invoke (confirmed against Haiku's own source,
// src/kits/interface/TextInput.cpp's _BTextInput_::KeyDown() -- it
// tracks fPreviousText and skips Invoke() entirely when unchanged), so
// pressing Enter a second time with the same reference or search term
// silently did nothing at all -- not a ScriptureGuide bug, but this
// window still needs Enter to always re-jump/re-search regardless.
// Intercepts B_ENTER specifically targeted at the universal search
// box's own BTextView, invokes it unconditionally, then skips the
// message so the box's own conditional-invoke KeyDown() never runs (and
// can't double-invoke when the text did change).
class UniversalSearchEnterFilter : public BMessageFilter
{
public:
	UniversalSearchEnterFilter(BTextControl* searchBox);
	~UniversalSearchEnterFilter(void);
	virtual filter_result Filter(BMessage* msg, BHandler **target);

private:
	BTextControl* fSearchBox;
};

class SGMainWindow : public BWindow
{
public:
	SGMainWindow(BRect frame, const char* module, const char* key,
					uint16 selectVers = 1, uint16 selectVersEnd = 0);
	~SGMainWindow();
	virtual bool QuitRequested();
	virtual void MessageReceived(BMessage* message);

private:
	void BuildGUI(void);
	void LoadPrefsForModule(void);
	void SavePrefsForModule(void);
	void RestoreColumnLayout(void);
	bool NeedsLineBreaks(void);

	void SetModule(const TextType &module, const int32 &index);
	void SetModuleFromString(const char* name);
	// updateParallelView: false marks the book without calling
	// UpdateParallelKey() -- used by SyncToolbarToActiveChain() (#12),
	// which is syncing the toolbar's OWN display to a chain that's
	// already at this position, not asking to navigate it there.
	void SetBook(const char* name, bool updateParallelView = true);
	void SetChapter(const int16 &chapter);
	void SetVerse(const int16 &verse);
	void UpdateParallelKey(void);

	// Navigates to `key` (a full VerseKey-parseable reference, whether
	// from a search-result drag (SG_BIBLE) or the universal search box
	// (UNIVERSAL_SEARCH, see #7)) and highlights the resulting verse.
	void JumpToKey(const char* key);
	// Mirrors the book/chapter/verse toolbar fields to whichever chain
	// is currently active in fParallelView (see issue #12's
	// PARALLEL_ACTIVE_COLUMN_CHANGED) -- unlike JumpToKey(), never
	// touches fParallelView itself (that chain is already at this
	// position; only the toolbar's own display was stale).
	void SyncToolbarToActiveChain(void);
	// Lazily creates fSearchWindow/fFindMessenger if needed and brings
	// the search window to front -- shared by MENU_EDIT_FIND and
	// UNIVERSAL_SEARCH's fall-back-to-search path.
	void EnsureSearchWindow(void);
	// Same idea for the dictionary window (#31) -- shared by the
	// Program menu's "Dictionary..." entry and a clicked Strong's
	// number (#27, PARALLEL_STRONGS_CLICKED).
	void EnsureDictionaryWindow(void);
	// Same idea for the verse list window (#47 v2) -- shared by the
	// Program menu's "Verse Lists..." entry.
	void EnsureVerseListWindow(void);

	// Navigation history, in the same two-stack shape a web browser uses:
	// fBackStack holds where we came from, fForwardStack where Back has
	// stepped away from. RecordHistory() captures where the chain that is
	// about to move currently is and must be called BEFORE the move; it
	// also clears fForwardStack, because navigating somewhere new
	// abandons whatever trail Back had opened up.
	struct HistoryEntry {
		int32	column;	// position in fParallelView's column order
		BString	key;
	};

	void RecordHistory(void);
	void GoBack(void);
	void GoForward(void);
	// Shared tail of GoBack()/GoForward(): moves to `entry`, pushing the
	// position being left onto `opposite`.
	void GoToHistoryEntry(const HistoryEntry& entry,
			std::vector<HistoryEntry>& opposite);
	// Reflects both stacks onto the menu items and toolbar buttons.
	void UpdateHistoryControls(void);
	// True while Back/Forward are driving the navigation, so the move
	// they perform isn't itself recorded as a fresh history entry --
	// which would clear the forward trail the user is walking along and
	// make Back toggle between two places instead of stepping back.
	bool fRestoringHistory;
	// Bounded (see kMaxHistoryEntries in RecordHistory()) because this
	// only ever needs to cover the last few jumps, not a session log.
	std::vector<HistoryEntry>	fBackStack;
	std::vector<HistoryEntry>	fForwardStack;

	BMenuBar		*fMenuBar;
	BMenuItem		*fBackItem;
	BMenuItem		*fForwardItem;
	BToolBar		*fToolBar;
	BMenuItem		*fShowVerseNumItem;
	BMenuItem		*fShowStrongsNumItem;
	BMenuItem		*fShowCrossRefItem;

	// #44: rebuilt every time Options opens, since a colour category is
	// just a folder the user can add, rename or delete at any moment.
	BMenu			*fHighlightColorsMenu;
	// Colours switched off, as "#rrggbb" -- keyed on the colour, not on
	// the category's name, so renaming a folder cannot silently bring
	// its highlights back.
	std::vector<BString>	fHiddenHighlightColors;
	void			_RebuildHighlightColorsMenu();
	void			_ApplyHiddenHighlightColors();

public:
	virtual void	MenusBeginning();

private:

	BMenu			*fBookMenu;

	BTextControl	*fChapterBox;
	BTextControl	*fVerseBox;
	BTextControl	*fUniversalSearchBox;

	BButton			*fNoteButton;

	ParallelBibleView	*fParallelView;
	BScrollView			*fScrollView;

	SGSearchWindow	*fSearchWindow;
	SGDictionaryWindow	*fDictionaryWindow;
	SGVerseListWindow	*fVerseListWindow;


	SwordBackend	*fModManager;
	SGModule		*fCurrentModule;
	uint16			fCurrentChapter;
	uint16			fCurrentVerse;
	uint16			fCurrentVerseEnd;

	int16			fFontSize;
	BFont			fDisplayFont;
	FontPanel		*fFontPanel;

	bool			fIsLineBreak,
					fShowVerseNumbers,
					fShowStrongsNumbers,
					fShowCrossReferences;

	BMessenger		*fFindMessenger;
};

#endif

#ifndef __TWCONSTANTS_H__
#define __TWCONSTANTS_H__

#include <InterfaceDefs.h>

//-----------------------------------------------------------------------------
//						Configuration Constants
//-----------------------------------------------------------------------------

// the help files, the website and the notes to be moved in a configuration file
#define HELPDIR "docs/index.html"
#define NOTESPATH "/boot/home/config/settings/scriptureguide/notes/"

// the currently detected fonts for greek and hebrew
#define GREEK "Aristarcoj"
#define HEBREW "SBL Hebrew"

#define PREFERENCES_PATH "/boot/home/config/settings/scriptureguide/"
#define PREFERENCES_FILE "/boot/home/config/settings/scriptureguide/settings"
#define MODULES_PATH "/boot/home/config/settings/sword/"
#define WEBSITE_URL "http://www.scripture-guide.org/"
#define SG_MANAGER_SIGNATURE "application/x-vnd.wgp.ScriptureGuideManager"

#define FONTSIZE 12
#define LINEBREAK false
#define BIBLE "Webster"
#define KEY "Gen 1:1"

const rgb_color BLACK = {0, 0, 0, 255};
const rgb_color BLUE = {50, 0, 200, 255};
const rgb_color YELLOW = {165, 165, 00, 255};
const rgb_color RED = {200, 0, 50, 255}; 
const rgb_color GREEN = {50, 200, 50, 255}; 


//-----------------------------------------------------------------------------
//						Message Constant Definitions
//-----------------------------------------------------------------------------

// Messages for window registry with application

const uint32 WINDOW_REGISTRY_ADD		= 'WRad';
const uint32 WINDOW_REGISTRY_SUB		= 'WRsb';
const uint32 WINDOW_REGISTRY_ADDED		= 'WRdd';

// Messages for menu commands

const uint32 MENU_FILE_NEW				= 'MFnw';
const uint32 MENU_FILE_QUIT				= 'MFqu';
const uint32 MENU_PROGRAM_BOOKMANAGER	= 'MPbm';
const uint32 MENU_PROGRAM_DICTIONARY	= 'MPdi';
const uint32 MENU_PROGRAM_EXPORT_PLAIN		= 'MPe1';
const uint32 MENU_PROGRAM_EXPORT_TSV		= 'MPe2';
const uint32 MENU_PROGRAM_EXPORT_MARKDOWN	= 'MPe3';
const uint32 MENU_PROGRAM_EXPORT_HTML		= 'MPe4';

const uint32 MENU_EDIT_NOTE				= 'MEno';
const uint32 MENU_EDIT_FIND				= 'MEfi';

const uint32 MENU_OPTIONS_LINE			= 'MOli';
const uint32 MENU_OPTIONS_FONT			= 'MOfo';
const uint32 MENU_OPTIONS_VERSENUMBERS	= 'MOvn';
const uint32 MENU_OPTIONS_STRONGS		= 'MOsn';
const uint32 MENU_OPTIONS_CROSSREF		= 'MOcr';
const uint32 MENU_OPTIONS_PARALLEL_VIEW	= 'MOpv';

const uint32 PARALLEL_ADD_COLUMN		= 'PVac';
const uint32 PARALLEL_SELECT_MODULE	= 'PVsm';
const uint32 PARALLEL_SELECT_NOTES		= 'PVsn';
const uint32 PARALLEL_ADD_COLUMN_MENU	= 'PVam';
const uint32 PARALLEL_REMOVE_COLUMN	= 'PVrc';
const uint32 PARALLEL_REORDER_COLUMN	= 'PVro';
const uint32 PARALLEL_TOGGLE_LINK		= 'PVtl';
const uint32 PARALLEL_INSERT_COLUMN_MENU	= 'PVic';
const uint32 PARALLEL_INSERT_MODULE	= 'PVim';
const uint32 PARALLEL_INSERT_NOTES		= 'PVin';
// Posted to Window() whenever the active column/chain changes (see
// ParallelBibleView::_SetActiveColumn()) -- lets the owning window sync
// its own book/chapter/verse toolbar fields to whichever chain is now
// active, without re-navigating it (contrast SG_BIBLE, which does).
const uint32 PARALLEL_ACTIVE_COLUMN_CHANGED = 'PVca';
// Posted by NoteVerseView::MakeFocus(false) (focus lost -- the normal
// "done editing this verse" signal) instead of calling
// ParallelBibleView::_StopEditingNoteVerse() directly, which would tear
// down (RemoveSelf()+delete) the very NoteVerseView whose own MakeFocus()
// override is still on the call stack at that point -- same deferred-
// dispatch reasoning already used for PARALLEL_SELECT_MODULE etc. (see
// ParallelBibleView::MessageReceived()'s own comment on the BMenuField
// thread-deadlock hazard that pattern avoids; this one avoids a
// self-delete-mid-virtual-call hazard instead, not a deadlock, but the
// fix is the same shape: let the current dispatch finish first).
const uint32 PARALLEL_STOP_EDITING_NOTE_VERSE = 'PVse';

const uint32 MENU_HELP_LOGOS			= 'MHlo';
const uint32 MENU_HELP_HOWTO			= 'MHho';
const uint32 MENU_HELP_ABOUT			= 'MHab';

const uint32 SELECT_BIBLE				= 'STbi';
const uint32 SELECT_COMMENTARY			= 'STcm';
const uint32 SELECT_LEXICON				= 'STlx';
const uint32 SELECT_GENERAL				= 'STgr';
const uint32 SELECT_MODULE				= 'STmo';
const uint32 SELECT_BOOK				= 'STbo';
const uint32 SELECT_CHAPTER				= 'STch';
const uint32 SELECT_VERSE				= 'STve';
const uint32 SELECT_FONT				= 'STfn';
const uint32 UNIVERSAL_SEARCH			= 'STus';

const uint32 NEXT_BOOK					= 'STnb';
const uint32 PREV_BOOK					= 'STpb';
const uint32 NEXT_CHAPTER				= 'STnc';
const uint32 PREV_CHAPTER				= 'STpc';

const uint32 FIND_BUTTON_OK				= 'FBok';
const uint32 FIND_BUTTON_HELP			= 'FBhe';
const uint32 FIND_CHECK_CASE_SENSITIVE	= 'FCcs';
const uint32 FIND_SELECT_FROM			= 'FSfr';
const uint32 FIND_SELECT_TO				= 'FSto';
const uint32 FIND_SEARCH_STR			= 'FSst';
const uint32 FIND_RADIO1				= 'FSr1';
const uint32 FIND_RADIO2				= 'FSr2';
const uint32 FIND_RADIO3				= 'FSr3';
const uint32 FIND_LIST_CLICK			= 'FLcl';
const uint32 FIND_LIST_DCLICK			= 'FLdc';
const uint32 FIND_TMP					= 'FTmp';

const uint32 DOCS_UNAVAILABLE			= 'DCUN';

const uint32 SG_BIBLE					= 'SGbl';
const uint32 SG_STRONGS_LOOKUP			= 'SGsl';

// search flags

const int REG_ICASE						= 2;  
// include "posix/regex.h" instead if more are needed

#endif

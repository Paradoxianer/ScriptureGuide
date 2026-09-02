#ifndef __TWCONSTANTS_H__
#define __TWCONSTANTS_H__

#include <InterfaceDefs.h>

//-----------------------------------------------------------------------------
//						Configuration Constants
//-----------------------------------------------------------------------------

// the help files and the website to be moved in a configuration file
#define HELPDIR "docs/index.html"

// the currently detected fonts for greek and hebrew
#define GREEK "Aristarcoj"
#define HEBREW "SBL Hebrew"

#define PREFERENCES_PATH "/boot/home/config/settings/scriptureguide/"
#define PREFERENCES_FILE "/boot/home/config/settings/scriptureguide/settings"
#define MODULES_PATH "/boot/home/config/settings/sword/"
#define WEBSITE_URL "http://www.scripture-guide.org/"
#define SG_APP_SIGNATURE "application/x-vnd.Scripture-Guide"
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
const uint32 MENU_PROGRAM_VERSELISTS	= 'MPvl';
const uint32 MENU_PROGRAM_EXPORT_PLAIN		= 'MPe1';
const uint32 MENU_PROGRAM_EXPORT_TSV		= 'MPe2';
const uint32 MENU_PROGRAM_EXPORT_MARKDOWN	= 'MPe3';
const uint32 MENU_PROGRAM_EXPORT_HTML		= 'MPe4';

const uint32 MENU_EDIT_NOTE				= 'MEno';
const uint32 MENU_EDIT_FIND				= 'MEfi';
// Steps the active chain back to wherever it was before the last jump
// (see SGMainWindow::GoBack()). Every navigation funnels through
// UpdateParallelKey(), so this covers following a cross-reference, the
// toolbar fields, search results and dropped references alike -- not
// just links.
const uint32 MENU_NAVIGATION_BACK		= 'MNbk';
const uint32 MENU_NAVIGATION_FORWARD	= 'MNfw';

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
// Posted (debounced) by NotesSaveListener when the user has typed in a
// notes column, so ParallelBibleView can re-run _Realign() and let the
// edited note's new height push its verse's row back into line with the
// Bible columns beside it. Debounced rather than immediate because
// _Realign() re-measures every verse of every column in the chain --
// cheap enough to run when typing pauses, far too expensive per
// keystroke (see ParallelBibleView::NoteTextEdited()).
const uint32 PARALLEL_NOTES_TEXT_CHANGED = 'PVnt';
// #67: right-click "Add to list ▸" on a verse (or selection) in the
// reading pane -- each item in the cascading collection tree carries
// the already-formatted reference plus its source versification/locale
// as "path"/"reference"/"versification"/"locale".
const uint32 PARALLEL_ADD_TO_VERSE_LIST	= 'PVal';
// #44: a colour was picked in the highlight palette that pops up after a
// drag-selection. Carries "color" (an int32-packed rgb_color) and
// "name" (the palette entry's own name, used as the colour's folder);
// absent "color" means "remove the highlights under this selection".
const uint32 PARALLEL_HIGHLIGHT_APPLY	= 'PVha';
// #44: the palette's trailing "..." cell -- opens the ordinary
// right-click menu for the same selection, so the quick colour bar and
// the full action menu are one set of actions rather than two.
const uint32 PARALLEL_HIGHLIGHT_MORE	= 'PVhm';

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

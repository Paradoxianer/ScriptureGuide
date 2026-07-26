#ifndef __SWORDBACKEND_H__
#define __SWORDBACKEND_H__

#include <swmgr.h>
#include <swtext.h>
#include <vector>
#include <Locale.h>
#include <String.h>
#include <StatusBar.h>

#include "ObjectList.h"

// Type for a particular module
typedef enum
{
	TEXT_UNASSIGNED = -1,
	TEXT_UNKNOWN = 0,
	TEXT_GENERIC,
	TEXT_BIBLE,
	TEXT_COMMENTARY,
	TEXT_LEXICON
} TextType;

// Flags for which testaments exist in a book
enum
{
	TESTAMENT_OLD = 1,
	TESTAMENT_NEW = 2,
	TESTAMENT_APOCRYPHA = 4
};

// Utility Functions
int				ChaptersInBook(const char* book); 
int				VersesInChapter(const char* book, int chapter); 

const char*		BookFromKey(const char* key);
int 			ChapterFromKey(const char* key);
int				VerseFromKey(const char* key);
int				UpperVerseFromKey(const char* key);

// For the universal search/goto box (#7): true if `input` parses as an
// actual Bible reference (localized or English book name/abbreviation,
// German comma or colon as the chapter:verse separator), with
// `normalizedKey` set to VerseKey's own canonical form (e.g. "1. Mose 1:1")
// -- ready to pass straight to ParallelBibleView::SetKey(). False for
// anything else (a plain search term), leaving normalizedKey untouched.
bool			ParseVerseReference(const char* input,
					BString& normalizedKey);

// One occurrence of a recognized verse reference embedded in a larger
// block of free text (a commentary's prose, not a whole search/goto
// field) -- see FindReferencesInText() below. start/length are a byte
// range into the ORIGINAL text passed in, suitable for splitting it into
// TextSpans around the match (see #28, cross-reference navigation).
struct TextReference {
	int32	start;
	int32	length;
	BString	normalizedKey;
};

// Scans free-flowing text (typically a commentary's rendered verse text,
// which -- unlike a dedicated cross-reference module -- SWORD has no
// structured API for; see #28) for substrings that look like a verse
// reference (a capitalized book-ish word, optionally preceded by "1 "/
// "2 "/"3 ", followed by chapter/verse digits) and validates each
// candidate through the exact same ParseVerseReference() every typed
// reference already goes through -- so a candidate the regex below
// spotted but that isn't actually a real book/chapter/verse (a stray
// "Kapitel 5, 3" or similar) is silently dropped rather than turned into
// a broken link, with no separate book-name dictionary to keep in sync.
std::vector<TextReference>	FindReferencesInText(const char* text);


std::vector<const char*>	GetBookNames(void);

// The main interface with the SWORD library	
class SGModule
{
public:
						SGModule(sword::SWModule* module);
	const char*			Name(void);
	const char*			FullName(void);
	const char*			Language(void);
	
	TextType			Type(void) { return fType; }
	
	const char*			GetKey(void);
	void 				SetKey(const char* key);
	
	const char*			GetVerse();
	const char*			GetVerse(const char* book, int chapter, int verse);
	const char*			GetVerse(const char* key);
	const char*			GetParagraph(const char* key);
	
	void				SetVerse(const char* book, int chapter, int verse);
	
	std::vector<const char*>	SearchModule(int searchType, int flags,
								const char* searchText, const char* scopeFrom,
								const char* scopeTo, BStatusBar* statusBar);
	
	bool				IsGreek(void);
	bool				IsHebrew(void);
	
 	bool				HasOT(void);
 	bool				HasNT(void);
	
	sword::SWModule*	GetModule(void) const { return fModule; }
	
private:
	void				DetectTestaments(void);
	
	sword::SWModule*	fModule;
	TextType			fType;
	
	bool				fDetectOTNT,
						fHasOT,
						fHasNT;
	
	BLanguage			language;

};

typedef BObjectList<SGModule, true> SGModuleList;

class SwordBackend
{
public:
						SwordBackend(void);
						~SwordBackend(void);
	
	int32				CountModules(void) const;
	int32				CountBibles(void) const;
	int32				CountCommentaries(void) const;
	int32				CountLexicons(void) const;
	int32				CountGeneralTexts(void) const;
	
	SGModule*			BibleAt(const int32 &index) const;
	SGModule*			CommentaryAt(const int32 &index) const;
	SGModule*			LexiconAt(const int32 &index) const;
	SGModule*			GeneralTextAt(const int32 &index) const;
	
	SGModule*			FindModule(const char* name);
	status_t			SetModule(SGModule* mod);
 	SGModule*			CurrentModule(void);

	sword::SWMgr*		Manager(void) const
							{ return fManager; }

private:
	sword::SWMgr* 		fManager;
	SGModule*			fModule;
	
	SGModuleList		*fBibleList,
						*fCommentList,
						*fLexiconList,
						*fTextList;
};
#endif


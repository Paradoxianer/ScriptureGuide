#ifndef __SWORDBACKEND_H__
#define __SWORDBACKEND_H__

#include <swmgr.h>
#include <swtext.h>
#include <vector>
#include <Locale.h>
#include <String.h>
#include <StatusBar.h>

#include "constants.h"
#include "ObjectList.h"


// Type for a particular module


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

typedef BObjectList<SGModule> SGModuleList;

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

private:
	sword::SWMgr* 		fManager;
	SGModule*			fModule;
	
	SGModuleList		*fBibleList,
						*fCommentList,
						*fLexiconList,
						*fTextList;
};
#endif


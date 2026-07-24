#include <StatusBar.h>
#include <Language.h>

#include <swmgr.h>
#include <swtext.h>
#include <versekey.h>

#include <localemgr.h>
#include <markupfiltmgr.h>
#include <gbfplain.h>
#include <gbfstrongs.h>

#include <vector>
#include <map>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <String.h>
#include "SwordBackend.h"
#include "constants.h"

using namespace std;

#ifndef NO_SWORD_NAMESPACE
using namespace sword;
#endif

// path for the modules; to be stored in a config file in future
#define CONFIGPATH MODULES_PATH

SGModule::SGModule(sword::SWModule* module)
 :	fModule(module),
 	fDetectOTNT(true),
 	fHasOT(false),
 	fHasNT(false)
{
	BLocale::Default()->GetLanguage(&language);	

	if (strcmp(fModule->getType(), "Biblical Texts")==0)
		fType = TEXT_BIBLE;
	else
	if (strcmp(fModule->getType(), "Commentaries")==0)
		fType = TEXT_COMMENTARY;
	else
	if (strcmp(fModule->getType(), "Lexicons / Dictionaries")==0)
		fType = TEXT_LEXICON;
	else
	if (strcmp(fModule->getType(), "Generic Text")==0)
		fType = TEXT_GENERIC;
	else
		fType = TEXT_UNKNOWN;
	
	if (!fModule->hasSearchFramework())
	{
		fModule->createSearchFramework();
	}
}


bool SGModule::IsGreek(void)
{
	return ( !strcmp(fModule->getLanguage(), "grc")
		|| !strcmp(fModule->getLanguage(), "el") );
}


bool SGModule::IsHebrew(void)
{
	return strcmp(fModule->getLanguage(), "he") == 0;
}


bool SGModule::HasOT(void)
{
	if (fDetectOTNT)
		DetectTestaments();
	
	return fHasOT;
}


bool SGModule::HasNT(void)
{
	if (fDetectOTNT)
		DetectTestaments();
	
	return fHasNT;
}


void SGModule::DetectTestaments(void)
{
	fDetectOTNT = false;
	if (fType == TEXT_BIBLE)
	{
		// Detect testaments in module
		fHasOT	= fModule->hasEntry(new SWKey("Gen 1:1"));
		fHasNT	= fModule->hasEntry(new SWKey("Mat 1:1"));
	} else if (fType == TEXT_COMMENTARY)
	{
		fHasOT = true;
		fHasNT = true;
	}
}


const char* SGModule::Name(void)
{
	return fModule->getName();
}


const char* SGModule::FullName(void)
{
	return fModule->getDescription();
}


const char* SGModule::Language(void)
{
	return fModule->getLanguage();
}


const char* SGModule::GetVerse(const char* book, int chapter, int verse)
{
	VerseKey myKey = VerseKey();
	myKey.setLocale(language.Code());
	myKey.setBookName(book);
	myKey.setChapter(chapter);
	myKey.setVerse(verse);
	fModule->setKey(myKey);
	return fModule->renderText();
}


const char* SGModule::GetVerse(const char* key)
{
	VerseKey myKey = VerseKey();
	myKey.setLocale(language.Code());
	fModule->setKey(key);
	return fModule->renderText();
}


const char* SGModule::GetParagraph(const char* key)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	BString bibleText = BString();
	VerseKey minKey(key);
	minKey.decrement();
	VerseKey maxKey(key);
	maxKey.increment();
	VerseKey paragraph;
	paragraph.setLowerBound(minKey);
	paragraph.setUpperBound(maxKey);
	paragraph.setLocale(language.Code());
	fModule->setKey(paragraph);
	bibleText << paragraph.getRangeText();
	bibleText << "\n";
	if (paragraph.isBoundSet())
	{
		VerseKey temp(paragraph);
		for (int i = paragraph.getLowerBound().getIndex();
			i <= paragraph.getUpperBound().getIndex(); ++i)
		{
			temp.setIndex(i);
			fModule->setKey(temp);
			bibleText << temp.getVerse();
			bibleText << " ";
			bibleText.Append(fModule->renderText());
		}
	}
	return bibleText.String();
}


const char* SGModule::GetKey(void)
{
	VerseKey* key = (VerseKey*)fModule->getKey();	
	if (!key)
		return NULL;
	
	key->setLocale(language.Code());
	return key->getText();
}


void SGModule::SetKey(const char* key)
{
	// TODO: Convert this to return a status_t - B_ERROR on failure.
	// This will depend on finding out what kinds of error codes are returned
	// when sword::SWModule::SetKey fails and succeeds
	if (!key)
		return;
	VerseKey vkey(key);
	vkey.setLocale(language.Code());
	fModule->setKey(vkey);
}


void SGModule::SetVerse(const char* book, int chapter, int verse)
{
	// TODO: Convert this to return a status_t - B_ERROR on failure.
	// This will depend on finding out what kinds of error codes are returned
	// when sword::SWModule::SetKey fails and succeeds
	VerseKey myKey = VerseKey();;
	myKey.setLocale(language.Code());
	myKey.setBookName(book);
	myKey.setChapter(chapter);
	myKey.setVerse(verse);
	fModule->setKey(myKey);
}


// callback function: sword library calls it with percentage-done
// during a search
void percentUpdate(char percent, void *userData)
{
	BStatusBar* bar;
	bar = (BStatusBar *)userData;
	bar->Update((float)percent - bar->CurrentValue());
}


// returns a list of search results on the current module with
// searchType: word search, phrase search, regex search
// flags: standard regex flags: see regex.h from POSIX
// searchText: the text to search for
// scopeFrom: book name to search from
// scopeTo: book name to search to
vector<const char*> SGModule::SearchModule(int searchType, int flags, 
						const char* searchText, const char* startbook,
						const char* endbook, BStatusBar* statusBar)
{
	vector<const char*> results;
	
	
	int chapter = ChaptersInBook(endbook);
	int verse = VersesInChapter(endbook, chapter);
	
	BString searchstr;
	searchstr << startbook << " 1:1-" << endbook << " " << chapter << ":" << verse;
	VerseKey parse = "Gen 1:1";
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);	
	parse.setLocale(language.Code());

	ListKey scope = parse.parseVerseList(searchstr.String(), parse, true);
	ListKey &listkey = fModule->search(searchText, searchType, flags,
								&scope, 0, &percentUpdate, statusBar);
	
	listkey.setPersist(true);
	fModule->setKey(listkey);

	for (listkey = TOP; !listkey.popError(); listkey++) 
		results.push_back((const char*) listkey);

	return results;
}


SwordBackend::SwordBackend(void)
{
	fManager = new SWMgr(CONFIGPATH, true, new MarkupFilterMgr(FMT_GBF, ENC_UTF8));
	
	// We are going to replace GetModuleDescriptions with some methods which
	// are a little easier to deal with outside the class
	
	// First, lists to contain the names of each type of text
	fBibleList = new SGModuleList(20);
	fCommentList = new SGModuleList(20);
	fLexiconList = new SGModuleList(20);
	fTextList = new SGModuleList(20);
	
	ModMap::iterator it;
	SWModule* currentmodule = 0;
	vector<const char*> tmp;
	
	for (it = fManager->Modules.begin(); it != fManager->Modules.end(); it++) 
	{
		currentmodule = (*it).second;
		currentmodule->addRenderFilter(new GBFPlain());
		
		if (!strcmp(currentmodule->getType(), "Biblical Texts"))
			fBibleList->AddItem(new SGModule(currentmodule));
		else
		if (!strcmp(currentmodule->getType(), "Commentaries"))
			fCommentList->AddItem(new SGModule(currentmodule));
		else
		if (!strcmp(currentmodule->getType(), "Lexicons / Dictionaries"))
			fLexiconList->AddItem(new SGModule(currentmodule));
		else
		if (!strcmp(currentmodule->getType(), "Generic Books"))
			fTextList->AddItem(new SGModule(currentmodule));
		else
		{
			printf("Found module %s with type %s\n",
				currentmodule->getDescription(), currentmodule->getType());
		}
	}
}


SwordBackend::~SwordBackend(void)
{
	delete fManager;
	
	delete fBibleList;
	delete fCommentList;
	delete fLexiconList;
	delete fTextList;
}


SGModule* SwordBackend::FindModule(const char* name)
{
	sword::SWModule* module = fManager->Modules[name];
	
	if (!module)
		return NULL;
	
	SGModuleList* list;
	
	if (!strcmp(module->getType(), "Biblical Texts"))
		list = fBibleList;
	else
	if (!strcmp(module->getType(), "Commentaries"))
		list = fCommentList;
	else
	if (!strcmp(module->getType(), "Lexicons / Dictionaries"))
		list = fLexiconList;
	else
	if (!strcmp(module->getType(), "Generic Books"))
		list = fTextList;
	else
		return NULL;
	
	for (int32 i = 0; i < list->CountItems(); i++)
	{
		SGModule* mod = list->ItemAt(i);
		if (mod->GetModule() == module)
			return mod;
	}
	return NULL;
}


status_t SwordBackend::SetModule(SGModule* mod)
{
	if (!mod)
		return B_ERROR;
	
	fModule = mod;
	return B_OK;
}


int32 SwordBackend::CountModules(void) const
{
	return fManager->Modules.size();
}


int32 SwordBackend::CountBibles(void) const
{
	return fBibleList->CountItems();
}


int32 SwordBackend::CountCommentaries(void) const
{
	return fCommentList->CountItems();
}


int32 SwordBackend::CountLexicons(void) const
{
	return fLexiconList->CountItems();
}


int32 SwordBackend::CountGeneralTexts(void) const
{
	return fTextList->CountItems();
}


SGModule* SwordBackend::BibleAt(const int32 &index) const
{
	return fBibleList->ItemAt(index);
}


SGModule* SwordBackend::CommentaryAt(const int32 &index) const
{
	return fCommentList->ItemAt(index);
}


SGModule* SwordBackend::LexiconAt(const int32 &index) const
{
	return fLexiconList->ItemAt(index);
}


SGModule* SwordBackend::GeneralTextAt(const int32 &index) const
{
	return fTextList->ItemAt(index);
}


vector<const char*> GetBookNames(void)
{
	vector<const char*> books;
	VerseKey myKey = VerseKey();
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	int i = 1;
	int j = 0;
	for (i = 1; i<=2; i++)
	{
		myKey.setTestament(i);
		for (j = 0; j<=myKey.getBookMax(); j++)
		{
			myKey.setTestament(i);
			myKey.setBook(j);
			myKey.setLocale(language.Code());
			books.push_back(myKey.getBookName());
		}
	}
	return books;
}


int ChaptersInBook(const char* book)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	VerseKey myKey = VerseKey();;
	myKey.setLocale(language.Code());
	myKey.setBookName(book);
	return myKey.getChapterMax();
}


int VersesInChapter(const char* book, int chapter)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	VerseKey myKey = VerseKey();;
	myKey.setLocale(language.Code());
	myKey.setBookName(book);
	myKey.setChapter(chapter);
	return myKey.getVerseMax();
}


const char* BookFromKey(const char* key)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	VerseKey myKey = VerseKey();
	myKey.setLocale(language.Code());
	myKey.setText(key);
	return myKey.getBookName();
}


int ChapterFromKey(const char* key)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	VerseKey myKey = VerseKey();
	myKey.setLocale(language.Code());
	myKey.setText(key);
	return myKey.getChapter();
}


int VerseFromKey(const char* key)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	VerseKey myKey = VerseKey();
	myKey.setLocale(language.Code());
	myKey.setText(key);
	return myKey.getVerse();
}


int UpperVerseFromKey(const char* key)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	VerseKey myKey = VerseKey();
	myKey.setLocale(language.Code());
	myKey.setText(key);
	return myKey.getUpperBound().getVerse();
}


// Trailing "<chapter>[:<verse>]" digit run at the very end of `text`, if
// any -- e.g. 3 and 16 out of "Joh 3:16", or just 13 (chapter, hasVerse
// false) out of "1 Kor 13". False (leaving the out-params untouched) if
// the string doesn't end in digits at all, e.g. a bare book name like
// "Genesis". Used by ParseVerseReference() below to catch VerseKey
// silently mis-parsing part of the input rather than erroring out --
// confirmed empirically that "Joh 3,16" (before comma-to-colon
// normalization) parses with no error at all, but as chapter 3 *verse 1*,
// silently dropping the ",16" instead of failing loudly.
static bool
ExtractTrailingChapterVerse(const BString& text, int& chapter, int& verse,
	bool& hasVerse)
{
	int32 end = text.Length();

	int32 verseEnd = end;
	while (end > 0 && isdigit((unsigned char)text[end - 1]))
		end--;
	int32 verseStart = end;

	if (verseStart == verseEnd)
		return false;

	hasVerse = false;
	verse = 0;

	if (end > 0 && text[end - 1] == ':') {
		hasVerse = true;
		verse = atoi(text.String() + verseStart);
		end--;

		int32 chapterEnd = end;
		while (end > 0 && isdigit((unsigned char)text[end - 1]))
			end--;
		int32 chapterStart = end;
		if (chapterStart == chapterEnd)
			return false;
		chapter = atoi(text.String() + chapterStart);
	} else {
		chapter = atoi(text.String() + verseStart);
	}

	return true;
}


bool ParseVerseReference(const char* input, BString& normalizedKey)
{
	BString trimmed(input);
	trimmed.Trim();
	if (trimmed.IsEmpty())
		return false;

	// A reference always names a book; guards against bare numbers ("13",
	// "2023") that VerseKey would otherwise silently accept as a verse/
	// chapter number in whatever book it happens to default to.
	bool hasLetter = false;
	for (int32 i = 0; i < trimmed.Length(); i++) {
		if (isalpha((unsigned char)trimmed[i])) {
			hasLetter = true;
			break;
		}
	}
	if (!hasLetter)
		return false;

	// German verse separator ("Joh 3,16") -- VerseKey only understands
	// ':' natively, see ExtractTrailingChapterVerse()'s comment above.
	trimmed.ReplaceAll(',', ':');

	int expectedChapter = 0;
	int expectedVerse = 0;
	bool hasVerse = false;
	bool hasChapterVerse = ExtractTrailingChapterVerse(trimmed,
		expectedChapter, expectedVerse, hasVerse);

	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	VerseKey key;
	key.setLocale(language.Code());
	key.setText(trimmed.String());

	if (key.popError())
		return false;

	if (hasChapterVerse) {
		if (key.getChapter() != expectedChapter)
			return false;
		if (hasVerse && key.getVerse() != expectedVerse)
			return false;
	}

	normalizedKey = key.getText();
	return true;
}

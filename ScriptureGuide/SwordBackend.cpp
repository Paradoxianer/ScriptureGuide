#include <StatusBar.h>
#include <Language.h>

#include <swmgr.h>
#include <swtext.h>
#include <versekey.h>

#include <localemgr.h>
#include <markupfiltmgr.h>
#include <gbfplain.h>

#include <vector>
#include <map>
#include <regex>
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
	// key is already locale-formatted text by the time it gets here (see
	// BibleItem's key, built from VerseKey::getText() after setLocale()
	// in FIND_BUTTON_OK) -- the VerseKey(const char*) constructor parses
	// with no locale set at all, silently mis-parsing it under a non-
	// English locale (confirmed empirically elsewhere this same
	// session: "Johannes 3:16" with no locale set resolves to
	// "Revelation of John" 1:1 instead), which is exactly why the
	// clicked-verse preview showed the wrong text.
	VerseKey minKey;
	minKey.setLocale(language.Code());
	minKey.setText(key);
	minKey.decrement();
	VerseKey maxKey;
	maxKey.setLocale(language.Code());
	maxKey.setText(key);
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


// Lexicon/dictionary modules (see #31) are keyed by a plain string --
// "3056", a headword, whatever the module itself uses -- never a
// VerseKey, so this bypasses every VerseKey-locale dance the Bible/
// Commentary methods above need entirely and just sets the module's raw
// key text directly. Confirmed empirically against GerStrongsGreek: its
// keys have no "G"/"H" prefix at all (LookupStrongsNumber() below strips
// it before calling this), and a leading zero is optional.
const char* SGModule::GetEntry(const char* key)
{
	fModule->setKey(key);
	return fModule->renderText();
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
	// Same class of bug as GetParagraph() above: locale has to be set
	// before parsing, not after -- key may already be locale-formatted
	// text (e.g. from a caller that read it back via GetKey(), which
	// returns already-localized text).
	VerseKey vkey;
	vkey.setLocale(language.Code());
	vkey.setText(key);
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
vector<BString> SGModule::SearchModule(int searchType, int flags,
						const char* searchText, const char* startbook,
						const char* endbook, BStatusBar* statusBar)
{
	vector<BString> results;


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

	// BString copies the text immediately -- (const char*)listkey points
	// into one of a small, rotating pool of buffers SWORD's ListKey/
	// VerseKey reuses across every hit in this loop, so holding onto the
	// raw pointer instead of an owned copy means later iterations
	// silently overwrite earlier hits' text (confirmed empirically: an
	// "Adam" search's own results shifted and repeated once the pool
	// wrapped around, well before the caller ever reads the vector back).
	for (listkey = TOP; !listkey.popError(); listkey++)
		results.push_back(BString((const char*) listkey));

	return results;
}


vector<BString> SGModule::SearchEntries(const char* searchText)
{
	vector<BString> results;

	ListKey &listkey = fModule->search(searchText, -2 /* multiword */,
		REG_ICASE);
	listkey.setPersist(true);
	fModule->setKey(listkey);

	// See SearchModule()'s own comment -- same buffer-reuse bug, same fix.
	for (listkey = TOP; !listkey.popError(); listkey++)
		results.push_back(BString((const char*) listkey));

	return results;
}


SwordBackend::SwordBackend(void)
{
	fManager = new SWMgr(CONFIGPATH, true, new MarkupFilterMgr(FMT_GBF, ENC_UTF8));

	// Harmless for every module that doesn't declare
	// GlobalOptionFilter=*Strongs in its own .conf (this is a no-op for
	// them, confirmed empirically) -- only modules that both support it
	// AND have this filter attached start emitting <w lemma="strong:
	// G1063"...>word</w> markup in renderText() at all (see #27,
	// StripStrongsMarkup()). No separate on/off UI toggle for now,
	// matching how cross-reference detection (#28) is likewise always
	// on rather than adding another preference to plumb through/persist.
	fManager->setGlobalOption("Strong's Numbers", "On");

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


std::vector<BString> SwordBackend::SearchableModuleNames(void) const
{
	std::vector<BString> names;
	for (int32 i = 0; i < CountBibles(); i++) {
		SGModule* module = BibleAt(i);
		if (module != NULL)
			names.push_back(BString(module->Name()));
	}
	for (int32 i = 0; i < CountCommentaries(); i++) {
		SGModule* module = CommentaryAt(i);
		if (module != NULL)
			names.push_back(BString(module->Name()));
	}
	return names;
}


// True if `lexicon` declares the standard SWORD Feature= config entry
// that marks it as a Strong's dictionary of the wanted kind.
static bool
declares_feature(SGModule* lexicon, const char* wantedFeature)
{
	if (lexicon == NULL || lexicon->GetModule() == NULL)
		return false;

	const ConfigEntMap& conf = lexicon->GetModule()->getConfig();
	std::pair<ConfigEntMap::const_iterator, ConfigEntMap::const_iterator>
		range = conf.equal_range("Feature");
	for (ConfigEntMap::const_iterator it = range.first;
			it != range.second; ++it) {
		if (it->second == wantedFeature)
			return true;
	}
	return false;
}


static const char*
strongs_feature_for(char prefix)
{
	if (prefix == 'G')
		return "GreekDef";
	if (prefix == 'H')
		return "HebrewDef";
	return NULL;
}


// True if `landed` -- the key a module actually ended up on -- is the
// same Strong's number as `wanted`, compared numerically.
//
// Both sides need normalizing: a Strong's module reports 2316 as "02316"
// (leading zero), and some modules prefix the letter, so a plain string
// compare rejects correct hits. Everything up to the first digit is
// skipped and the rest read as a number.
static bool
landed_on_number(const char* landed, const char* wanted)
{
	if (landed == NULL || wanted == NULL)
		return false;
	while (*landed != '\0' && (*landed < '0' || *landed > '9'))
		landed++;
	while (*wanted != '\0' && (*wanted < '0' || *wanted > '9'))
		wanted++;
	if (*landed == '\0' || *wanted == '\0')
		return false;
	return strtol(landed, NULL, 10) == strtol(wanted, NULL, 10);
}


bool HasStrongsDictionary(SWMgr* manager, char prefix)
{
	const char* wantedFeature = strongs_feature_for(prefix);
	if (manager == NULL || wantedFeature == NULL)
		return false;

	// Scans every installed module rather than only those SwordBackend
	// sorted into its lexicon list: what makes a module able to answer a
	// Strong's lookup is the Feature entry, not which list it landed in.
	ModMap::iterator it;
	for (it = manager->Modules.begin(); it != manager->Modules.end(); it++) {
		SWModule* module = it->second;
		if (module == NULL)
			continue;
		const ConfigEntMap& conf = module->getConfig();
		std::pair<ConfigEntMap::const_iterator, ConfigEntMap::const_iterator>
			range = conf.equal_range("Feature");
		bool declaresFeature = false;
		for (ConfigEntMap::const_iterator f = range.first;
				f != range.second && !declaresFeature; ++f) {
			if (f->second == wantedFeature)
				declaresFeature = true;
		}
		if (!declaresFeature)
			continue;

		// Declaring the feature is not enough -- see
		// LookupStrongsNumber() for the module that proves it (Dodson
		// declares GreekDef and is keyed by Greek lemma, not by number).
		// Probe a number every Strong's dictionary has and see whether the
		// module lands on it; if it snaps somewhere else, it cannot
		// answer a Strong's lookup and must not make one look possible.
		module->setKey("1");
		if (landed_on_number(module->getKeyText(), "1"))
			return true;
	}
	return false;
}


bool SwordBackend::HasStrongsDictionary(char prefix) const
{
	return ::HasStrongsDictionary(fManager, prefix);
}


const char* SwordBackend::StrongsDictionaryNameFor(char prefix)
{
	if (prefix == 'G')
		return "StrongsGreek";
	if (prefix == 'H')
		return "StrongsHebrew";
	return NULL;
}


BString SwordBackend::LookupStrongsNumber(const char* strongsNumber) const
{
	if (strongsNumber == NULL || *strongsNumber == '\0')
		return BString();

	char prefix = strongsNumber[0];
	const char* wantedFeature = strongs_feature_for(prefix);
	if (wantedFeature == NULL)
		return BString();

	const char* number = strongsNumber + 1;

	for (int32 i = 0; i < CountLexicons(); i++) {
		SGModule* lexicon = LexiconAt(i);
		if (!declares_feature(lexicon, wantedFeature))
			continue;

		BString entry(lexicon->GetEntry(number));
		if (entry.IsEmpty())
			continue;

		// A non-empty entry is NOT proof of a hit. Feature=GreekDef means
		// "this defines Greek words", not "this is keyed by Strong's
		// number" -- Dodson's Greek-English Lexicon declares it and is
		// keyed by the Greek lemma. Asking it for 2316 doesn't fail; SWORD
		// snaps to the nearest key and hands back a perfectly valid entry
		// for something else entirely (confirmed live: it landed on G0001
		// and returned the article for alpha, which is what the dictionary
		// window then showed for every Greek word clicked).
		//
		// So check where the lookup actually landed. A real Strong's
		// module reports 02316 for 2316; Dodson reports G0001.
		// getKeyText() on the module itself, NOT SGModule::GetKey(): that
		// one casts the key to VerseKey, which a lexicon's key is not.
		if (landed_on_number(lexicon->GetModule()->getKeyText(), number))
			return entry;
	}

	return BString();
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
		// VerseKey::setBook() is 1-indexed (valid range [1, getBookMax()]);
		// setBook(0) is out of range and silently clamps to book 1 instead
		// of failing, so starting this loop at 0 didn't skip a "book 0" --
		// it just asked for book 1 (Genesis/Matthew) twice, duplicating
		// each testament's first entry in the book menu (confirmed live:
		// the Book dropdown listed "1. Mose" twice before "2. Mose").
		for (j = 1; j<=myKey.getBookMax(); j++)
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


// Shared by ParseVerseReference() and ConvertVerseReference() below --
// both ultimately hand their input to VerseKey::setText(), which only
// understands ':' as a chapter:verse separator and, worse, silently
// accepts a bare book-less number/comma-list instead of rejecting it
// (confirmed empirically: "1,1 - 1,6" with no book name at all parses
// as whatever book VerseKey defaults to, not an error) -- ConvertVerseReference()
// went straight to setText() with neither guard for a long time, which
// is why a typed "Johannes 3, 16" silently became "Johannes 3:1" (the
// comma read as SWORD's own list separator, "16" discarded) instead of
// failing loudly or working correctly. Trims and lowercases neither --
// only the two things VerseKey itself gets wrong on its own.
static bool
NormalizeReferenceText(const char* input, BString& trimmed)
{
	trimmed = input;
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
	// "Joh 3, 16" (space after the comma, common when typing it out)
	// would otherwise leave a space between the ':' and the verse
	// digits -- confirmed empirically that ExtractTrailingChapterVerse()
	// then fails to recognize the ':' at all and misreads the verse
	// number as a second chapter number instead, rejecting the whole
	// reference as invalid.
	trimmed.ReplaceAll(": ", ":");
	return true;
}


bool ParseVerseReference(const char* input, BString& normalizedKey)
{
	BString trimmed;
	if (!NormalizeReferenceText(input, trimmed))
		return false;

	// A verse range's end ("Epheser 6:4-5") has to be stripped before
	// ExtractTrailingChapterVerse() runs, not passed through -- its
	// trailing-digit scan has no notion of a range and reads the "-5"
	// as a second chapter number instead of part of the verse, rejecting
	// an otherwise valid reference as a mismatch. VerseKey itself has no
	// use for it either (confirmed empirically -- a plain VerseKey's
	// setText() parses only the range's start, "6:4", and silently drops
	// "-5" on its own), so nothing downstream needs it kept.
	BString withoutRangeEnd(trimmed);
	int32 dash = withoutRangeEnd.FindLast('-');
	if (dash >= 0) {
		bool isRangeEnd = dash + 1 < withoutRangeEnd.Length();
		for (int32 i = dash + 1; i < withoutRangeEnd.Length() && isRangeEnd;
				i++) {
			if (!isdigit((unsigned char)withoutRangeEnd[i]))
				isRangeEnd = false;
		}
		if (isRangeEnd)
			withoutRangeEnd.Truncate(dash);
	}

	int expectedChapter = 0;
	int expectedVerse = 0;
	bool hasVerse = false;
	bool hasChapterVerse = ExtractTrailingChapterVerse(withoutRangeEnd,
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


bool
ConvertVerseReference(const char* key, const char* sourceLocale,
	const char* sourceVersification, const char* targetLocale,
	const char* targetVersification, BString& outText)
{
	// NormalizeReferenceText() (see its own comment) -- this used to
	// hand `key` to VerseKey::setText() completely as-is, which is
	// exactly right for an already-canonical key (e.g. a drag source's
	// own "key" field, or a stored bookmark's own Reference()) but
	// silently mangled a user-typed one with a German comma separator
	// or no book name at all. Every caller's input already satisfies
	// this normalization trivially (no comma to replace, already has a
	// book name) except the one that didn't.
	BString normalized;
	if (!NormalizeReferenceText(key, normalized))
		return false;

	VerseKey source;
	if (sourceLocale != NULL && sourceLocale[0] != '\0')
		source.setLocale(sourceLocale);
	source.setVersificationSystem(sourceVersification);
	source.setText(normalized.String());
	if (source.popError() != 0)
		return false;

	VerseKey target;
	if (targetLocale != NULL && targetLocale[0] != '\0')
		target.setLocale(targetLocale);
	target.setVersificationSystem(targetVersification);
	target.positionFrom(source);

	outText = target.getText();
	return true;
}


void
SplitVerseRangeSuffix(const BString& reference, BString& base, BString& suffix)
{
	base = reference;
	suffix = "";

	int32 dash = reference.FindLast('-');
	if (dash < 0)
		return;
	bool isRangeEnd = dash + 1 < reference.Length();
	for (int32 i = dash + 1; i < reference.Length() && isRangeEnd; i++) {
		if (!isdigit((unsigned char)reference.ByteAt(i)))
			isRangeEnd = false;
	}
	if (!isRangeEnd)
		return;

	reference.CopyInto(suffix, dash, reference.Length() - dash);
	base.Truncate(dash);
}


std::vector<TextReference>
FindReferencesInText(const char* text)
{
	std::vector<TextReference> result;
	if (text == NULL || *text == '\0')
		return result;

	// A candidate: an optional "1 "/"2 "/"3 " numbered-book prefix, a
	// capitalized book-ish word (with an optional trailing abbreviation
	// period), then chapter/verse digits separated by ':' or ',' (see
	// ParseVerseReference()'s own comment on the German comma
	// convention), with an optional "-verseEnd" range. Deliberately
	// liberal -- ParseVerseReference() below is the actual filter; this
	// only needs to be cheap and not miss real references, not be
	// precise on its own.
	//
	// ASCII letters only ([A-Za-z], not e.g. German "ö"/"ü"): std::regex
	// matches individual bytes, not UTF-8 codepoints, so a multi-byte
	// accented character in a character class here would risk matching
	// half of one and corrupting the scan. Standard SWORD locale files
	// already register ASCII-safe alternate abbreviations for accented
	// book names for exactly this kind of typing/matching convenience
	// (confirmed in this file's own German-locale reference work
	// earlier), and the one commentary this was actually tested against
	// (GerKingComments) uses only ASCII abbreviations ("Mt", "Off") in
	// practice -- so this is a real but narrow gap, not a blocker.
	static const std::regex kReferencePattern(
		"([1-3][ ]|)"
		"[A-Z][A-Za-z]*\\.?"
		"[ \t]+[0-9]{1,3}[,:][ \t]?[0-9]{1,3}(-[0-9]{1,3}|)");

	BString source(text);
	const char* str = source.String();
	std::cregex_iterator it(str, str + source.Length(), kReferencePattern);
	std::cregex_iterator end;
	for (; it != end; ++it) {
		const std::cmatch& match = *it;
		BString candidate(match.str().c_str());

		BString normalizedKey;
		if (!ParseVerseReference(candidate.String(), normalizedKey))
			continue;

		TextReference reference;
		reference.start = (int32)match.position(0);
		reference.length = (int32)match.length(0);
		reference.normalizedKey = normalizedKey;
		result.push_back(reference);
	}

	return result;
}


std::vector<StrongsWord>
FindStrongsWordsInText(SWModule* module, const BString& renderedText)
{
	std::vector<StrongsWord> result;
	if (module == NULL)
		return result;

	AttributeTypeList& attrs = module->getEntryAttributes();
	AttributeTypeList::iterator wordType = attrs.find("Word");
	if (wordType == attrs.end())
		return result;

	int32 searchCursor = 0;
	for (AttributeList::iterator it = wordType->second.begin();
			it != wordType->second.end(); ++it) {
		AttributeValue& value = it->second;

		AttributeValue::iterator classIt = value.find("LemmaClass");
		if (classIt == value.end() || classIt->second != "strong")
			continue;

		AttributeValue::iterator lemmaIt = value.find("Lemma");
		AttributeValue::iterator textIt = value.find("Text");
		if (lemmaIt == value.end() || textIt == value.end())
			continue;

		BString lemma(lemmaIt->second.c_str());
		int32 spacePos = lemma.FindFirst(' ');
		if (spacePos >= 0)
			lemma.Truncate(spacePos);
		if (lemma.IsEmpty())
			continue;

		BString wordText(textIt->second.c_str());
		if (wordText.IsEmpty())
			continue;

		int32 foundAt = renderedText.FindFirst(wordText, searchCursor);
		if (foundAt < 0)
			continue;

		StrongsWord word;
		word.start = foundAt;
		word.length = wordText.Length();
		word.strongsNumber = lemma;
		result.push_back(word);

		searchCursor = foundAt + wordText.Length();
	}

	return result;
}

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


vector<const char*> SGModule::SearchEntries(const char* searchText)
{
	vector<const char*> results;

	ListKey &listkey = fModule->search(searchText, -2 /* multiword */,
		REG_ICASE);
	listkey.setPersist(true);
	fModule->setKey(listkey);

	for (listkey = TOP; !listkey.popError(); listkey++)
		results.push_back((const char*) listkey);

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


BString SwordBackend::LookupStrongsNumber(const char* strongsNumber) const
{
	if (strongsNumber == NULL || *strongsNumber == '\0')
		return BString();

	char prefix = strongsNumber[0];
	if (prefix != 'G' && prefix != 'H')
		return BString();

	const char* wantedFeature = (prefix == 'G') ? "GreekDef" : "HebrewDef";
	const char* number = strongsNumber + 1;

	for (int32 i = 0; i < CountLexicons(); i++) {
		SGModule* lexicon = LexiconAt(i);
		if (lexicon == NULL)
			continue;

		const ConfigEntMap& conf = lexicon->GetModule()->getConfig();
		std::pair<ConfigEntMap::const_iterator, ConfigEntMap::const_iterator>
			range = conf.equal_range("Feature");
		bool matches = false;
		for (ConfigEntMap::const_iterator it = range.first;
				it != range.second && !matches; ++it) {
			if (it->second == wantedFeature)
				matches = true;
		}
		if (!matches)
			continue;

		BString entry(lexicon->GetEntry(number));
		if (!entry.IsEmpty())
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
	// "Joh 3, 16" (space after the comma, common when typing it out)
	// would otherwise leave a space between the ':' and the verse
	// digits -- confirmed empirically that ExtractTrailingChapterVerse()
	// then fails to recognize the ':' at all and misreads the verse
	// number as a second chapter number instead, rejecting the whole
	// reference as invalid.
	trimmed.ReplaceAll(": ", ":");

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

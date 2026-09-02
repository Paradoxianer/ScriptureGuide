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

// Parses `key` (a single, unhyphenated verse reference -- a hyphenated
// range's end has to be split off first, see SplitVerseRangeSuffix())
// as `sourceLocale`/`sourceVersification`, repositions it into
// `targetVersification`, and writes the result rendered in
// `targetLocale` into `outText`. Either locale may be empty, which
// leaves VerseKey's own locale unset -- its default, which always
// recognizes/produces English/ASCII book names regardless of what
// locale (if any) is otherwise active (confirmed empirically). False
// (leaving `outText` untouched) if `key` doesn't parse at all. Shared
// by BookmarkFile (#55, converting a bookmark's own stored reference
// back into a navigable key) and SGVerseListWindow (building a fresh
// bookmark's own stored content from a dropped reference).
bool			ConvertVerseReference(const char* key,
					const char* sourceLocale,
					const char* sourceVersification,
					const char* targetLocale,
					const char* targetVersification, BString& outText);

// Reformats an already-canonical reference (VerseKey::getText()'s own
// ':'-separated output, e.g. "John 3:16" or a range "John 3:16-18",
// possibly two such joined by " - ") for on-screen DISPLAY in
// `locale`'s own convention -- German uses a comma instead
// ("Johannes 3, 16"), matching the one place in this app that already
// built a German-comma reference by hand
// (BibleColumnView::_ReferenceFor()'s drag tooltip, ParallelBibleView.cpp
// -- now itself just a caller of this). Every ':' in a canonical
// reference is a chapter:verse separator (a book name never contains
// one), so replacing all of them is safe and never touches a '-'
// range suffix. Purely a display reformatting -- what actually gets
// STORED and round-tripped through VerseKey::setText() (BookmarkFile::
// Reference(), a drag's own "key"/"endKey") stays colon-only,
// unconditionally, regardless of locale; conflating the two is exactly
// the bug _ReferenceFor()'s own long-standing comment warns about.
BString			FormatVerseReferenceForDisplay(const BString& reference,
					const char* locale);

// Splits a stored range reference ("John 3:12-16") into `base`
// ("John 3:12") and `suffix` ("-16", including the leading '-'), or
// leaves `base` as the whole of `reference` and `suffix` empty if it
// isn't a range at all. ConvertVerseReference() (and VerseKey::setText()
// generally) only understands a single, unhyphenated reference -- see
// BibleColumnView::_StartDrag()'s own comment (ParallelBibleView.cpp)
// on why a drag never hands over a combined "start-end" string for
// something else to re-split, which is exactly the situation this
// exists to handle on the read side.
void			SplitVerseRangeSuffix(const BString& reference,
					BString& base, BString& suffix);

// The write-side counterpart: combines two already-canonical
// VerseKey::getText() results ("<Book> <Chapter>:<Verse>") into one
// compact range ("<Book> <Chapter>:<Start>-<End>") when they share the
// same book and chapter -- the one shape SplitVerseRangeSuffix() above
// (and VerseKey::setText() generally) can read back, rather than
// silently keeping only the start. Falls back to
// "<startText> - <endText>" (both kept in full) when the book/chapter
// differ -- a versification-driven shift across a boundary, or the two
// texts just aren't actually a range of each other. Does no reference
// parsing of its own; `startText`/`endText` must already be in
// canonical form. Shared by _AppendDroppedReferences() (a reading-pane
// multi-verse selection drag) and #50's typed-reference range support
// (LogosVerseListWindow.cpp) -- same shape, two different sources for
// the pair of endpoints.
BString			CombineVerseRange(const BString& startText,
					const BString& endText);

// #50: parses `text` as either a single reference or a typed RANGE,
// both forms -- "Johannes 3,5 - Johannes 3,7" (a full reference on
// each side of the dash) and the compact "Johannes 1,1-5" shorthand (a
// bare trailing verse number, borrowing the start's own book/chapter,
// the same shape SplitVerseRangeSuffix() reads on the other end). Only
// ConvertVerseReference() itself (single-reference success, silently
// keeping just the start of anything with a "-..." tail) is NOT enough
// to catch either shape -- this tries the range split FIRST, and only
// falls back to a plain ConvertVerseReference() call on the whole
// string when that split doesn't produce two real, distinct endpoints.
bool			ConvertTypedVerseReference(const char* text,
					const char* locale, const char* versification,
					BString& outText);

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

// One word tagged with a Strong's number -- start/length are a byte
// range into the ALREADY fully-rendered verse text (the same string
// BibleTextDocument builds its TextSpans from), not some intermediate
// tag-laden form. See #27, FindStrongsWordsInText().
struct StrongsWord {
	int32	start;
	int32	length;
	BString	strongsNumber;	// e.g. "G3056" or "H430"
};

// A Strong's-capable module's <w lemma="strong:G1063" ...>word</w>
// markup never survives to renderText()'s actual output as inline text
// (confirmed empirically both ways: it's completely absent from this
// app's own rendering, which always appends an extra GBFPlain filter
// for good reason -- see the comment where that's added -- and, more
// fundamentally, even withOUT that extra filter, using the tag itself
// as the source of truth is fragile), so this uses SWORD's own
// structured side-channel instead: `module`'s getEntryAttributes()
// (populated as a side effect of the render filter chain processing
// the raw markup, regardless of what any later filter does to the
// visible text) reports each word's Lemma/Text under the "Word"
// attribute type, in reading order. Each Word entry's Text is located
// in `renderedText` by sequential search starting where the previous
// one left off -- correct even when several words share identical text
// ("the", "and", ...), since a forward-only cursor naturally lands on
// each successive real occurrence rather than always the first one.
// Only entries whose LemmaClass is "strong" are considered (a module
// could in principle tag other kinds of lemmas); a Lemma with more than
// one space-separated token (SWORD merges several English words
// sharing one Greek/Hebrew word into a single tag) only keeps the
// first.
// Trims, rejects a reference that names no book at all, and turns the
// German comma separator into the colon VerseKey actually understands --
// without it "1. Mose 1, 8" parses as chapter 1 with the "8" discarded
// as a list element, i.e. silently as verse 1. Anything that hands a
// user-facing reference to VerseKey::setText() needs this first.
bool NormalizeReferenceText(const char* input, BString& normalized);

std::vector<StrongsWord> FindStrongsWordsInText(sword::SWModule* module,
					const BString& renderedText);


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

	// For Lexicon/Dictionary-type modules (see #31): sets the module's
	// own raw string key directly (no VerseKey involved -- these aren't
	// keyed by book/chapter/verse) and renders that entry.
	const char*			GetEntry(const char* key);

	void				SetVerse(const char* book, int chapter, int verse);
	
	// Returns owned BStrings, not raw const char*: SWORD's ListKey/
	// VerseKey text conversion ((const char*)listkey) returns a pointer
	// into a small, rotating pool of internal buffers shared by every
	// hit -- confirmed empirically (SG_LOG at both push time and after
	// the loop showed correct text at push time but shifted/repeated/
	// garbled text once the pool wrapped around and later hits reused
	// an earlier hit's buffer). Copying into a BString immediately,
	// inside the loop, is the fix; a vector<const char*> returned from
	// this function can never be safe to read after the loop that filled
	// it has finished.
	std::vector<BString>	SearchModule(int searchType, int flags,
								const char* searchText, const char* scopeFrom,
								const char* scopeTo, BStatusBar* statusBar);

	// Same underlying SWModule::search(), but with no VerseKey scope at
	// all (unlike SearchModule() above, built for Bible/Commentary book
	// ranges) -- a Lexicon/Dictionary module (see #31) has no book/
	// chapter concept to scope by, and SWModule::search()'s `scope`
	// parameter already defaults to "search the whole module" when
	// omitted. Multiword, case-insensitive; returns each matching
	// entry's own raw key (suitable for GetEntry()), not the matched
	// text itself. Owned BStrings, same reasoning as SearchModule()
	// above -- this had the identical buffer-reuse bug.
	std::vector<BString>	SearchEntries(const char* searchText);

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

	// Every installed Bible/Commentary module's name (the same two
	// categories _PopulateModuleMenu() offers as real, key-searchable
	// text -- Lexicons/GeneralTexts have no book/chapter/verse concept
	// to search by). What the search window's "Search in" field should
	// offer: every module the user could search, not just whichever
	// ones happen to be open as reading-pane columns right now.
	std::vector<BString>	SearchableModuleNames(void) const;

	SGModule*			FindModule(const char* name);
	status_t			SetModule(SGModule* mod);
 	SGModule*			CurrentModule(void);

	// Looks up a Strong's number (e.g. "G3056" or "H430", the exact form
	// SWModule::getEntryAttributes()'s "Word" attributes report -- see
	// #27) in whichever installed Lexicon/Dictionary module declares the
	// standard SWORD Feature=GreekDef (for a "G..." number) or
	// Feature=HebrewDef (for "H...") config entry -- the same feature
	// tag CrossWire's own StrongsGreek/StrongsHebrew modules use, so
	// this works for whichever compatible dictionary happens to be
	// installed rather than hardcoding a specific module name. Empty if
	// no matching dictionary is installed, or the number isn't found in
	// it. The "G"/"H" prefix itself is stripped before the lookup --
	// confirmed empirically that keeping it silently mismatches to a
	// nearby, unrelated entry instead of failing outright.
	BString				LookupStrongsNumber(const char* strongsNumber) const;

	// Whether a dictionary that could resolve numbers of this kind is
	// installed at all -- 'G' for Greek (New Testament), 'H' for Hebrew
	// (Old Testament). Answers the question BEFORE a lookup, so a caller
	// can avoid offering a link that provably cannot lead anywhere:
	// rendering every Strong's-tagged word as clickable regardless makes
	// the affordance lie, and on a system with only one of the two
	// dictionaries installed that is a whole testament of dead ends.
	bool				HasStrongsDictionary(char prefix) const;
	// The conventional CrossWire module name a user would need to install
	// to make HasStrongsDictionary(prefix) true -- for naming the actual
	// missing piece in a message instead of "no matching dictionary".
	static const char*	StrongsDictionaryNameFor(char prefix);

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


// Same question as SwordBackend::HasStrongsDictionary(), for callers that
// hold only a bare SWMgr (see ParallelBibleView, which needs to know
// whether a Strong's number is worth rendering as a link before it builds
// the document). The member function delegates here, so both answers can
// never drift apart.
bool HasStrongsDictionary(sword::SWMgr* manager, char prefix);

#endif


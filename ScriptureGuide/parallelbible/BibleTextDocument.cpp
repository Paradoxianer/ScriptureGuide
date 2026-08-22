/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "BibleTextDocument.h"

#include <algorithm>
#include <cstdio>

#include <Language.h>
#include <Locale.h>
#include <OS.h>
#include <String.h>

#include <Catalog.h>

#include <string.h>

#include "constants.h"
#include "SGDebug.h"
#include "SwordBackend.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BibleTextDocument"


// Mirrors SGModule::IsGreek()/IsHebrew() (SwordBackend.cpp) -- duplicated
// rather than shared because this class works directly on the sword::
// SWModule layer and has no dependency on the app's SGModule wrapper.
static bool
IsGreekModule(SWModule* module)
{
	return strcmp(module->getLanguage(), "grc") == 0
		|| strcmp(module->getLanguage(), "el") == 0;
}


static bool
IsHebrewModule(SWModule* module)
{
	return strcmp(module->getLanguage(), "he") == 0;
}


// VerseKey::setText()/setBookName() only recognize localized book names
// (e.g. German "1. Mose" for Genesis) if the key's locale has been set
// first -- otherwise they fail silently and the key is left unchanged.
// fCurrentKey (see ParallelBibleView) ultimately comes from the main
// window's book menu, which is localized, so every VerseKey built from
// caller-supplied key text needs this before setText()/setBookName().
static void
SetVerseKeyLocale(VerseKey& key)
{
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	key.setLocale(language.Code());
}

// Prepares a key to speak the module's language AND to count in its
// versification. A default-constructed VerseKey is always KJV, and
// without the second half a chapter was measured with KJV's verse
// numbers whatever the module actually used: German counting gives
// Malachi 3 twenty-four verses against KJV's eighteen, and the last six
// were simply never rendered, with nothing on screen to suggest anything
// was missing (#46). Lookup was never the problem -- SWModule::setKey()
// maps a key into the module's own system by itself -- only the counting
// and the arithmetic done on the key here.
void
BibleTextDocument::_PrepareKey(VerseKey& key) const
{
	// An override wins: a notes document has to count the way the Bible
	// column beside it does, not the way its own module does.
	const char* versification = !fVersification.IsEmpty()
		? fVersification.String()
		: (fModule != NULL ? fModule->getConfigEntry("Versification") : NULL);
	// A module that declares none counts in KJV, which is what a fresh
	// key already does; saying so explicitly keeps the answer in one
	// place rather than resting on that default.
	key.setVersificationSystem(versification != NULL
		&& versification[0] != '\0' ? versification : "KJV");
	SetVerseKeyLocale(key);
}


void
BibleTextDocument::SetVersification(const char* versification)
{
	BString wanted(versification != NULL ? versification : "");
	if (wanted == fVersification)
		return;
	fVersification = wanted;
	// The verse count and every key built from here on change with it,
	// so what is already laid out is stale.
	_Rebuild();
}



BibleTextDocument::BibleTextDocument(SWModule* module, int singleVerse)
	:
	TextDocument(),
	fModule(module),
	fShowVerseNumbers(true),
	fSkipEmptyVerses(true),
	fShowStrongsNumbers(true),
	fShowCrossReferences(true),
	fSingleVerse(singleVerse),
	fParagraphsEndWithNewline(false),
	fResolvableStrongsGreek(true),
	fResolvableStrongsHebrew(true)
{
	fVerseNumberStyle.SetBold(true);
	fParagraphStyle.SetJustify(true);

	// Distinguishes a detected cross-reference (see #28, ReferenceLinkAt())
	// from ordinary verse text at a glance, the same way underlined blue
	// text reads as "clickable" everywhere else.
	rgb_color linkColor = { 0, 0, 200, 255 };
	fReferenceLinkStyle.SetForegroundColor(linkColor);
	fReferenceLinkStyle.SetUnderline(1);

	// Deliberately no color change here, unlike fReferenceLinkStyle --
	// a colored underline under nearly every word of a fully Strong's-
	// tagged translation (which is most of them) made the text hard to
	// read at a glance (reported). Just a plain underline in the text's
	// own color, plus the hover cursor MouseMoved() sets (see
	// BibleColumnView), is enough to read as "clickable" without
	// fighting the prose for attention.
	fStrongsNumberStyle.SetUnderline(1);

	// Seeded from the module's key AT THIS MOMENT -- the caller (see
	// ParallelBibleView::_SetColumnToBible()) already set it to whatever
	// this new document should open showing, immediately before
	// constructing it, so this is still reliably "mine" here even though
	// fModule may be shared with other documents from here on (see the
	// class comment).
	if (fModule != NULL)
		fKeyText = fModule->getKeyText();

	_Rebuild();
}


BibleTextDocument::~BibleTextDocument()
{
}


// fKeyText, not fModule->getKeyText() -- see the class comment. Another
// BibleTextDocument sharing fModule may have changed its live key since
// this one last touched it; fKeyText is the only value that's reliably
// still this document's own.
const char*
BibleTextDocument::Key() const
{
	if (fModule == NULL)
		return NULL;
	return fKeyText.String();
}


const char*
BibleTextDocument::BookName() const
{
	if (fModule == NULL)
		return NULL;
	_PrepareKey(fDisplayKey);
	fDisplayKey.setText(fKeyText.String());
	return fDisplayKey.getBookName();
}


int
BibleTextDocument::Chapter() const
{
	if (fModule == NULL)
		return 0;
	_PrepareKey(fDisplayKey);
	fDisplayKey.setText(fKeyText.String());
	return fDisplayKey.getChapter();
}


int
BibleTextDocument::Verse() const
{
	if (fModule == NULL)
		return 0;
	_PrepareKey(fDisplayKey);
	fDisplayKey.setText(fKeyText.String());
	return fDisplayKey.getVerse();
}


// fModule->setKey() does not carry over its argument's locale -- verified
// empirically, it always resets the module's own persistent key back to
// the default ("en") locale no matter what the passed-in key had set.
// Every setter below therefore needs to reapply it afterward, or
// BookName()/getKeyText() silently revert to English right after any
// navigation call. Also captures the result into fKeyText -- this
// document's own record of its position, independent of fModule's live
// (and, once another document sharing it runs, potentially stale-for-us)
// key state -- see the class comment.
void
BibleTextDocument::_SetModuleKey(VerseKey& verseKey)
{
	fModule->setKey(verseKey);
	// Locale only: this is the module's OWN key, which already counts in
	// the module's versification by definition, and re-setting the system
	// on a live key can move it.
	SetVerseKeyLocale(*(VerseKey*)fModule->getKey());
	fKeyText = fModule->getKeyText();
}


status_t
BibleTextDocument::SetKey(const char* key)
{
	if (fModule == NULL)
		return B_NO_INIT;

	// Seeded from THIS document's own current position (fKeyText, not
	// fModule->getKeyText() -- see the class comment) via an explicit
	// setText() call, not the VerseKey(const char*) constructor -- once
	// _SetModuleKey() keeps fKeyText permanently localized, parsing it
	// through a fresh, still-default-locale key (what the constructor
	// form does) silently mis-parses it -- confirmed empirically,
	// "Johannes 3:16" with no locale set resolves to "Revelation of
	// John" 1:1 instead. Locale has to be set before either parse, not
	// just the second one.
	VerseKey verseKey;
	_PrepareKey(verseKey);
	verseKey.setText(fKeyText.String());
	verseKey.setText(key);
	// Deliberately not forcing verse 1 here, unlike SetChapter()/Next/
	// PrevChapter() (which always want the chapter's first verse):
	// _Rebuild() below always renders the whole chapter regardless of
	// which verse `key` names, so preserving it only affects Verse()
	// afterward -- which is exactly what ParallelBibleView::SetKey() uses
	// to scroll straight to the requested verse.
	_SetModuleKey(verseKey);

	_Rebuild();
	return B_OK;
}


status_t
BibleTextDocument::SetChapter(const char* book, int chapter)
{
	if (fModule == NULL)
		return B_NO_INIT;

	VerseKey verseKey;
	_PrepareKey(verseKey);
	verseKey.setText(fKeyText.String());
	if (book != NULL)
		verseKey.setBookName(book);
	verseKey.setChapter(chapter);
	verseKey.setVerse(1);
	_SetModuleKey(verseKey);

	_Rebuild();
	return B_OK;
}


status_t
BibleTextDocument::NextChapter()
{
	if (fModule == NULL)
		return B_NO_INIT;

	VerseKey verseKey;
	_PrepareKey(verseKey);
	verseKey.setText(fKeyText.String());
	verseKey.setChapter(verseKey.getChapter() + 1);
	verseKey.setVerse(1);
	_SetModuleKey(verseKey);

	_Rebuild();
	return B_OK;
}


status_t
BibleTextDocument::PrevChapter()
{
	if (fModule == NULL)
		return B_NO_INIT;

	VerseKey verseKey;
	_PrepareKey(verseKey);
	verseKey.setText(fKeyText.String());
	verseKey.setChapter(verseKey.getChapter() - 1);
	verseKey.setVerse(1);
	_SetModuleKey(verseKey);

	_Rebuild();
	return B_OK;
}


void
BibleTextDocument::SetShowVerseNumbers(bool show)
{
	if (fShowVerseNumbers == show)
		return;

	fShowVerseNumbers = show;
	_Rebuild();
}


void
BibleTextDocument::SetShowStrongsNumbers(bool show)
{
	if (fShowStrongsNumbers == show)
		return;

	fShowStrongsNumbers = show;
	_Rebuild();
}


void
BibleTextDocument::SetShowCrossReferences(bool show)
{
	if (fShowCrossReferences == show)
		return;

	fShowCrossReferences = show;
	_Rebuild();
}


void
BibleTextDocument::SetBaseFont(const BFont& font)
{
	BFont effective = _EffectiveFont(font);

	fVerseTextStyle.SetFont(effective);

	// SetFont() replaces the style's whole BFont, including its face, so
	// the bold weight set in the constructor has to be reapplied after it.
	fVerseNumberStyle.SetFont(effective);
	fVerseNumberStyle.SetBold(true);

	// Same reapply-after-SetFont() rationale as fVerseNumberStyle above,
	// for the underline/color set in the constructor.
	rgb_color linkColor = { 0, 0, 200, 255 };
	fReferenceLinkStyle.SetFont(effective);
	fReferenceLinkStyle.SetForegroundColor(linkColor);
	fReferenceLinkStyle.SetUnderline(1);

	fStrongsNumberStyle.SetFont(effective);
	fStrongsNumberStyle.SetUnderline(1);

	_Rebuild();
}


// Greek/Hebrew modules (fModule->getLanguage(), see IsGreekModule()/
// IsHebrewModule() above) get a family suited to those scripts instead of
// whatever the user picked for the rest of the reading pane -- baseFont's
// size carries over either way. Falls back to baseFont unchanged if that
// family isn't actually installed (SetFamilyAndFace() failing is the
// expected case on a system without it, not a bug -- see #21).
BFont
BibleTextDocument::_EffectiveFont(const BFont& baseFont) const
{
	const char* family = NULL;
	if (IsGreekModule(fModule))
		family = GREEK;
	else if (IsHebrewModule(fModule))
		family = HEBREW;

	if (family == NULL)
		return baseFont;

	font_family fontFamily;
	strlcpy(fontFamily, family, sizeof(font_family));

	BFont effective(baseFont);
	if (effective.SetFamilyAndFace(fontFamily, B_REGULAR_FACE) != B_OK)
		return baseFont;

	return effective;
}


void
BibleTextDocument::SetSkipEmptyVerses(bool skip)
{
	if (fSkipEmptyVerses == skip)
		return;

	fSkipEmptyVerses = skip;
	_Rebuild();
}


void
BibleTextDocument::SetSingleVerse(int verse)
{
	if (fSingleVerse == verse)
		return;

	fSingleVerse = verse;
	_Rebuild();
}


void
BibleTextDocument::SetResolvableStrongsPrefixes(bool greek, bool hebrew)
{
	if (fResolvableStrongsGreek == greek
		&& fResolvableStrongsHebrew == hebrew) {
		return;
	}

	fResolvableStrongsGreek = greek;
	fResolvableStrongsHebrew = hebrew;
	_Rebuild();
}


void
BibleTextDocument::SetParagraphsEndWithNewline(bool enabled)
{
	if (fParagraphsEndWithNewline == enabled)
		return;

	fParagraphsEndWithNewline = enabled;
	_Rebuild();
}


int32
BibleTextDocument::ParagraphIndexForVerse(int verse) const
{
	for (size_t i = 0; i < fParagraphVerse.size(); i++) {
		if (fParagraphVerse[i] == verse)
			return (int32)i;
	}
	return -1;
}


int
BibleTextDocument::VerseForParagraphIndex(int32 index) const
{
	if (index < 0 || (size_t)index >= fParagraphVerse.size())
		return -1;
	return fParagraphVerse[index];
}


bool
BibleTextDocument::IsLinkedToPrevious(int verse) const
{
	std::map<int, bool>::const_iterator it = fLinkedToPrevious.find(verse);
	if (it == fLinkedToPrevious.end())
		return false;
	return it->second;
}


bool
BibleTextDocument::TextRangeForVerseRange(int startVerse, int endVerse,
	int32& start, int32& end) const
{
	int32 startParagraph = ParagraphIndexForVerse(startVerse);
	int32 endParagraph = ParagraphIndexForVerse(endVerse);
	if (startParagraph < 0 || endParagraph < 0)
		return false;

	// Mirrors how TextDocument::ParagraphIndexFor() itself accumulates
	// paragraphOffset from Paragraph::Length() -- there's no public
	// "offset of paragraph N" lookup to reuse directly.
	int32 offset = 0;
	for (int32 i = 0; i < startParagraph; i++)
		offset += ParagraphAtIndex(i).Length();
	start = offset;

	for (int32 i = startParagraph; i <= endParagraph; i++)
		offset += ParagraphAtIndex(i).Length();
	end = offset;

	return true;
}


bool
BibleTextDocument::ReferenceLinkAt(int32 documentOffset, BString& outKey) const
{
	for (size_t i = 0; i < fReferenceLinks.size(); i++) {
		if (documentOffset >= fReferenceLinks[i].start
			&& documentOffset < fReferenceLinks[i].end) {
			outKey = fReferenceLinks[i].key;
			return true;
		}
	}
	return false;
}


bool
BibleTextDocument::StrongsNumberAt(int32 documentOffset,
	BString& outNumber) const
{
	for (size_t i = 0; i < fStrongsLinks.size(); i++) {
		if (documentOffset >= fStrongsLinks[i].start
			&& documentOffset < fStrongsLinks[i].end) {
			outNumber = fStrongsLinks[i].number;
			return true;
		}
	}
	return false;
}


void
BibleTextDocument::SetVerseSpacing(const std::map<int, float>& spacing)
{
	fVerseSpacingBottom = spacing;
	_ApplyVerseSpacing();
}


// Restyles every existing paragraph's spacing in place instead of calling
// _Rebuild() (see the header comment on SetVerseSpacing() for why this
// matters). fParagraphVerse[i] already maps paragraph index -> verse from
// the last real _Rebuild(); this only ever runs after at least one such
// rebuild has populated it, so no bounds/staleness check beyond
// CountParagraphs() itself is needed.
void
BibleTextDocument::_ApplyVerseSpacing()
{
	int32 count = CountParagraphs();
	for (int32 i = 0; i < count; i++) {
		float spacingBottom = 0.0f;
		if ((size_t)i < fParagraphVerse.size()) {
			std::map<int, float>::const_iterator found
				= fVerseSpacingBottom.find(fParagraphVerse[i]);
			if (found != fVerseSpacingBottom.end())
				spacingBottom = found->second;
		}

		ParagraphStyle style(ParagraphAtIndex(i).Style());
		style.SetSpacingBottom(spacingBottom);
		SetParagraphStyle(i, style);
	}
}


void
BibleTextDocument::_Rebuild()
{
	bigtime_t rebuildStart = system_time();

	// Remove(0, Length()) -- clearing by replacing the entire document
	// with empty text -- reliably leaves exactly one empty placeholder
	// paragraph behind at index 0 instead of reaching zero paragraphs
	// (the underlying TextDocument::Replace()/_Remove() guarantees "at
	// least one paragraph always exists", the same way an empty text
	// file conceptually still has one empty line). That placeholder is
	// harmless on its own, but our verse loop below always Append()s
	// rather than replacing it, so every verse's paragraph ends up one
	// index higher than fParagraphVerse (built alongside the very same
	// Append() calls) expects -- verse 1 silently maps to index 1, not
	// 0, and whatever the true index-0 paragraph is (the leftover
	// placeholder) gets misread as verse 1's content instead.
	//
	// Resetting the inherited TextDocument state directly sidesteps
	// Remove()'s placeholder guarantee entirely: a fresh, default-
	// constructed TextDocument's fParagraphs is a genuinely empty
	// vector, and TextDocument::operator=() is a plain field copy, not
	// the Insert/Remove machinery that leaves the placeholder behind.
	// BibleTextDocument's own members (fModule, fShowVerseNumbers, etc.)
	// are untouched since only the base-class subobject is reassigned.
	TextDocument::operator=(TextDocument());
	fParagraphVerse.clear();
	fLinkedToPrevious.clear();
	fReferenceLinks.clear();
	fStrongsLinks.clear();

	if (fModule == NULL) {
		Paragraph paragraph(fParagraphStyle);
		paragraph.Append(TextSpan(
			B_TRANSLATE("No module selected."), fVerseTextStyle));
		Append(paragraph);
		return;
	}

	// Build independent VerseKey objects from THIS document's own
	// fKeyText, not fModule->getKeyText() (see the class comment) --
	// another document sharing fModule may have pointed its live key at
	// a completely different book/chapter since this document's own
	// SetKey()/SetChapter() last ran, and _Rebuild() must render what
	// fKeyText says regardless (confirmed via a live test: two columns
	// on the same module in different, unlinked chains rendered
	// whichever chapter the OTHER one had most recently navigated to).
	// Passing the module's own live key object back into setKey()
	// directly, instead of building independent VerseKey objects, would
	// also crash deep inside SWORD (ListKey's copy constructor), since
	// setKey() replaces its internal key before cloning from what was
	// just passed in.
	//
	// Locale has to be set on savedKey/iterKey before parsing
	// savedKeyText, not left to the VerseKey(const char*) constructor --
	// _SetModuleKey() keeps fKeyText permanently localized, so
	// savedKeyText itself is already localized text (e.g. "1. Mose
	// 1:1") by the time this runs. Parsing that through a fresh,
	// default-locale key (what the constructor form does) silently
	// mis-parses it into "Revelation of John" 1:1 instead -- confirmed
	// empirically -- which fModule->setKey(savedKey) at the bottom of
	// this function then wrote back as the module's new position,
	// undoing whatever SetKey()/SetChapter() had just correctly set
	// right before calling _Rebuild().
	SG_LOG("[SG] BibleTextDocument::_Rebuild this=%p module=%p "
		"fKeyText=\"%s\" module->getKeyText()=\"%s\"\n", (void*)this,
		(void*)fModule, fKeyText.String(), fModule->getKeyText());
	BString savedKeyText(fKeyText);
	VerseKey savedKey;
	_PrepareKey(savedKey);
	savedKey.setText(savedKeyText.String());

	VerseKey iterKey;
	_PrepareKey(iterKey);
	iterKey.setText(savedKeyText.String());
	int verseCount = iterKey.getVerseMax();

	// fSingleVerse's own book/chapter still comes from fKeyText above --
	// only which verse(s) of THAT chapter get rendered narrows down, to
	// exactly one -- see the header comment on why.
	int firstVerse = 1;
	if (fSingleVerse > 0) {
		firstVerse = fSingleVerse;
		verseCount = fSingleVerse;
	}

	// Commentary modules commonly link one entry across a whole verse
	// range (e.g. a single comment discussing verses 13-20) rather than
	// storing separate text per verse; renderText() then returns the
	// SAME long text for every linked verse. Without this check each of
	// those verses became its own full-size paragraph, forcing
	// VerseAligner to inflate every other column's spacing once per
	// linked verse instead of just once overall -- the more verses an
	// entry spans, the more the whole chapter's height balloons.
	VerseKey previousKey;
	bool havePreviousEntry = false;

	// Running character offset into this TextDocument as a whole (the
	// same space TextDocumentView::TextOffsetAt() and
	// TextRangeForVerseRange() already operate in) -- built up here
	// rather than queried back from ParagraphAtIndex() after the fact,
	// since ReferenceLinkAt() (see #28) needs it anyway to translate a
	// reference match's offset *within one verse's text* into a
	// document-wide one.
	int32 documentOffset = 0;

	for (int verse = firstVerse; verse <= verseCount; verse++) {
		iterKey.setVerse(verse);
		fModule->setKey(iterKey);

		bool linkedToPrevious = havePreviousEntry
			&& fModule->isLinked(&iterKey, &previousKey);

		BString text;
		if (!linkedToPrevious)
			text = fModule->renderText();

		if (text.CountChars() < 1 && fSkipEmptyVerses && !linkedToPrevious)
			continue;

		// An editable document's own line breaks come back in as soft
		// ones, BEFORE the GBF cleanup below -- both so a user-typed
		// blank line isn't mistaken for a GBF paragraph marker and
		// swallowed by the RemoveAll("\x0a\x0a") on the next line, and so
		// the newline never reaches the paragraph builder, where it would
		// split this one verse's note across several paragraphs. See
		// SetParagraphsEndWithNewline() and NotesDisplayView::
		// _InsertSoftLineBreak() for the two halves of this translation.
		if (fParagraphsEndWithNewline)
			text.ReplaceAll('\n', '\v');

		// GBFPlain leaves paragraph markers behind; strip them so verses
		// don't carry stray blank lines or pilcrows into the layout.
		text.RemoveAll("\x0a\x0a");
		text.RemoveAll("\xc2\xb6 ");
		text.RemoveAll("<P> ");

		// Strong's numbers (#27): fModule->getEntryAttributes() reflects
		// whatever the most recent renderText() call above populated --
		// stale (and, since text is still empty here, harmless either
		// way) if linkedToPrevious skipped calling it this iteration.
		std::vector<StrongsWord> strongsWords;
		if (!linkedToPrevious && fShowStrongsNumbers) {
			strongsWords = FindStrongsWordsInText(fModule, text);

			// Drop the ones nothing installed could resolve, so they
			// render as ordinary text instead of as a link that cannot
			// lead anywhere -- see SetResolvableStrongsPrefixes().
			std::vector<StrongsWord> resolvable;
			for (size_t i = 0; i < strongsWords.size(); i++) {
				char prefix = strongsWords[i].strongsNumber.Length() > 0
					? strongsWords[i].strongsNumber.ByteAt(0) : '\0';
				bool keep = (prefix == 'G') ? fResolvableStrongsGreek
					: (prefix == 'H') ? fResolvableStrongsHebrew
					: true;
				if (keep)
					resolvable.push_back(strongsWords[i]);
			}
			strongsWords = resolvable;
		}

		ParagraphStyle style(fParagraphStyle);
		std::map<int, float>::const_iterator spacing
			= fVerseSpacingBottom.find(verse);
		if (spacing != fVerseSpacingBottom.end())
			style.SetSpacingBottom(spacing->second);

		Paragraph paragraph(style);
		int32 verseNumberLength = 0;
		if (fShowVerseNumbers) {
			BString number;
			number << " " << verse << " ";
			paragraph.Append(TextSpan(number, fVerseNumberStyle));
			verseNumberLength = number.Length();
		} else if (text.IsEmpty()) {
			// A paragraph whose only span is empty makes the whole
			// document's Length() undercount how many paragraphs actually
			// exist (Length() sums *text* length, not paragraph count),
			// which breaks Remove(0, Length())'s ability to clear them on
			// the next rebuild (see _Rebuild()'s CountParagraphs() check
			// above -- Remove() itself still no-ops on a zero length). A
			// single space keeps every paragraph's length nonzero and
			// still reads as an empty, clickable line to type a note into.
			text = " ";
		}

		// Cross-references (#28): a commentary citing "(Mt 16:18)" gets
		// that substring split into its own, distinctly-styled span
		// (see fReferenceLinkStyle) rather than the whole verse being one
		// plain span -- FindReferencesInText() has already validated it
		// against ParseVerseReference(), the same check a typed
		// reference goes through, so this is never more than the
		// occasional false negative (a real reference it missed), not a
		// false positive turned into a broken link.
		std::vector<TextReference> references;
		if (fShowCrossReferences)
			references = FindReferencesInText(text.String());

		// One merged, position-sorted list of every special span in this
		// verse's text -- Strong's-tagged words (#27) and cross-
		// references (#28) alike -- so both can be laid down in a
		// single left-to-right walk instead of two independent passes
		// that would have no way to agree on which one wins if they
		// ever overlapped (they shouldn't in practice: a Strong's tag
		// wraps a single word, a reference is a whole "(Book Ch:V)"
		// citation, not the same text).
		struct SpecialSpan {
			int32	start;
			int32	length;
			bool	isStrongs;
			BString	strongsNumber;
			BString	referenceKey;
		};
		std::vector<SpecialSpan> spans;
		for (size_t i = 0; i < strongsWords.size(); i++) {
			SpecialSpan span;
			span.start = strongsWords[i].start;
			span.length = strongsWords[i].length;
			span.isStrongs = true;
			span.strongsNumber = strongsWords[i].strongsNumber;
			spans.push_back(span);
		}
		for (size_t i = 0; i < references.size(); i++) {
			SpecialSpan span;
			span.start = references[i].start;
			span.length = references[i].length;
			span.isStrongs = false;
			span.referenceKey = references[i].normalizedKey;
			spans.push_back(span);
		}
		std::sort(spans.begin(), spans.end(),
			[](const SpecialSpan& a, const SpecialSpan& b) {
				return a.start < b.start;
			});

		if (spans.empty()) {
			paragraph.Append(TextSpan(text, fVerseTextStyle));
		} else {
			int32 cursor = 0;
			for (size_t i = 0; i < spans.size(); i++) {
				const SpecialSpan& span = spans[i];
				if (span.start < cursor)
					continue; // overlapping match -- keep the earlier one

				if (span.start > cursor) {
					BString before;
					text.CopyInto(before, cursor, span.start - cursor);
					paragraph.Append(TextSpan(before, fVerseTextStyle));
				}

				BString spanText;
				text.CopyInto(spanText, span.start, span.length);

				int32 linkStart = documentOffset + verseNumberLength
					+ span.start;
				if (span.isStrongs) {
					paragraph.Append(
						TextSpan(spanText, fStrongsNumberStyle));
					StrongsLink link;
					link.start = linkStart;
					link.end = linkStart + span.length;
					link.number = span.strongsNumber;
					fStrongsLinks.push_back(link);
				} else {
					paragraph.Append(
						TextSpan(spanText, fReferenceLinkStyle));
					ReferenceLink link;
					link.start = linkStart;
					link.end = linkStart + span.length;
					link.key = span.referenceKey;
					fReferenceLinks.push_back(link);
				}

				cursor = span.start + span.length;
			}
			if (cursor < text.Length()) {
				BString after;
				text.CopyInto(after, cursor, text.Length() - cursor);
				paragraph.Append(TextSpan(after, fVerseTextStyle));
			}
		}

		// Appended last, after every styled/linked span above, so none of
		// the offsets those recorded need to account for it -- see the
		// header comment on SetParagraphsEndWithNewline() for why an
		// editable document needs the paragraph terminator to physically
		// exist. paragraph.Length() below picks it up, so documentOffset
		// stays consistent with the document's own flat offsets.
		if (fParagraphsEndWithNewline)
			paragraph.Append(TextSpan("\n", fVerseTextStyle));

		Append(paragraph);
		fParagraphVerse.push_back(verse);
		fLinkedToPrevious[verse] = linkedToPrevious;
		documentOffset += paragraph.Length();

		if (!linkedToPrevious) {
			previousKey = iterKey;
			// VerseKey::operator=() does not carry over the source key's
			// versification system (confirmed empirically -- previousKey
			// silently reverts to the default, KJV, even when iterKey is
			// e.g. "Luther"), which then made isLinked() misidentify
			// merely ADJACENT verses (N and N-1) as linked whenever the
			// two keys being compared belonged to different canons --
			// reproduced live against GerMenge (Luther) and GerSch
			// (German): every even-numbered verse in 1 Corinthians 6 came
			// back "linked" to the odd one before it and rendered as an
			// empty stub, though renderText() called directly for that
			// same verse returns its own real, distinct text. Re-running
			// _PrepareKey() restores what the assignment dropped.
			_PrepareKey(previousKey);
			havePreviousEntry = true;
		}
	}

	_SetModuleKey(savedKey);

	SG_LOG("[SG-PERF] BibleTextDocument::_Rebuild this=%p "
		"verses=%d elapsed=%.2fms\n", (void*)this, verseCount,
		(system_time() - rebuildStart) / 1000.0);
}

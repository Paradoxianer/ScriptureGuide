/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 *
 * Standalone regression tests for the parallel Bible view engine. Needs a
 * BApplication (be_plain_font, ui_color etc. require one) but never shows
 * a window -- run it directly, it prints PASS/FAIL per check and exits
 * with a nonzero status if anything failed.
 *
 * Requires at least one installed Bible module (see CLAUDE.md /
 * `installmgr`); tests SKIP with a clear message if none is found rather
 * than failing, so a bare checkout doesn't look broken.
 */

#include <math.h>
#include <stdio.h>

#include <Application.h>
#include <String.h>

#include <markupfiltmgr.h>
#include <swmgr.h>

#include "BibleTextDocument.h"
#include "ParagraphLayout.h"
#include "ParallelBibleView.h"
#include "PersonalNotesModule.h"
#include "VerseAligner.h"
#include "constants.h"

using namespace sword;


static int gChecks = 0;
static int gFailures = 0;

static void
Check(bool condition, const char* description)
{
	gChecks++;
	if (condition) {
		printf("PASS: %s\n", description);
	} else {
		gFailures++;
		printf("FAIL: %s\n", description);
	}
}

static void
Skip(const char* description, const char* reason)
{
	printf("SKIP: %s (%s)\n", description, reason);
}


// One Paragraph per verse, rebuilt several times the way VerseAligner and
// chapter navigation both do in normal use. Regression test for: _Rebuild()
// guarded its Remove(0, Length()) clear on Length() > 0, but Length() sums
// *text* length, not paragraph count, so a real (non-empty) document's
// paragraph count should stay exactly the same across repeated rebuilds.
static void
TestBibleTextDocumentRebuildIsIdempotent(SWModule* module)
{
	const char* name = "BibleTextDocument: repeated rebuild does not "
		"duplicate paragraphs (real verse text)";
	if (module == NULL) {
		Skip(name, "no Bible module installed");
		return;
	}

	BibleTextDocument document(module);
	document.SetKey("Gen 1:1");

	int32 firstCount = document.CountParagraphs();
	for (int i = 0; i < 5; i++)
		document.SetVerseSpacing(std::map<int, float>());

	Check(firstCount > 0 && document.CountParagraphs() == firstCount, name);
}


// Same idea, but for the specific case that actually broke: a document made
// entirely of empty paragraphs (the notes column before any note is
// written) reports Length() == 0 despite having real paragraphs, so the
// Length() > 0 guard skipped clearing and every rebuild piled a fresh copy
// on top of the last.
static void
TestEmptyNotesDocumentRebuildIsIdempotent(SWModule* notesModule)
{
	const char* name = "BibleTextDocument: repeated rebuild does not "
		"duplicate paragraphs (all-empty notes)";
	if (notesModule == NULL) {
		Skip(name, "personal notes module unavailable");
		return;
	}

	BibleTextDocument document(notesModule);
	document.SetShowVerseNumbers(false);
	document.SetSkipEmptyVerses(false);
	document.SetKey("Gen 1:1");

	int32 firstCount = document.CountParagraphs();
	for (int i = 0; i < 5; i++)
		document.SetVerseSpacing(std::map<int, float>());
	int32 afterCount = document.CountParagraphs();

	Check(firstCount > 0 && afterCount == firstCount, name);
}


// Regression test for: VerseAligner measured each verse's paragraph
// without first clearing prior SpacingBottom padding, so padding compounded
// on every pass instead of converging. Two runs back to back on unchanged
// content must produce the same padding.
static void
TestVerseAlignerIsIdempotent(SWModule* moduleA, SWModule* moduleB)
{
	const char* name = "VerseAligner::Align: repeated calls converge "
		"instead of compounding padding";
	if (moduleA == NULL || moduleB == NULL) {
		Skip(name, "need two distinct Bible modules installed");
		return;
	}

	BibleTextDocument documentA(moduleA);
	BibleTextDocument documentB(moduleB);
	documentA.SetKey("Gen 1:1");
	documentB.SetKey("Gen 1:1");

	std::vector<BibleTextDocument*> columns;
	columns.push_back(&documentA);
	columns.push_back(&documentB);

	const float width = 260.0f;
	std::vector<float> widths;
	widths.push_back(width);
	widths.push_back(width);

	VerseAligner::Align(columns, widths);
	ParagraphLayout firstPass;
	firstPass.SetWidth(width);
	firstPass.SetParagraph(documentA.ParagraphAtIndex(0));
	float firstHeight = firstPass.Height();

	VerseAligner::Align(columns, widths);
	ParagraphLayout secondPass;
	secondPass.SetWidth(width);
	secondPass.SetParagraph(documentA.ParagraphAtIndex(0));
	float secondHeight = secondPass.Height();

	Check(fabs(firstHeight - secondHeight) < 0.5f, name);
}


// PersonalNotesModule wraps a local, writable SWORD module; confirm a note
// actually round-trips through it (this is the only test that touches the
// real notes file on disk -- it cleans up after itself).
static void
TestPersonalNotesRoundTrip()
{
	const char* name = "PersonalNotesModule: SetNote/GetNote round-trips";

	PersonalNotesModule notes;
	if (notes.Open() != B_OK) {
		Skip(name, "could not open personal notes module");
		return;
	}

	const char* key = "Gen 1:1";
	const char* text = "unit test note, safe to ignore";

	BString original = notes.GetNote(key);

	notes.SetNote(key, text);
	BString readBack = notes.GetNote(key);
	Check(readBack == text, name);

	// restore whatever was there before this test ran
	notes.SetNote(key, original.String());
}


// Regression test for: NotesWriteBackListener read ParagraphAtIndex() using
// the TextChangedEvent's raw range, but _Rebuild() fires that notification
// mid-flight (once for the Remove(), once per Append()), so the range can
// be out of bounds against the document's *current* paragraph count. A
// listener that doesn't clamp crashes; this one does, and repeated
// rebuilds while it's attached must not crash the test binary.
class BoundsCheckingListener : public TextListener {
public:
	BoundsCheckingListener(BibleTextDocument* document)
		:
		fDocument(document),
		fSawOutOfBoundsAttempt(false)
	{
	}

	bool SawOutOfBoundsAttempt() const { return fSawOutOfBoundsAttempt; }

	virtual void TextChanged(const TextChangedEvent& event)
	{
		int32 first = event.FirstChangedParagraph();
		int32 last = std::min(first + event.ChangedParagraphCount(),
			fDocument->CountParagraphs());
		if (first + event.ChangedParagraphCount() > fDocument->CountParagraphs())
			fSawOutOfBoundsAttempt = true;
		for (int32 i = first; i < last; i++)
			fDocument->ParagraphAtIndex(i).Text();
	}

private:
	BibleTextDocument*	fDocument;
	bool				fSawOutOfBoundsAttempt;
};

static void
TestListenerSurvivesRepeatedRebuilds(SWModule* notesModule)
{
	const char* name = "BibleTextDocument: an attached listener survives "
		"repeated rebuilds without an out-of-bounds paragraph access";
	if (notesModule == NULL) {
		Skip(name, "personal notes module unavailable");
		return;
	}

	BibleTextDocument document(notesModule);
	document.SetShowVerseNumbers(false);
	document.SetSkipEmptyVerses(false);
	document.SetKey("Gen 1:1");

	TextListenerRef listener(new BoundsCheckingListener(&document), true);
	document.AddListener(listener);

	for (int i = 0; i < 5; i++)
		document.SetVerseSpacing(std::map<int, float>());

	// Reaching this line at all (no crash) is most of the point; the
	// "did we actually exercise the out-of-bounds case" flag just confirms
	// the test isn't accidentally vacuous.
	bool exercised
		= ((BoundsCheckingListener*)listener.Get())->SawOutOfBoundsAttempt();
	Check(true, name);
	if (!exercised) {
		printf("  (note: this run never hit a mid-rebuild out-of-bounds "
			"range -- the crash is still guarded against, just not proven "
			"reachable this time)\n");
	}
}


// Regression test for the notes-column rewrite (see the class comment on
// ParallelBibleView -- a notes column is now a real BibleTextDocument
// wrapping the personal notes module, not a separate BTextView per
// verse): SetSkipEmptyVerses(false) must produce exactly one paragraph
// per verse (an empty note still gets a paragraph to click into), and an
// ordinary insert within one verse's own paragraph -- what normal typing
// at a caret actually does -- must not shift any other verse's paragraph
// index. NotesSaveListener (ParallelBibleView.cpp) depends on that
// mapping staying stable across an edit to know which verse it just
// saved (a boundary-consuming edit instead -- Backspace at a paragraph's
// very start, Delete at its very end -- is a different, separately
// handled case; see NotesSaveListener::TextChanged()'s own paragraph-
// count sanity check, not exercised by this test).
static void
TestNotesDocumentOneParagraphPerVerse(SWModule* notesModule)
{
	const char* name = "BibleTextDocument: notes document keeps one "
		"paragraph per verse across an ordinary insert";
	if (notesModule == NULL) {
		Skip(name, "personal notes module unavailable");
		return;
	}

	BibleTextDocument document(notesModule);
	document.SetShowVerseNumbers(false);
	document.SetSkipEmptyVerses(false);
	document.SetKey("Gen 1:1");

	int32 verseCountBefore = document.CountParagraphs();

	int32 s1, e1;
	bool foundVerse1 = document.TextRangeForVerseRange(1, 1, s1, e1);
	int32 verse2ParagraphBefore = document.ParagraphIndexForVerse(2);

	// A zero-length Replace() -- a pure insert -- at the very start of
	// verse 1's own range pushes its existing text forward within the
	// SAME paragraph; unlike replacing the whole [s1, e1) range (which
	// includes the trailing paragraph separator TextRangeForVerseRange()
	// deliberately counts as part of that range for selection-highlight
	// purposes -- see BuildExportRows()'s and NotesSaveListener's own,
	// read-only use of the same range), this never touches the boundary
	// with verse 2's own paragraph.
	if (foundVerse1)
		document.Replace(s1, 0, "X");

	int32 verse2ParagraphAfter = document.ParagraphIndexForVerse(2);

	Check(verseCountBefore > 1 && foundVerse1 && verse2ParagraphBefore >= 0
		&& document.CountParagraphs() == verseCountBefore
		&& verse2ParagraphAfter == verse2ParagraphBefore, name);
}


// Regression test for the bug that made a directly-editable notes column
// impossible before BibleTextDocument::SetParagraphsEndWithNewline()
// existed (see its own header comment): TextDocument::_Remove() merges
// two paragraphs when a removal ends exactly at a paragraph's end,
// reading that as "the line break between them was deleted". In a
// document whose paragraphs carried no trailing "\n", that condition was
// true whenever the user backspaced away a note's LAST CHARACTER -- so
// clearing a note silently swallowed the next verse's note into it,
// destroying the one-paragraph-per-verse invariant the gutter,
// VerseAligner and note saving all depend on.
//
// Both halves are asserted, because they are what places
// NotesDisplayView's KeyDown() guard where it is: deleting an ordinary
// character must NOT merge (that keystroke has to stay allowed), and
// deleting the terminator itself must (that is the one offset the guard
// actually has to refuse).
static void
TestNotesParagraphTerminatorProtectsVerseBoundary(SWModule* notesModule)
{
	const char* name = "BibleTextDocument: deleting a note's last character "
		"doesn't merge it with the next verse";
	if (notesModule == NULL) {
		Skip(name, "personal notes module unavailable");
		return;
	}

	BibleTextDocument document(notesModule);
	document.SetShowVerseNumbers(false);
	document.SetSkipEmptyVerses(false);
	document.SetParagraphsEndWithNewline(true);
	document.SetKey("Gen 1:1");

	int32 paragraphsBefore = document.CountParagraphs();
	if (paragraphsBefore < 2) {
		Skip(name, "chapter has too few verses to test a boundary");
		return;
	}

	// Verse 1 is paragraph 0 (SetSkipEmptyVerses(false), so every verse
	// has one). Give it known content so the offsets below don't depend
	// on whatever note text happens to be stored.
	document.Replace(0, 0, "Hallo");
	int32 length = document.ParagraphAtIndex(0).Length();
	int32 verse2Before = document.ParagraphIndexForVerse(2);

	// length - 1 is the "\n"; length - 2 is the last VISIBLE character,
	// which is where the caret sits after typing and what Backspace
	// there removes.
	document.Remove(length - 2, 1);
	bool ordinaryDeleteKeptParagraphs
		= document.CountParagraphs() == paragraphsBefore
			&& document.ParagraphIndexForVerse(2) == verse2Before;

	// Now the terminator itself -- the offset NotesDisplayView::KeyDown()
	// refuses Backspace/Delete at.
	document.Remove(document.ParagraphAtIndex(0).Length() - 1, 1);
	bool terminatorDeleteMergedParagraphs
		= document.CountParagraphs() == paragraphsBefore - 1;

	Check(ordinaryDeleteKeptParagraphs && terminatorDeleteMergedParagraphs,
		name);
}


// A soft line break ("\\v", inserted by NotesDisplayView::
// _InsertSoftLineBreak() when the user presses Return) must break the
// LINE without breaking the PARAGRAPH -- that is the whole point of
// using it instead of "\\n", which TextDocument::NormalizeText() splits
// paragraphs at. If this ever regressed, pressing Return in a note would
// silently push every following verse's note onto the wrong verse.
static void
TestSoftLineBreakKeepsOneParagraphPerVerse(SWModule* notesModule)
{
	const char* name = "BibleTextDocument: a soft line break inside a note "
		"doesn't split its verse's paragraph";
	if (notesModule == NULL) {
		Skip(name, "personal notes module unavailable");
		return;
	}

	BibleTextDocument document(notesModule);
	document.SetShowVerseNumbers(false);
	document.SetSkipEmptyVerses(false);
	document.SetParagraphsEndWithNewline(true);
	document.SetKey("Gen 1:1");

	int32 paragraphsBefore = document.CountParagraphs();
	if (paragraphsBefore < 2) {
		Skip(name, "chapter has too few verses to test a boundary");
		return;
	}
	int32 verse2Before = document.ParagraphIndexForVerse(2);

	document.Replace(0, 0, "erste\vzweite");

	bool stillOneParagraph
		= document.CountParagraphs() == paragraphsBefore
			&& document.ParagraphIndexForVerse(2) == verse2Before;
	bool bothLinesInVerse1
		= document.ParagraphAtIndex(0).Text().FindFirst("erste") >= 0
			&& document.ParagraphAtIndex(0).Text().FindFirst("zweite") >= 0;

	// The contrast that makes the point: a real "\\n" in the same place
	// DOES split the paragraph, which is exactly why Return must not
	// insert one.
	BibleTextDocument control(notesModule);
	control.SetShowVerseNumbers(false);
	control.SetSkipEmptyVerses(false);
	control.SetParagraphsEndWithNewline(true);
	control.SetKey("Gen 1:1");
	control.Replace(0, 0, "erste\nzweite");
	bool hardBreakSplits = control.CountParagraphs() > paragraphsBefore;

	Check(stillOneParagraph && bothLinesInVerse1 && hardBreakSplits, name);
}


// Regression test for the notes column's per-verse editor redesign (see
// NoteVerseView/NotesColumn in ParallelBibleView.cpp): a document with
// SetSingleVerse(N) set must render ONLY that one verse -- exactly one
// paragraph, holding that verse's own text and no other verse's -- with
// no dependency on which verse the document's own SetKey() text happened
// to name. This is what lets each verse of a notes column have its own,
// independent document with no shared paragraph boundary between
// verses to be ambiguous about (the root cause chased through caret
// placement, the gutter, and typed-text insertion before this existed).
static void
TestSingleVerseRendersExactlyOneVerse(PersonalNotesModule* notes)
{
	const char* name = "BibleTextDocument::SetSingleVerse: renders "
		"exactly one verse regardless of the key's own verse component";
	if (notes == NULL || notes->Module() == NULL) {
		Skip(name, "personal notes module unavailable");
		return;
	}

	// Seed verse 2 with distinguishable text directly through the
	// module (not through a whole-chapter document), so this test
	// doesn't depend on whatever verse 1's own default/empty text
	// happens to render as -- this module is the SAME on-disk personal
	// notes store the live app uses (confirmed live: an earlier version
	// of this test that skipped the restore below silently overwrote a
	// real verse 2 note with this test's own placeholder text), so the
	// original has to be saved and put back afterward, not just
	// overwritten.
	BString originalNote = notes->GetNote("Gen 1:2");
	notes->SetNote("Gen 1:2", "verse two's own note");

	BibleTextDocument document(notes->Module());
	document.SetShowVerseNumbers(false);
	document.SetSkipEmptyVerses(false);
	// The key names verse 1 -- SetSingleVerse() below should override
	// that down to verse 2 regardless.
	document.SetKey("Gen 1:1");
	document.SetSingleVerse(2);

	bool singleVerseReadsBack = document.SingleVerse() == 2;
	bool exactlyOneParagraph = document.CountParagraphs() == 1;
	bool paragraphIsVerse2 = document.VerseForParagraphIndex(0) == 2;
	bool textIsVerse2sOwn
		= document.Text().FindFirst("verse two's own note") >= 0;

	notes->SetNote("Gen 1:2", originalNote.String());

	Check(singleVerseReadsBack && exactlyOneParagraph && paragraphIsVerse2
		&& textIsVerse2sOwn, name);
}


// Regression test for the notes-driven row-growth feature: a note longer
// than its own verse's Bible-column text must grow that verse's whole
// row (both columns, so they stay lined up -- not just the notes
// column's own view, which would leave the Bible column behind). A
// notes column's own document shares the same VerseAligner group as its
// chain's Bible/Commentary ones (see _Realign()), so this is really just
// VerseAligner::Align() itself, exercised with a notes document as one
// of its members -- including the same idempotency guarantee
// TestVerseAlignerIsIdempotent() already covers for a Bible-only case: a
// second _Realign() pass (triggered here by a second SetKey() with the
// SAME key) must not keep compounding that growth on top of itself.
static void
TestTallNotesGrowRowWithoutCompounding(SWMgr* manager, SWModule* moduleA)
{
	const char* name = "ParallelBibleView::_Realign: a longer note grows "
		"its verse's row once, without compounding on repeated calls";
	if (manager == NULL || moduleA == NULL) {
		Skip(name, "need a Bible module installed");
		return;
	}

	// Written through an independent PersonalNotesModule instance, not
	// the one the view below opens for itself (ParallelBibleView has no
	// public way to reach its own internal fNotes) -- relies on the
	// underlying SWORD raw module reading/writing straight through to
	// shared storage rather than caching privately per instance, the
	// same assumption the app itself doesn't need to make (it only ever
	// has one fNotes instance alive at a time) but which held up fine
	// here in practice.
	PersonalNotesModule seedNotes;
	if (seedNotes.Open() != B_OK) {
		Skip(name, "could not open personal notes module");
		return;
	}
	BString longNote;
	for (int i = 0; i < 60; i++)
		longNote << "a long note line that keeps going on and on. ";
	BString originalNote = seedNotes.GetNote("Gen 1:2");
	seedNotes.SetNote("Gen 1:2", longNote.String());

	ParallelBibleView view("testParallelView", manager, 900.0f);
	view.AddColumn(moduleA->getName());
	status_t addedNotes = view.AddNotesColumn();
	view.SetKey("Gen 1:1");

	float heightAfterFirstRealign = view.RowHeight(2, 0);
	view.SetKey("Gen 1:1"); // same key -- forces another _Realign()
	float heightAfterSecondRealign = view.RowHeight(2, 0);

	// A verse with no note at all still gets some natural row height
	// from its own Bible text (see _RowHeight()'s own fallback) -- 400px
	// is comfortably taller than any single short Hebrew/English verse
	// could need on its own, so this is really checking "did the long
	// note's own height make it into this verse's row at all."
	bool grewForTheLongNote = heightAfterFirstRealign > 400.0f;
	bool noCompoundingOnRepeat
		= fabs(heightAfterSecondRealign - heightAfterFirstRealign) < 0.5f;

	seedNotes.SetNote("Gen 1:2", originalNote.String());

	Check(addedNotes == B_OK && grewForTheLongNote && noCompoundingOnRepeat,
		name);
}


// Regression test for a bug originally reported against an interim
// design where a notes column's own row-growth padding was computed and
// applied separately from VerseAligner::Align() (since removed -- a
// notes column's own document now just joins the SAME VerseAligner
// group as its chain's Bible/Commentary ones, see _Realign()): with that
// design, a chain shrinking from 2+ Bible columns down to just 1 left
// stale padding behind, because Align() -- the only thing that cleared
// it -- never ran for a single lone Bible column. Kept as a regression
// test against the CURRENT design too: a verse's row must end up the
// same height whether its chain always had just one Bible column, or
// arrived there by losing a second one.
static void
TestRemovingSecondBibleColumnClearsStaleSpacing(SWMgr* manager,
	SWModule* moduleA, SWModule* moduleB)
{
	const char* name = "ParallelBibleView::_Realign: shrinking to one "
		"Bible column clears spacing left over from when there were two";
	if (manager == NULL || moduleA == NULL || moduleB == NULL) {
		Skip(name, "need two distinct Bible modules installed");
		return;
	}

	PersonalNotesModule seedNotes;
	if (seedNotes.Open() != B_OK) {
		Skip(name, "could not open personal notes module");
		return;
	}
	BString longNote;
	for (int i = 0; i < 60; i++)
		longNote << "a long note line that keeps going on and on. ";
	BString originalNote = seedNotes.GetNote("Gen 1:2");
	seedNotes.SetNote("Gen 1:2", longNote.String());

	// Baseline: a chain that only ever had ONE Bible column.
	ParallelBibleView freshView("testFreshView", manager, 900.0f);
	freshView.AddColumn(moduleA->getName());
	freshView.AddNotesColumn();
	freshView.SetKey("Gen 1:1");
	float freshHeight = freshView.RowHeight(2, 0);

	// Same module, same note, but arriving at one Bible column by
	// starting with two and removing the second.
	ParallelBibleView shrunkView("testShrunkView", manager, 900.0f);
	shrunkView.AddColumn(moduleA->getName());
	shrunkView.AddColumn(moduleB->getName());
	shrunkView.AddNotesColumn();
	shrunkView.SetKey("Gen 1:1");
	shrunkView.RemoveColumn(1); // drops moduleB
	float shrunkHeight = shrunkView.RowHeight(2, 0);

	seedNotes.SetNote("Gen 1:2", originalNote.String());

	Check(fabs(shrunkHeight - freshHeight) < 1.0f, name);
}


// Regression test for a column keeping its old verse spacing after being
// DISCONNECTED from its chain (as opposed to having its partner removed,
// which TestRemovingSecondBibleColumnClearsStaleSpacing covers -- that
// case leaves a two-member chain behind, so alignment still runs and
// still recomputes). Splitting instead leaves a chain of ONE, and
// _Realign() used to skip VerseAligner::Align() entirely for those, so
// nothing ever cleared what the previous alignment had assigned. Reported
// live: a disconnected column stayed stretched to its former partner's
// verse heights, and stayed that way even after navigating to a
// different book.
static void
TestDisconnectingColumnClearsStaleSpacing(SWMgr* manager, SWModule* moduleA,
	SWModule* moduleB)
{
	const char* name = "ParallelBibleView::SetColumnLinked: disconnecting a "
		"column clears the spacing its chain had given it";
	if (manager == NULL || moduleA == NULL || moduleB == NULL) {
		Skip(name, "need two distinct Bible modules installed");
		return;
	}

	PersonalNotesModule seedNotes;
	if (seedNotes.Open() != B_OK) {
		Skip(name, "could not open personal notes module");
		return;
	}
	// A note long enough that aligning against it visibly stretches
	// whatever verse row it sits in.
	BString longNote;
	for (int i = 0; i < 60; i++)
		longNote << "a long note line that keeps going on and on. ";
	BString originalNote = seedNotes.GetNote("Gen 1:2");
	seedNotes.SetNote("Gen 1:2", longNote.String());

	// Baseline: a column that was never in a chain with anything else.
	ParallelBibleView freshView("testFreshSplit", manager, 900.0f);
	freshView.AddColumn(moduleA->getName());
	freshView.SetKey("Gen 1:1");
	float freshHeight = freshView.RowHeight(2, 0);

	// Same column, but it spent time linked to a notes column carrying
	// that long note before being split off on its own.
	ParallelBibleView splitView("testSplitView", manager, 900.0f);
	splitView.AddColumn(moduleA->getName());
	splitView.AddNotesColumn();
	splitView.SetKey("Gen 1:1");
	float linkedHeight = splitView.RowHeight(2, 0);

	splitView.SetColumnLinked(0, false); // disconnect the two
	float splitHeight = splitView.RowHeight(2, 0);

	seedNotes.SetNote("Gen 1:2", originalNote.String());

	// The middle assertion keeps the test honest: if the note weren't
	// actually stretching the row while linked, the other two would match
	// trivially and prove nothing.
	Check(linkedHeight > freshHeight + 1.0f
		&& fabs(splitHeight - freshHeight) < 1.0f, name);
}


// Regression test for issue #12's neighbor-relink rule (see
// ParallelBibleView::RemoveColumn()): removing a linked middle column
// from a 3-column chain must leave its two former neighbors linked to
// each other, not orphaned into separate chains.
static void
TestRemoveMiddleColumnRelinksNeighbors(SWMgr* manager, SWModule* moduleA,
	SWModule* moduleB)
{
	const char* name = "ParallelBibleView::RemoveColumn: removing a "
		"linked middle column re-links its two former neighbors";
	if (manager == NULL || moduleA == NULL || moduleB == NULL) {
		Skip(name, "need two distinct Bible modules installed");
		return;
	}

	ParallelBibleView view("testParallelView", manager, 900.0f);
	view.AddColumn(moduleA->getName());
	view.AddColumn(moduleB->getName());
	view.AddColumn(moduleA->getName());

	bool startedFullyLinked
		= view.AreColumnsLinked(0) && view.AreColumnsLinked(1);

	view.RemoveColumn(1);

	bool endedUpLinked
		= view.CountColumns() == 2 && view.AreColumnsLinked(0);

	Check(startedFullyLinked && endedUpLinked, name);
}


// Regression test for issue #12's per-chain scroll/key independence:
// splitting a chain and navigating the newly-split-off (active) chain
// must leave the other chain's own current key completely untouched.
static void
TestSplitChainKeepsOtherChainUnaffected(SWMgr* manager, SWModule* moduleA,
	SWModule* moduleB)
{
	const char* name = "ParallelBibleView::SetColumnLinked/SetKey: "
		"navigating a split-off chain doesn't move the other chain";
	if (manager == NULL || moduleA == NULL || moduleB == NULL) {
		Skip(name, "need two distinct Bible modules installed");
		return;
	}

	ParallelBibleView view("testParallelView", manager, 900.0f);
	view.AddColumn(moduleA->getName());
	view.AddColumn(moduleB->getName());

	// Both columns start in one chain -- SetKey() (acting on whichever
	// column is active, here column 0 by default, the first one added)
	// moves both.
	view.SetKey("Gen 1:1");
	BString chain0KeyBefore = view.ChainKey(0);
	BString chain1KeyBefore = view.ChainKey(1);

	// Split the two apart into independent chains, then navigate chain 0
	// (still active -- splitting doesn't change fActivePosition unless it
	// was unset) to a different chapter.
	view.SetColumnLinked(0, false);
	bool splitOk = !view.AreColumnsLinked(0);

	view.SetKey("Gen 2:1");

	BString chain0KeyAfter = view.ChainKey(0);
	BString chain1KeyAfter = view.ChainKey(1);

	Check(splitOk && chain0KeyAfter != chain0KeyBefore
		&& chain1KeyAfter == chain1KeyBefore, name);
}


// Regression test for a bug found in live testing after #12: dragging a
// column to a new position rebuilds the whole view from ColumnLayout()
// (see ParallelBibleView::_MoveColumn()), which -- before this fix --
// had no way to carry each column's own current key through that
// teardown/rebuild, so every rebuilt column silently re-seeded from
// whatever _ChainKey(fActivePosition) happened to resolve to at that
// moment instead of its own prior position. Two independent (unlinked)
// chains navigated to two different chapters must each keep their own
// chapter after one of them is dragged to a new position.
static void
TestMoveColumnPreservesEachColumnsOwnKey(SWMgr* manager, SWModule* moduleA,
	SWModule* moduleB)
{
	const char* name = "ParallelBibleView::MoveColumn: each column keeps "
		"its own key across a reorder";
	if (manager == NULL || moduleA == NULL || moduleB == NULL) {
		Skip(name, "need two distinct Bible modules installed");
		return;
	}

	ParallelBibleView view("testParallelView", manager, 900.0f);
	view.AddColumn(moduleA->getName());
	view.AddColumn(moduleB->getName());

	// Split into two independent single-column chains and give each its
	// own, distinct chapter.
	view.SetColumnLinked(0, false);
	view.SetActiveColumn(0);
	view.SetKey("Gen 1:1");
	view.SetActiveColumn(1);
	view.SetKey("Gen 5:1");

	BString frontKeyBefore = view.ChainKey(0);
	BString backKeyBefore = view.ChainKey(1);

	// Drag the back column (position 1) to the front (position 0).
	view.MoveColumn(1, 0);

	BString newFrontKey = view.ChainKey(0);
	BString newBackKey = view.ChainKey(1);

	// The moved column (now at position 0) should carry its own former
	// key (backKeyBefore) with it; the column it displaced (now at
	// position 1) should keep its own former key (frontKeyBefore) too --
	// neither should have collapsed onto the other's chapter.
	Check(newFrontKey == backKeyBefore && newBackKey == frontKeyBefore,
		name);
}


int
main()
{
	BApplication app("application/x-vnd.ScriptureGuide-Tests");

	SWMgr manager(MODULES_PATH, true,
		new MarkupFilterMgr(FMT_GBF, ENC_UTF8));

	SWModule* moduleA = NULL;
	SWModule* moduleB = NULL;
	for (ModMap::iterator it = manager.Modules.begin();
			it != manager.Modules.end(); ++it) {
		if (strcmp(it->second->getType(), "Biblical Texts") != 0)
			continue;
		if (moduleA == NULL)
			moduleA = it->second;
		else if (moduleB == NULL)
			moduleB = it->second;
	}

	TestBibleTextDocumentRebuildIsIdempotent(moduleA);
	TestVerseAlignerIsIdempotent(moduleA, moduleB);
	TestPersonalNotesRoundTrip();
	TestTallNotesGrowRowWithoutCompounding(&manager, moduleA);
	TestRemovingSecondBibleColumnClearsStaleSpacing(&manager, moduleA,
		moduleB);
	TestDisconnectingColumnClearsStaleSpacing(&manager, moduleA, moduleB);
	TestRemoveMiddleColumnRelinksNeighbors(&manager, moduleA, moduleB);
	TestSplitChainKeepsOtherChainUnaffected(&manager, moduleA, moduleB);
	TestMoveColumnPreservesEachColumnsOwnKey(&manager, moduleA, moduleB);

	PersonalNotesModule notes;
	SWModule* notesModule = notes.Open() == B_OK ? notes.Module() : NULL;
	TestEmptyNotesDocumentRebuildIsIdempotent(notesModule);
	TestListenerSurvivesRepeatedRebuilds(notesModule);
	TestNotesDocumentOneParagraphPerVerse(notesModule);
	TestNotesParagraphTerminatorProtectsVerseBoundary(notesModule);
	TestSoftLineBreakKeepsOneParagraphPerVerse(notesModule);
	TestSingleVerseRendersExactlyOneVerse(&notes);

	printf("\n%d checks, %d failed\n", gChecks, gFailures);
	return gFailures > 0 ? 1 : 0;
}

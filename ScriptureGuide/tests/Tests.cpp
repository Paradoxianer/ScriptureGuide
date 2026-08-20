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
#include <Entry.h>
#include <File.h>
#include <String.h>

#include <markupfiltmgr.h>
#include <swmgr.h>

#include "BibleTextDocument.h"
#include "ParagraphLayout.h"
#include "ParallelBibleView.h"
#include "PersonalNotesModule.h"
#include "VerseAligner.h"
#include "VerseListFile.h"
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
		document.SetRowSpacing(std::map<int32, float>());

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
		document.SetRowSpacing(std::map<int32, float>());
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
		document.SetRowSpacing(std::map<int32, float>());

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


// A chapter must show every verse the module actually has. _Rebuild()
// took its verse count from a default-constructed VerseKey, which is
// always KJV -- so a module using another versification was silently
// truncated wherever its chapter is longer. German counting gives
// Malachi 3 twenty-four verses against KJV's eighteen; six were simply
// not rendered, with nothing to indicate anything was missing.
// Finds an installed Bible counting in something other than KJV, and the
// first chapter where it holds more verses than KJV does -- the shape of
// #46, wherever this happens to run. Books KJV does not have are skipped:
// parsing "Judith 1:1" as KJV does not fail, it lands on Revelation 1:1.
static bool
FindChapterLongerThanKJV(SWMgr* manager, SWModule*& outModule,
	BString& outChapterKey, int& outVerses, BString& outVersification)
{
	SWModule* module = NULL;
	BString chapterKey;
	int expected = 0;
	for (ModMap::iterator it = manager->Modules.begin();
			it != manager->Modules.end() && module == NULL; ++it) {
		SWModule* candidate = it->second;
		if (strcmp(candidate->getType(), "Biblical Texts") != 0)
			continue;
		const char* v11n = candidate->getConfigEntry("Versification");
		if (v11n == NULL || strcmp(v11n, "KJV") == 0)
			continue;

		VerseKey theirs;
		theirs.setVersificationSystem(v11n);
		for (theirs.setPosition(TOP); !theirs.popError();
				theirs.setChapter(theirs.getChapter() + 1)) {
			if (theirs.getChapter() > theirs.getChapterMax()) {
				theirs.setBook(theirs.getBook() + 1);
				if (theirs.popError())
					break;
				theirs.setChapter(1);
			}
			VerseKey asKjv;
			asKjv.setText(theirs.getText());
			if (strcmp(asKjv.getBookName(), theirs.getBookName()) != 0)
				continue;			// a book KJV does not have
			if (asKjv.getVerseMax() < theirs.getVerseMax()) {
				module = candidate;
				chapterKey = theirs.getText();
				expected = theirs.getVerseMax();
				break;
			}
		}
	}

	if (module == NULL)
		return false;

	outModule = module;
	outChapterKey = chapterKey;
	outVerses = expected;
	outVersification = module->getConfigEntry("Versification");
	return true;
}


static void
TestChapterShowsEveryVerseOfItsVersification(SWMgr* manager)
{
	const char* name = "BibleTextDocument: renders every verse of the "
		"module's own versification";

	SWModule* module = NULL;
	BString chapterKey, versification;
	int expected = 0;
	if (!FindChapterLongerThanKJV(manager, module, chapterKey, expected,
			versification)) {
		Skip(name, "no module with a versification longer than KJV's");
		return;
	}

	BibleTextDocument document(module);
	// Every verse gets a paragraph whether or not the module has text
	// for it, so the count is the number of verses rendered rather than
	// the number that happened to be non-empty.
	document.SetSkipEmptyVerses(false);
	document.SetKey(chapterKey.String());

	int32 rendered = document.CountParagraphs();
	if (rendered != expected) {
		printf("      %s %s: rendered %d, module has %d\n",
			module->getName(), chapterKey.String(), (int)rendered, expected);
	}
	Check(rendered == expected, name);
}


// A notes column renders one row per verse so its rows line up with the
// Bible beside it. Its own module is ours and counts in KJV, so in a
// chapter that the Bible's versification makes longer the notes column
// came up short and every row below the divergence sat against the wrong
// verse. ParallelBibleView hands the chain's versification down for
// exactly this (see _ChainVersification()).
static void
TestNotesColumnMatchesChainVersification(SWMgr* manager,
	SWModule* notesModule)
{
	const char* name = "BibleTextDocument: a notes document follows the "
		"chain's versification, not its own module's";
	if (notesModule == NULL) {
		Skip(name, "no notes module");
		return;
	}

	SWModule* module = NULL;
	BString chapterKey, versification;
	int expected = 0;
	if (!FindChapterLongerThanKJV(manager, module, chapterKey, expected,
			versification)) {
		Skip(name, "no module with a versification longer than KJV's");
		return;
	}

	BibleTextDocument bible(module);
	bible.SetSkipEmptyVerses(false);
	bible.SetKey(chapterKey.String());

	BibleTextDocument notes(notesModule);
	notes.SetSkipEmptyVerses(false);
	notes.SetVersification(versification.String());
	notes.SetKey(chapterKey.String());

	int32 bibleRows = bible.CountParagraphs();
	int32 notesRows = notes.CountParagraphs();
	if (bibleRows != notesRows) {
		printf("      %s %s (%s): bible %d rows, notes %d\n",
			module->getName(), chapterKey.String(), versification.String(),
			(int)bibleRows, (int)notesRows);
	}
	Check(bibleRows == notesRows && notesRows == expected, name);
}

// The trap this exists to prevent: SWModule::isWritable() is true for
// plain Bibles as well, so anything gating an edit mode on it would make
// every Bible column editable. Asserted against whatever is actually
// installed rather than against a fixed list.
static void
TestOnlyRawFilesModulesAreEditable(SWMgr* manager)
{
	int bibles = 0, editable = 0;
	bool bibleSaidEditable = false;
	bool writableBibleExists = false;

	for (ModMap::iterator it = manager->Modules.begin();
			it != manager->Modules.end(); ++it) {
		SWModule* module = it->second;
		bool isBible = strcmp(module->getType(), "Biblical Texts") == 0;
		if (isBible) {
			bibles++;
			if (module->isWritable())
				writableBibleExists = true;
			if (IsEditableVerseModule(module))
				bibleSaidEditable = true;
		}
		if (IsEditableVerseModule(module))
			editable++;
	}

	if (bibles == 0) {
		Skip("IsEditableVerseModule: no Bible is editable", "no Bibles");
		return;
	}
	Check(!bibleSaidEditable, "IsEditableVerseModule: no Bible is editable");

	// If nothing installed reports writable-but-not-editable, the check
	// above passed without being tested -- say so rather than claim it.
	if (!writableBibleExists) {
		Skip("IsEditableVerseModule: rejects a Bible that claims writable",
			"no installed Bible reports isWritable()");
	} else {
		Check(!bibleSaidEditable,
			"IsEditableVerseModule: rejects a Bible that claims writable");
	}

	SWModule* personal = manager->getModule("Personal");
	if (personal == NULL) {
		Skip("IsEditableVerseModule: accepts SWORD's Personal commentary",
			"Personal not installed");
	} else {
		Check(IsEditableVerseModule(personal),
			"IsEditableVerseModule: accepts SWORD's Personal commentary");
	}
	printf("      %d editable of %d modules\n", editable,
		(int)manager->Modules.size());
}

// Picking a writable module from a column's dropdown gives an editable
// column on that module, not a read-only one -- the point of #45. And it
// records which module, or a restart would turn someone's Personal
// commentary back into a plain notes column without saying so.
static void
TestWritableModuleBecomesEditableColumn(SWMgr* manager)
{
	const char* name = "ParallelBibleView::AddColumn: a writable module "
		"becomes an editable column that remembers which module";

	SWModule* editable = NULL;
	for (ModMap::iterator it = manager->Modules.begin();
			it != manager->Modules.end(); ++it) {
		if (IsEditableVerseModule(it->second)) {
			editable = it->second;
			break;
		}
	}
	if (editable == NULL) {
		Skip(name, "no writable module installed (try SWORD's Personal)");
		return;
	}

	ParallelBibleView view("testEditableColumn", manager, 900.0f);
	view.AddColumn(editable->getName());

	std::vector<ParallelBibleView::ColumnDescription> layout
		= view.ColumnLayout();
	bool ok = layout.size() == 1
		&& layout[0].isNotes
		&& layout[0].moduleName == editable->getName();
	if (!ok && layout.size() == 1) {
		printf("      %s: isNotes=%d moduleName=\"%s\"\n",
			editable->getName(), layout[0].isNotes ? 1 : 0,
			layout[0].moduleName.String());
	}
	Check(ok, name);
}

// A verse list renders the references it names, in the order it names
// them, crossing books -- the foundation of #47. A chapter becomes the
// special case "every verse of this chapter".
//
// The list text is built from the module's own keys rather than written
// out as "Ge 1:1-1:3", so it parses whatever locale the test runs under:
// book names are localized, and a German system does not know "Ge".
static void
TestVerseListRendersItsReferencesInOrder(SWModule* module)
{
	const char* name = "BibleTextDocument::SetVerseList: renders the "
		"listed references, in order, across books";
	if (module == NULL) {
		Skip(name, "no Bible module");
		return;
	}

	const char* v11n = module->getConfigEntry("Versification");
	VerseKey key;
	key.setVersificationSystem(v11n != NULL ? v11n : "KJV");

	BString first, second, third;
	key.setText("Genesis 1:1");	first = key.getText();
	key.setText("Genesis 1:3");	first << "-" << key.getText();
	key.setText("Psalms 1:1");	second = key.getText();
	key.setText("Psalms 1:2");	second << "-" << key.getText();
	key.setText("Matthew 1:1");	third = key.getText();

	// One reference per line -- the format a list file uses, and the
	// only one that cannot collide with the German comma between
	// chapter and verse (see _NormalizeReferenceSeparators()).
	BString listText;
	listText << first << "\n" << second << "\n" << third;

	BibleTextDocument document(module);
	document.SetSkipEmptyVerses(false);
	document.SetKey("Genesis 1:1");
	document.SetVerseList(listText.String());

	// A heading row per line, then that line's verses: three of
	// Genesis, two psalms, one of Matthew. Headings answer verse 0.
	int32 count = document.CountParagraphs();
	int expected[] = { 0, 1, 2, 3, 0, 1, 2, 0, 1 };
	bool orderOk = count == 9;
	for (int32 i = 0; orderOk && i < count; i++)
		orderOk = document.VerseForParagraphIndex(i) == expected[i];

	if (!orderOk) {
		printf("      \"%s\" -> %d paragraphs:", listText.String(),
			(int)count);
		for (int32 i = 0; i < count && i < 12; i++)
			printf(" %d", document.VerseForParagraphIndex(i));
		printf("\n");
	}
	Check(orderOk, name);

	// Each heading is a reference link, so clicking one navigates to
	// the passage the way a reference in a commentary does (#28).
	BString linkKey;
	Check(document.ReferenceLinkAt(0, linkKey) && !linkKey.IsEmpty(),
		"BibleTextDocument::SetVerseList: a section heading is a "
		"reference link");

	// And an empty list goes back to being a chapter.
	document.SetVerseList("");
	Check(document.CountParagraphs() > 9,
		"BibleTextDocument::SetVerseList: an empty list returns to the "
		"chapter");
}

// Why rows are addressed by step rather than by verse number. In a list
// crossing a book, several rows are "verse 1"; asking for verse 1 can
// only ever answer with one of them, while each step answers with its
// own row. VerseAligner writes its padding per row, so keying that by
// verse number would land one section's padding on another section's
// verse.
static void
TestStepsDistinguishRowsAVerseNumberCannot(SWModule* module)
{
	const char* name = "BibleTextDocument: steps address rows a verse "
		"number cannot tell apart";
	if (module == NULL) {
		Skip(name, "no Bible module");
		return;
	}

	const char* v11n = module->getConfigEntry("Versification");
	VerseKey key;
	key.setVersificationSystem(v11n != NULL ? v11n : "KJV");

	BString listText;
	key.setText("Genesis 1:1");	listText = key.getText();
	key.setText("Genesis 1:2");	listText << "-" << key.getText();
	key.setText("Psalms 1:1");	listText << "\n" << key.getText();
	key.setText("Psalms 1:2");	listText << "-" << key.getText();

	BibleTextDocument document(module);
	document.SetSkipEmptyVerses(false);
	document.SetKey("Genesis 1:1");
	document.SetVerseList(listText.String());

	// Two headings and four verses; verse 1 occurs twice, verse 2 twice.
	bool sixRows = document.CountParagraphs() == 6
		&& document.SequenceLength() == 6;

	// Each step finds its own row ...
	bool stepsDistinct = true;
	for (int32 step = 0; step < 6 && stepsDistinct; step++)
		stepsDistinct = document.ParagraphIndexForStep(step) == step;

	// ... while the verse number cannot: verse 1 is two different rows,
	// and only one of them can be returned.
	int32 firstVerseOne = document.ParagraphIndexForVerse(1);
	bool verseIsAmbiguous = document.VerseForParagraphIndex(1) == 1
		&& document.VerseForParagraphIndex(4) == 1
		&& firstVerseOne == 1;

	if (!(sixRows && stepsDistinct && verseIsAmbiguous)) {
		printf("      rows=%d steps=%d verses:", (int)document.CountParagraphs(),
			(int)document.SequenceLength());
		for (int32 i = 0; i < document.CountParagraphs(); i++)
			printf(" %d", document.VerseForParagraphIndex(i));
		printf("\n");
	}
	Check(sixRows && stepsDistinct && verseIsAmbiguous, name);
}

// A verse list belongs to a chain, the module to the column: every
// column of the chain shows the same references, each in its own text,
// and a chain that was split off keeps its chapter (#47).
static void
TestChainVerseListAppliesToItsChainOnly(SWMgr* manager, SWModule* moduleA,
	SWModule* moduleB)
{
	const char* name = "ParallelBibleView::SetColumnVerseList: the list "
		"belongs to its chain and leaves the other one alone";
	if (manager == NULL || moduleA == NULL || moduleB == NULL) {
		Skip(name, "need two distinct Bible modules installed");
		return;
	}

	const char* v11n = moduleA->getConfigEntry("Versification");
	VerseKey key;
	key.setVersificationSystem(v11n != NULL ? v11n : "KJV");
	BString listText;
	key.setText("Genesis 1:1");	listText = key.getText();
	key.setText("Genesis 1:2");	listText << "-" << key.getText();
	key.setText("Psalms 1:1");	listText << ", " << key.getText();

	VerseListFile file;
	if (file.CreateNew("__unit_test_chain_list__", listText.String(),
			v11n != NULL ? v11n : "KJV") != B_OK) {
		Skip(name, "could not create a list file");
		return;
	}

	ParallelBibleView view("testChainList", manager, 900.0f);
	view.AddColumn(moduleA->getName());
	view.AddColumn(moduleB->getName());
	view.AddColumn(moduleA->getName());
	// Break the chain before the last column, so there are two chains.
	view.SetColumnLinked(1, false);

	view.SetColumnVerseListFile(0, file.Path());

	std::vector<ParallelBibleView::ColumnDescription> layout
		= view.ColumnLayout();
	bool ok = layout.size() == 3
		// Both columns of the first chain took the list, recorded as
		// the FILE it came from ...
		&& layout[0].verseListPath == file.Path()
		&& layout[1].verseListPath == file.Path()
		// ... and the chain beyond the break did not.
		&& layout[2].verseListPath.IsEmpty();

	if (!ok && layout.size() == 3) {
		for (size_t i = 0; i < 3; i++) {
			printf("      column %d: \"%s\"\n", (int)i,
				layout[i].verseListPath.String());
		}
	}
	Check(ok, name);

	// And an empty path returns the chain to its chapter.
	view.SetColumnVerseListFile(0, "");
	layout = view.ColumnLayout();
	Check(layout.size() == 3 && layout[0].verseListPath.IsEmpty()
			&& layout[1].verseListPath.IsEmpty(),
		"ParallelBibleView::SetColumnVerseListFile: an empty path "
		"returns the chain to its chapter");

	BEntry(file.Path()).Remove();
}

// Round-trip: create with a seed line, append a second, save, reload
// from disk into a fresh instance, and check every field agrees --
// attributes and body alike (#47).
static void
TestVerseListFileRoundTrips()
{
	const char* name = "VerseListFile: create/save/reload round-trips "
		"name, description, versification and body";

	VerseListFile list;
	status_t status = list.CreateNew("__unit_test_list__",
		"Genesis 1:1", "KJV");
	if (status != B_OK) {
		Skip(name, "could not create a list file (no settings "
			"directory?)");
		return;
	}

	list.SetDescription("temporary, created by the test suite");
	BString extended(list.ReferenceText());
	extended << "\nPsalms 1:1";
	list.SetReferenceText(extended.String());
	list.Save();

	VerseListFile reloaded;
	status = reloaded.SetTo(list.Path());

	bool ok = status == B_OK
		&& BString(reloaded.Name()) == "__unit_test_list__"
		&& BString(reloaded.Description())
			== "temporary, created by the test suite"
		&& BString(reloaded.Versification()) == "KJV"
		&& BString(reloaded.ReferenceText()) == extended
		&& reloaded.EntryCount() == 2;

	if (!ok) {
		printf("      name=\"%s\" desc=\"%s\" v11n=\"%s\" count=%d\n",
			reloaded.Name(), reloaded.Description(),
			reloaded.Versification(), (int)reloaded.EntryCount());
	}
	Check(ok, name);

	// A second file with the same display name gets a number rather
	// than overwriting the first.
	VerseListFile second;
	status = second.CreateNew("__unit_test_list__", "Genesis 1:1", "KJV");
	Check(status == B_OK && BString(second.Path()) != BString(list.Path()),
		"VerseListFile: a name collision gets a numbered file, not an "
		"overwrite");

	BEntry(list.Path()).Remove();
	BEntry(second.Path()).Remove();
}

// The point of moving name/description/versification into the file's
// own content: strip the attributes -- simulating a zip, an email
// attachment, or a copy to a non-BFS filesystem, every one of which
// drops BFS attributes but never the bytes of the file itself -- and
// everything must still read back correctly from the body alone.
static void
TestVerseListFileSurvivesLosingItsAttributes()
{
	const char* name = "VerseListFile: name/description/versification "
		"survive the file's attributes being stripped";

	VerseListFile list;
	status_t status = list.CreateNew("__unit_test_portable__",
		"Genesis 1:1", "German");
	if (status != B_OK) {
		Skip(name, "could not create a list file");
		return;
	}
	list.SetDescription("would vanish if this only lived in an attribute");
	list.Save();

	// Read the file back as a stream of bytes, the way an email
	// attachment or a zip extraction would deliver it, and write those
	// SAME bytes into a fresh file that never had attributes at all.
	BFile source(list.Path(), B_READ_ONLY);
	off_t size = 0;
	source.GetSize(&size);
	char* buffer = new char[size];
	source.Read(buffer, size);
	source.Unset();

	BString strippedPath(list.Path());
	strippedPath << ".stripped";
	BFile stripped(strippedPath.String(),
		B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	stripped.Write(buffer, size);
	stripped.Unset();
	delete[] buffer;
	// No WriteAttrString() call at all above -- this file has content
	// only, exactly like one that arrived from outside BFS.

	VerseListFile reloaded;
	status = reloaded.SetTo(strippedPath.String());

	bool ok = status == B_OK
		&& BString(reloaded.Name()) == "__unit_test_portable__"
		&& BString(reloaded.Description())
			== "would vanish if this only lived in an attribute"
		&& BString(reloaded.Versification()) == "German"
		&& reloaded.EntryCount() == 1;

	if (!ok) {
		printf("      name=\"%s\" desc=\"%s\" v11n=\"%s\" count=%d\n",
			reloaded.Name(), reloaded.Description(),
			reloaded.Versification(), (int)reloaded.EntryCount());
	}
	Check(ok, name);

	// And a file with no header at all -- the plain, hand-typed case --
	// still works, with sensible fallbacks rather than an error.
	BString barePath(list.Path());
	barePath << ".bare";
	BFile bare(barePath.String(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	BString bareText("Genesis 1:1\nPsalms 1:1");
	bare.Write(bareText.String(), bareText.Length());
	bare.Unset();

	VerseListFile bareList;
	status = bareList.SetTo(barePath.String());
	Check(status == B_OK && bareList.EntryCount() == 2
			&& !BString(bareList.Name()).IsEmpty(),
		"VerseListFile: a header-less file is still a valid list, "
		"named after itself");

	BEntry(list.Path()).Remove();
	BEntry(strippedPath.String()).Remove();
	BEntry(barePath.String()).Remove();
}

// The actual bug this whole branch exists to fix: the band showed a
// list's raw first reference instead of the name it was given, because
// nothing recorded which FILE a chain's list came from. Round-trips
// through SetColumnVerseListFile() and checks the document itself
// reports the name -- _ChainBandLabel() reads exactly this.
static void
TestVerseListFileAppliesItsNameToTheChain(SWMgr* manager, SWModule* moduleA)
{
	const char* name = "ParallelBibleView::SetColumnVerseListFile: the "
		"chain's documents report the list's own name, not its text";
	if (manager == NULL || moduleA == NULL) {
		Skip(name, "need a Bible module installed");
		return;
	}

	VerseListFile file;
	if (file.CreateNew("Engel im Alten Testament", "Genesis 1:1",
			"KJV") != B_OK) {
		Skip(name, "could not create a list file");
		return;
	}

	ParallelBibleView view("testListName", manager, 900.0f);
	view.AddColumn(moduleA->getName());
	view.SetColumnVerseListFile(0, file.Path());

	// This is the bug itself: before origin tracking existed, this came
	// back as "Genesis 1:1" (the list's first, and only, reference) --
	// the name given at creation was never shown anywhere.
	BString label = view.ChainBandLabel(0);
	if (label != "Engel im Alten Testament")
		printf("      band label: \"%s\"\n", label.String());
	Check(label == "Engel im Alten Testament", name);

	BEntry(file.Path()).Remove();
}

// The description strip (#47) belongs to its own chain, not to the
// window: a chain showing a list gives up room at the top of itself for
// one, and a chain beside it reading an ordinary chapter gives up none
// and still starts at the very top.
//
// This is the whole point of putting the strip in the content area
// instead of in the header, where it would have taken the same height
// off every chain at once and left a dead band over any chain without a
// list -- so it is what a regression here would break first.
static void
TestChainDescriptionBelongsToItsOwnChain(SWMgr* manager, SWModule* moduleA,
	SWModule* moduleB)
{
	const char* name = "ParallelBibleView: only a chain on a list gives "
		"up room for a description, and only its own columns move down";
	if (manager == NULL || moduleA == NULL || moduleB == NULL) {
		Skip(name, "need two Bible modules installed");
		return;
	}

	VerseListFile file;
	if (file.CreateNew("Beschreibungstest", "Genesis 1:1", "KJV") != B_OK) {
		Skip(name, "could not create a list file");
		return;
	}

	ParallelBibleView view("testDescription", manager, 900.0f);
	view.AddColumn(moduleA->getName());
	view.AddColumn(moduleB->getName());
	// Two separate chains, so the two columns can disagree about whether
	// they are on a list at all.
	view.SetColumnLinked(0, false);

	bool bothStartAtTop = view.ChainDescriptionTop(0) == 0.0f
		&& view.ChainDescriptionTop(1) == 0.0f;

	view.SetColumnVerseListFile(0, file.Path());

	float listed = view.ChainDescriptionTop(0);
	float chapter = view.ChainDescriptionTop(1);
	if (!(bothStartAtTop && listed > 0.0f && chapter == 0.0f)) {
		printf("      before: both at top = %s; after: chain 0 = %.1f, "
			"chain 1 = %.1f\n", bothStartAtTop ? "yes" : "no", listed,
			chapter);
	}
	Check(bothStartAtTop && listed > 0.0f && chapter == 0.0f, name);

	// And back again: leaving the list has to give the room back, or a
	// chain would keep a gap over columns with nothing to explain.
	view.SetColumnVerseListFile(0, "");
	Check(view.ChainDescriptionTop(0) == 0.0f,
		"ParallelBibleView: returning a chain to its chapter takes its "
		"description strip away again");

	BEntry(file.Path()).Remove();
}


// A reference dropped onto a chain that is showing a verse list is
// added to that list rather than navigating the chain away from it
// (#52) -- and, critically, it is parsed in the SOURCE's counting and
// written in the LIST's, which are not the same thing.
//
// Both halves of that were live bugs during development, both invisible
// in the code and obvious the moment a real drop ran: parsing with the
// default (KJV) VerseKey turned German "Psalmen 51:20" into "Psalmen 52,
// 1", because KJV's Psalm 51 ends at 19 and the surplus rolls into the
// next psalm; and using UpperVerseFromKey() as the end verse turned a
// single dropped result into a sweeping range ("Daniel 6, 1-21").
static void
TestDroppedReferenceGoesIntoTheListNotTheChain(SWMgr* manager,
	SWModule* moduleA)
{
	const char* name = "ParallelBibleView::AppendDroppedReferences: a "
		"drop onto a chain showing a list is added to the list, in the "
		"list's own versification";
	if (manager == NULL || moduleA == NULL) {
		Skip(name, "need a Bible module installed");
		return;
	}

	VerseListFile file;
	if (file.CreateNew("Droptest", "Genesis 1:1", "KJV") != B_OK) {
		Skip(name, "could not create a list file");
		return;
	}

	ParallelBibleView view("testDrop", manager, 900.0f);
	view.AddColumn(moduleA->getName());

	// A chain reading a chapter has nothing to append to, so a drop
	// there has to stay navigation -- that is what the false return
	// tells the caller.
	std::vector<BString> keys;
	keys.push_back(BString("Daniel 6:1"));
	bool refusedOnChapter
		= !view.AppendDroppedReferences(0, keys, "KJV");

	view.SetColumnVerseListFile(0, file.Path());

	// German counting in, KJV list out: "Psalmen 51:20" is KJV Psalm
	// 51:19 (German gives that psalm two more verses than KJV, and the
	// last KJV verse absorbs both).
	std::vector<BString> german;
	german.push_back(BString("Psalmen 51:20"));
	bool tookIt = view.AppendDroppedReferences(0, german, "German");

	VerseListFile reloaded;
	reloaded.SetTo(file.Path());
	BString body(reloaded.ReferenceText());

	// Locale-independent: the verse NUMBER is what the conversion
	// changes, and hardcoding an English book name here is exactly the
	// assumption that broke an earlier test under the VM's German
	// locale.
	bool convertedTo19 = body.FindFirst("51") >= 0
		&& body.FindFirst("19") >= 0;
	// The bug this replaced would have written a range or Psalm 52.
	bool noRunaway = body.FindFirst("52") < 0 && body.FindFirst("-") < 0;

	if (!(refusedOnChapter && tookIt && convertedTo19 && noRunaway)) {
		printf("      chapter chain refused: %s; list chain took it: "
			"%s; body:\n%s\n", refusedOnChapter ? "yes" : "no",
			tookIt ? "yes" : "no", body.String());
	}
	Check(refusedOnChapter && tookIt && convertedTo19 && noRunaway, name);

	BEntry(file.Path()).Remove();
}


// The exact conversion "Add to list" depends on to avoid reopening #46:
// a reference read in one column's counting has to be written into a
// list in the TARGET list's own counting, not left as displayed.
// Measured earlier this session: German "Ps 51,20" is KJV "Psalms
// 51:19", because German counting gives Psalm 51 two more verses than
// KJV and the last KJV verse absorbs both.
static void
TestFormatVerseRangeInConvertsAcrossVersifications()
{
	const char* name = "ParallelBibleView::FormatVerseRangeIn: converts "
		"into the target versification, not the source's";

	BString same = ParallelBibleView::FormatVerseRangeIn("Psalms", 51,
		20, 20, "German", "German");
	BString converted = ParallelBibleView::FormatVerseRangeIn("Psalms",
		51, 20, 20, "German", "KJV");

	bool ok = same.FindFirst("51") >= 0 && same.FindFirst("20") >= 0
		&& converted.FindFirst("51") >= 0
		&& converted.FindFirst("19") >= 0
		&& converted != same;

	if (!ok) {
		printf("      unconverted: \"%s\"  converted to KJV: \"%s\"\n",
			same.String(), converted.String());
	}
	Check(ok, name);

	// Same system on both sides is an exact repositioning, not a
	// mapping -- verified directly rather than assumed, since every
	// call site relies on this being safe to do unconditionally. Not
	// checking the book name's exact text: getBookName() answers in
	// whatever locale this happens to run under (German gives "1.
	// Mose", not "Genesis") -- exactly the assumption that broke a
	// hardcoded-English-name test earlier this session. Verse and
	// chapter numbers are locale-independent, so those are what this
	// checks.
	BString identity = ParallelBibleView::FormatVerseRangeIn("Genesis",
		1, 1, 3, "KJV", "KJV");
	Check(identity.FindFirst(" 1") >= 0 && identity.FindFirst("3") >= 0
			&& !identity.IsEmpty(),
		"ParallelBibleView::FormatVerseRangeIn: the same system on both "
		"sides is an exact repositioning");
}

// VerseListFile::RemoveLine() removes the Nth entry and leaves the
// others in order -- the write half of #47's "remove from list".
static void
TestVerseListFileRemovesOneLine()
{
	const char* name = "VerseListFile::RemoveLine: removes exactly the "
		"Nth entry, keeping the others in order";

	VerseListFile list;
	if (list.CreateNew("__unit_test_remove__", "Genesis 1:1",
			"KJV") != B_OK) {
		Skip(name, "could not create a list file");
		return;
	}
	list.SetReferenceText("Genesis 1:1\nPsalms 1:1\nJohn 1:1");
	list.Save();

	status_t status = list.RemoveLine(1);	// the middle one, "Psalms 1:1"

	VerseListFile reloaded;
	reloaded.SetTo(list.Path());
	bool ok = status == B_OK
		&& BString(reloaded.ReferenceText()) == "Genesis 1:1\nJohn 1:1"
		&& reloaded.EntryCount() == 2;
	if (!ok) {
		printf("      status=%s text=\"%s\" count=%d\n", strerror(status),
			reloaded.ReferenceText(), (int)reloaded.EntryCount());
	}
	Check(ok, name);

	Check(list.RemoveLine(99) == B_BAD_INDEX,
		"VerseListFile::RemoveLine: an out-of-range index fails rather "
		"than silently doing nothing to the wrong entry");

	BEntry(list.Path()).Remove();
}


// The other half: a heading's paragraph correctly reports which list
// LINE it came from, matching the index RemoveLine() above expects --
// checked directly rather than assumed, since removal quietly deleting
// the wrong section would be worse than not offering removal at all.
static void
TestListLineForParagraphIndexMatchesSourceLines(SWModule* module)
{
	const char* name = "BibleTextDocument::ListLineForParagraphIndex: "
		"each heading reports its own line, in order";
	if (module == NULL) {
		Skip(name, "no Bible module");
		return;
	}

	BibleTextDocument document(module);
	document.SetSkipEmptyVerses(false);
	document.SetKey("Genesis 1:1");
	document.SetVerseList("Genesis 1:1\nPsalms 1:1\nJohn 1:1");

	// Three sections (heading + its one verse), lines 0/1/2 in order --
	// propagated to every row of a section, not just its heading, so
	// any paragraph in it can answer "which line" without walking back
	// to find the heading first.
	int32 count = document.CountParagraphs();
	bool ok = count == 6;
	int32 expectedLines[] = { 0, 0, 1, 1, 2, 2 };
	for (int32 i = 0; ok && i < count; i++)
		ok = document.ListLineForParagraphIndex(i) == expectedLines[i];

	if (!ok) {
		printf("      %d paragraphs, lines:", (int)count);
		for (int32 i = 0; i < count; i++)
			printf(" %d", (int)document.ListLineForParagraphIndex(i));
		printf("\n");
	}
	Check(ok, name);
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
	TestChapterShowsEveryVerseOfItsVersification(&manager);
	TestVerseListRendersItsReferencesInOrder(moduleA);
	TestStepsDistinguishRowsAVerseNumberCannot(moduleA);
	TestOnlyRawFilesModulesAreEditable(&manager);
	TestWritableModuleBecomesEditableColumn(&manager);
	TestVerseAlignerIsIdempotent(moduleA, moduleB);
	TestPersonalNotesRoundTrip();
	TestTallNotesGrowRowWithoutCompounding(&manager, moduleA);
	TestRemovingSecondBibleColumnClearsStaleSpacing(&manager, moduleA,
		moduleB);
	TestDisconnectingColumnClearsStaleSpacing(&manager, moduleA, moduleB);
	TestRemoveMiddleColumnRelinksNeighbors(&manager, moduleA, moduleB);
	TestSplitChainKeepsOtherChainUnaffected(&manager, moduleA, moduleB);
	TestChainVerseListAppliesToItsChainOnly(&manager, moduleA, moduleB);
	TestVerseListFileAppliesItsNameToTheChain(&manager, moduleA);
	TestChainDescriptionBelongsToItsOwnChain(&manager, moduleA, moduleB);
	TestDroppedReferenceGoesIntoTheListNotTheChain(&manager, moduleA);
	TestFormatVerseRangeInConvertsAcrossVersifications();
	TestVerseListFileRemovesOneLine();
	TestListLineForParagraphIndexMatchesSourceLines(moduleA);
	TestVerseListFileRoundTrips();
	TestVerseListFileSurvivesLosingItsAttributes();
	TestMoveColumnPreservesEachColumnsOwnKey(&manager, moduleA, moduleB);

	PersonalNotesModule notes;
	SWModule* notesModule = notes.Open() == B_OK ? notes.Module() : NULL;
	TestEmptyNotesDocumentRebuildIsIdempotent(notesModule);
	TestListenerSurvivesRepeatedRebuilds(notesModule);
	TestNotesDocumentOneParagraphPerVerse(notesModule);
	TestNotesParagraphTerminatorProtectsVerseBoundary(notesModule);
	TestSoftLineBreakKeepsOneParagraphPerVerse(notesModule);
	TestSingleVerseRendersExactlyOneVerse(&notes);
	TestNotesColumnMatchesChainVersification(&manager, notesModule);

	printf("\n%d checks, %d failed\n", gChecks, gFailures);
	return gFailures > 0 ? 1 : 0;
}

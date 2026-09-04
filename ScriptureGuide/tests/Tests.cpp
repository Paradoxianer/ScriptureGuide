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
#include <Path.h>
#include <View.h>
#include <Bitmap.h>
#include <Language.h>
#include <Locale.h>
#include <String.h>

#include <markupfiltmgr.h>
#include <swmgr.h>

#include "BibleTextDocument.h"
#include "BookmarkFile.h"
#include "Paragraph.h"
#include "ParagraphLayout.h"
#include "ParallelBibleView.h"
#include "PersonalNotesModule.h"
#include "SwordBackend.h"
#include "TextDocument.h"
#include "TextDocumentLayout.h"
#include "TextEditor.h"
#include "TextListener.h"
#include "TextSelection.h"
#include "TextSpan.h"
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


// Regression test for a real bug (found live, not in review): typed
// text handed straight to ConvertVerseReference() -- as the Verse List
// window's "Add Reference..." and #50's Description auto-add both do
// -- silently mangled a German comma-separated verse ("Johannes 3, 16"
// became "Johannes 3:1", the comma read as SWORD's own list separator
// and "16" discarded) or, worse, a book-less fragment ("1,1 - 1,6")
// silently succeeded under WHATEVER book VerseKey defaults to instead
// of failing. ParseVerseReference() (used for in-place cross-reference
// detection, #28) already normalized both cases correctly; the same
// normalization is now shared via SwordBackend.cpp's own
// NormalizeReferenceText(), so this exercises it through
// ConvertVerseReference() specifically, the path that broke.
static void
TestConvertVerseReferenceNormalizesCommaAndRejectsBookless()
{
	BString result;

	bool ok = ConvertVerseReference("Genesis 1, 1", "", "KJV", "", "KJV",
		result);
	Check(ok && result == "Genesis 1:1",
		"ConvertVerseReference: comma separator, not just colon, keeps "
		"the actual verse number");

	ok = ConvertVerseReference("1,1 - 1,6", "", "KJV", "", "KJV", result);
	Check(!ok, "ConvertVerseReference: a book-less fragment is rejected, "
		"not silently resolved against some default book");
}


// CombineVerseRange() is the write-side counterpart used both by a
// reading-pane multi-verse selection drag (_AppendDroppedReferences())
// and by ConvertTypedVerseReference() below.
static void
TestCombineVerseRange()
{
	BString result = CombineVerseRange("Genesis 1:1", "Genesis 1:5");
	Check(result == "Genesis 1:1-5",
		"CombineVerseRange: same book/chapter collapses to one line");

	result = CombineVerseRange("Genesis 1:1", "Genesis 2:5");
	Check(result == "Genesis 1:1 - Genesis 2:5",
		"CombineVerseRange: different chapters keep both references in "
		"full");
}


// Regression test for a second live-reported bug: typing a range into
// #50's description auto-add only ever kept the START verse, both for
// the compact "Johannes 1,1-5" shorthand (a bare trailing verse number)
// and the "Johannes 3,5 - Johannes 3,7" full-reference-on-both-sides
// form -- ConvertVerseReference() succeeds on either, it just silently
// drops everything from the '-' on (see NormalizeReferenceText()'s own
// comment on this being VerseKey::setText()'s own long-standing
// behavior). ConvertTypedVerseReference() is the shared fix used by
// both _NormalizeTypedReference() (LogosVerseListWindow.cpp) and this
// test directly.
static void
TestConvertTypedVerseReferenceKeepsBothEndsOfARange()
{
	BString result;

	bool ok = ConvertTypedVerseReference("Genesis 1,1-5", "", "KJV",
		result);
	Check(ok && result == "Genesis 1:1-5",
		"ConvertTypedVerseReference: compact shorthand range keeps "
		"both ends");

	ok = ConvertTypedVerseReference("Genesis 3,5 - Genesis 3,7", "", "KJV",
		result);
	Check(ok && result == "Genesis 3:5-7",
		"ConvertTypedVerseReference: full-reference-both-sides range "
		"keeps both ends");

	ok = ConvertTypedVerseReference("Genesis 1,1", "", "KJV", result);
	Check(ok && result == "Genesis 1:1",
		"ConvertTypedVerseReference: a plain single reference (no "
		"dash) is unaffected");
}


// Requested directly: the Verses table's Reference column (and
// BibleColumnView::_ReferenceFor()'s own drag tooltip, now refactored
// to share this instead of picking its own separator inline) should
// show the German comma convention, the same one _NormalizeTypedReference()
// already accepts on input -- FormatVerseReferenceForDisplay() is the
// single place that now does the colon->comma reformatting for
// on-screen text, leaving the actually-STORED/round-tripped reference
// (BookmarkFile::Reference(), always colon, unconditionally) untouched.
static void
TestFormatVerseReferenceForDisplay()
{
	BString result = FormatVerseReferenceForDisplay("Genesis 1:1", "de");
	Check(result == "Genesis 1, 1",
		"FormatVerseReferenceForDisplay: German locale uses a comma");

	result = FormatVerseReferenceForDisplay("Genesis 1:1-5", "de");
	Check(result == "Genesis 1, 1-5",
		"FormatVerseReferenceForDisplay: a range's own '-' is untouched");

	result = FormatVerseReferenceForDisplay("Genesis 1:1", "en");
	Check(result == "Genesis 1:1",
		"FormatVerseReferenceForDisplay: non-German locale is unchanged");
}


// Regression test for a third live-reported bug, both cases found in
// one real Description field: "1. Mose 3, 5" (a German numbered book)
// and "Matthäus 3, 7" (an accented book name) both went completely
// unrecognized -- no blue link, and per this app's own whole-line #50
// auto-add rule, never offered as something to add to the list either
// (which only fires for a reference FindReferencesInText() itself
// recognizes). Root causes, both in kReferencePattern
// (FindReferencesInText()'s own regex, SwordBackend.cpp): the numbered-
// book prefix only matched a bare "1 "/"2 "/"3 ", not the German
// "1. "/"2. "/"3. " convention (so the scan started at "Mose" alone,
// which ParseVerseReference() correctly rejects -- not one of the five
// actual book names "Mose" is short for on its own); and the book-name
// character class was ASCII letters only, so "Matthäus" broke the
// match the moment it reached "äus" (a two-byte UTF-8 sequence, no
// byte of which is an ASCII letter).
static void
TestFindReferencesInTextRecognizesGermanNumberedAndAccentedBooks()
{
	// Unlike ConvertVerseReference()/ConvertTypedVerseReference() (both
	// take a locale as an explicit parameter, so those tests aren't
	// environment-dependent), FindReferencesInText() has no locale
	// parameter at all -- it always parses against BLocale::Default(),
	// the SYSTEM's own configured locale, matching how the app itself
	// calls it live. "1. Mose"/"Matthäus" only resolve under German;
	// skip cleanly rather than fail on a system configured for anything
	// else, same as the module-availability skips elsewhere in this
	// file.
	BLanguage language;
	BLocale::Default()->GetLanguage(&language);
	if (strcmp(language.Code(), "de") != 0) {
		Skip("FindReferencesInText: German numbered/accented book "
			"recognition", "system locale is not German");
		return;
	}

	std::vector<TextReference> refs
		= FindReferencesInText("1. Mose 3, 5");
	Check(refs.size() == 1 && refs[0].normalizedKey == "1. Mose 3:5",
		"FindReferencesInText: German numbered book with a period "
		"(\"1. Mose\") is recognized");

	refs = FindReferencesInText("Matthäus 3, 7");
	Check(refs.size() == 1 && refs[0].normalizedKey == "Matthäus 3:7",
		"FindReferencesInText: an accented book name (\"Matthäus\") is "
		"recognized");

	// Both together, embedded in surrounding prose -- the actual shape
	// reported (a description field with running text between and
	// around two references).
	refs = FindReferencesInText(
		"Wenn ich Matthäus 3, 5 schreibe, klappt es -- 1. Mose 3, 5 "
		"aber nicht.");
	Check(refs.size() == 2
			&& refs[0].normalizedKey == "Matthäus 3:5"
			&& refs[1].normalizedKey == "1. Mose 3:5",
		"FindReferencesInText: both recognized together, embedded in "
		"prose");
}


// Regression test for a real bug reported live: typing a space (or any
// character) into the verse list's Description field, while a
// TextListener rebuilds the whole document from scratch on every
// keystroke (SGVerseListWindow::_RestyleDescriptionReferences(), attached
// via DescriptionSaveListener -- mirrored here exactly, minus the
// Bible-reference styling, which is irrelevant to the caret bug itself),
// left the caret in the wrong place. Deliberately built at the
// TextDocument/TextEditor level directly, not through TextDocumentView or
// SGVerseListWindow -- GetSelection()/SetSelection() on the view are thin
// wrappers straight onto TextEditor::SelectionStart()/End()/SetSelection()
// (confirmed by reading TextDocumentView.cpp), so this reproduces the
// exact mechanism without needing a live window.
class RebuildOnChangeListener : public TextListener {
public:
	// Raw pointers, deliberately not TextDocumentRef/TextEditorRef --
	// this test's own local variables already outlive the listener (C++
	// destroys them in reverse declaration order, listener first), and
	// storing owning references here instead created a genuine reference
	// cycle (document->fTextListeners holds this listener,
	// this listener holds the document right back) that kept both alive
	// past the end of the test function -- which, in turn, kept the
	// TextEditor's own TextDocumentLayoutRef pointing at `layout` alive
	// past that stack variable's own C++-guaranteed destruction, a
	// dangling-reference crash confirmed live via debug_server's own
	// "Crashed program" dialog. Matches DescriptionSaveListener's own
	// plain SGVerseListWindow* -- the real bug this whole test exists to
	// chase is in there, not in ownership semantics that don't apply
	// once this is a raw pointer instead.
	RebuildOnChangeListener(TextDocument* document, TextEditor* editor)
		:
		fDocument(document),
		fEditor(editor)
	{
	}

	virtual void TextChanged(const TextChangedEvent& event)
	{
		// Same detach-before/reattach-after shape as
		// SGVerseListWindow::_RestyleDescriptionReferences() -- without
		// it, the nested Replace() below would recurse back into this
		// same method.
		fDocument->RemoveListener(TextListenerRef(this));

		int32 caretStart = fEditor->SelectionStart();
		int32 caretEnd = fEditor->SelectionEnd();

		BString text(fDocument->Text());
		TextDocumentRef newDocument(new TextDocument(), true);
		int32 lineStart = 0;
		int32 textLength = text.Length();
		while (true) {
			int32 newline = text.FindFirst('\n', lineStart);
			int32 lineEnd = newline < 0 ? textLength : newline;

			Paragraph paragraph;
			if (lineEnd > lineStart) {
				BString line;
				text.CopyInto(line, lineStart, lineEnd - lineStart);
				paragraph.Append(TextSpan(line, CharacterStyle()));
			}
			bool hasNewline = newline >= 0;
			if (hasNewline || paragraph.CountTextSpans() == 0)
				paragraph.Append(TextSpan(hasNewline ? "\n" : "",
					CharacterStyle()));
			newDocument->Append(paragraph);

			if (!hasNewline)
				break;
			lineStart = newline + 1;
		}

		fDocument->Replace(0, fDocument->Length(), newDocument);
		fDocument->AddListener(TextListenerRef(this));

		int32 documentLength = fDocument->Length();
		if (caretStart > documentLength)
			caretStart = documentLength;
		if (caretEnd > documentLength)
			caretEnd = documentLength;
		fEditor->SetSelection(TextSelection(caretStart, caretEnd));
	}

private:
	TextDocument*	fDocument;
	TextEditor*		fEditor;
};


static void
TestCaretPositionAfterListenerRebuildsOnKeystroke()
{
	TextDocumentRef document(new TextDocument(), true);
	// A brand-new TextDocument has ZERO paragraphs, not one empty one --
	// Insert()/Replace() on it fails silently until it has at least one
	// (see the identical comment on fDescriptionDocument's own seeding
	// in LogosVerseListWindow.cpp -- same engine, same gotcha).
	document->Append(Paragraph());
	document->Insert(0, "Erste Zeile.\nZweite Zeile hier.\nDritte Zeile.");

	TextDocumentLayout layout;
	layout.SetTextDocument(document);
	// Never set by a real TextDocumentView constructed off-screen like
	// this one -- production always has this set to the view's real
	// pixel width by the time anything gets typed. Left at the default
	// 0.0f, GetTextBounds() (called from SetSelection()'s own
	// updateAnchor path) hung indefinitely -- confirmed live by killing
	// the process after this exact call chain sat unchanged in a debug
	// log for several seconds straight.
	layout.SetWidth(400.0f);

	TextEditorRef editor(new TextEditor(), true);
	editor->SetDocument(document);
	editor->SetLayout(TextDocumentLayoutRef(&layout));
	editor->SetEditingEnabled(true);

	TextListenerRef listener(
		new RebuildOnChangeListener(document.Get(), editor.Get()), true);
	document->AddListener(listener);

	// Caret placed right after "Zweite" in the second line -- 13
	// (length of "Erste Zeile.\n") + 6 ("Zweite").
	int32 caretBefore = 13 + 6;
	editor->SetSelection(TextSelection(caretBefore, caretBefore));

	KeyEvent spaceEvent;
	BString spaceBytes(" ");
	spaceEvent.bytes = spaceBytes.String();
	spaceEvent.length = 1;
	spaceEvent.key = ' ';
	spaceEvent.modifiers = 0;
	editor->KeyDown(spaceEvent);

	Check(editor->CaretOffset() == caretBefore + 1,
		"TextEditor: caret lands right after a space typed mid-document, "
		"even when a listener rebuilds the whole document on every "
		"keystroke");
	Check(document->Text() == "Erste Zeile.\nZweite  Zeile hier.\nDritte Zeile."
			|| document->Text()
				== "Erste Zeile.\nZweite  Zeile hier.\nDritte Zeile.\n",
		"TextEditor: the typed space actually landed at the caret, not "
		"somewhere else or nowhere");

	// Same check for an ordinary letter -- KeyDown()'s default case
	// handles every printable character identically to space, so if the
	// bug above is real, it isn't space-specific.
	int32 caretBeforeLetter = editor->CaretOffset();
	KeyEvent letterEvent;
	BString letterBytes("x");
	letterEvent.bytes = letterBytes.String();
	letterEvent.length = 1;
	letterEvent.key = 'x';
	letterEvent.modifiers = 0;
	editor->KeyDown(letterEvent);
	Check(editor->CaretOffset() == caretBeforeLetter + 1,
		"TextEditor: caret lands right after an ordinary letter typed "
		"mid-document under the same rebuild-on-change listener");

	// The rebuild must not accumulate extra empty paragraphs across
	// repeated edits (three lines in, three lines typed into should
	// still be three lines out) -- a growing paragraph count would
	// explain "text appears further and further down" after a few
	// keystrokes.
	Check(document->CountParagraphs() == 3,
		"TextEditor: repeated rebuilds-on-keystroke don't accumulate "
		"extra empty paragraphs");
}


// One Paragraph per line, the last TextSpan of a line carrying its own
// "\n" -- the exact shape SGVerseListWindow::_RestyleDescriptionReferences()
// builds when it rebuilds the description field from scratch.
static TextDocumentRef
BuildLineDocument(const BString& text)
{
	TextDocumentRef document(new TextDocument(), true);
	CharacterStyle plainStyle;

	int32 lineStart = 0;
	int32 textLength = text.Length();
	while (true) {
		int32 newline = text.FindFirst('\n', lineStart);
		int32 lineEnd = newline < 0 ? textLength : newline;

		Paragraph paragraph;
		if (lineEnd > lineStart) {
			BString line;
			text.CopyInto(line, lineStart, lineEnd - lineStart);
			paragraph.Append(TextSpan(line, plainStyle));
		}
		bool hasNewline = newline >= 0;
		if (hasNewline || paragraph.CountTextSpans() == 0)
			paragraph.Append(TextSpan(hasNewline ? "\n" : "", plainStyle));
		document->Append(paragraph);

		if (!hasNewline)
			break;
		lineStart = newline + 1;
	}
	return document;
}


// Regression test for a live-reported bug: text in the verse list's
// description field drifted further and further down the box (needing
// scrolling to see) as you typed. _RestyleDescriptionReferences() runs on
// every keystroke and swapped its freshly built document in with
// Replace(0, Length(), newDocument) -- but Length() sums the document's
// TEXT length, and an empty paragraph contributes zero, so trailing empty
// paragraphs are outside the replaced range entirely and survive. Measured
// live before the fix: the paragraph count grew by exactly 2 per rebuild
// (5, 7, 9, 11, ... for the same unchanged four-line text).
static void
TestRestyleRebuildDoesNotAccumulateEmptyParagraphs()
{
	const char* kText = "Hallo das ist ein Test\nZweite Zeile\nDritte Zeile\n";

	// First half: prove the trap is real, so this test still means
	// something if someone ever reaches for Replace() here again.
	TextDocumentRef viaReplace = BuildLineDocument(kText);
	int32 initialCount = viaReplace->CountParagraphs();
	for (int i = 0; i < 3; i++) {
		TextDocumentRef rebuilt = BuildLineDocument(viaReplace->Text());
		viaReplace->Replace(0, viaReplace->Length(), rebuilt);
	}
	Check(viaReplace->CountParagraphs() > initialCount,
		"TextDocument: Replace(0, Length(), document) really does strand "
		"empty paragraphs (the trap the description field fell into)");

	// Second half: the fix actually in use -- TextDocument::operator=
	// swaps the paragraph vector itself, so nothing can be stranded.
	TextDocumentRef viaAssign = BuildLineDocument(kText);
	for (int i = 0; i < 5; i++) {
		TextDocumentRef rebuilt = BuildLineDocument(viaAssign->Text());
		*viaAssign = *rebuilt;
	}
	Check(viaAssign->CountParagraphs() == initialCount,
		"TextDocument: wholesale assignment keeps the paragraph count "
		"stable across repeated rebuilds");
	Check(BString(viaAssign->Text()) == BString(kText),
		"TextDocument: repeated wholesale rebuilds preserve the text "
		"exactly, spaces and line breaks included");
}


// Live-reported: a space typed into the description field is swallowed
// outright ("es wird komplett rausgenommen in der UI"), while ordinary
// letters go in fine. The one structural difference between a document
// the field has just RESTYLED (built by hand, one Paragraph per line,
// see BuildLineDocument()) and one merely LOADED (built by
// TextDocument::NormalizeText()) is the span layout inside each
// paragraph -- so this types into the hand-built shape specifically,
// which is what every keystroke after the first actually lands in.
static void
TestTypingIntoARestyleBuiltDocument()
{
	const char* kText = "Hallo das\nZweite Zeile\n";

	TextDocumentRef document = BuildLineDocument(kText);
	TextDocumentLayout layout;
	layout.SetTextDocument(document);
	layout.SetWidth(400.0f);

	TextEditorRef editor(new TextEditor(), true);
	editor->SetDocument(document);
	editor->SetLayout(TextDocumentLayoutRef(&layout));
	editor->SetEditingEnabled(true);

	// Right after "Hallo", i.e. mid-line, mid-document.
	editor->SetSelection(TextSelection(5, 5));

	KeyEvent spaceEvent;
	BString spaceBytes(" ");
	spaceEvent.bytes = spaceBytes.String();
	spaceEvent.length = 1;
	spaceEvent.key = ' ';
	spaceEvent.modifiers = 0;
	editor->KeyDown(spaceEvent);

	Check(BString(document->Text()) == BString("Hallo  das\nZweite Zeile\n"),
		"TextEditor: a space typed into a restyle-built document actually "
		"lands in the text");
	Check(editor->CaretOffset() == 6,
		"TextEditor: the caret follows a space typed into a restyle-built "
		"document");

	// Same document, same shape, an ordinary letter -- the reported
	// difference is that letters work and spaces don't, so both belong
	// in the same test or the comparison proves nothing.
	KeyEvent letterEvent;
	BString letterBytes("x");
	letterEvent.bytes = letterBytes.String();
	letterEvent.length = 1;
	letterEvent.key = 'x';
	letterEvent.modifiers = 0;
	editor->KeyDown(letterEvent);

	Check(BString(document->Text()) == BString("Hallo x das\nZweite Zeile\n"),
		"TextEditor: an ordinary letter typed into a restyle-built document "
		"lands in the text");
}


// #44 (highlighting): a span carrying an explicitly set background
// colour must actually get a rectangle painted behind it. Drawn into an
// offscreen BBitmap so the result can be inspected pixel by pixel --
// there is no other way to tell "the fill happened" from "the fill was
// skipped", since both leave the glyphs looking identical.
//
// Guards the distinction ParagraphLayout::_DrawSpan() relies on: every
// CharacterStyle starts at B_PANEL_BACKGROUND_COLOR, so only a colour
// assigned as a plain rgb_color (which clears the `which` marker to
// B_NO_COLOR) counts as a highlight. A test that merely checked "some
// colour is set" would pass on unhighlighted text too.
static void
TestHighlightedSpanPaintsItsBackground()
{
	const rgb_color kHighlight = (rgb_color){ 255, 240, 120, 255 };
	const rgb_color kCanvas = (rgb_color){ 255, 255, 255, 255 };

	CharacterStyle plainStyle;
	CharacterStyle highlightStyle;
	highlightStyle.SetBackgroundColor(kHighlight);

	Check(plainStyle.WhichBackgroundColor() != B_NO_COLOR,
		"CharacterStyle: an untouched style is not mistaken for a "
		"highlight");
	Check(highlightStyle.WhichBackgroundColor() == B_NO_COLOR,
		"CharacterStyle: setting a plain rgb background marks the style "
		"as explicitly coloured");

	Paragraph paragraph;
	paragraph.Append(TextSpan("AAAA", plainStyle));
	paragraph.Append(TextSpan("BBBB", highlightStyle));

	ParagraphLayout layout;
	layout.SetWidth(600.0f);
	layout.SetParagraph(paragraph);

	BRect bounds(0, 0, 599, 99);
	BBitmap* bitmap = new BBitmap(bounds, B_RGBA32, true);
	BView* view = new BView(bounds, "probe", B_FOLLOW_NONE, B_WILL_DRAW);
	bitmap->AddChild(view);
	if (!bitmap->Lock()) {
		Skip("ParagraphLayout: a highlighted span paints its background",
			"could not lock the offscreen bitmap");
		delete bitmap;
		return;
	}
	view->SetHighColor(kCanvas);
	view->FillRect(bounds);
	layout.Draw(view, BPoint(0, 0));
	view->Sync();
	bitmap->Unlock();

	// Scan the first text line for the highlight colour. Sampling a
	// single guessed coordinate would be fragile against font metrics,
	// so this asks the simpler, robust question: does the colour appear
	// at all, and does it appear to the RIGHT of where plain text sits?
	// BBitmap has no GetPixel(); read the buffer directly. B_RGBA32 is
	// stored blue-green-red-alpha per pixel in memory on this platform.
	int32 highlightPixels = 0;
	float leftmostHighlightX = -1.0f;
	const uint8* bits = (const uint8*)bitmap->Bits();
	int32 bytesPerRow = bitmap->BytesPerRow();
	for (int32 y = 0; y < 40; y++) {
		const uint8* row = bits + (y * bytesPerRow);
		for (int32 x = 0; x < 600; x++) {
			const uint8* pixel = row + (x * 4);
			if (pixel[2] == kHighlight.red && pixel[1] == kHighlight.green
				&& pixel[0] == kHighlight.blue) {
				highlightPixels++;
				if (leftmostHighlightX < 0)
					leftmostHighlightX = (float)x;
			}
		}
	}
	delete bitmap;

	Check(highlightPixels > 0,
		"ParagraphLayout: a span with an explicit background colour "
		"actually gets a rectangle painted behind it");
	Check(leftmostHighlightX > 0.0f,
		"ParagraphLayout: the highlight starts after the unhighlighted "
		"span, not at the paragraph's left edge");
}


// #44: highlights are a background layer over whatever foreground
// styling a verse already carries, so they must split spans without
// disturbing that styling, and two overlapping highlights must mix
// rather than one silently winning.
static void
TestHighlightsSplitSpansAndBlend(SWModule* module)
{
	const char* name = "BibleTextDocument: a highlight splits the verse "
		"into background-coloured pieces without losing text";
	if (module == NULL) {
		Skip(name, "no Bible module installed");
		return;
	}

	BibleTextDocument document(module);
	document.SetShowVerseNumbers(false);
	document.SetKey("Gen 1:1");

	BString before = document.Text();
	int32 paragraphsBefore = document.CountParagraphs();

	std::vector<BibleTextDocument::VerseHighlight> highlights;
	BibleTextDocument::VerseHighlight first;
	first.verse = 1;
	first.start = 2;
	first.end = 10;
	first.color = (rgb_color){ 200, 0, 0, 255 };
	highlights.push_back(first);
	// Deliberately overlapping the first, to exercise the blend.
	BibleTextDocument::VerseHighlight second;
	second.verse = 1;
	second.start = 6;
	second.end = 14;
	second.color = (rgb_color){ 0, 200, 0, 255 };
	highlights.push_back(second);
	document.SetHighlights(highlights);

	Check(BString(document.Text()) == before,
		name);
	Check(document.CountParagraphs() == paragraphsBefore,
		"BibleTextDocument: highlighting does not change the paragraph "
		"count");

	// Walk verse 1's spans and classify them by background.
	const Paragraph& paragraph = document.ParagraphAtIndex(0);
	int32 plainSpans = 0;
	int32 redSpans = 0;
	int32 greenSpans = 0;
	int32 blendedSpans = 0;
	for (int32 i = 0; i < paragraph.CountTextSpans(); i++) {
		const CharacterStyle& style = paragraph.TextSpanAtIndex(i).Style();
		if (style.WhichBackgroundColor() != B_NO_COLOR) {
			plainSpans++;
			continue;
		}
		rgb_color background = style.BackgroundColor();
		if (background.red == 200 && background.green == 0)
			redSpans++;
		else if (background.red == 0 && background.green == 200)
			greenSpans++;
		else if (background.red == 100 && background.green == 100)
			blendedSpans++;
	}

	Check(redSpans == 1 && greenSpans == 1,
		"BibleTextDocument: each highlight's non-overlapping part keeps "
		"its own colour");
	Check(blendedSpans == 1,
		"BibleTextDocument: the overlapping part is the average of both "
		"colours, not one of them");
	Check(plainSpans > 0,
		"BibleTextDocument: text outside every highlight stays "
		"unpainted");

	// Clearing highlights must restore the original span structure.
	document.SetHighlights(std::vector<BibleTextDocument::VerseHighlight>());
	const Paragraph& cleared = document.ParagraphAtIndex(0);
	int32 stillPainted = 0;
	for (int32 i = 0; i < cleared.CountTextSpans(); i++) {
		if (cleared.TextSpanAtIndex(i).Style().WhichBackgroundColor()
				== B_NO_COLOR) {
			stillPainted++;
		}
	}
	Check(stillPainted == 0,
		"BibleTextDocument: clearing the highlights removes every "
		"painted background again");
}


// #44: the coordinate system a stored highlight lives in must not move
// when a display option is toggled. This is the property the whole
// feature rests on -- offsets that shift when verse numbers are switched
// on would silently relocate every highlight.
static void
TestVersePositionSurvivesDisplayOptions(SWModule* module)
{
	const char* name = "BibleTextDocument::VersePositionAt: the same "
		"verse offset is reported whether or not verse numbers are shown";
	if (module == NULL) {
		Skip(name, "no Bible module installed");
		return;
	}

	BibleTextDocument withoutNumbers(module);
	withoutNumbers.SetShowVerseNumbers(false);
	withoutNumbers.SetKey("Gen 1:2");

	BibleTextDocument withNumbers(module);
	withNumbers.SetShowVerseNumbers(true);
	withNumbers.SetKey("Gen 1:2");

	// Aim at the same logical spot -- five characters into verse 2's own
	// text -- reached through each document's own offsets.
	int32 plainStart = 0, plainEnd = 0;
	int32 numberedStart = 0, numberedEnd = 0;
	if (!withoutNumbers.TextRangeForVerseRange(2, 2, plainStart, plainEnd)
		|| !withNumbers.TextRangeForVerseRange(2, 2, numberedStart,
			numberedEnd)) {
		Skip(name, "verse 2 not present in this module");
		return;
	}

	int plainVerse = 0, numberedVerse = 0;
	int32 plainOffset = -1, numberedOffset = -1;
	bool plainOk = withoutNumbers.VersePositionAt(plainStart + 5,
		plainVerse, plainOffset);
	// The numbered document carries a " 2 " prefix, so the same logical
	// position sits further along its own offsets -- VersePositionAt()
	// is what has to subtract that again.
	bool numberedOk = false;
	for (int32 probe = numberedStart; probe < numberedEnd; probe++) {
		int verse = 0;
		int32 verseOffset = -1;
		if (withNumbers.VersePositionAt(probe, verse, verseOffset)
			&& verse == 2 && verseOffset == 5) {
			numberedOk = true;
			numberedVerse = verse;
			numberedOffset = verseOffset;
			break;
		}
	}

	Check(plainOk && plainVerse == 2 && plainOffset == 5,
		"BibleTextDocument::VersePositionAt: reports the verse and the "
		"offset within it");
	Check(numberedOk && numberedVerse == 2 && numberedOffset == 5, name);

	// And the payoff: the same stored highlight paints in both.
	std::vector<BibleTextDocument::VerseHighlight> highlights;
	BibleTextDocument::VerseHighlight highlight;
	highlight.verse = 2;
	highlight.start = 5;
	highlight.end = 12;
	highlight.color = (rgb_color){ 120, 200, 255, 255 };
	highlights.push_back(highlight);

	withoutNumbers.SetHighlights(highlights);
	withNumbers.SetHighlights(highlights);

	int32 paintedPlain = 0;
	int32 paintedNumbered = 0;
	int32 plainParagraph = withoutNumbers.ParagraphIndexForVerse(2);
	int32 numberedParagraph = withNumbers.ParagraphIndexForVerse(2);
	if (plainParagraph >= 0) {
		const Paragraph& p = withoutNumbers.ParagraphAtIndex(plainParagraph);
		for (int32 i = 0; i < p.CountTextSpans(); i++) {
			if (p.TextSpanAtIndex(i).Style().WhichBackgroundColor()
					== B_NO_COLOR) {
				paintedPlain += p.TextSpanAtIndex(i).CountChars();
			}
		}
	}
	if (numberedParagraph >= 0) {
		const Paragraph& p = withNumbers.ParagraphAtIndex(numberedParagraph);
		for (int32 i = 0; i < p.CountTextSpans(); i++) {
			if (p.TextSpanAtIndex(i).Style().WhichBackgroundColor()
					== B_NO_COLOR) {
				paintedNumbered += p.TextSpanAtIndex(i).CountChars();
			}
		}
	}

	Check(paintedPlain == 7 && paintedNumbered == 7,
		"BibleTextDocument: one stored highlight paints the same seven "
		"characters with verse numbers on and off");
}


// #44: a highlight is an ordinary BookmarkFile carrying extra optional
// attributes, so the round trip that matters is "write span + colour,
// read them back identically" -- and, just as importantly, that an
// ordinary bookmark is not mistaken for one.
static void
TestHighlightBookmarkRoundTrip()
{
	BString root = BookmarkFile::HighlightsDirectory();
	if (root.IsEmpty()) {
		Skip("BookmarkFile: highlight span/colour round-trip",
			"could not create the highlights directory");
		return;
	}

	BString collection = BookmarkFile::CreateCollection(root.String(),
		"UnitTestColour");
	if (collection.IsEmpty()) {
		Skip("BookmarkFile: highlight span/colour round-trip",
			"could not create a test colour folder");
		return;
	}

	const rgb_color kColor = (rgb_color){ 0x66, 0xcc, 0x66, 255 };

	BookmarkFile written;
	Check(written.CreateNew(collection.String(), "Johannes 3:16", "KJV", "de",
		0) == B_OK, "BookmarkFile: a highlight bookmark can be created");
	written.SetSpan("GerSch", 12, 45, "sondern das ewige Leben hat");
	written.SetColor(kColor);
	Check(written.Save() == B_OK,
		"BookmarkFile: saving a bookmark with span and colour succeeds");

	BookmarkFile readBack;
	Check(readBack.SetTo(written.Path()) == B_OK,
		"BookmarkFile: a highlight bookmark reads back");
	Check(readBack.HasSpan() && readBack.HasColor(),
		"BookmarkFile: span and colour survive the round trip");
	Check(BString(readBack.SpanModule()) == "GerSch"
			&& readBack.SpanStart() == 12 && readBack.SpanEnd() == 45,
		"BookmarkFile: the span's module and offsets are unchanged");
	Check(BString(readBack.SpanText()) == "sondern das ewige Leben hat",
		"BookmarkFile: the healing text snippet is unchanged");
	Check(readBack.Color().red == kColor.red
			&& readBack.Color().green == kColor.green
			&& readBack.Color().blue == kColor.blue,
		"BookmarkFile: the colour is unchanged");

	// The same file type is used for ordinary bookmarks, so absence has
	// to be just as reliable as presence.
	BookmarkFile plain;
	if (plain.CreateNew(collection.String(), "Johannes 3:17", "KJV", "de", 1)
			== B_OK) {
		BookmarkFile plainReadBack;
		plainReadBack.SetTo(plain.Path());
		Check(!plainReadBack.HasSpan() && !plainReadBack.HasColor(),
			"BookmarkFile: an ordinary bookmark is not mistaken for a "
			"highlight");
		plain.Remove();
	}

	std::vector<BookmarkFile> listed = BookmarkFile::ListHighlights();
	bool found = false;
	for (size_t i = 0; i < listed.size(); i++) {
		if (BString(listed[i].Path()) == BString(written.Path()))
			found = true;
	}
	Check(found,
		"BookmarkFile::ListHighlights: finds a highlight inside a colour "
		"folder");

	// Clean up after itself -- this test writes into the real settings
	// tree, same as the personal-notes test above.
	written.Remove();
	BEntry(collection.String()).Remove();
}


// Regression test for a bug that mislocated every highlight and, less
// visibly, every German bookmark's Bible-order sort key: a reference
// stored in its DISPLAY form carries the German comma ("1. Mose 1, 8"),
// and SWORD reads that comma as its own list separator -- so the verse
// was discarded and Code() came back pointing at verse 1.
static void
TestBookmarkCodeHandlesDisplayFormReferences()
{
	BookmarkFile commaForm;
	commaForm.SetReference("1. Mose 1, 8");
	commaForm.SetVersification("KJV");
	commaForm.SetLocale("de");

	BookmarkFile colonForm;
	colonForm.SetReference("1. Mose 1:8");
	colonForm.SetVersification("KJV");
	colonForm.SetLocale("de");

	BString commaCode = commaForm.Code();
	BString colonCode = colonForm.Code();

	Check(colonCode.Length() == 9,
		"BookmarkFile::Code: a colon-form reference produces a code");
	Check(commaCode == colonCode,
		"BookmarkFile::Code: the German comma form yields the same code "
		"as the colon form, not verse 1");
	Check(commaCode.Length() == 9 && atoi(commaCode.String() + 6) == 8,
		"BookmarkFile::Code: the verse number survives the comma form");
}


// #44: a highlight spanning several verses must stay ONE bookmark --
// splitting it per verse would make the obvious next step, turning it
// into a verse-list entry like "1. Mose 1:4-10", impossible to
// reconstruct.
static void
TestHighlightSpanCoversAVerseRange()
{
	BString root = BookmarkFile::HighlightsDirectory();
	BString collection = BookmarkFile::CreateCollection(root.String(),
		"UnitTestRange");
	if (collection.IsEmpty()) {
		Skip("BookmarkFile: a span can cover a verse range",
			"could not create a test colour folder");
		return;
	}

	BookmarkFile written;
	Check(written.CreateNew(collection.String(), "1. Mose 1:5-8", "KJV", "de",
		0) == B_OK, "BookmarkFile: a range highlight can be created");
	written.SetSpan("AKJV", 12, 30, "some text", 8);
	written.SetColor((rgb_color){ 0xc5, 0xdd, 0xf2, 255 });
	written.Save();

	BookmarkFile readBack;
	readBack.SetTo(written.Path());
	Check(readBack.SpanEndVerse() == 8,
		"BookmarkFile: the span's last verse survives the round trip");
	Check(readBack.SpanStart() == 12 && readBack.SpanEnd() == 30,
		"BookmarkFile: a range span keeps its first and last offsets");

	// Code() must resolve to the range's FIRST verse, which is what the
	// renderer expands forward from.
	BString code = readBack.Code();
	Check(code.Length() == 9 && atoi(code.String() + 6) == 5,
		"BookmarkFile::Code: a range reference resolves to its first "
		"verse");

	// A single-verse highlight must still report no range.
	BookmarkFile single;
	if (single.CreateNew(collection.String(), "1. Mose 1:3", "KJV", "de", 1)
			== B_OK) {
		single.SetSpan("AKJV", 4, 9, "", 0);
		single.SetColor((rgb_color){ 0xf7, 0xec, 0xb3, 255 });
		single.Save();
		BookmarkFile singleReadBack;
		singleReadBack.SetTo(single.Path());
		Check(singleReadBack.SpanEndVerse() == 0,
			"BookmarkFile: a single-verse span records no end verse");
		single.Remove();
	}

	written.Remove();
	BEntry(collection.String()).Remove();
}


// #44: a colour folder is matched by its own SG:colorvalue attribute,
// never by its name -- so it can be renamed (to something meaningful,
// with its own Description.txt) and the next highlight of that colour
// still lands in it instead of resurrecting a fresh "Red" beside it.
static void
TestHighlightFolderSurvivesRenaming()
{
	const rgb_color kRed = (rgb_color){ 0xf5, 0xc6, 0xc6, 255 };

	BString first = BookmarkFile::HighlightFolderForColor(kRed,
		"UnitTestRed");
	if (first.IsEmpty()) {
		Skip("BookmarkFile::HighlightFolderForColor: survives a rename",
			"could not create the colour folder");
		return;
	}

	// Asking again must reuse it rather than make a second one.
	BString again = BookmarkFile::HighlightFolderForColor(kRed,
		"UnitTestRed");
	Check(again == first,
		"BookmarkFile::HighlightFolderForColor: the same colour reuses "
		"the same folder");

	// Rename it the way a user would, then ask again.
	BEntry entry(first.String());
	BString renamed;
	if (entry.Rename("UnitTestRenamedColour") == B_OK) {
		BPath renamedPath;
		entry.GetPath(&renamedPath);
		renamed = renamedPath.Path();

		BString afterRename = BookmarkFile::HighlightFolderForColor(kRed,
			"UnitTestRed");
		Check(afterRename == renamed,
			"BookmarkFile::HighlightFolderForColor: survives a rename "
			"instead of creating a fresh folder under the old name");
	} else {
		Skip("BookmarkFile::HighlightFolderForColor: survives a rename",
			"could not rename the test folder");
	}

	// A different colour must NOT reuse it.
	const rgb_color kBlue = (rgb_color){ 0xc5, 0xdd, 0xf2, 255 };
	BString other = BookmarkFile::HighlightFolderForColor(kBlue,
		"UnitTestBlue");
	Check(!other.IsEmpty() && other != renamed && other != first,
		"BookmarkFile::HighlightFolderForColor: a different colour gets "
		"its own folder");

	if (!renamed.IsEmpty())
		BEntry(renamed.String()).Remove();
	else
		BEntry(first.String()).Remove();
	if (!other.IsEmpty())
		BEntry(other.String()).Remove();
}


// #44: a verse-wide highlight -- what dragging a selection across
// columns produces -- has a colour and a verse range but deliberately no
// span module, because it belongs to the verses rather than to one
// translation's character offsets.
static void
TestVerseWideHighlightNeedsNoSpanModule()
{
	BString root = BookmarkFile::HighlightsDirectory();
	BString collection = BookmarkFile::CreateCollection(root.String(),
		"UnitTestVerseWide");
	if (collection.IsEmpty()) {
		Skip("BookmarkFile: a verse-wide highlight needs no span module",
			"could not create a test folder");
		return;
	}

	BookmarkFile written;
	Check(written.CreateNew(collection.String(), "1. Mose 1:5-8", "KJV", "de",
		0) == B_OK, "BookmarkFile: a verse-wide highlight can be created");
	written.SetSpan("", 0, 0, "", 8);
	written.SetColor((rgb_color){ 0xc8, 0xe6, 0xc9, 255 });
	written.Save();

	BookmarkFile readBack;
	readBack.SetTo(written.Path());
	Check(!readBack.HasSpan(),
		"BookmarkFile: a verse-wide highlight reports no span");
	Check(readBack.HasColor(),
		"BookmarkFile: a verse-wide highlight keeps its colour");
	Check(readBack.SpanEndVerse() == 8,
		"BookmarkFile: the verse range survives without a span module");

	// It must still be listed as a highlight -- a colour is what makes
	// one, a span is optional.
	std::vector<BookmarkFile> listed = BookmarkFile::ListHighlights();
	bool found = false;
	for (size_t i = 0; i < listed.size(); i++) {
		if (BString(listed[i].Path()) == BString(written.Path()))
			found = true;
	}
	Check(found,
		"BookmarkFile::ListHighlights: a verse-wide highlight is listed "
		"even though it carries no span");

	written.Remove();
	BEntry(collection.String()).Remove();
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

	TestConvertVerseReferenceNormalizesCommaAndRejectsBookless();
	TestCombineVerseRange();
	TestConvertTypedVerseReferenceKeepsBothEndsOfARange();
	TestFormatVerseReferenceForDisplay();
	TestFindReferencesInTextRecognizesGermanNumberedAndAccentedBooks();
	TestBibleTextDocumentRebuildIsIdempotent(moduleA);
	TestChapterShowsEveryVerseOfItsVersification(&manager);
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

	TestCaretPositionAfterListenerRebuildsOnKeystroke();
	TestRestyleRebuildDoesNotAccumulateEmptyParagraphs();
	TestTypingIntoARestyleBuiltDocument();
	TestHighlightedSpanPaintsItsBackground();
	TestHighlightsSplitSpansAndBlend(moduleA);
	TestVersePositionSurvivesDisplayOptions(moduleA);
	TestHighlightBookmarkRoundTrip();
	TestBookmarkCodeHandlesDisplayFormReferences();
	TestHighlightSpanCoversAVerseRange();
	TestHighlightFolderSurvivesRenaming();
	TestVerseWideHighlightNeedsNoSpanModule();

	printf("\n%d checks, %d failed\n", gChecks, gFailures);
	return gFailures > 0 ? 1 : 0;
}

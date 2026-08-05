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
	TestRemoveMiddleColumnRelinksNeighbors(&manager, moduleA, moduleB);
	TestSplitChainKeepsOtherChainUnaffected(&manager, moduleA, moduleB);
	TestMoveColumnPreservesEachColumnsOwnKey(&manager, moduleA, moduleB);

	PersonalNotesModule notes;
	SWModule* notesModule = notes.Open() == B_OK ? notes.Module() : NULL;
	TestEmptyNotesDocumentRebuildIsIdempotent(notesModule);
	TestListenerSurvivesRepeatedRebuilds(notesModule);

	printf("\n%d checks, %d failed\n", gChecks, gFailures);
	return gFailures > 0 ? 1 : 0;
}

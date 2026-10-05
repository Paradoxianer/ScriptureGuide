/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#include "PrintSupport.h"

#include <algorithm>
#include <vector>

#include <math.h>

#include <PrintJob.h>
#include <View.h>
#include <Window.h>

#include "textview/TextDocumentLayout.h"


void
AppendPrintHeading(TextDocumentRef document, const BString& heading)
{
	CharacterStyle style;
	style.SetBold(true);
	style.SetFontSize(14.0f);
	ParagraphStyle paragraphStyle;
	paragraphStyle.SetSpacingTop(12.0f);
	paragraphStyle.SetSpacingBottom(6.0f);

	Paragraph paragraph(paragraphStyle);
	BString text(heading);
	text << "\n";
	paragraph.Append(TextSpan(text, style));
	document->Append(paragraph);
}


void
AppendPrintBodyLine(TextDocumentRef document, const BString& line,
	const rgb_color* backgroundColor)
{
	CharacterStyle style;
	if (backgroundColor != NULL)
		style.SetBackgroundColor(*backgroundColor);
	ParagraphStyle paragraphStyle;
	paragraphStyle.SetSpacingBottom(2.0f);

	Paragraph paragraph(paragraphStyle);
	BString text(line);
	text << "\n";
	paragraph.Append(TextSpan(text, style));
	document->Append(paragraph);
}


namespace {

// A minimal, print-only view: draws a TextDocumentLayout laid out at a
// fixed (page) width directly, bypassing TextDocumentView entirely --
// that class ties its layout width to its own on-screen Bounds()
// (irrelevant here, and actively in the way: a print page's width has
// nothing to do with any view's current screen size) and carries
// scrolling/selection/editing machinery print doesn't need at all.
class PrintPageView : public BView {
public:
	PrintPageView(const TextDocumentRef& document, float width)
		:
		BView(BRect(0.0f, 0.0f, width, 10.0f), "printPage", B_FOLLOW_NONE,
			B_WILL_DRAW),
		fLayout(document)
	{
		fLayout.SetWidth(width);
		float height = fLayout.Height();
		ResizeTo(width, height);
	}

	virtual void Draw(BRect updateRect)
	{
		fLayout.Draw(this, BPoint(0.0f, 0.0f), updateRect);
	}

	float ContentHeight()
	{
		return fLayout.Height();
	}

	void ParagraphBounds(int32 index, float& y1, float& y2)
	{
		fLayout.GetParagraphBounds(index, y1, y2);
	}

private:
	TextDocumentLayout	fLayout;
};


// One entry per page: the y-offset (in the print view's own coordinate
// space) where that page starts. A page's own end is either the next
// entry, or the document's total height for the last page. Breaks land on
// paragraph boundaries (via ParagraphBounds()) rather than a plain
// `pageHeight` multiple, so a verse's own paragraph is never split across
// two pages -- a paragraph taller than one full page is the one case that
// still overflows a single page, since there is nothing narrower to break
// it on.
//
// `forcedBreaks` (sorted ascending, see PrintTextDocument()'s own comment)
// are paragraph indices that must start a fresh page even if the current
// one has room left -- Options > New Page per Translation When Printing.
void
ComputePageBreaks(int32 paragraphCount, PrintPageView* view,
	float pageHeight, const std::vector<int32>& forcedBreaks,
	std::vector<float>& outPageStartY)
{
	float totalHeight = view->ContentHeight();

	outPageStartY.push_back(0.0f);
	float pageStart = 0.0f;
	int32 paragraph = 0;
	size_t nextForcedBreak = 0;
	while (pageStart + pageHeight < totalHeight) {
		// Skip past any forced-break marks already satisfied by wherever
		// this page is starting from (a previous page may have just
		// ended exactly there).
		while (nextForcedBreak < forcedBreaks.size()
			&& forcedBreaks[nextForcedBreak] <= paragraph) {
			nextForcedBreak++;
		}

		float pageLimit = pageStart + pageHeight;
		float lastFittingEnd = pageStart;
		bool includedAny = false;
		while (paragraph < paragraphCount) {
			// A forced break only ends the CURRENT page early if it
			// already holds at least one paragraph -- otherwise this
			// same paragraph would already be sitting at the top of a
			// fresh page with nothing to break away from.
			if (includedAny && nextForcedBreak < forcedBreaks.size()
				&& forcedBreaks[nextForcedBreak] == paragraph) {
				break;
			}
			float y1, y2;
			view->ParagraphBounds(paragraph, y1, y2);
			if (y2 > pageLimit)
				break;
			lastFittingEnd = y2;
			paragraph++;
			includedAny = true;
		}
		if (lastFittingEnd <= pageStart) {
			// This one paragraph alone is taller than a full page --
			// let it overflow onto the next page instead of looping
			// forever. `paragraph` deliberately isn't advanced: its own
			// end is still ahead, so the next iteration (pageStart now
			// at pageLimit) picks up measuring the same paragraph
			// again, once its bottom actually fits within the new,
			// further-out pageLimit.
			lastFittingEnd = pageLimit;
		}
		outPageStartY.push_back(lastFittingEnd);
		pageStart = lastFittingEnd;
	}
}

}	// namespace


bool
PrintTextDocument(BWindow* parentWindow, TextDocumentRef document,
	const char* jobName,
	const std::vector<int32>& forcedPageBreaksBeforeParagraph)
{
	BRect frame = parentWindow != NULL
		? parentWindow->Frame() : BRect(50.0f, 50.0f, 550.0f, 750.0f);
	// BPrintJob::DrawView() requires its target view be attached to a
	// window app_server actually knows about -- hosting the print
	// content in its own throwaway window (rather than borrowing
	// `parentWindow`) means printing never touches anything the user
	// can see. Show()+Hide() right away starts the window's message
	// loop and unlocks it -- a freshly constructed BWindow's loop does
	// not start until the first Show() (see scriptureguide-quirks.md's
	// entry on this) -- without ever actually displaying it.
	BWindow* hostWindow = new BWindow(frame, "", B_TITLED_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL, B_ASYNCHRONOUS_CONTROLS);
	hostWindow->Show();
	hostWindow->Hide();

	BPrintJob printJob(jobName);
	if (printJob.ConfigPage() != B_OK) {
		if (hostWindow->LockLooper())
			hostWindow->Quit();
		return false;
	}
	if (printJob.ConfigJob() != B_OK) {
		if (hostWindow->LockLooper())
			hostWindow->Quit();
		return false;
	}

	BRect printableRect = printJob.PrintableRect();
	float printableWidth = printableRect.Width();
	float printableHeight = printableRect.Height();

	PrintPageView* view = new PrintPageView(document, printableWidth);
	if (hostWindow->Lock()) {
		hostWindow->AddChild(view);
		hostWindow->Unlock();
	} else {
		delete view;
		if (hostWindow->LockLooper())
			hostWindow->Quit();
		return false;
	}

	std::vector<float> pageStartY;
	ComputePageBreaks(document->CountParagraphs(), view, printableHeight,
		forcedPageBreaksBeforeParagraph, pageStartY);
	int32 totalPages = (int32)pageStartY.size();

	// Respect the page range the user actually chose in ConfigJob()'s own
	// panel -- FirstPage()/LastPage() default to 0/LONG_MAX ("everything")
	// when the user didn't narrow it, which the clamps below already
	// collapse to the full [1, totalPages] range.
	int32 firstPage = printJob.FirstPage();
	if (firstPage < 1)
		firstPage = 1;
	int32 lastPage = printJob.LastPage();
	if (lastPage < 1 || lastPage > totalPages)
		lastPage = totalPages;

	printJob.BeginJob();
	float totalHeight = view->ContentHeight();
	for (int32 page = 1; page <= lastPage && printJob.CanContinue(); page++) {
		if (page < firstPage)
			continue;
		float top = pageStartY[page - 1];
		float bottom = (page < totalPages) ? pageStartY[page] : totalHeight;
		printJob.DrawView(view, BRect(0.0f, top, printableWidth, bottom),
			BPoint(0.0f, 0.0f));
		printJob.SpoolPage();
	}
	printJob.CommitJob();

	if (hostWindow->LockLooper())
		hostWindow->Quit();

	return true;
}


bool
PrintViewFittedToPage(BWindow* parentWindow, const PageViewFactory& makeView,
	const char* jobName)
{
	BRect frame = parentWindow != NULL
		? parentWindow->Frame() : BRect(50.0f, 50.0f, 550.0f, 750.0f);
	BWindow* hostWindow = new BWindow(frame, "", B_TITLED_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL, B_ASYNCHRONOUS_CONTROLS);
	hostWindow->Show();
	hostWindow->Hide();

	BPrintJob printJob(jobName);
	if (printJob.ConfigPage() != B_OK || printJob.ConfigJob() != B_OK) {
		if (hostWindow->LockLooper())
			hostWindow->Quit();
		return false;
	}

	BRect printableRect = printJob.PrintableRect();
	float pageWidth = printableRect.Width();
	float pageHeight = printableRect.Height();

	float contentHeight = pageHeight;
	BView* view = makeView(pageWidth, pageHeight, &contentHeight);
	if (view == NULL || !hostWindow->Lock()) {
		delete view;
		if (hostWindow->LockLooper())
			hostWindow->Quit();
		return false;
	}
	hostWindow->AddChild(view);
	hostWindow->Unlock();

	int32 totalPages = std::max((int32)1,
		(int32)ceilf(contentHeight / pageHeight));
	int32 firstPage = printJob.FirstPage();
	if (firstPage < 1)
		firstPage = 1;
	int32 lastPage = printJob.LastPage();
	if (lastPage < 1 || lastPage > totalPages)
		lastPage = totalPages;

	printJob.BeginJob();
	for (int32 page = firstPage; page <= lastPage && printJob.CanContinue();
			page++) {
		float top = (page - 1) * pageHeight;
		float bottom = std::min(contentHeight, top + pageHeight);
		printJob.DrawView(view, BRect(0.0f, top, pageWidth, bottom),
			BPoint(0.0f, 0.0f));
		printJob.SpoolPage();
	}
	printJob.CommitJob();

	if (hostWindow->LockLooper())
		hostWindow->Quit();
	return true;
}

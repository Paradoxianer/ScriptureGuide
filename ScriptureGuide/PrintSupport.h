/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef PRINT_SUPPORT_H
#define PRINT_SUPPORT_H

#include <functional>
#include <vector>

#include <GraphicsDefs.h>

#include "textview/TextDocument.h"

class BView;
class BWindow;

// #113/#87: the one BPrintJob-driven print path shared by "print the
// reading pane" and "print a verse list" -- neither needs anything beyond
// a TextDocument (headings + body paragraphs) and a job name. See
// LogosMainWindow.cpp's _PrintReadingPane() and LogosVerseListWindow.cpp's
// _PrintList() for the two callers, each building their own TextDocument
// via AppendPrintHeading()/AppendPrintBodyLine() below before handing it
// here.

// A bold section heading (module name, chapter/reference, or a verse
// list's own name) -- used identically by both callers, so it lives here
// rather than being duplicated at each call site.
void AppendPrintHeading(TextDocumentRef document, const BString& heading);

// One plain paragraph of body text (a verse, or a bare reference) --
// same reasoning as AppendPrintHeading(). `backgroundColor`, when given,
// paints the whole line's background with it -- Options > Include
// Highlight Colours When Printing's own building block (LogosMainWindow.
// cpp's _PrintReadingPane()): one colour for the whole verse's line, not
// the exact on-screen character range, since a print line is already a
// single paragraph/TextSpan pair -- splitting one line into several
// differently-coloured spans for sub-verse precision is explicitly not
// attempted here.
void AppendPrintBodyLine(TextDocumentRef document, const BString& line,
	const rgb_color* backgroundColor = NULL);

// Runs a full print job for `document`: shows the real Print Server
// page-setup and job-setup panels (ConfigPage()/ConfigJob()), paginates by
// paragraph boundary (a paragraph never splits across a page -- see
// scriptureguide-quirks.md for the general BWindow-loop-needs-Show()
// caveat this also relies on), and only spools the pages the user actually
// asked for via ConfigJob()'s own page-range field. `parentWindow` only
// seeds where the (never shown) hosting window is created; printing never
// touches anything on screen. Returns false if the user canceled at either
// panel -- not an error, the caller does nothing further in that case.
//
// `forcedPageBreaksBeforeParagraph`: paragraph indices (into `document`)
// that must start a fresh page regardless of how much room is left on the
// current one -- Options > New Page per Translation When Printing's own
// building block: the caller (SGMainWindow::_PrintReadingPane()) records
// the paragraph index of each translation's own heading, before appending
// it, whenever that option is on. Empty (the default) means purely
// natural, height-driven pagination, same as before this option existed.
bool PrintTextDocument(BWindow* parentWindow, TextDocumentRef document,
	const char* jobName,
	const std::vector<int32>& forcedPageBreaksBeforeParagraph
		= std::vector<int32>());

// The Hit Chart's print path. Shows the usual Print Server panels first, then
// asks `makeView` for a view laid out to the page's real printable width.
// The view reports its own full height through `*contentHeight`; anything
// taller than one page is tiled top-to-bottom over as many pages as it needs,
// so content can size itself for the real page width instead of being shrunk
// afterwards. Returns false if the user canceled at either panel.
typedef std::function<BView*(float pageWidth, float pageHeight,
	float* contentHeight)>
	PageViewFactory;

bool PrintViewFittedToPage(BWindow* parentWindow,
	const PageViewFactory& makeView, const char* jobName);

#endif // PRINT_SUPPORT_H

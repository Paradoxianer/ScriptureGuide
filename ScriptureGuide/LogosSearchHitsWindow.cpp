/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */

#include "LogosSearchHitsWindow.h"

#include <algorithm>
#include <map>

#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <StringView.h>
#include <View.h>
#include <Window.h>

#include "constants.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SearchHitsWindow"


// Six broad genre groups across the 66-book canonical order
// GetBookNames()/GetBookChapterCounts() already enumerate in (5 Law +
// 12 History + 5 Poetry/Wisdom + 17 Prophets [major and minor combined]
// + 5 Gospels/Acts + 22 Epistles/Revelation = 66) -- matched by
// POSITION in that list, not by name, so this works under any locale
// (a German system's book names are already localized by the time
// they reach here; matching English names against them would silently
// group nothing).
static int32
BookGroup(int32 bookIndex)
{
	if (bookIndex < 5)
		return 0;	// Law
	if (bookIndex < 17)
		return 1;	// History
	if (bookIndex < 22)
		return 2;	// Poetry / Wisdom
	if (bookIndex < 39)
		return 3;	// Prophets
	if (bookIndex < 44)
		return 4;	// Gospels / Acts
	return 5;		// Epistles / Revelation
}


static rgb_color
BookGroupColor(int32 group)
{
	static const rgb_color kColors[6] = {
		{ 216, 191, 230, 255 },	// Law -- light purple
		{ 191, 209, 232, 255 },	// History -- light blue
		{ 197, 224, 197, 255 },	// Poetry/Wisdom -- light green
		{ 232, 219, 180, 255 },	// Prophets -- light tan
		{ 184, 224, 224, 255 },	// Gospels/Acts -- light teal
		{ 232, 197, 197, 255 }	// Epistles/Revelation -- light red
	};
	return kColors[group % 6];
}


// A single book's own colour -- the same genre hue BookGroupColor()
// already gives its whole group, shaded darker the further into the
// group this book falls, so books sharing a genre stay distinguishable
// from each other instead of all reading as one indistinct block. Used
// for both the chapter grid's per-row swatch and the treemap's fill, so
// a book reads as the same colour in both places -- the two views were
// only ever tied together by genre before, which does not help pick out
// one specific book from among several its own colour shares a hue with.
static rgb_color
BookColor(int32 bookIndex)
{
	static const int32 kGroupStart[6] = { 0, 5, 17, 22, 39, 44 };
	static const int32 kGroupSize[6] = { 5, 12, 5, 17, 5, 22 };

	int32 group = BookGroup(bookIndex);
	rgb_color base = BookGroupColor(group);

	int32 within = bookIndex - kGroupStart[group];
	int32 size = kGroupSize[group];
	float t = size > 1 ? (float)within / (float)(size - 1) : 0.0f;
	float factor = 1.0f - 0.35f * t;	// 1.0 (first in group) .. 0.65 (last)

	rgb_color color;
	color.red = (uint8)(base.red * factor);
	color.green = (uint8)(base.green * factor);
	color.blue = (uint8)(base.blue * factor);
	color.alpha = 255;
	return color;
}


// A hit chapter's colour scales with how many hits it actually has --
// otherwise a chapter with one hit and a chapter with a dozen looked
// identical, and there was no way to tell a single red square apart
// from one standing in for several. Saturates at 4 hits rather than the
// dataset's true maximum so one unusually dense chapter (a common word
// can turn up 10+ times in one chapter) doesn't wash out every other
// chapter's shade to near-white by comparison.
static rgb_color
HitColorForCount(int32 count)
{
	static const rgb_color kLight = { 245, 200, 175, 255 };
	static const rgb_color kStrong = { 200, 60, 30, 255 };

	float t = count > 4 ? 1.0f : (count <= 1 ? 0.0f : (count - 1) / 3.0f);
	rgb_color color;
	color.red = (uint8)(kLight.red + (kStrong.red - kLight.red) * t);
	color.green = (uint8)(kLight.green + (kStrong.green - kLight.green) * t);
	color.blue = (uint8)(kLight.blue + (kStrong.blue - kLight.blue) * t);
	color.alpha = 255;
	return color;
}


// Width of the per-book colour swatch ChapterGridView draws next to each
// row's label (see BookColor()).
static const float kSwatchWidth = 10.0f;


// One row per book (see GetBookChapterCounts()), one small square per
// chapter of that book -- coloured by genre group (see BookGroupColor())
// normally, or a count-scaled shade of red (see HitColorForCount()) if
// this exact book+chapter has at least one hit. Hovering a hit square
// shows its first hit's verse text as a native tool tip; clicking it
// posts SEARCHHITS_JUMP with that hit's reference to the window itself.
class ChapterGridView : public BView {
public:
	ChapterGridView(const char* name, const std::vector<SearchHit>& hits)
		:
		// B_SUPPORTS_LAYOUT is what makes the enclosing BScrollView use
		// GetPreferredSize() to drive the scrollbars' range directly
		// (BScrollView::FrameResized()), instead of its default
		// behaviour of forcibly ResizeTo()-ing this view to match its
		// own viewport on every layout pass (BScrollView::DoLayout()) --
		// confirmed by reading Haiku's own ScrollView.cpp. Without it,
		// there was never a real 66-book-tall (or 150-chapter-wide)
		// canvas for a scrollbar to reveal by scrolling, no matter what
		// MinSize()/MaxSize() below advertised.
		BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_SUPPORTS_LAYOUT),
		fBooks(GetBookChapterCounts())
	{
		SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);

		for (size_t i = 0; i < fBooks.size(); i++)
			fBookIndex[fBooks[i].book] = (int32)i;

		for (size_t i = 0; i < hits.size(); i++) {
			BString key;
			key << hits[i].book << "|" << hits[i].chapter;
			fHits[key].push_back(&hits[i]);
		}
		// `hits` outlives this view (owned by SGSearchHitsWindow, which
		// owns this view too, and never mutates it after construction)
		// -- storing pointers into it directly is safe and avoids a
		// second full copy of every hit's rendered verse text.

		font_height fh;
		GetFontHeight(&fh);
		fRowHeight = ceilf(fh.ascent + fh.descent + fh.leading) + 2.0f;
		fSquareSize = fRowHeight - 2.0f;
		fLabelWidth = 0.0f;
		for (size_t i = 0; i < fBooks.size(); i++) {
			float width = StringWidth(fBooks[i].book.String());
			if (width > fLabelWidth)
				fLabelWidth = width;
		}
		// kSwatchWidth + two 4px gaps -- room for the per-book colour
		// swatch (see Draw()) between the left edge and the book name.
		fLabelWidth += kSwatchWidth + 16.0f;

		int32 maxChapters = 0;
		for (size_t i = 0; i < fBooks.size(); i++) {
			if (fBooks[i].chapters > maxChapters)
				maxChapters = fBooks[i].chapters;
		}
		fMaxChapters = maxChapters;
	}

	virtual void GetPreferredSize(float* _width, float* _height)
	{
		if (_width != NULL) {
			*_width = fLabelWidth
				+ fMaxChapters * fSquareSize + 8.0f;
		}
		if (_height != NULL)
			*_height = fBooks.size() * fRowHeight + 8.0f;
	}

	// Deliberately modest on BOTH axes -- this is what the enclosing
	// BScrollView tells the split it needs, and what actually decides
	// how big the scroll view's own on-screen frame becomes. Pinning
	// width to the full content width here (150+ chapters wide) was the
	// actual bug: the scroll view's own frame then had to BE that
	// width, extending 2600+px past the right edge of an ordinary
	// window instead of staying window-sized with a working horizontal
	// scrollbar inside it -- confirmed live (BRect(0,0,2704,218), a
	// window only 520px wide). GetPreferredSize() (the real content
	// size) is what B_SUPPORTS_LAYOUT above needs for the scrollbars'
	// actual range; it has nothing to do with what these two report.
	virtual BSize MinSize()
	{
		return BSize(200.0f, 150.0f);
	}

	virtual BSize MaxSize()
	{
		return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
	}

	virtual void Draw(BRect updateRect)
	{
		SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		FillRect(updateRect, B_SOLID_LOW);

		for (size_t row = 0; row < fBooks.size(); row++) {
			float top = 4.0f + row * fRowHeight;
			if (top > updateRect.bottom)
				break;
			if (top + fRowHeight < updateRect.top)
				continue;

			font_height fh;
			GetFontHeight(&fh);
			// The swatch ties this row to its book's own treemap
			// rectangle below (see BookColor()) -- genre colour alone
			// left no way to tell, say, Job's row from Psalms' at a
			// glance, since both share the Poetry/Wisdom hue.
			SetHighColor(BookColor((int32)row));
			FillRect(BRect(4.0f, top + 1.0f,
				4.0f + kSwatchWidth, top + fRowHeight - 3.0f));
			SetHighColor(0, 0, 0);
			DrawString(fBooks[row].book.String(),
				BPoint(8.0f + kSwatchWidth, top + fh.ascent));

			int32 group = BookGroup((int32)row);
			rgb_color plain = BookGroupColor(group);
			for (int32 chapter = 1; chapter <= fBooks[row].chapters;
					chapter++) {
				BRect square = _SquareFor((int32)row, chapter);
				const std::vector<const SearchHit*>* hits
					= _HitsFor(fBooks[row].book, chapter);
				bool hasHit = hits != NULL && !hits->empty();
				SetHighColor(hasHit
					? HitColorForCount((int32)hits->size()) : plain);
				FillRect(square);
				// A thin border between chapters -- without it, several
				// consecutive hit chapters read as one solid block with
				// no way to tell how many chapters (or, combined with
				// the colour above, how many hits) it actually covers.
				SetHighColor(120, 120, 120);
				StrokeRect(square);
			}
		}
	}

						// `hits` must outlive this view -- same
						// requirement and reasoning as the constructor's
						// own comment above (SGSearchHitsWindow::SetHits()
						// reassigns its own fHits member first, then calls
						// this with that now-updated member).
	void SetHits(const std::vector<SearchHit>& hits)
	{
		fHits.clear();
		for (size_t i = 0; i < hits.size(); i++) {
			BString key;
			key << hits[i].book << "|" << hits[i].chapter;
			fHits[key].push_back(&hits[i]);
		}
		Invalidate();
	}

	virtual void MouseMoved(BPoint where, uint32 code,
		const BMessage* dragMessage)
	{
		int32 row, chapter;
		if (_SquareAt(where, &row, &chapter)) {
			const std::vector<const SearchHit*>* hits
				= _HitsFor(fBooks[row].book, chapter);
			if (hits != NULL && !hits->empty()) {
				BString tip((*hits)[0]->reference);
				tip << ": " << (*hits)[0]->verseText;
				SetToolTip(tip.String());
				return;
			}
		}
		SetToolTip((BToolTip*)NULL);
	}

	virtual void MouseDown(BPoint where)
	{
		int32 row, chapter;
		if (_SquareAt(where, &row, &chapter)) {
			const std::vector<const SearchHit*>* hits
				= _HitsFor(fBooks[row].book, chapter);
			if (hits != NULL && !hits->empty()) {
				BMessage jump(SEARCHHITS_JUMP);
				jump.AddString("key", (*hits)[0]->reference);
				if (Window() != NULL)
					Window()->PostMessage(&jump);
				return;
			}
		}
		BView::MouseDown(where);
	}

private:
	BRect _SquareFor(int32 row, int32 chapter) const
	{
		float top = 4.0f + row * fRowHeight;
		float left = fLabelWidth + (chapter - 1) * fSquareSize;
		return BRect(left, top, left + fSquareSize - 1,
			top + fSquareSize - 1);
	}

	bool _SquareAt(BPoint where, int32* _row, int32* _chapter) const
	{
		if (where.x < fLabelWidth)
			return false;
		int32 row = (int32)((where.y - 4.0f) / fRowHeight);
		if (row < 0 || (size_t)row >= fBooks.size())
			return false;
		int32 chapter = (int32)((where.x - fLabelWidth) / fSquareSize) + 1;
		if (chapter < 1 || chapter > fBooks[row].chapters)
			return false;
		*_row = row;
		*_chapter = chapter;
		return true;
	}

	const std::vector<const SearchHit*>* _HitsFor(const BString& book,
		int32 chapter) const
	{
		BString key;
		key << book << "|" << chapter;
		std::map<BString, std::vector<const SearchHit*> >::const_iterator
			it = fHits.find(key);
		return it != fHits.end() ? &it->second : NULL;
	}

	std::vector<BookChapterCount>	fBooks;
	std::map<BString, int32>		fBookIndex;
	std::map<BString, std::vector<const SearchHit*> >	fHits;
	float	fRowHeight;
	float	fSquareSize;
	float	fLabelWidth;
	int32	fMaxChapters;
};


// One rectangle per book that has at least one hit, area-proportional
// to that book's hit count, coloured by the same genre grouping the
// chapter grid uses. A plain slice-and-dice layout (alternating
// horizontal/vertical strips, each sized by its share of the
// remaining total) rather than a fully squarified treemap -- less
// visually optimal for very uneven data (can produce a thin sliver for
// a book with very few hits next to one with many), but a handful of
// lines instead of the Bruls/Huizing/van Wijk algorithm, and always
// geometrically correct.
class TreemapView : public BView {
public:
	TreemapView(const char* name, const std::vector<SearchHit>& hits)
		:
		BView(name, B_WILL_DRAW | B_FRAME_EVENTS)
	{
		SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		_BuildItems(hits);
	}

	// Fills whatever space the split (see _BuildGUI()) actually gives
	// it, same as before -- an attempt to instead give this an intrinsic,
	// scrollable size that grows with the book count (mirroring
	// ChapterGridView's own fixed-size/scrollbar approach) was tried and
	// reverted: a slice-and-dice treemap wants comparable width and
	// height to produce sensible rectangles, and forcing the height to
	// grow while the width stayed fixed produced an extremely tall,
	// narrow canvas where the first one or two (of what can be 60+ for a
	// common word) items swallowed the entire visible viewport, with
	// every other item pushed out of view below rather than merely
	// smaller -- confirmed live via a debug dump of the actual item
	// list against what the window showed. A dense mosaic of small
	// rectangles (what filling the available space instead produces)
	// is the normal, useful treemap outcome for a many-book result.
	virtual void FrameResized(float width, float height)
	{
		BView::FrameResized(width, height);
		_Layout();
		Invalidate();
	}

	void SetHits(const std::vector<SearchHit>& hits)
	{
		_BuildItems(hits);
		_Layout();
		Invalidate();
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		_Layout();
	}

	virtual void Draw(BRect updateRect)
	{
		SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		FillRect(updateRect, B_SOLID_LOW);

		for (size_t i = 0; i < fItems.size(); i++) {
			if (!fItems[i].rect.Intersects(updateRect))
				continue;
			// Drawn slightly smaller than the slot _Layout() actually
			// gave this item -- a visible gap between neighbours, not
			// just a 1px line, since adjacent same-genre-group books
			// (light, muted colours by design -- see BookGroupColor())
			// otherwise read as one undivided block. MouseDown() still
			// hits-test the full, ungapped rect, so the click target
			// stays exactly as generous as before.
			BRect visual = fItems[i].rect.InsetByCopy(2.0f, 2.0f);
			// The same per-book colour as this book's row swatch in the
			// chapter grid (see BookColor()) -- ties a treemap rectangle
			// back to a specific grid row instead of just its genre.
			SetHighColor(fItems[i].bookIndex >= 0
				? BookColor(fItems[i].bookIndex) : BookGroupColor(0));
			FillRect(visual);
			SetHighColor(60, 60, 60);
			SetPenSize(1.5f);
			StrokeRect(visual);
			SetPenSize(1.0f);

			BString label(fItems[i].book);
			label << " (" << fItems[i].count << ")";
			if (StringWidth(label.String()) < visual.Width() - 4
				&& visual.Height() > 14) {
				font_height fh;
				GetFontHeight(&fh);
				DrawString(label.String(),
					visual.LeftTop() + BPoint(3.0f, fh.ascent + 2.0f));
			}
		}
	}

	virtual void MouseDown(BPoint where)
	{
		for (size_t i = 0; i < fItems.size(); i++) {
			if (!fItems[i].rect.Contains(where))
				continue;
			BMessage jump(SEARCHHITS_JUMP);
			// The bare book name alone (what this used to send) isn't a
			// resolvable reference -- confirmed live: SGMainWindow's
			// JumpToKey()/BookFromKey() silently misparsed it and landed
			// on Revelation (the last book) instead of the book actually
			// clicked. This book's first hit's own reference is a real,
			// already-valid key, same as what the chapter grid sends.
			jump.AddString("key", fItems[i].firstReference);
			if (Window() != NULL)
				Window()->PostMessage(&jump);
			return;
		}
		BView::MouseDown(where);
	}

private:
	struct Item {
		BString	book;
		BString	firstReference;
		int32	count;
		// Canonical 0-65 book index (see GetBookNames()), -1 if this
		// book's name did not match any of them -- used to look up this
		// item's own colour (see BookColor()), not just its genre group.
		int32	bookIndex;
		BRect	rect;

		static bool MoreFirst(const Item& a, const Item& b)
		{
			return a.count > b.count;
		}
	};

	void _BuildItems(const std::vector<SearchHit>& hits)
	{
		std::vector<const char*> bookNames = GetBookNames();
		std::map<BString, int32> bookIndex;
		for (size_t i = 0; i < bookNames.size(); i++)
			bookIndex[bookNames[i]] = (int32)i;

		std::map<BString, int32> countByBook;
		std::map<BString, BString> firstReferenceByBook;
		std::vector<BString> order;
		for (size_t i = 0; i < hits.size(); i++) {
			if (countByBook.find(hits[i].book) == countByBook.end()) {
				order.push_back(hits[i].book);
				firstReferenceByBook[hits[i].book] = hits[i].reference;
			}
			countByBook[hits[i].book]++;
		}

		fItems.clear();
		for (size_t i = 0; i < order.size(); i++) {
			Item item;
			item.book = order[i];
			item.firstReference = firstReferenceByBook[order[i]];
			item.count = countByBook[order[i]];
			std::map<BString, int32>::iterator idxIt
				= bookIndex.find(order[i]);
			item.bookIndex = idxIt != bookIndex.end() ? idxIt->second : -1;
			fItems.push_back(item);
		}
		std::sort(fItems.begin(), fItems.end(), &Item::MoreFirst);
	}

	void _Layout()
	{
		int32 total = 0;
		for (size_t i = 0; i < fItems.size(); i++)
			total += fItems[i].count;
		if (total <= 0)
			return;

		BRect bounds = Bounds();
		bool horizontal = true;
		// Alternates axis every item -- a plain, always-terminating
		// slice-and-dice variant (each remaining item gets its exact
		// share of whatever space is left, alternating which edge it's
		// sliced from) rather than one single-axis pass, which would
		// put every item in one row/column regardless of how many
		// there are.
		BRect remaining = bounds;
		int32 remainingTotal = total;
		for (size_t i = 0; i < fItems.size(); i++) {
			float fraction = (float)fItems[i].count / remainingTotal;
			if (horizontal) {
				float width = remaining.Width() * fraction;
				fItems[i].rect = BRect(remaining.left, remaining.top,
					remaining.left + width, remaining.bottom);
				remaining.left += width;
			} else {
				float height = remaining.Height() * fraction;
				fItems[i].rect = BRect(remaining.left, remaining.top,
					remaining.right, remaining.top + height);
				remaining.top += height;
			}
			remainingTotal -= fItems[i].count;
			horizontal = !horizontal;
		}
	}

	std::vector<Item>	fItems;
};


SGSearchHitsWindow::SGSearchHitsWindow(BRect frame,
	const std::vector<SearchHit>& hits, const char* title,
	BMessenger* owner)
	:
	BWindow(frame, B_TRANSLATE("Search Hits"), B_TITLED_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL, B_ASYNCHRONOUS_CONTROLS),
	fOwner(owner),
	fHits(hits),
	fTitle(title)
{
	_BuildGUI();
}


SGSearchHitsWindow::~SGSearchHitsWindow()
{
	fOwner->SendMessage(SEARCHHITS_QUIT);
	delete fOwner;
}


bool
SGSearchHitsWindow::QuitRequested()
{
	Hide();
	return false;
}


void
SGSearchHitsWindow::SetHits(const std::vector<SearchHit>& hits,
	const char* title)
{
	// Called directly (a plain C++ call, not a posted BMessage) from
	// SGSearchWindow's own thread -- crashed the whole team with Haiku's
	// "Looper must be locked" debugger call the first time a second
	// search reused an already-shown hits window, since touching this
	// window's views from another window's thread without locking this
	// one first is exactly what that assertion catches. The first-ever
	// build (the constructor, from the same calling thread) never hit
	// this: a BWindow is constructed already locked for its creating
	// thread, and Show() is what first releases that -- SetHits() runs
	// well after that point, once the window is running its own looper.
	if (!Lock())
		return;
	fHits = hits;
	fTitle = title;
	fTitleView->SetText(fTitle.String());
	fGridView->SetHits(fHits);
	fTreemapView->SetHits(fHits);
	Unlock();
}


void
SGSearchHitsWindow::_BuildGUI()
{
	fTitleView = new BStringView("searchHitsTitle", fTitle.String());
	fTitleView->SetFont(be_bold_font);

	fGridView = new ChapterGridView("searchHitsGrid", fHits);
	// Both scrollbars, not just vertical -- a book with many chapters
	// (Psalms' 150) needs far more width than this window's frame ever
	// gives it (fMaxChapters * fSquareSize easily exceeds 2000px), and
	// with only a vertical scrollbar every chapter past whatever fits in
	// the window's own width was simply unreachable.
	BScrollView* gridScroll = new BScrollView("searchHitsGridScroll",
		fGridView, 0, true, true);

	fTreemapView = new TreemapView("searchHitsTreemap", fHits);
	fTreemapView->SetExplicitMinSize(BSize(B_SIZE_UNSET, 150.0f));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(fTitleView)
		.AddSplit(B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			.Add(gridScroll, 2.0f)
			.Add(fTreemapView, 1.0f)
		.End()
	.End();

	SetSizeLimits(400.0f, 4000.0f, 300.0f, 4000.0f);
}


void
SGSearchHitsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case SEARCHHITS_JUMP:
		{
			BString key;
			if (message->FindString("key", &key) == B_OK && fOwner != NULL) {
				BMessage jump(SG_BIBLE);
				jump.AddString("key", key);
				fOwner->SendMessage(&jump);
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}

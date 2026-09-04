/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef HIGHLIGHT_PALETTE_H
#define HIGHLIGHT_PALETTE_H

#include <Catalog.h>
#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <Menu.h>
#include <MenuItem.h>
#include <String.h>

#include "../constants.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ParallelBibleView"


// #44: the highlight colours themselves. This used to also define a
// borderless colour-bar window that appeared automatically at the end
// of a drag, beside the ordinary right-click menu. Two different things
// opening on the same selection was the confusing part, so the bar is
// gone and both routes now open the one popup menu built in
// ParallelBibleView::_ShowSelectionMenu(); what is left here is the
// palette that menu reads and the item class that draws it.
struct HighlightPalette {
	struct Entry {
		const char*	name;
		rgb_color	color;
	};

	// Muted on purpose -- these sit behind body text that still has to
	// read comfortably, in both light and dark appearance, so they are
	// closer to a wash than to a marker pen.
	static const Entry* Palette(int32& outCount)
	{
		static const Entry sPalette[] = {
			{ "Green",  { 0xc8, 0xe6, 0xc9, 255 } },
			{ "Yellow", { 0xf7, 0xec, 0xb3, 255 } },
			{ "Red",    { 0xf5, 0xc6, 0xc6, 255 } },
			{ "Blue",   { 0xc5, 0xdd, 0xf2, 255 } },
			{ "Purple", { 0xdc, 0xcb, 0xe8, 255 } },
			// Deliberately NOT a grey: the text editor already paints its
			// own selection in one, so a grey highlight and a selected
			// passage are indistinguishable at a glance.
			{ "Orange", { 0xf7, 0xd9, 0xb0, 255 } }
		};
		outCount = (int32)(sizeof(sPalette) / sizeof(sPalette[0]));
		return sPalette;
	}
};


// #44: swatch geometry for the colour items below.
static const float kHighlightSwatchWidth = 16.0f;
static const float kHighlightSwatchGap = 6.0f;


// #44: a menu item that shows the colour it stands for. Lets the
// highlight colours live in the ordinary right-click menu -- one menu
// for everything that acts on a selection -- instead of needing a
// second, differently-styled window beside it.
class HighlightColorMenuItem : public BMenuItem {
public:
	HighlightColorMenuItem(const char* label, rgb_color color,
		BMessage* message)
		:
		BMenuItem(label, message),
		fColor(color)
	{
	}

	virtual void GetContentSize(float* width, float* height)
	{
		BMenuItem::GetContentSize(width, height);
		if (width != NULL)
			*width += kHighlightSwatchWidth + kHighlightSwatchGap;
	}

	virtual void DrawContent()
	{
		BMenu* menu = Menu();
		if (menu == NULL) {
			BMenuItem::DrawContent();
			return;
		}

		BPoint where = ContentLocation();
		float height = 0.0f;
		float width = 0.0f;
		BMenuItem::GetContentSize(&width, &height);

		BRect swatch(where.x, where.y + 2.0f,
			where.x + kHighlightSwatchWidth, where.y + height - 3.0f);
		rgb_color previous = menu->HighColor();
		menu->SetHighColor(fColor);
		menu->FillRect(swatch);
		menu->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_2_TINT));
		menu->StrokeRect(swatch);
		menu->SetHighColor(previous);

		menu->MovePenTo(where.x + kHighlightSwatchWidth + kHighlightSwatchGap,
			menu->PenLocation().y);
		BMenuItem::DrawContent();
	}

private:
	rgb_color			fColor;
};

#endif // HIGHLIGHT_PALETTE_H

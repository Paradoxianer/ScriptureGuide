/*
 * Copyright 2026, ScriptureGuide contributors.
 * All rights reserved. Distributed under the terms of the GPL v2 license.
 */
#ifndef HIGHLIGHT_PALETTE_H
#define HIGHLIGHT_PALETTE_H

#include <Catalog.h>
#include <ControlLook.h>
#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Screen.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include "../constants.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ParallelBibleView"


// #44: the little colour bar that appears next to a fresh drag-
// selection. Deliberately a borderless window rather than a popup menu:
// a menu grabs focus and the mouse when the user did not ask for one,
// and the automatic appearance here is exactly the case where that
// feels wrong.
//
// B_AVOID_FOCUS is the load-bearing flag. Without it this window takes
// focus the moment it opens, the reading column loses its selection,
// and the colour the user is about to click has nothing left to apply
// to. It also means the window never gets key events, hence the
// explicit dismissal paths below.
class HighlightPaletteWindow : public BWindow {
public:
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

	HighlightPaletteWindow(BPoint where, BMessenger target)
		:
		BWindow(BRect(where.x, where.y, where.x + 10, where.y + 10), "",
			B_NO_BORDER_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL,
			B_AVOID_FOCUS | B_NOT_RESIZABLE | B_NOT_ZOOMABLE
				| B_NOT_MINIMIZABLE | B_ASYNCHRONOUS_CONTROLS),
		fTarget(target)
	{
		const float kSwatch = 20.0f;
		const float kGap = 4.0f;

		int32 count = 0;
		const Entry* palette = Palette(count);

		// Two extra cells on the end: "remove the highlight here", so
		// undoing a mis-click needs no different gesture than making
		// one, and a labelled one that opens the collection tree.
		//
		// This one window is what BOTH routes show -- the automatic
		// popup after a drag and a right-click on a selection -- so
		// there is one implementation and one look, rather than a
		// window for one and a menu for the other. The colours stay
		// immediately visible rather than sitting behind an entry:
		// highlighting is the common case right after selecting, and a
		// step in front of it would be paid on every single use.
		BString listLabel(B_TRANSLATE("To Verse List" B_UTF8_ELLIPSIS));
		float listWidth = be_plain_font->StringWidth(listLabel.String())
			+ 12.0f;
		float width = kGap + (count + 1) * (kSwatch + kGap) + listWidth
			+ kGap;
		float height = kGap * 2 + kSwatch;
		ResizeTo(width, height);

		BView* backdrop = new BView(Bounds(), "backdrop", B_FOLLOW_ALL,
			B_WILL_DRAW);
		backdrop->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		AddChild(backdrop);

		for (int32 i = 0; i < count; i++) {
			BRect frame(kGap + i * (kSwatch + kGap), kGap, 0, 0);
			frame.right = frame.left + kSwatch;
			frame.bottom = frame.top + kSwatch;

			BMessage* message = new BMessage(PARALLEL_HIGHLIGHT_APPLY);
			message->AddInt32("color", _PackColor(palette[i].color));
			message->AddString("name", palette[i].name);
			backdrop->AddChild(new SwatchView(frame, palette[i].color,
				message, this));
		}

		// Painted in the document background rather than the panel
		// colour it sits on: as a panel-coloured cell on a panel-coloured
		// bar it was effectively invisible, which is no use for the one
		// control that undoes a mistake.
		BRect removeFrame(kGap + count * (kSwatch + kGap), kGap, 0, 0);
		removeFrame.right = removeFrame.left + kSwatch;
		removeFrame.bottom = removeFrame.top + kSwatch;
		backdrop->AddChild(new SwatchView(removeFrame,
			ui_color(B_DOCUMENT_BACKGROUND_COLOR),
			new BMessage(PARALLEL_HIGHLIGHT_APPLY), this, true));

		BRect moreFrame(kGap + (count + 1) * (kSwatch + kGap), kGap, 0, 0);
		moreFrame.right = moreFrame.left + listWidth;
		moreFrame.bottom = moreFrame.top + kSwatch;
		backdrop->AddChild(new SwatchView(moreFrame,
			ui_color(B_DOCUMENT_BACKGROUND_COLOR),
			new BMessage(PARALLEL_HIGHLIGHT_MORE), this, false, true,
			listLabel.String()));

		// Keep the bar on screen when the selection ends near an edge.
		BScreen screen;
		BRect screenFrame = screen.Frame();
		BPoint position = where;
		if (position.x + width > screenFrame.right)
			position.x = screenFrame.right - width;
		if (position.y + height > screenFrame.bottom)
			position.y = where.y - height - 4.0f;
		MoveTo(position);
	}

	void Chosen(BMessage* message)
	{
		fTarget.SendMessage(message);
		PostMessage(B_QUIT_REQUESTED);
	}

	// B_AVOID_FOCUS means no key events and no deactivation to hook, so
	// dismissal rides on the mouse leaving instead: anything that is not
	// a click on a swatch closes the bar and highlights nothing, which
	// is the "click elsewhere and the selection stays free for its other
	// uses" behaviour this was asked to have.
	virtual void WindowActivated(bool active)
	{
		if (!active)
			PostMessage(B_QUIT_REQUESTED);
	}

private:
	static int32 _PackColor(rgb_color color)
	{
		return ((int32)color.red << 16) | ((int32)color.green << 8)
			| (int32)color.blue;
	}

	class SwatchView : public BView {
	public:
		SwatchView(BRect frame, rgb_color color, BMessage* message,
			HighlightPaletteWindow* owner, bool isRemove = false,
			bool isMore = false, const char* label = NULL)
			:
			BView(frame, "swatch", B_FOLLOW_NONE, B_WILL_DRAW),
			fColor(color),
			fMessage(message),
			fOwner(owner),
			fIsRemove(isRemove),
			fIsMore(isMore),
			fLabel(label != NULL ? label : ""),
			fPressed(false)
		{
		}

		virtual ~SwatchView() { delete fMessage; }

		virtual void Draw(BRect updateRect)
		{
			BRect bounds(Bounds());
			SetHighColor(fColor);
			FillRect(bounds);
			SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
				fPressed ? B_DARKEN_4_TINT : B_DARKEN_2_TINT));
			StrokeRect(bounds);
			if (fPressed)
				StrokeRect(bounds.InsetByCopy(1, 1));
			if (fIsRemove) {
				// A plain diagonal cross reads as "none" without needing
				// a glyph or a translated label in a 20px cell. Drawn in
				// a strong colour, not the border tint, so it is legible
				// at this size.
				SetHighColor(200, 60, 60);
				SetPenSize(2.0f);
				StrokeLine(bounds.LeftTop() + BPoint(5, 5),
					bounds.RightBottom() - BPoint(5, 5));
				StrokeLine(bounds.LeftBottom() + BPoint(5, -5),
					bounds.RightTop() - BPoint(5, -5));
				SetPenSize(1.0f);
			}
			if (fIsMore) {
				SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
				SetLowColor(fColor);
				font_height height;
				GetFontHeight(&height);
				float baseline = bounds.top
					+ (bounds.Height() + height.ascent - height.descent) / 2.0f;
				DrawString(fLabel.String(),
					BPoint(bounds.left + 6.0f, baseline));
			}
		}

		// Acts on mouse UP, not down, and not only for the usual
		// button-like reasons. Choosing on mouse-down closed this window
		// while the button was still held -- and the pointer was then
		// over the reading column the palette had been covering, whose
		// TextDocumentView::MouseMoved() extends the selection from the
		// current anchor on any movement while a button is down
		// (`if (buttons > 0 && dragMessage == NULL) SetCaret(where,
		// true)`). With the anchor just reset to 0, that produced a
		// selection running from the top of the chapter to wherever the
		// pointer happened to be -- reported as the whole passage going
		// grey after picking a colour.
		virtual void MouseDown(BPoint where)
		{
			fPressed = true;
			SetMouseEventMask(B_POINTER_EVENTS);
			Invalidate();
		}

		virtual void MouseUp(BPoint where)
		{
			if (!fPressed)
				return;
			fPressed = false;

			// Released outside the cell it was pressed in: treat it as
			// cancelled, the way any button does.
			if (!Bounds().Contains(where)) {
				Invalidate();
				return;
			}
			// Chosen() closes the window, which destroys this view --
			// nothing may touch members afterwards.
			if (fOwner != NULL && fMessage != NULL)
				fOwner->Chosen(fMessage);
		}

	private:
		rgb_color				fColor;
		BMessage*				fMessage;
		HighlightPaletteWindow*	fOwner;
		bool					fIsRemove;
		bool					fIsMore;
		BString					fLabel;
		bool					fPressed;
	};

	BMessenger	fTarget;
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

/*
 * Copyright 2020, Paradoxon <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Catalog.h>
#include <Debug.h>
#include <Locale.h>
#include <Rect.h>
#include <StringView.h>

#include <gbfplain.h>
#include <gbfstrongs.h>
#include <localemgr.h>
#include <markupfiltmgr.h>
#include <swmgr.h>
#include <versekey.h>

#include "constants.h"
#include "BibleTextColumn.h"
#include "BibleTextField.h"

using namespace sword;
using namespace std;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BibleTextColumn"

#define CONFIGPATH MODULES_PATH

BibleTextColumn::BibleTextColumn(SWMgr *manager, SWModule* mod,  
										float width, float minWidth,
										float maxWidth, alignment align) 
	: BTitledColumn("Select Bible", width, minWidth, maxWidth,align),
	fIsLineBreak(false),
	fShowVerseNumbers(true),
	fManager(manager),
	fModule(mod)
{
	Init();
}

BibleTextColumn::BibleTextColumn(SWMgr *manager, const char *moduleName, 
										float width, float minWidth,
										float maxWidth, alignment align) 
	: BTitledColumn("Select Bible", width, minWidth, maxWidth,align),
	fIsLineBreak(false),
	fShowVerseNumbers(true),
	fManager(manager)
{
	SetModule(moduleName);
	Init();
}

void BibleTextColumn::Init()
{
	BLocale::Default()->GetLanguage(&language);
	verseRenderer	= new BTextView("verseRenderer");
	verseBitmap		= NULL;
	SetTitle(fModule->getName());
}


void BibleTextColumn::DrawField(BField* _field, BRect rect, BView* parent)
{
	BibleTextField* field = static_cast<BibleTextField*>(_field);
	field->SetWidth(rect.Width());
	fModule->setKey(field->Key());
	field->SetHeight(verseRenderer->TextRect().Height());
	DrawBibeltext(fModule->renderText(), parent, rect);
}


int BibleTextColumn::CompareFields(BField* field1, BField* field2)
{
	const BibleTextField *first = dynamic_cast<const BibleTextField*>(field1);
	const BibleTextField *second = dynamic_cast<const BibleTextField*>(field2);
	if (first!=NULL & second!=NULL)
	{
//		return (first->Key() == second->Key());
	}
	else
		return -1;
}


float BibleTextColumn::GetPreferredWidth(BField* field, BView* parent) const
{
}


bool BibleTextColumn::AcceptsField(const BField* field) const
{
	return static_cast<bool>(dynamic_cast<const BibleTextField*>(field));
}

void BibleTextColumn::MouseMoved(BColumnListView* parent, BRow* row,
							BField* field, BRect fieldRect,
							BPoint point, uint32 buttons, int32 code)
{
}


void BibleTextColumn::MouseDown(BColumnListView* parent, BRow* row,
							BField* field, BRect fieldRect,
							BPoint point, uint32 buttons)
{
	printf("BibleTextColumn::MouseDown()\n");
}


void BibleTextColumn::MouseUp(BColumnListView* parent, BRow* row,
							BField* field)
{
}


status_t BibleTextColumn::SetModule(SWModule* mod)
{
	fModule=mod;
}


status_t BibleTextColumn::SetModule(const char* modulName)
{
	fModule	= fManager->getModule(modulName);
}


SWModule* BibleTextColumn::CurrentModule(void)
{
	return fModule;
}


void BibleTextColumn::DrawBibeltext(const char* string, BView* parent, BRect rect)
{
	verseRenderer->ResizeTo(rect.Width(),rect.Height());
	verseRenderer->SetTextRect(BRect(0,0,rect.Width(),rect.Height()));
	verseRenderer->SetText(string);
	BRect textRect = verseRenderer->TextRect();
	BRect bitmapRect = textRect;
	verseBitmap = new BBitmap(bitmapRect, B_RGBA32, true, false);
	if (verseBitmap != NULL && verseBitmap->Lock()) 
	{
		verseBitmap->AddChild(verseRenderer);
		verseRenderer->Draw(textRect);
		verseRenderer->Sync();
		verseBitmap->Unlock();
	}
	parent->DrawBitmap(verseBitmap, BPoint(rect.left, rect.top));
	if (verseBitmap != NULL && verseBitmap->Lock()) 
	{
		verseRenderer->RemoveSelf();
		verseBitmap->Unlock();
	}
	delete verseBitmap;
	verseBitmap = NULL;
}


void  BibleTextColumn::SetShowVerseNumbers(bool showVerseNumbers)
{
	if (fShowVerseNumbers != showVerseNumbers)
	{
		fShowVerseNumbers=showVerseNumbers;
	}
};

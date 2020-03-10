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
	DrawString(field->Key()->getText(), parent, rect);
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


void BibleTextColumn::_UpdateBibleText()
{
	//verseRenderer->Delete(0, verseRenderer->TextLength());
/*	VerseKey key = fModule->getKey();
	VerseKey oldKey = fModule->getKey();

	uint16 versecount = key.getVerseMax();
	if (fModule == NULL)
	{
		verseRenderer->SetText(
				B_TRANSLATE("No Modules installed\n\n \
				Please use ScriptureGuideManager to download the books you want."));
	}
	if (strcmp(fModule->getType(), "Biblical Texts")==0) 
	{
		key.setVerse(1);
		fModule->setKey(key);
		BString text(fModule->renderText());
		if (text.CountChars()<1)
		{
			// this condition will only happen if the module is only one particular
			// testament.
			verseRenderer->SetText(
						B_TRANSLATE("This module does not have this section."));
		}
		for (uint16 currentverse = 1; currentverse <= versecount; currentverse++)
		{
			key.setVerse(currentverse);
			fModule->setKey(key);
			if (text.CountChars() < 1)
				continue;
			
			// Remove <P> tags and 0xc2 0xb6 sequences to carriage returns. 
			// The crazy hex sequence is actually the UTF-8 encoding for the 
			// paragraph symbol. If we convert them to \n's, output looks funky
			text.RemoveAll("\x0a\x0a");
			text.RemoveAll("\xc2\xb6 ");
			text.RemoveAll("<P> ");
			
			if (fIsLineBreak)
				text += "\n";
			
			if (fShowVerseNumbers)
			{
				BString string;
				string << " " << currentverse << " ";
				verseRenderer->Insert(string);
			}
			verseRenderer->Insert(fModule->renderText());
		}
	}
	fModule->setKey(oldKey);	*/
	
	if (verseBitmap != NULL && verseBitmap->Lock())
	{
		delete verseBitmap;
		verseBitmap = NULL;
	}
	//BRect textRect = verseRenderer->TextRect();
	BRect textRect = BRect(0,0,400,100);
	BRect bitmapRect(0, 0, textRect.Width() + 5.0, textRect.Height());
	verseBitmap = new BBitmap(bitmapRect, B_RGBA32, true, false);
	verseRenderer = new BTextView(BRect(0,0,100,20), "verseRenderer", 0, 0);
	if (verseBitmap != NULL && verseBitmap->Lock()) 
	{
		verseBitmap->AddChild(verseRenderer);
		verseRenderer->SetText("Test Test Test Test \n Test Test Test");
		verseRenderer->Insert("Noch mehr text");		
		verseRenderer->Draw(textRect);
		verseRenderer->DrawAfterChildren(textRect);
		verseRenderer->Sync();
		verseBitmap->Unlock();
	}
}


void  BibleTextColumn::SetShowVerseNumbers(bool showVerseNumbers)
{
	if (fShowVerseNumbers != showVerseNumbers)
	{
		fShowVerseNumbers=showVerseNumbers;
	}
};

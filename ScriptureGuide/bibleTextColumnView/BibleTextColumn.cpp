/*
 * Copyright 2020, Paradoxon <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Catalog.h>
#include <Debug.h>
#include <Locale.h>

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


BibleTextColumn::BibleTextColumn(const char *moduleName, 
										float width, float minWidth,
										float maxWidth, alignment align) 
	: BTitledColumn("Select Bible", width, minWidth, maxWidth,align)
	fIsLineBreak(false),
	fShowVerseNumbers(true)
{
	PRINT(("key %s module %s\n", iKey, moduleName));
	BLocale::Default()->GetLanguage(&language);
	fManager = new SWMgr(CONFIGPATH, true, new MarkupFilterMgr(FMT_GBF, ENC_UTF8));
	SetModule(moduleName);
	verseRenderer	= new BTextView("verseRenderer");
	verseBitmap		= NULL;
}


void BibleTextColumn::DrawField(BField* field, BRect rect, BView* parent)
{
}


int BibleTextColumn::CompareFields(BField* field1, BField* field2)
{
	const BibleTextField *first = dynamic_cast<const BibleTextField*>(field1);
	const BibleTextField *second = dynamic_cast<const BibleTextField*>(field1);
	if (first!=NULL & second!=NULL)
		return first->Key() == second->Key();
	else
		return;
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
}


void BibleTextColumn::MouseUp(BColumnListView* parent, BRow* row,
							BField* field)
{
}


status_t BibleTextColumn::SetModule(SWModule* mod)
{
	VerseKey key = ((VerseKey *)fModule->getKey());
	fModule=mod;
	fModule->setKey(key);
	
}


status_t BibleTextColumn::SetModule(const char* modulName)
{
	
	VerseKey	key;
	bool		setKey=false;
	if (fModule)
	{
		key = ((VerseKey *)fModule->getKey());
		setKey=true;
	}
	fModule	= fManager->getModule(modulName);
	if (fModule)
	{
		fModule->addRenderFilter(new GBFPlain());
		if (setKey)
			fModule->setKey(key);
	}	
	
}


SWModule* BibleTextColumn::CurrentModule(void)
{
	return fModule;
}


VerseKey& BibleTextColumn::KeyAt(int32 index)
{
}


Paragraph& BibleTextColumn::ParagraphFor(SWKey key)
{
}


const ParagraphStyle& BibleTextColumn::ParagraphStyleFor(SWKey key)
{
	return ParagraphFor(key).Style();
}


void BibleTextColumn::_UpdateBibleText()
{
	Remove(0,Length());
	ParagraphStyle bibleVerseStyle;
	bibleVerseStyle.SetJustify(true);
	Paragraph paragraph;
	VerseKey key = fModule->getKey();
	VerseKey oldKey = fModule->getKey();

	uint16 versecount = key.getVerseMax();
	if (fModule == NULL)
	{
		paragraph = Paragraph(bibleVerseStyle);
		paragraph.Append(TextSpan(
				B_TRANSLATE("No Modules installed\n\n \
				Please use ScriptureGuideManager to download the books you want."),
				*fVerseStyle));
		Append(paragraph);
		return;
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
			paragraph = Paragraph(bibleVerseStyle);
			paragraph.Append(TextSpan(
							B_TRANSLATE("This module does not have this section."),
							*fVerseStyle));
			Append(paragraph);
			return;
		}
		for (uint16 currentverse = 1; currentverse <= versecount; currentverse++)
		{
			key.setVerse(currentverse);
			fModule->setKey(key);
			paragraph = Paragraph(bibleVerseStyle);
			// Get the verse for processing
			text.SetTo(fModule->renderText());
			
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
				paragraph.Append(TextSpan(string, *fNumberStyle));
			}
			paragraph.Append(TextSpan(text.String(),*fVerseStyle));
			Append(paragraph);
		}
	}
	fModule->setKey(oldKey);	
}


void  BibleTextColumn::SetShowVerseNumbers(bool showVerseNumbers)
{
	if (fShowVerseNumbers != showVerseNumbers)
	{
		fShowVerseNumbers=showVerseNumbers;
	}
};

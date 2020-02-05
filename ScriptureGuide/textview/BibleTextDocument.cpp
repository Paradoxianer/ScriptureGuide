/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include "BibleTextDocument.h"

BibleTextDocument::BibleTextDocument(char iKey, char *moduleName) 
	: TextDocument()
{
	BLocale::Default()->GetLanguage(&language);
	fVerseStyle = new CharacterStyle();
	fNumberStyle = new CharacterStyle();
	fNumberStyle->SetForegroundColor(BLUE);
	fNumberStyle->SetBold(true);
	
	module	= new SWModule(moduleName);
	module->addRenderFilter(new GBFPlain());
	const char *error = module->SetKey(iKey);
	printf("ERROR setting Key %s", error);
}


char* BibleTextDocument::GetKey()
{
	module->getKeyText();
}


const char* BibleTextDocument::GetTestament() const
{
}
	

const char* BibleTextDocument::GetBook() const
{
	((VerseKey*)module->getKey())->GetBook();
}


int BibleTextDocument::GetChapter() const
{
	((VerseKey*)module->getKey())->GetChapter();
}


int BibleTextDocument::GetVerse() const
{
	((VerseKey*)module->getKey())->GetVerse();
}


status_t BibleTextDocument::SetBook(const char *book)
{
	((VerseKey*)module->getKey())->SetBook(book);
}


status_t BibleTextDocument::SetChapter(int iChapter)
{
	((VerseKey*)module->getKey())->SetChapter(iChapter);
}


status_t BibleTextDocument::SetVerse(int iVerse)
{
	((VerseKey*)module->getKey())->SetVerse(iVerse);
}


status_t BibleTextDocument::SetKey(const char iKey)
{
	module->SetKey(new VerseKey(iKey));
}


status_t BibleTextDocument::NextBook()
{
	_UpdateBibleText();
}


status_t BibleTextDocument::NextChapter()
{
	module->increment();
	_UpdateBibleText();
}


status_t BibleTextDocument::NextVerse()
{
}


status_t BibleTextDocument::PrevBook()
{
	module->decrement();
	_UpdateBibleText();
}


status_t BibleTextDocument::PrevChapter()
{
	_UpdateBibleText();
}


status_t BibleTextDocument::PrevVerse()
{
}


void BibleTextDocument::Select(int start, int end)
{
}


status_t BibleTextDocument::SelectVerse(int vStart, int vEnd)
{
}


const char* BibleTextDocument::VerseForSelection()
{
}


status_t BibleTextDocument::SetModule(SGModule* mod)
{
	VerseKey key = ((VerseKey *)module->GetKey());
	module=mod;
	module->SetKey(key);
}


SGModule* BibleTextDocument::CurrentModule(void)
{
	return module;
}


VerseKey& BibleTextDocument::KeyAt(int32 index)
{
}

Paragraph& BibleTextDocument::ParagraphFor(SWKey key)
{
	return verseParagraph[key];
}

BibleVerseLayout& BibleTextDocument::ParagraphStyleFor(SWKey key)
{
	return (BibleVerseLayout *)ParagraphFor(key)->Style();
	
}


void BibleTextDocument::_UpdateBibleText()
{
	BibleVerseStyle bibleVerseStyle;
	bibleVerseStyle.SetJustify(true);
	Paragraph paragraph;
	
	BString oldtxt("1"), newtxt("2");
	BString currentbook(fBookMenu->FindMarked()->Label());
	int32	highlightStart = 0;
	int32	highlightEnd = 0;
	module->getKey()
	uint16 versecount = key->getVerseMax();
	if (fCurrentModule == NULL)
	{
		paragraph = Paragraph(paragraphStyle);
		paragraph.Append(TextSpan(
				B_TRANSLATE("No Modules installed\n\n \
				Please use ScriptureGuideManager to download the books you want."),
				*fVerseStyle));
		document->Append(paragraph);
		fVerseView->SetTextDocument(document);
		be_roster->Launch("application/x-vnd.wgp.ScriptureGuideManager");
		return;
	}
	if (fCurrentModule->Type() == TEXT_BIBLE) 
	{
		BString text(fCurrentModule->GetVerse(currentbook.String(),
			fCurrentChapter, 1));

		if (text.CountChars()<1)
		{
			// this condition will only happen if the module is only one particular
			// testament.
			paragraph = Paragraph(paragraphStyle);
			paragraph.Append(TextSpan(
							B_TRANSLATE("This module does not have this section."),
							*fVerseStyle));
			document->Append(paragraph);
			fVerseView->SetTextDocument(document);
			return;
		}
		if ((fCurrentVerseEnd != 0) && (fCurrentVerseEnd < fCurrentVerse))
			fCurrentVerseEnd = fCurrentVerse;
		for (uint16 currentverse = 1; currentverse <= versecount; currentverse++)
		{
			paragraph = Paragraph(paragraphStyle);

			// Get the verse for processing
			text.SetTo(fCurrentModule->GetVerse(currentbook.String(),
						fCurrentChapter, currentverse));
			
			if (text.CountChars() < 1)
				continue;
			
			/**ToDo
			if ((fCurrentVerse!=0) && (fCurrentVerse == currentverse))
				fVerseView->GetSelection(&highlightStart,&highlightStart);
			*/
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
			document->Append(paragraph);
		}
}


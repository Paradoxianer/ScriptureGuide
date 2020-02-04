/*
 * Copyright 2018, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "BibelTextView.h"


BibleTextView::BibleTextView(char module, char iKey)
{
}


char* BibleTextView::GetKey()
{
}


char* BibleTextView::GetTestament()
{
}


char* BibleTextView::GetBook()
{
}

int BibleTextView::GetChapter()
{
}


int BibleTextView::GetVerse()
{
}


status_t BibleTextView::SetBook(const char *book)
{
}


status_t BibleTextView::SetChapter(int ichapter)
{
}


status_t BibleTextView::SetVerse(int iverse)
{
}


status_t BibleTextView::SetKey()
{
}


status_t BibleTextView::NextBook()
{
}


status_t BibleTextView::NextChapter()
{
}


status_t BibleTextView::NextVerse()
{
}


status_t BibleTextView::PrevBook()
{
}


status_t BibleTextView::PrevChapter()
{
}


status_t BibleTextView::PrevVerse()
{
}


void BibleTextView::Select(int start, int end)
{
}


status_t BibleTextView::SelectVerse(int vStart, int vEnd)
{
}


const char* BibleTextView::VerseForSelection()
{
}


status_t BibleTextView::SetModule(SGModule* mod)
{
}


SGModule* BibleTextView::CurrentModule(void)
{
}

void BibleTextView::_UpdateBibleText()
{
}

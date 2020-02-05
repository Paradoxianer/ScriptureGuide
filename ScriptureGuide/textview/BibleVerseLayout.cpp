/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include "BibleVerseLayout.h"

BibleVerseLayout::BibleVerseLayout()
	: 
	ParagraphLayout(),
	fHeight(0)
{
}


float BibleVerseLayout::Height()
{
	float calculatedHeight = BibleVerseLayout::Height();
	if (calculatedHeight > fHeight)
		fHeight = calculatedHeight;
	return fHeight;
}


float BibleVerseLayout::SetHeight(float height)
{
	float calculatedHeight = BibleVerseLayout::Height();
	if (calculatedHeight > height)
		fHeight = calculatedHeight;
	else
		fHeight = height;
	return fHeight;
}

/*
 * Copyright 2020, Paradoxianer <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include "BibleTextField.h"

BibleTextField::BibleTextField(VerseKey *key)
	: BField(),
	fKey(key)
{
}

void BibleTextField::SetKey(VerseKey *key)
{
	fKey = key;
}
	
void BibleTextField::SetWidth(float)
{
}


float BibleTextField::Width()
{
}


float BibleTextField::HeightForWidth()
{
	return 100.0;
}

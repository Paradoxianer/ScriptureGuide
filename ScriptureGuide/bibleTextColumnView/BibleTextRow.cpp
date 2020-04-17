/*
 * Copyright 2020, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "BibleTextRow.h"
#include "BibleTextField.h"

BibleTextRow::BibleTextRow():BRow()
{
}


BibleTextRow::BibleTextRow(float height):BRow(height)
{
}


float BibleTextRow::Height() const
{
	float height = 0 ;
	BibleTextField *btField;
	for (int i =0 ; i<CountFields();i++){
		btField=(BibleTextField *)GetField(i);
		if (btField->Height()<height)
			height = btField->Height();
	}
	return height;
}


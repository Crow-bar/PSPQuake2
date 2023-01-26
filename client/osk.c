/*
Copyright (C) 2023 Sergey Galushko

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// osk.c
#include "../client/client.h"

#define OSK_MAX_ROWS    13 // X
#define OSK_MAX_LINES   4  // Y

#define OSK_KEY_BACKSPACE   0x7f
#define OSK_KEY_ENTER       0x0d

static const char *osk_layout[OSK_MAX_LINES][OSK_MAX_ROWS] =
{
	{
	/* Y  X  0   1   2   3   4   5   6   7   8   9   10  11   12 */
	/* 0 */ "1" "2" "3" "4" "5" "6" "7" "8" "9" "0" "-" "="  " "     ,
	/* 1 */ "q" "w" "e" "r" "t" "y" "u" "i" "o" "p" "[" "]"  "\\"    ,
	/* 2 */ " " "a" "s" "d" "f" "g" "h" "j" "k" "l" ";" "'"  "\x7f"  ,
	/* 3 */ " " "z" "x" "c" "v" "b" "n" "m" "," "." "/" " "  "\x0d"
	},
	{
	/* 0 */ "!" "@" "#" "$" "%" "^" "&" "*" "(" ")" "_" "+"  " "     ,
	/* 1 */ "Q" "W" "E" "R" "T" "Y" "U" "I" "O" "P" "{" "}"  "|"     ,
	/* 2 */ " " "A" "S" "D" "F" "G" "H" "J" "K" "L" ":" "\"" "\x7f"  ,
	/* 3 */ " " "Z" "X" "C" "V" "B" "N" "M" "<" ">" "?" " "  "\x0d"
	}
};

static struct
{
	qboolean    active;
	int         cursor_lay;
	int         cursor_x;
	int         cursor_y;
} osk_state = {0};


/*
================
OSK_SetActive
================
*/
qboolean OSK_SetActive(qboolean active)
{
	if(!osk->value && active)
		return false;

	osk_state.active = active;
	if(active) // clear before interception
		Key_ClearStates();

	return true;
}

/*
================
OSK_IsActive
================
*/
qboolean OSK_IsActive(void)
{
	return osk_state.active;
}

/*
================
OSK_KeyEvent

event interception
================
*/
qboolean OSK_KeyEvent(int *key, qboolean down)
{
	qboolean    skip_event;
	char        osk_key;

	if(!osk_state.active)
		return false;

	skip_event = true;

	switch(*key)
	{
	case K_ENTER:
	case K_A_BUTTON:
		osk_key = osk_layout[osk_state.cursor_lay][osk_state.cursor_y][osk_state.cursor_x];
		skip_event = false;
		switch(osk_key)
		{
		case OSK_KEY_ENTER:
			*key = K_ENTER;
			break;
		case OSK_KEY_BACKSPACE:
			*key = K_BACKSPACE;
			break;
		default:
			*key = osk_key;
			break;
		}
		break;
	case K_UPARROW:
		if(down && --osk_state.cursor_y < 0)
		{
			osk_state.cursor_y = OSK_MAX_LINES - 1;
			osk_state.active = false;
		}
		break;
	case K_DOWNARROW:
		if(down && ++osk_state.cursor_y >= OSK_MAX_LINES )
		{
			osk_state.cursor_y = 0;
			osk_state.active = false;
		}
		break;
	case K_LEFTARROW:
		if( down && --osk_state.cursor_x < 0 )
			osk_state.cursor_x = OSK_MAX_ROWS - 1;
		break;
	case K_RIGHTARROW:
		if( down && ++osk_state.cursor_x >= OSK_MAX_ROWS )
			osk_state.cursor_x = 0;
		break;
	case K_L1_BUTTON:
		if(down)
		{
			if(osk_state.cursor_lay & 1)
				osk_state.cursor_lay--;
			else
				osk_state.cursor_lay++;
		}
		break;
	case K_R1_BUTTON:
		skip_event = false;
		*key = ' ';
		break;
	default:
		skip_event = false;
		break;
	}

	return skip_event;
}

/*
================
OSK_DrawBox
================
*/
static void OSK_DrawBox(int x, int y, int width, int lines)
{
	int     cx, cy;
	int     i, j;

	// draw left side
	re.DrawChar (x, y, 1);
	for (i = 0, cy = y + 8; i < lines; i++, cy += 8)
		re.DrawChar (x, cy, 4);
	re.DrawChar (x, cy, 7);

	// draw middle
	for(i = 0, cx = x + 8; i < width; i++, cx += 8)
	{
		re.DrawChar (cx, y, 2);
		for (j = 0, cy = y + 8; j < lines; j++, cy += 8)
			re.DrawChar (cx, cy, 5);
		re.DrawChar (cx, cy, 8);
	}

	// draw right side
	re.DrawChar (cx, y, 3);
	for (i = 0, cy = y + 8; i < lines; i++, cy += 8)
		re.DrawChar (cx, cy, 6);
	re.DrawChar (cx, cy, 9);
}

/*
================
OSK_Draw
================
*/
void OSK_Draw(void)
{
	char    osk_key;
	int     x, y, pos_x, pos_y;

	if(!osk_state.active)
		return;

	pos_x = (viddef.width - OSK_MAX_ROWS * 16) >> 1;
	pos_y = (viddef.height - OSK_MAX_LINES * 16) >> 3;

	OSK_DrawBox(pos_x - 8, pos_y - 8, OSK_MAX_ROWS * 2 - 1, OSK_MAX_LINES * 2 - 1);

	for(y = 0; y < OSK_MAX_LINES; y++)
	{
		for(x = 0; x < OSK_MAX_ROWS; x++)
		{
			osk_key = osk_layout[osk_state.cursor_lay][y][x];

			if(osk_state.cursor_x == x && osk_state.cursor_y == y)
				osk_key = (osk_key == ' ') ? 0x8b : osk_key + 0x80;

			re.DrawChar (x * 16 + pos_x, y * 16 + pos_y, osk_key);
		}
	}
}

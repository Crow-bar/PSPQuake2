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

#ifndef DEBUG_PSP_H
#define DEBUG_PSP_H 

void Dbg_SetClearColor (int32_t r, int32_t g, int32_t b);
void Dbg_SetTextColor (int32_t r, int32_t g, int32_t b);
void Dbg_SetTextBackColor (int32_t r, int32_t g, int32_t b);
void Dbg_SetTextBackOff (void);
void Dbg_GetDisplayWH (int32_t *width, int32_t *height);
void Dbg_GetTextXY (uint16_t *x, uint16_t *y);
void Dbg_GetTextMaxXY (uint16_t *x, uint16_t *y);
void Dbg_SetTextXY (uint16_t x, uint16_t y);
void Dbg_DisplayActivate (void);
void Dbg_DisplayClear (void);
void Dbg_DrawTextFill (int32_t tx, int32_t ty, int32_t tw, int32_t th, int32_t r, int32_t g, int32_t b);
void Dbg_DrawChar (int32_t c, int32_t x, int32_t y, uint32_t chcolor, uint32_t bgcolor);
void Dbg_DrawText (const uint8_t *text, int32_t size);
void Dbg_Printf (const uint8_t *format, ...);
int32_t Dbg_Init (void *dbuffer, int32_t format, uint8_t *fontpath, uint8_t usbuffer);
void Dbg_Shutdown (void);

#endif //DEBUG_PSP_H

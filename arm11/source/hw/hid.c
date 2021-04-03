/*
 *   This file is part of GodMode9
 *   Copyright (C) 2019 Wolfvak
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <common.h>
#include <types.h>
#include <hid_map.h>

#include "hw/codec.h"
#include "hw/hid.h"
#include "hw/mcu.h"

#define REG_HID	(~(*(vu16*)(0x10146000)) & BUTTON_ANY)

void HID_GetState(u32 *keys, u32 *touch, u32 *cpad)
{
	CODEC_Input codec;
	CODEC_Get(&codec);

	*keys = REG_HID | mcuGetSpecialHID();
	*touch = ((u32)codec.ts_x << 16) | (u32)codec.ts_y;
	*cpad = ((u32)codec.cpad_x << 16) | (u32)codec.cpad_y;
}

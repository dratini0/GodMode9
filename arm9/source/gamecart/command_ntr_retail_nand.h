// Copyright 2014 Normmatt
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
//
// modifyed by osilloscopion (2 Jul 2016)
//

#pragma once

#include "common.h"

void NTRRetailNand_Init(void);
u8 NTRRetailNand_Status(void);
void NTRRetailNand_WritePage(u32 address, size_t pages, const void *data);
void NTRRetailNand_WriteDisable(void);
void NTRRetailNand_WriteEnable(void);
void NTRRetailNand_SwitchToRom(void);
void NTRRetailNand_SetSavePosition(u32 address);

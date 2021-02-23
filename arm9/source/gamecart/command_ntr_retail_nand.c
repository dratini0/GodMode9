// Copyright 2014 Normmatt
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
//
// modifyed by osilloscopion (2 Jul 2016)
//

#include "command_ntr.h"
#include "command_ak2i.h"
#include "protocol_ntr.h"
#include "card_ntr.h"


void NTRRetailNand_Init(void) {
    u32 cmd[2] = {0x94000000, 0x00000000};
    NTR_SendCommand(cmd, 0, 0, NULL);
}

u8 NTRRetailNand_Status(void) {
    u32 cmd[2] = {0xD6000000, 0x00000000};
    u32 ver = 0;

    NTR_SendCommand(cmd, 4, 0, &ver);
    return ver & 0xFF;
}

// TODO
//void NTRRetailNand_WritePage(u32 address, size_t pages, const void *data);

void NTRRetailNand_WriteDisable(void) {
    u32 cmd[2] = {0x84000000, 0x00000000};
    NTR_SendCommand(cmd, 0, 0, NULL);
}

void NTRRetailNand_WriteEnable(void) {
    u32 cmd[2] = {0x85000000, 0x00000000};
    NTR_SendCommand(cmd, 0, 0, NULL);
}

void NTRRetailNand_SwitchToRom(void) {
    u32 cmd[2] = {0x8B000000, 0x00000000};
    NTR_SendCommand(cmd, 0, 0, NULL);
}

void NTRRetailNand_SetSavePosition(u32 address) {
    u32 cmd[2] = {0xB2000000 | (address >> 8), address << 24};
    NTR_SendCommand(cmd, 0, 0, NULL);
}




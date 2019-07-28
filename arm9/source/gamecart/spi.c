/*
 *  This file is part of TWLSaveTool.
 *  Copyright (C) 2015-2016 TuxSH
 *
 *  TWLSaveTool is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include "spi.h"
#include "spicard.h"
#include "ui.h"

// Deliberately written in C! (except for a few lines)

// u8* fill_buf = NULL;

int SPIWriteRead(CardType type, void* cmd, u32 cmdSize, void* answer, u32 answerSize, void* data, u32 dataSize) {
	const u32 headerFooterVal = 0;
	bool b = type == FLASH_512KB_INFRARED || type == FLASH_256KB_INFRARED;

	SPICARD_init();

	if (b) {
		SPICARD_writeRead(NSPI_CLK_1MHz, &headerFooterVal, NULL, 1, 0, false);
	}
	SPICARD_writeRead(NSPI_CLK_4MHz, cmd, answer, cmdSize, answerSize, false);
	SPICARD_writeRead(NSPI_CLK_4MHz, data, NULL, dataSize, 0, true);

	SPICARD_deinit();
	
	return 0;
}

int SPIWaitWriteEnd(CardType type) {
	u8 cmd = SPI_CMD_RDSR, statusReg = 0;
	int res = 0;

	do{
		res = SPIWriteRead(type, &cmd, 1, &statusReg, 1, 0, 0);
		if(res) return res;
	} while(statusReg & SPI_FLG_WIP);

	return 0;
}

int SPIEnableWriting(CardType type) {
	u8 cmd = SPI_CMD_WREN, statusReg = 0;
	int res = SPIWriteRead(type, &cmd, 1, NULL, 0, 0, 0);

	if(res || type == EEPROM_512B) return res; // Weird, but works (otherwise we're getting an infinite loop for that chip type).
	cmd = SPI_CMD_RDSR;
	
	do{
		res = SPIWriteRead(type, &cmd, 1, &statusReg, 1, 0, 0);
		if(res) return res;
	} while(statusReg & ~SPI_FLG_WEL);
	
	return 0;
}

int SPIReadJEDECIDAndStatusReg(CardType type, u32* id, u8* statusReg) {
	u8 cmd = SPI_FLASH_CMD_RDID;
	u8 reg = 0;
	u8 idbuf[3] = { 0 };
	u32 id_ = 0;
	int res = SPIWaitWriteEnd(type);
	if(res) return res;
	
	if((res = SPIWriteRead(type, &cmd, 1, idbuf, 3, 0, 0))) return res;
	
	id_ = (idbuf[0] << 16) | (idbuf[1] << 8) | idbuf[2];
	cmd = SPI_CMD_RDSR;
	
	if((res = SPIWriteRead(type, &cmd, 1, &reg, 1, 0, 0))) return res;
	
	if(id) *id = id_;
	if(statusReg) *statusReg = reg;
	
	ShowPrompt(false, "JEDEC = %lx, StatusReg = %hhx", *id, reg);

	return 0;
}

u32 SPIGetPageSize(CardType type) {
	u32 EEPROMSizes[] = { 16, 32, 128, 256 };
	if(type == NO_CHIP || type > CHIP_LAST) return 0;
	else if(type < FLASH_256KB_1) return EEPROMSizes[(int) type];
	else return 256;
}

u32 SPIGetCapacity(CardType type) {
	u32 sz[] = { 9, 13, 16, 17, 18, 18, 19, 19, 20, 23, 19, 19 };
	
	if(type == NO_CHIP || type > CHIP_LAST) return 0;
	else return 1 << sz[(int) type];
}

int SPIWriteSaveData(CardType type, u32 offset, void* data, u32 size) {
	u8 cmd[4] = { 0 };
	u32 cmdSize = 4;
	
	u32 end = offset + size;
	u32 pos = offset;
	if(size == 0) return 0;
	u32 pageSize = SPIGetPageSize(type);
	if(pageSize == 0) return 0xC8E13404;
	
	int res = SPIWaitWriteEnd(type);
	if(res) return res;
	
	size = (size <= SPIGetCapacity(type) - offset) ? size : SPIGetCapacity(type) - offset; 

	while(pos < end) {
		switch(type) {
			case EEPROM_512B:
				cmdSize = 2;
				cmd[0] = (pos >= 0x100) ? SPI_512B_EEPROM_CMD_WRHI : SPI_512B_EEPROM_CMD_WRLO;
				cmd[1] = (u8) pos;
				break;
			case EEPROM_8KB:
			case EEPROM_64KB:
				cmdSize = 3;
				cmd[0] = SPI_EEPROM_CMD_WRITE;
				cmd[1] = (u8)(pos >> 8);
				cmd[2] = (u8) pos;
				break;
			case EEPROM_128KB:
				cmdSize = 4;
				cmd[0] = SPI_EEPROM_CMD_WRITE;
				cmd[1] = (u8)(pos >> 16);
				cmd[2] = (u8)(pos >> 8);
				cmd[3] = (u8) pos;
				break;
			case FLASH_256KB_1:
			/*	
			This is what is done in the official implementation, but I think it's wrong
				cmdSize = 4;
				cmd[0] = SPI_CMD_PP;
				cmd[1] = (u8)(pos >> 16);
				cmd[2] = (u8)(pos >> 8);
				cmd[3] = (u8) pos;
				break;
			*/
			case FLASH_256KB_2:
			case FLASH_512KB_1:
			case FLASH_512KB_2:
			case FLASH_1MB:
			case FLASH_512KB_INFRARED:
			case FLASH_256KB_INFRARED:
				cmdSize = 4;
				cmd[0] = SPI_FLASH_CMD_PW;
				cmd[1] = (u8)(pos >> 16);
				cmd[2] = (u8)(pos >> 8);
				cmd[3] = (u8) pos;
				break;
			case FLASH_8MB:
				return 0xC8E13404; // writing is unsupported (so is reading? need to test)
			default:
				return 0; // never happens
		}
		
		u32 remaining = end - pos;
		u32 nb = pageSize - (pos % pageSize);
		
		u32 dataSize = (remaining < nb) ? remaining : nb;
		
		if( (res = SPIEnableWriting(type)) ) return res;
		if( (res = SPIWriteRead(type, cmd, cmdSize, NULL, 0, (void*) ((u8*) data - offset + pos), dataSize)) ) return res;
		if( (res = SPIWaitWriteEnd(type)) ) return res;
		
		pos = ((pos / pageSize) + 1) * pageSize; // truncate
	}
	
	return 0;
}

int _SPIReadSaveData_512B_impl(u32 pos, void* data, u32 size) { 
	u8 cmd[4];
	u32 cmdSize = 2;
	
	u32 end = pos + size;
	
	u32 read = 0;
	if(pos < 0x100) {
		u32 len = 0x100 - pos;
		cmd[0] = SPI_512B_EEPROM_CMD_RDLO;
		cmd[1] = (u8) pos;
		
		int res = SPIWriteRead(EEPROM_512B, cmd, cmdSize, data, len, NULL, 0);
		if(res) return res;
		
		read += len;
	}
	
	if(end >= 0x100) {
		u32 len = end - 0x100;

		cmd[0] = SPI_512B_EEPROM_CMD_RDHI;
		cmd[1] = (u8)(pos + read);
		
		int res = SPIWriteRead(EEPROM_512B, cmd, cmdSize, (void*)((u8*)data + read), len, NULL, 0);

		if(res) return res;
	}
	
	return 0;
}

int SPIReadSaveData(CardType type, u32 offset, void* data, u32 size) {	
	u8 cmd[4] = { SPI_CMD_READ };
	u32 cmdSize = 4;
	if(size == 0) return 0;
	if(type == NO_CHIP) return 0xC8E13404;
	
	int res = SPIWaitWriteEnd(type);
	if(res) return res;
	
	size = (size <= SPIGetCapacity(type) - offset) ? size : SPIGetCapacity(type) - offset; 
	u32 pos = offset;
	switch(type) {
		case EEPROM_512B:
			return _SPIReadSaveData_512B_impl(offset, data, size);
			break;
		case EEPROM_8KB:
		case EEPROM_64KB:
			cmdSize = 3;
			cmd[1] = (u8)(pos >> 8);
			cmd[2] = (u8) pos;
			break;
		case EEPROM_128KB:
			cmdSize = 4;
			cmd[1] = (u8)(pos >> 16);
			cmd[2] = (u8)(pos >> 8);
			cmd[3] = (u8) pos;
			break;
		case FLASH_256KB_1:
		case FLASH_256KB_2:
		case FLASH_512KB_1:
		case FLASH_512KB_2:
		case FLASH_1MB:
		case FLASH_8MB:
		case FLASH_512KB_INFRARED:
		case FLASH_256KB_INFRARED:
			cmdSize = 4;
			cmd[1] = (u8)(pos >> 16);
			cmd[2] = (u8)(pos >> 8);
			cmd[3] = (u8) pos;
			break;
		default:
			return 0; // never happens
	}
	
	return SPIWriteRead(type, cmd, cmdSize, data, size, NULL, 0);
}

/* int SPIEraseSector(CardType type, u32 offset) {
	u8 cmd[4] = {  SPI_FLASH_CMD_SE, (u8)(offset >> 16), (u8)(offset >> 8), (u8) offset };
	if(type == NO_CHIP || type == FLASH_8MB) return 0xC8E13404;
	
	if(type < FLASH_256KB_1 && fill_buf == NULL) { 
			fill_buf = new u8[0x10000];
			memset(fill_buf, 0xff, 0x10000);
	}
	
	int res = SPIWaitWriteEnd(type);
	
	if(type >= FLASH_256KB_1) {
		if( (res = SPIEnableWriting(type)) ) return res;
		if( (res = SPIWriteRead(type, cmd, 4, NULL, 0, NULL, 0)) ) return res;
		if( (res = SPIWaitWriteEnd(type)) ) return res;
	}
	// Simulate the same behavior on EEPROM chips.
	else {
		u32 sz = SPIGetCapacity(type);
		int res = SPIWriteSaveData(type, 0, fill_buf, (sz < 0x10000) ? sz : 0x10000);
		return res;
	}
	return 0;
} */


// The following routine use code from savegame-manager:

/*
 * savegame_manager: a tool to backup and restore savegames from Nintendo
 *  DS cartridges. Nintendo DS and all derivative names are trademarks
 *  by Nintendo. EZFlash 3-in-1 is a trademark by EZFlash.
 *
 * auxspi.cpp: A thin reimplementation of the AUXSPI protocol
 *   (high level functions)
 *
 * Copyright (C) Pokedoc (2010)
 */
/* 
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License 
 * for more details.
 * 
 * You should have received a copy of the GNU General Public License along 
 * with this program; if not, write to the Free Software Foundation, Inc., 
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

 
int _SPIIsDataMirrored(CardType type, int size, bool* mirrored) {
	u32 offset0 = (size-1);        //      n KB
	u32 offset1 = (2*size-1);      //     2n KB
	
	u8 buf1;     //      +0k data        read -> write
	u8 buf2;     //      +n k data        read -> read
	u8 buf3;     //      +0k ~data          write
	u8 buf4;     //      +n k data new    comp buf2
	
	int res;
	
	if( (res = SPIReadSaveData(type, offset0, &buf1, 1)) ) return res;
	if( (res = SPIReadSaveData(type, offset1, &buf2, 1)) ) return res;
	buf3=~buf1;
	if( (res = SPIWriteSaveData(type, offset0, &buf3, 1)) ) return res;
	if( (res = SPIReadSaveData(type, offset1, &buf4, 1)) ) return res;
	if( (res = SPIWriteSaveData(type, offset0, &buf1, 1)) ) return res;
	
	*mirrored = buf2 != buf4;
	return 0;
}

int SPIGetCardType(CardType* type, int infrared) {
	u8 sr = 0;
	u32 jedec = 0;
	u32 tries = 0;
	CardType t = (infrared == 1) ? FLASH_INFRARED_DUMMY : FLASH_STD_DUMMY;
	int res; 
	u32 jedecOrderedList[] = { 0x204012, 0x621600, 0x204013, 0x621100, 0x204014, 0x202017};
	
	u32 maxTries = (infrared == -1) ? 2 : 1; // note: infrared = -1 fails 1/3 of the time
	while(tries < maxTries){ 
		res = SPIReadJEDECIDAndStatusReg(t, &jedec, &sr); // dummy
		if(res) return res;
		
		if ((sr & 0xfd) == 0x00 && (jedec != 0x00ffffff)) { break; }		
		if ((sr & 0xfd) == 0xF0 && (jedec == 0x00ffffff)) { t = EEPROM_512B; break; }
		if ((sr & 0xfd) == 0x00 && (jedec == 0x00ffffff)) { t = EEPROM_STD_DUMMY; break; }
		
		++tries;
		t = FLASH_INFRARED_DUMMY;
	}
	
	if(t == EEPROM_512B) { *type = t; return 0; }
	else if(t == EEPROM_STD_DUMMY) {
		bool mirrored = false;
		
		if( (res = _SPIIsDataMirrored(t, 8192, &mirrored)) ) return res;
		if(mirrored) t = EEPROM_8KB;
		else{
			if( (res = _SPIIsDataMirrored(t, 65536, &mirrored)) ) return res;
			if(mirrored) t = EEPROM_64KB;
			else t = EEPROM_128KB;
		}
		
		*type = t;
		return 0;
	}
	
	else if(t == FLASH_INFRARED_DUMMY) {
		if(infrared == 0) *type = NO_CHIP; // did anything go wrong?
		
		if(jedec == jedecOrderedList[0] || jedec == jedecOrderedList[1]) *type = FLASH_256KB_INFRARED;
		else *type = FLASH_512KB_INFRARED;
		
		return 0;
	}
	
	else {
		if(infrared == 1) *type = NO_CHIP; // did anything go wrong?
		if(jedec == 0x204017) { *type = FLASH_8MB; return 0; } // 8MB. savegame-manager: which one? (more work is required to unlock this save chip!)
		
		int i;
		
		for(i = 0; i < 6; ++i) {
			if(jedec == jedecOrderedList[i]) { *type = (CardType)((int) FLASH_256KB_1 + i); return 0; }  
		}
		
		*type = NO_CHIP;
		return 0;
	}
}

#include "gamecart.h"
#include "protocol.h"
#include "command_ctr.h"
#include "command_ntr.h"
#include "card_eeprom.h"
#include "spi.h"
#include "nds.h"
#include "ncch.h"
#include "ncsd.h"

#define CART_INSERTED (!(REG_CARDSTATUS & 0x1))

typedef struct {
    NcsdHeader ncsd;
    u32 card2_offset;
    u8  cinfo0[0x312 - (0x200 + sizeof(u32))];
    u32 rom_version;
    u8  cinfo1[0x1000 - (0x312 + sizeof(u32))];
    NcchHeader ncch;
    u8  padding[0x3000 - 0x200];
    u8  private[PRIV_HDR_SIZE];
    u8  unused[0x4000 + 0x8000 - PRIV_HDR_SIZE]; // 0xFF
    u32 cart_type;
    u32 cart_id;
    u64 cart_size;
    u64 data_size;
    u32 save_size;
    int save_type;
    u32 unused_offset;
} PACKED_ALIGN(16) CartDataCtr;

typedef struct {
    TwlHeader ntr_header;
    u8 ntr_padding[0x3000]; // 0x00
    u8 secure_area[0x4000];
    TwlHeader twl_header;
    u8 twl_padding[0x3000]; // 0x00
    u8 modcrypt_area[0x4000];
    u32 cart_type;
    u32 cart_id;
    u64 cart_size;
    u64 data_size;
    u32 save_size;
    int save_type;
    u32 arm9i_rom_offset;
} PACKED_ALIGN(16) CartDataNtrTwl;

u32 GetCartName(char* name, CartData* cdata) {
    if (cdata->cart_type & CART_CTR) {
        CartDataCtr* cdata_i = (CartDataCtr*)cdata;
        NcsdHeader* ncsd = &(cdata_i->ncsd);
        snprintf(name, 24, "%016llX_v%02lu", ncsd->mediaId, cdata_i->rom_version);
    }  else if (cdata->cart_type & CART_NTR) {
        CartDataNtrTwl* cdata_i = (CartDataNtrTwl*)cdata;
        TwlHeader* nds = &(cdata_i->ntr_header);
        snprintf(name, 24, "%.12s_%.6s_%02u", nds->game_title, nds->game_code, nds->rom_version);
    } else return 1;
    for (char* c = name; *c != '\0'; c++)
        if ((*c == ':') || (*c == '*') || (*c == '?') || (*c == '/') || (*c == '\\') || (*c == ' ')) *c = '_';
    return 0;
}

u32 InitCartRead(CartData* cdata) {
    memset(cdata, 0x00, sizeof(CartData));
    cdata->cart_type = CART_NONE;
    if (!CART_INSERTED) return 1;
    Cart_Init();
    cdata->cart_id = Cart_GetID();
    cdata->cart_type = (cdata->cart_id & 0x10000000) ? CART_CTR : CART_NTR;
    if (cdata->cart_type & CART_CTR) { // CTR cartridges
        memset(cdata, 0xFF, 0x4000 + PRIV_HDR_SIZE); // switch the padding to 0xFF
        
        // init, NCCH header
        static u32 sec_keys[4];
        u8* ncch_header = cdata->header + 0x1000;
        CTR_CmdReadHeader(ncch_header);
        Cart_Secure_Init((u32*) (void*) ncch_header, sec_keys);
        
        // NCSD header and CINFO
        // Cart_Dummy();
        // Cart_Dummy();
        CTR_CmdReadData(0, 0x200, 8, cdata->header);
        
        // safety checks, cart size
        NcsdHeader* ncsd = (NcsdHeader*) (void*) cdata->header;
        NcchHeader* ncch = (NcchHeader*) (void*) ncch_header;
        if ((ValidateNcsdHeader(ncsd) != 0) || (ValidateNcchHeader(ncch) != 0))
            return 1;
        cdata->cart_size = (u64) ncsd->size * NCSD_MEDIA_UNIT;
        cdata->data_size = GetNcsdTrimmedSize(ncsd);
        if (cdata->cart_size > 0x100000000) return 1; // carts > 4GB don't exist
        // else if (cdata->cart_size == 0x100000000) cdata->cart_size -= 0x200; // silent 4GB fix
        if (cdata->data_size > cdata->cart_size) return 1;
        
        // private header
        u8* priv_header = cdata->header + 0x4000;
        CTR_CmdReadUniqueID(priv_header);
        memcpy(priv_header + 0x40, &(cdata->cart_id), 4);
        memset(priv_header + 0x44, 0x00, 4);
        memset(priv_header + 0x48, 0xFF, 8);
        
        // save data
        u32 card2_offset = getle32(cdata->header + 0x200);
        if ((card2_offset != 0xFFFFFFFF) || (SPIGetCardType((CardType*) (CardType*) &(cdata->save_type), 0) != 0) || (cdata->save_type < 0)) {
            cdata->save_type = -1;
            cdata->save_size = 0;
        } else cdata->save_size = SPIGetCapacity((CardType) cdata->save_type);
    } else { // NTR/TWL cartridges
        // NTR header
        TwlHeader* nds_header = (void*)cdata->header;
        NTR_CmdReadHeader(cdata->header);
        if (!(*(cdata->header))) return 1; // error reading the header
        if (!NTR_Secure_Init(cdata->header, Cart_GetID(), 0)) return 1;
        
        // cartridge size, trimmed size, twl presets
        if (nds_header->device_capacity >= 15) return 1; // too big, not valid
        cdata->cart_size = (128 * 1024) << nds_header->device_capacity;
        cdata->data_size = nds_header->ntr_rom_size;
        cdata->arm9i_rom_offset = 0;
        
        // TWL header
        if (nds_header->unit_code != 0x00) { // DSi or NDS+DSi
            cdata->cart_type |= CART_TWL;
            cdata->data_size = nds_header->ntr_twl_rom_size;
            cdata->arm9i_rom_offset = nds_header->arm9i_rom_offset;
            if ((cdata->arm9i_rom_offset < nds_header->ntr_rom_size) ||
                (cdata->arm9i_rom_offset + MODC_AREA_SIZE > cdata->data_size))
                return 1; // safety first
            Cart_Init();
            NTR_CmdReadHeader(cdata->twl_header);
            if (!NTR_Secure_Init(cdata->twl_header, Cart_GetID(), 1)) return 1;
        }
        
        // last safety check
        if (cdata->data_size > cdata->cart_size) return 1;
        
        // save data
        u32 infrared = (*(nds_header->game_code) == 'I') ? 1 : 0;
        if ((SPIGetCardType((CardType*) &(cdata->save_type), infrared) != 0) || (cdata->save_type < 0)) {
            cdata->save_type = -1;
            cdata->save_size = 0;
        } else cdata->save_size = SPIGetCapacity((CardType) cdata->save_type);
    }
    return 0;
}

u32 ReadCartSectors(void* buffer, u32 sector, u32 count, CartData* cdata) {
    u8* buffer8 = (u8*) buffer;
    if (!CART_INSERTED) return 1;
    // header
    u32 header_sectors = (cdata->cart_type & CART_CTR) ? 0x4000/0x200 : 0x8000/0x200;
    if (sector < header_sectors) {
        u32 header_count = (sector + count > header_sectors) ? header_sectors - sector : count;
        memcpy(buffer8, cdata->header + (sector * 0x200), header_count * 0x200);
        buffer8 += header_count * 0x200;
        sector += header_count;
        count -= header_count;
    }
    if (!count) return 0;
    // actual cart reads
    if (cdata->cart_type & CART_CTR) {
        // don't read more than 1MB at once
        const u32 max_read = 0x800;
        u8* buff = buffer8;
        for (u32 i = 0; i < count; i += max_read) {
            // Cart_Dummy();
            // Cart_Dummy();
            CTR_CmdReadData(sector + i, 0x200, min(max_read, count - i), buff);
            buff += max_read * 0x200;
        }
        // overwrite the card2 savegame with 0xFF
        u32 card2_offset = getle32(cdata->header + 0x200);
        if ((card2_offset != 0xFFFFFFFF) &&
            ((card2_offset * 0x200) >= cdata->data_size) &&
            (sector + count > card2_offset)) {
            if (sector > card2_offset)
                memset(buffer8, 0xFF, (count * 0x200));
            else memset(buffer8 + (card2_offset - sector) * 0x200, 0xFF,
                (count - (card2_offset - sector)) * 0x200);
        }
    } else if (cdata->cart_type & CART_NTR) {
        u8* buff = buffer8;
        u32 off = sector * 0x200;
        for (u32 i = 0; i < count; i++, off += 0x200, buff += 0x200)
            NTR_CmdReadData(off, buff);
        // modcrypt area handling
        if ((cdata->cart_type & CART_TWL) &&
            ((sector+count) * 0x200 > cdata->arm9i_rom_offset) &&
            (sector * 0x200 < cdata->arm9i_rom_offset + MODC_AREA_SIZE)) {
            u32 arm9i_rom_offset = cdata->arm9i_rom_offset;
            u8* buffer_arm9i = buffer8;
            u32 offset_i = 0;
            u32 size_i = MODC_AREA_SIZE;
            if (arm9i_rom_offset < (sector * 0x200))
                offset_i = (sector * 0x200) - arm9i_rom_offset;
            else buffer_arm9i = buffer8 + (arm9i_rom_offset - (sector * 0x200));
            size_i = MODC_AREA_SIZE - offset_i;
            if (size_i > (count * 0x200) - (buffer_arm9i - buffer8))
                size_i = (count * 0x200) - (buffer_arm9i - buffer8);
            if (size_i) memcpy(buffer_arm9i, cdata->twl_header + 0x4000 + offset_i, size_i);
        }
    } else return 1;
    return 0;
}

u32 ReadCartBytes(void* buffer, u64 offset, u64 count, CartData* cdata) {
    if (!(offset % 0x200) && !(count % 0x200)) { // aligned data -> simple case 
        // simple wrapper function for ReadCartSectors(...)
        return ReadCartSectors(buffer, offset / 0x200, count / 0x200, cdata);
    } else { // misaligned data -> -___-
        u8* buffer8 = (u8*) buffer;
        u8 l_buffer[0x200];
        if (offset % 0x200) { // handle misaligned offset
            u32 offset_fix = 0x200 - (offset % 0x200);
            if (ReadCartSectors(l_buffer, offset / 0x200, 1, cdata) != 0) return 1;
            memcpy(buffer8, l_buffer + 0x200 - offset_fix, min(offset_fix, count));
            if (count <= offset_fix) return 0;
            offset += offset_fix;
            buffer8 += offset_fix;
            count -= offset_fix;
        } // offset is now aligned and part of the data is read
        if (count >= 0x200) { // otherwise this is misaligned and will be handled below
            if (ReadCartSectors(buffer8, offset / 0x200, count / 0x200, cdata) != 0) return 1;
        }
        if (count % 0x200) { // handle misaligned count
            u32 count_fix = count % 0x200;
            if (ReadCartSectors(l_buffer, (offset + count) / 0x200, 1, cdata) != 0) return 1;
            memcpy(buffer8 + count - count_fix, l_buffer, count_fix);
        }
        return 0;
    }
}

u32 ReadCartPrivateHeader(void* buffer, u64 offset, u64 count, CartData* cdata) {
    if (!(cdata->cart_type & CART_CTR)) return 1;
    if (offset < PRIV_HDR_SIZE) {
        u8* priv_hdr = cdata->header + 0x4000;
        if (offset + count > PRIV_HDR_SIZE) count = PRIV_HDR_SIZE - offset;
        memcpy(buffer, priv_hdr + offset, count);
    }
    return 0;
}

u32 ReadCartSave(u8* buffer, u64 offset, u64 count, CartData* cdata) {
    if (offset >= cdata->save_size) return 1;
    if (offset + count > cdata->save_size) count = cdata->save_size - offset;
    return (SPIReadSaveData((CardType) cdata->save_type, offset, buffer, count) == 0) ? 0 : 1;
}

u32 WriteCartSave(u8* buffer, u64 offset, u64 count, CartData* cdata) {
    if (offset >= cdata->save_size) return 1;
    if (offset + count > cdata->save_size) count = cdata->save_size - offset;
    return (SPIWriteSaveData((CardType) cdata->save_type, offset, buffer, count) == 0) ? 0 : 1;
}

u32 ReadCartSaveJedecId(u8* buffer, u64 offset, u64 count, CartData* cdata) {
    u8 ownBuf[JEDECID_AND_SREG_SIZE] = { 0 };
    u32 id;
    u8 sReg;
    if (offset >= JEDECID_AND_SREG_SIZE) return 1;
    if (offset + count > JEDECID_AND_SREG_SIZE) count = JEDECID_AND_SREG_SIZE - offset;
    SPIReadJEDECIDAndStatusReg((CardType) cdata->save_type, &id, &sReg);
    ownBuf[0] = (id >> 16) & 0xff;
    ownBuf[1] = (id >> 8) & 0xff;
    ownBuf[2] = id & 0xff;
    ownBuf[JEDECID_AND_SREG_SIZE - 1] = sReg;
    memcpy(buffer, ownBuf, count);
    return 0;
}

#include "imports.h"
#include "gfx.h"
#include "mdinfo.h"
#include "fsa.h"
#include "menu.h"
#include "utils.h"

#include <string.h>
#include <unistd.h>


int clone_mlc(int y_offset){
    int result = -1;

    MDReadInfo();
    MDBlkDrv *blkDrv = MDGetBlkDrv(0);
    uint64_t mlc_num_blocks = blkDrv->params.numBlocks;
    uint32_t mlc_block_size = blkDrv->params.blockSize;
    uint64_t mlc_size = mlc_num_blocks * mlc_block_size;

    MDBlkDrv *sdBlkDrv = MDGetBlkDrv(1);
    if(mlc_block_size != sdBlkDrv->params.blockSize){
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, y_offset, 0, "SD card has wrong block size %lu", sdBlkDrv->params.blockSize);
        return -1;
    }
    if(mlc_num_blocks > sdBlkDrv->params.numBlocks){
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, y_offset, 0, "SD card to small %llu blocks but %llu block required", sdBlkDrv->params.numBlocks, mlc_num_blocks);
        return -1;
    }

    if(mlc_num_blocks * 2 < sdBlkDrv->params.numBlocks){
        gfx_set_font_color(COLOR_ERROR);
        gfx_print(16, y_offset, 0, "SD card to big");
        return -1;
    }

    size_t buffer_size_lba = 128;
    size_t buffer_size = mlc_block_size * buffer_size_lba;

    
    void* io_buffer = IOS_HeapAllocAligned(CROSS_PROCESS_HEAP_ID, buffer_size, 0x40);
    if (!io_buffer) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_print(16, y_offset, 0, "Out of memory!");
        return -1;
    }

    int fsa_raw_handle = 0xFFFFFFFF;
    int fsa_raw_open_result = 0xFFFFFFFF;

    while (fsa_raw_open_result < 0)
    {
      // Open target device
      fsa_raw_open_result = FSA_RawOpen(fsaHandle, "/dev/mlc01", &fsa_raw_handle);
      usleep(1000);
    }

    gfx_print(20, y_offset, GfxPrintFlag_ClearBG, "Open SDcard");
    int fsa_raw_dest = 0;
    
    do{
      // Open target device
      fsa_raw_open_result = FSA_RawOpen(fsaHandle, "/dev/sdcard01", &fsa_raw_dest);
      usleep(1000);
    } while (fsa_raw_open_result < 0);

    y_offset += 20;

    int print_counter = 0;
    int mlc_result = 0;
    uint32_t read_errors2 = 0;
    uint32_t bad_blocks = 0;
    uint64_t lba = 0;
    uint8_t color = 0;
    do
    {
        uint64_t remaining_lbas = mlc_num_blocks - lba;
        if ( remaining_lbas < buffer_size_lba){
            buffer_size_lba = remaining_lbas;
            buffer_size = buffer_size_lba * mlc_block_size; 
        }
        //! print only every 4th time
        if(print_counter-- == 0)
        {
            SMC_SetNotificationLED(color?NOTIF_LED_BLUE:NOTIF_LED_ORANGE);
            color=!color;
            print_counter = 40;
            gfx_printf(20, y_offset, GfxPrintFlag_ClearBG, "mlc         = %011llu / %011llu, mlc res %08X, errors %lu, bad sectors %lu", lba * mlc_block_size, mlc_size, mlc_result, read_errors2, bad_blocks);
        }

        mlc_result = FSA_RawRead(fsaHandle, io_buffer, mlc_block_size, buffer_size_lba, lba, fsa_raw_handle);

        //! retry 5 times as there are read failures in several places
        if(mlc_result)
        {
            read_errors2++;
            gfx_printf(20, y_offset + 20, 0, "mlc_result: %d", mlc_result);
            memset(io_buffer, 0xbaad, buffer_size);
            //read one sector at a time
            for(uint64_t s = 0; s < buffer_size_lba; s++){
                int retry;
                for(retry=0; mlc_result && (retry < 5); retry++){
                    usleep(1000);
                    mlc_result = FSA_RawRead(fsaHandle, io_buffer, mlc_block_size, 1, lba + s, fsa_raw_handle);
                }
                if(mlc_result){
                    bad_blocks++;
                }
            }
            print_counter = 0; // print errors directly
        }
        int write_result = FSA_RawWrite(fsaHandle, io_buffer, mlc_block_size, buffer_size_lba, lba, fsa_raw_dest);
        if (write_result) {
            gfx_printf(20, y_offset + 20, GfxPrintFlag_ClearBG, "mlc: Failed to write %d bytes to file (result: -%08X)!", buffer_size, -write_result);
            goto error;
        }         
        lba += buffer_size_lba;
    }
    while(lba < mlc_num_blocks); //! TODO: make define MLC32_SECTOR_COUNT:

    result = 0;
    SMC_SetNotificationLED(NOTIF_LED_RED | NOTIF_LED_BLUE);

error:
    if(result)
        SMC_SetNotificationLED(NOTIF_LED_RED_BLINKING);
        
    IOS_HeapFree(CROSS_PROCESS_HEAP_ID, io_buffer);
    if (fsa_raw_dest) {
        FSA_RawClose(fsaHandle, fsa_raw_dest);
    }
    FSA_RawClose(fsaHandle, fsa_raw_handle);
    // last print to show "done"
    gfx_printf(20, y_offset, GfxPrintFlag_ClearBG, "mlc         = %011llu / %011llu, res %08X, errors %lu, bad sectors %lu", lba * mlc_block_size, mlc_size, mlc_result, read_errors2, bad_blocks);

    return result;
}

static void ssleep(uint32_t s){
    usleep(s * 1000 * 1000);
}

static void check_result(int res, int y_offset){
    if (res) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(160, y_offset, 0, "Error %x", res);
        waitButtonInput();
        gfx_set_font_color(COLOR_PRIMARY);
    }
}

int unmount_mlc(int y_offset){
    int res = -1;
    for(uint8_t i=0; res && (i< 100); i++){
        gfx_print(20, y_offset, GfxPrintFlag_ClearBG, "Waiting for System to settle...");
        ssleep(5);
        gfx_printf(20, y_offset, GfxPrintFlag_ClearBG, "Unmounting MLC...");
        res = FSA_Unmount(fsaHandle, "/vol/storage_mlc01", 2);
    }
    check_result(res, y_offset);
    return res;
}
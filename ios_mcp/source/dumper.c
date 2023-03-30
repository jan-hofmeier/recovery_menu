#include "imports.h"
#include "gfx.h"
#include "mdinfo.h"
#include "fsa.h"
#include "menu.h"
#include "utils.h"

#include <string.h>
#include <unistd.h>

#define SLC_SIZE 0x20000000
#define STR_BUFF_SZ 0x120

//mlc_end in bytes
int mlc_dump(int fsaHandle, int y_offset){
    int result = -1;

    MDReadInfo();
    MDBlkDrv *blkDrv = MDGetBlkDrv(0);
    uint64_t mlc_num_blocks = blkDrv->params.numBlocks;
    uint32_t mlc_block_size = blkDrv->params.blockSize;
    uint64_t mlc_size = mlc_num_blocks * mlc_block_size;

    size_t buffer_size_lba = 128;
    size_t buffer_size = mlc_block_size * buffer_size_lba;

    
    void* io_buffer = IOS_HeapAllocAligned(CROSS_PROCESS_HEAP_ID, buffer_size, 0x40);
    if (!io_buffer) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_print(16, y_offset, 0, "Out of memory!");
        return -1;
    }
    
    char* str_buffer = IOS_HeapAllocAligned(CROSS_PROCESS_HEAP_ID, STR_BUFF_SZ, 0x40);
    if (!str_buffer) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_print(16, y_offset, 0, "Out of memory!");
        goto error;
    }

    int logfile = 0;
    int res = FSA_OpenFile(fsaHandle, "/vol/storage_recovsd/mlc.log", "w", &logfile);
    if (res < 0) {
        gfx_printf(20, y_offset, 0, "Failed to open mlc.log for writing");
        goto error;
    }

    const uint16_t mid = blkDrv->params.mid_prv >> 16;
    snprintf(str_buffer, STR_BUFF_SZ, "mid: %X\n", mid);
    res = FSA_WriteFile(fsaHandle, str_buffer, 1, strnlen(str_buffer, STR_BUFF_SZ), logfile, 0);

    uint32_t cid[4];
    res = MDGetCID(blkDrv->deviceId, cid);
    if (res == 0) {
        snprintf(str_buffer, STR_BUFF_SZ, "CID: %08lx%08lx%08lx%08lx\n", cid[0], cid[1], cid[2], cid[3]);
        FSA_WriteFile(fsaHandle, str_buffer, 1, strnlen(str_buffer, STR_BUFF_SZ), logfile, 0);
    }

    int fsa_raw_handle = 0xFFFFFFFF;
    int fsa_raw_open_result = 0xFFFFFFFF;

    while (fsa_raw_open_result < 0)
    {
      // Open target device
      fsa_raw_open_result = FSA_RawOpen(fsaHandle, "/dev/mlc01", &fsa_raw_handle);
      usleep(1000);
    }

    int file = 0;
    int print_counter = 0;
    int current_file_index = 0;
    int mlc_result = 0;
    uint32_t read_errors2 = 0;
    uint32_t bad_blocks = 0;
    uint64_t lba = 0;
    uint8_t color = 0;
    do
    {
        setNotificationLED(color?NOTIF_LED_BLUE:NOTIF_LED_ORANGE);
        color=!color;
        uint64_t remaining_lbas = mlc_num_blocks - lba;
        if ( remaining_lbas < buffer_size_lba){
            buffer_size_lba = remaining_lbas;
            buffer_size = buffer_size_lba * mlc_block_size; 
        }
        if (!file) {
            snprintf(str_buffer, STR_BUFF_SZ, "/vol/storage_recovsd/mlc.bin.part%02d", ++current_file_index);
            int res = FSA_OpenFile(fsaHandle, str_buffer, "w", &file);
            if (res < 0) {
                gfx_printf(20, y_offset, 0, "Failed to open %s for writing", str_buffer);
                goto error;
            }
        }
        //! print only every 4th time
        if(print_counter-- == 0)
        {
            print_counter = 40;
            gfx_printf(20, y_offset, GfxPrintFlag_ClearBG, "mlc         = %011llu / %011llu, mlc res %08X, errors %lu, bad sectors %lu", lba * mlc_block_size, mlc_size, mlc_result, read_errors2, bad_blocks);
        }

        mlc_result = FSA_RawRead(fsaHandle, io_buffer, mlc_block_size, buffer_size_lba, lba, fsa_raw_handle);

        //! retry 5 times as there are read failures in several places
        if(mlc_result)
        {
            read_errors2++;
            gfx_printf(20, y_offset + 20, 0, "mlc_result: %d", mlc_result);
            snprintf(str_buffer, STR_BUFF_SZ, "Readerror: %08llX;%u\n", lba, mlc_result);
            FSA_WriteFile(fsaHandle, str_buffer, 1, strnlen(str_buffer, STR_BUFF_SZ), logfile, 0);
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
                if(retry>1){
                    snprintf(str_buffer, STR_BUFF_SZ, "%08llX;%u;%u\n", lba + s, retry, mlc_result);
                    FSA_WriteFile(fsaHandle, str_buffer, 1, strnlen(str_buffer, STR_BUFF_SZ), logfile, 0);
                }
            }
            print_counter = 0; // print errors directly
        }
        int write_result = FSA_WriteFile(fsaHandle, io_buffer, 1, buffer_size, file, 0);
        if (write_result != buffer_size) {
            gfx_printf(20, y_offset + 10, GfxPrintFlag_ClearBG, "mlc: Failed to write %d bytes to file(result: %d)!", buffer_size, write_result);
            goto error;
        }         
        lba += buffer_size_lba;
        //split every 2GB because of FAT32
        if ((lba * mlc_block_size % 0x80000000) == 0) {
            FSA_CloseFile(fsaHandle, file);
            file = 0;
        }
    }
    while(lba < mlc_num_blocks); //! TODO: make define MLC32_SECTOR_COUNT:

    result = 0;

error:
    IOS_HeapFree(CROSS_PROCESS_HEAP_ID, io_buffer);
    if (file) {
        FSA_CloseFile(fsaHandle, file);
    }
    if(str_buffer)
         IOS_HeapFree(CROSS_PROCESS_HEAP_ID, str_buffer);
    FSA_RawClose(fsaHandle, fsa_raw_handle);
    // last print to show "done"
    gfx_printf(20, y_offset, GfxPrintFlag_ClearBG, "mlc         = %011llu / %011llu, res %08X, errors %lu, bad sectors %lu", lba * mlc_block_size, mlc_size, mlc_result, read_errors2, bad_blocks);
    if(logfile)
        FSA_CloseFile(fsaHandle, logfile);

    setNotificationLED(NOTIF_LED_RED | NOTIF_LED_BLUE);
    return result;
}

int slc_dump(int fsaHandle, int y_offset, char *filename){
    int result = -1;
    uint64_t slc_size = SLC_SIZE;
    size_t buffer_size = 2048;

    void* io_buffer = IOS_HeapAllocAligned(CROSS_PROCESS_HEAP_ID, buffer_size, 0x40);
    if (!io_buffer) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_print(16, y_offset, GfxPrintFlag_ClearBG, "Out of memory!");
        return -1;
    }

    char* str_buffer = IOS_HeapAllocAligned(CROSS_PROCESS_HEAP_ID, STR_BUFF_SZ, 0x40);
    if (!str_buffer) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_print(16, y_offset, 0, "Out of memory!");
        goto error;
    }

    int logfile = 0;
    int res = FSA_OpenFile(fsaHandle, "/vol/storage_recovsd/slc.log", "w", &logfile);
    if (res < 0) {
        gfx_printf(20, y_offset, 0, "Failed to open slc.log for writing");
        goto error;
    }

    int fsa_raw_handle = 0xFFFFFFFF;
    int fsa_raw_open_result = 0xFFFFFFFF;

    while (fsa_raw_open_result < 0)
    {
      // Open target device
      fsa_raw_open_result = FSA_RawOpen(fsaHandle, "/dev/slc01", &fsa_raw_handle);
      usleep(1000);
    }

    int file = 0;
    res = FSA_OpenFile(fsaHandle, filename, "w", &file);
    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(26, y_offset, GfxPrintFlag_ClearBG, "Failed to create slc.bin: %x", res);
        goto error;
    }

    int read_error = 0;
    int print_counter = 0;
    int slc_result = 0;
    uint64_t offset = 0;
    int local_retry = 0;
    uint8_t color = 0;
    do
    {
        setNotificationLED(color?NOTIF_LED_BLUE:NOTIF_LED_ORANGE);
        //! print only every 4th time
        if(print_counter == 0)
        {
            print_counter = 40;
            gfx_printf(20, y_offset, GfxPrintFlag_ClearBG, "slc         = %09llu / %09llu, res %08X, error %d", offset, slc_size, slc_result, read_error);
        }
        else
        {
            --print_counter;
        }


        memset(io_buffer, 0xbaad, buffer_size);
        slc_result = FSA_RawRead(fsaHandle, io_buffer, 1, buffer_size, offset, fsa_raw_handle);

        //! retry 5 times as there are read failures in several places
        if(slc_result && (local_retry < 5))
        {
            gfx_printf(20, y_offset + 20, GfxPrintFlag_ClearBG, "slc_result: %d", slc_result);
            usleep(100);
            local_retry++;
            print_counter = 0; // print errors directly
            //snprintf(str_buffer, STR_BUFF_SZ, "%09llu;%u\n", offset, slc_result);
            snprintf(str_buffer, STR_BUFF_SZ, "Readerror: %05lX;%08X\n", (uint32_t)(offset / buffer_size), slc_result);
            FSA_WriteFile(fsaHandle, str_buffer, 1, strnlen(str_buffer, STR_BUFF_SZ), logfile, 0);
        }
        else
        {
            if(slc_result)
                read_error++;
            int write_result = FSA_WriteFile(fsaHandle, io_buffer, 1, buffer_size, file, 0);
            if (write_result != buffer_size) {
                gfx_printf(20, y_offset + 10, GfxPrintFlag_ClearBG, "slc: Failed to write %d bytes to file %s (result: %d)!", buffer_size, filename, write_result);
                goto error;
            }         
            offset += buffer_size;
            local_retry = 0;
        }
    }
    while(offset < slc_size);
    result = 0;

error:
    IOS_HeapFree(CROSS_PROCESS_HEAP_ID, io_buffer);
    if (file) {
         FSA_CloseFile(fsaHandle, file);
    }
    FSA_RawClose(fsaHandle, fsa_raw_handle);
    if(logfile)
        FSA_CloseFile(fsaHandle, logfile);
    // last print to show "done"
    gfx_printf(20, y_offset, GfxPrintFlag_ClearBG, "slc         = %09llu / %09llu, res %08X, error %d", offset, slc_size, slc_result, read_error);

    setNotificationLED(NOTIF_LED_RED | NOTIF_LED_BLUE);
    return result;
}

static void check_result(int res, int y_offset){
    if (res) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(160, y_offset, 0, "Error %x", res);
        waitButtonInput();
        gfx_set_font_color(COLOR_PRIMARY);
    }
}

static void ssleep(uint32_t s){
    usleep(s * 1000 * 1000);
}

void dump_nand_complete(int fsaHandle){
    gfx_print(20, 30, GfxPrintFlag_ClearBG, "Waiting for System to settle...");
    ssleep(20);
    gfx_printf(20, 30, GfxPrintFlag_ClearBG, "Unmounting MLC...");
    int res = FSA_Unmount(fsaHandle, "/vol/storage_mlc01", 2);
    check_result(res, 30);
    ssleep(10);
    gfx_printf(20, 45, GfxPrintFlag_ClearBG, "Unmounting SLC...");
    res = FSA_Unmount(fsaHandle, "/vol/system", 0);
    check_result(res, 45);

    slc_dump(fsaHandle, 75, "/vol/storage_recovsd/slc1.bin");
    //slc_dump(fsaHandle, 125, "/vol/storage_recovsd/slc2.bin");
    mlc_dump(fsaHandle, 125);
}
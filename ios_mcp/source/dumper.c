#include "imports.h"
#include "gfx.h"
#include "mdinfo.h"
#include "fsa.h"
#include "menu.h"

#include <string.h>
#include <unistd.h>

#define SLC_SIZE 0x20000000

//mlc_end in bytes
int mlc_dump(int fsaHandle, int y_offset){
    int result = -1;

    MDReadInfo();
    MDBlkDrv *blkDrv = MDGetBlkDrv(0);
    uint64_t mlc_num_blocks = blkDrv->params.numBlocks;
    uint32_t mlc_block_size = blkDrv->params.blockSize;
    uint64_t mlc_size = mlc_num_blocks * mlc_block_size;

    //size_t buffer_size = 0x10 * 0x1000;
    size_t buffer_size = mlc_block_size * 128;

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

    char filename[40];
    int file = 0;
    int retry = 0;
    int print_counter = 0;
    int current_file_index = 0;
    int mlc_result;
    uint64_t read_errors = 0;
    uint64_t bad_blocks = 0;
    uint64_t offset = 0;
    do
    {
        if (mlc_size - offset < buffer_size){
            buffer_size = mlc_size - offset;
        }
        if (!file) {
            snprintf(filename, sizeof(filename), "/vol/storage_recovsd/mlc.bin.part%02d", ++current_file_index);
            int res = FSA_OpenFile(fsaHandle, filename, "w", &file);
            if (res < 0) {
                gfx_printf(20, y_offset, 0, "Failed to open %s for writing", filename);
                goto error;
            }
        }
        //! print only every 4th time
        if(print_counter == 0)
        {
            print_counter = 40;
            gfx_printf(20, y_offset, GfxPrintFlag_ClearBG, "mlc         = %011llu / %011llu, mlc res %08X, retry %d", offset, mlc_size, mlc_result, retry);
        }
        else
        {
            --print_counter;
        }

        mlc_result = FSA_RawRead(fsaHandle, io_buffer, mlc_block_size, buffer_size / mlc_block_size, offset / mlc_block_size, fsa_raw_handle);

        //! retry 5 times as there are read failures in several places
        if(mlc_result)
        {
            read_errors++;
            gfx_printf(20, y_offset + 20, 0, "mlc_result: %d", mlc_result);
            memset(io_buffer, 0xbaad, buffer_size);
            //read one sector at a time
            for(uint64_t s = 0; s < buffer_size; s += mlc_block_size){
                for(int retry=0; mlc_result && (retry < 5); retry++){
                    usleep(100);
                    mlc_result = FSA_RawRead(fsaHandle, io_buffer, mlc_block_size, 1, offset + s, fsa_raw_handle);
                }
                if(mlc_result)
                    bad_blocks++;
            }
            print_counter = 0; // print errors directly
        }
        int write_result = FSA_WriteFile(fsaHandle, io_buffer, 1, buffer_size, file, 0);
        if (write_result != buffer_size) {
            gfx_printf(20, y_offset + 10, GfxPrintFlag_ClearBG, "mlc: Failed to write %d bytes to file %s (result: %d)!", buffer_size, filename, write_result);
            goto error;
        }         
        offset += buffer_size;
        //split every 2GB because of FAT32
        if ((offset % 0x80000000) == 0) {
            FSA_CloseFile(fsaHandle, file);
            file = 0;
        }
    }
    while(offset < mlc_size); //! TODO: make define MLC32_SECTOR_COUNT:

    result = 0;

error:
    IOS_HeapFree(CROSS_PROCESS_HEAP_ID, io_buffer);
    if (file) {
         FSA_CloseFile(fsaHandle, file);
    }
    FSA_RawClose(fsaHandle, fsa_raw_handle);
    // last print to show "done"
    gfx_printf(20, y_offset, GfxPrintFlag_ClearBG, "mlc         = %08llu / %08llu, mlc res %08X, retry %d", offset, mlc_size, mlc_result, retry);

    return result;
}

int slc_dump(int fsaHandle, int y_offset){
    int result = -1;
    uint64_t slc_size = SLC_SIZE;
    size_t buffer_size = 0x1000;

    void* io_buffer = IOS_HeapAllocAligned(CROSS_PROCESS_HEAP_ID, buffer_size, 0x40);
    if (!io_buffer) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_print(16, y_offset, GfxPrintFlag_ClearBG, "Out of memory!");
        return -1;
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
    int res = FSA_OpenFile(fsaHandle, "/vol/storage_recovsd/slc.bin", "w", &file);
    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(26, y_offset, GfxPrintFlag_ClearBG, "Failed to create slc.bin: %x", res);
        goto error;
    }

    int retry = 0;
    int print_counter = 0;
    int slc_result;
    uint64_t offset = 0;
    do
    {
        //! print only every 4th time
        if(print_counter == 0)
        {
            print_counter = 40;
            gfx_printf(20, y_offset, GfxPrintFlag_ClearBG, "slc         = %011llu / %011llu, mlc res %08X, retry %d", offset, slc_size, slc_result, retry);
        }
        else
        {
            --print_counter;
        }

        //! set flash erased byte to buffer
        memset(io_buffer, 0xbaad, buffer_size);
        slc_result = FSA_RawRead(fsaHandle, io_buffer, 1, buffer_size, offset, fsa_raw_handle);

        //! retry 5 times as there are read failures in several places
        if(slc_result && (retry < 5))
        {
            gfx_printf(20, y_offset + 20, GfxPrintFlag_ClearBG, "slc_result: %d", slc_result);
            usleep(100);
            retry++;
            print_counter = 0; // print errors directly
        }
        else
        {
            int write_result = FSA_WriteFile(fsaHandle, io_buffer, 1, buffer_size, file, 0);
            if (write_result != buffer_size) {
                gfx_printf(20, y_offset + 10, GfxPrintFlag_ClearBG, "slc: Failed to write %d bytes to file slc.bin (result: %d)!", buffer_size, write_result);
                goto error;
            }         
            offset += buffer_size;
        }
    }
    while(offset < slc_size); //! TODO: make define MLC32_SECTOR_COUNT:

    result = 0;

error:
    IOS_HeapFree(CROSS_PROCESS_HEAP_ID, io_buffer);
    if (file) {
         FSA_CloseFile(fsaHandle, file);
    }
    FSA_RawClose(fsaHandle, fsa_raw_handle);
    // last print to show "done"
    gfx_printf(20, y_offset, 0, "slc         = %08llu / %08llu, slc res %08X, retry %d", offset, slc_size, slc_result, retry);

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

    mlc_dump(fsaHandle, 60);
    slc_dump(fsaHandle, 90);
}
#include <stdio.h>
//#include <gctypes.h>
#include "imports.h"
#include "devices.h"
#include "sdio.h"
#include "mlcio.h"
//#include "sd_fat.h"
//#include "text.h"
#include "hardware_registers.h"
#include "svc.h"
#include "fs_config.h"

#include "gfx.h"

#define IO_BUFFER_SIZE 0x40000
#define IO_BUFFER_SPARE_SIZE (IO_BUFFER_SIZE+0x2000)

// the IO buffer is put behind everything else because there is no access to this region from IOS-FS it seems
unsigned char io_buffer[IO_BUFFER_SIZE]  __attribute__((aligned(0x40))) __attribute__((section(".io_buffer")));
unsigned char io_buffer_spare[IO_BUFFER_SPARE_SIZE]  __attribute__((aligned(0x40))) __attribute__((section(".io_buffer")));
unsigned long io_buffer_spare_pos;
int io_buffer_spare_status;

//! this one is required for the read function
static void slc_read_callback(int result, int priv)
{
    int *private_data = (int*)priv;
    private_data[1] = result;
    FS_SVC_RELEASEMUTEX(private_data[0]);
}

static int srcRead(void* deviceHandle, void *data_ptr, uint32_t offset, uint32_t sectors, int * result_array)
{
    int readResult = slcRead1_original(deviceHandle, 0, offset, sectors, SLC_BYTES_PER_SECTOR, data_ptr, slc_read_callback, (int)result_array);
    if(readResult == 0)
    {
        // wait for process to finish
        FS_SVC_ACQUIREMUTEX(result_array[0], 0);
        readResult = result_array[1];
    }
    return readResult;
}

int slc_dump(void *deviceHandle, const char* device, const int fsaHandle, const char* filename, int y_offset)
{
    //also create a mutex for synchronization with end of operation...
    int sync_mutex = FS_SVC_CREATEMUTEX(1, 1);
    FS_SVC_ACQUIREMUTEX(sync_mutex, 0);

    int result_array[2];
    result_array[0] = sync_mutex;

    uint32_t offset = 0;
    int readResult = 0;
    int writeResult = 0;
    int result = -1;
    uint32_t readSize = IO_BUFFER_SPARE_SIZE / SLC_BYTES_PER_SECTOR;

    FS_SLEEP(1000);

    int fileHandle = 0;
    int res = FSA_OpenFile(fsaHandle, filename, "w", &fileHandle);

    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, y_offset, 0, "Failed to open %s for writing: %x", filename, res);
        goto error;
    }

    do
    {
        gfx_printf(20, y_offset, 0, "%s     = %05X / 40000", device, offset);

        //! set flash erased byte to buffer
        FS_MEMSET(io_buffer_spare, 0xff, IO_BUFFER_SPARE_SIZE);
        io_buffer_spare_status = 0;
        io_buffer_spare_pos = 0;
        //readResult = readSlc(io_buffer, offset, (sizeof(io_buffer) / SLC_BYTES_PER_SECTOR), deviceHandle);
        readResult = srcRead(deviceHandle, io_buffer, offset, readSize, result_array);

        if (readResult || io_buffer_spare_status || io_buffer_spare_pos != IO_BUFFER_SPARE_SIZE) {
            
            gfx_printf(20, y_offset+10, 0, "Failed to read flash block. read result: 0x%08X spare status: 0x%08X spare pos: 0x%08X", readResult, io_buffer_spare_status, io_buffer_spare_pos);
            goto error;
        }
        //FS_SLEEP(10);
        writeResult = FSA_WriteFile(fsaHandle, io_buffer_spare, 1, readSize * SLC_BYTES_PER_SECTOR, fileHandle, 0);

        if (writeResult != readSize * SLC_BYTES_PER_SECTOR) {
            gfx_printf(20, y_offset + 10, 0, "%s: Failed to write %d bytes to file %s (result: %d)!", device, readSize * SLC_BYTES_PER_SECTOR, fileHandle, filename, writeResult);
            goto error;
        }
        offset += readSize;
    }
    while (offset < SLC_SECTOR_COUNT);

    result = 0;

error:
    FS_SVC_DESTROYMUTEX(sync_mutex);

    if (fileHandle) {
        FSA_CloseFile(fsaHandle, fileHandle);
    }
    // last print to show "done"
    gfx_printf(20, y_offset, 0, "%s     = %05X / 40000", device, offset);
    return result;
}

int mlc_dump(uint32_t mlc_end, int fsaHandle, int y_offset)
{
    uint32_t offset = 0;

    int result = -1;
    int retry = 0;
    int mlc_result = 0;
    int callback_result = 0;
    int write_result = 0;
    int print_counter = 0;
    int current_file_index = 0;


    char filename[40];
    int file = 0;
    do
    {
        if (!file) {
            FS_SNPRINTF(filename, sizeof(filename), "/vol/storage_recovsd/mlc.bin.part%02d", ++current_file_index);
            int res = FSA_OpenFile(fsaHandle, "/vol/storage_recovsd/otp.bin", "w", &file);
            if (res < 0) {
                gfx_printf(20, y_offset, 0, "Failed to open %s for writing", filename);
                goto error;
            }
        }
        //! print only every 4th time
        if(print_counter == 0)
        {
            print_counter = 4;
            gfx_printf(20, y_offset, 0, "mlc         = %08X / %08X, mlc res %08X, retry %d", offset, mlc_end, mlc_result, retry);
        }
        else
        {
            --print_counter;
        }

        //! set flash erased byte to buffer
        FS_MEMSET(io_buffer, 0xff, IO_BUFFER_SIZE);
        mlc_result = sdcard_readwrite(SDIO_READ, io_buffer, (IO_BUFFER_SIZE / MLC_BYTES_PER_SECTOR), MLC_BYTES_PER_SECTOR, offset, &callback_result, DEVICE_ID_MLC);

        if((mlc_result == 0) && (callback_result != 0))
        {
            mlc_result = callback_result;
        }

        //! retry 5 times as there are read failures in several places
        if((mlc_result != 0) && (retry < 5))
        {
            FS_SLEEP(100);
            retry++;
            print_counter = 0; // print errors directly
        }
        else
        {
            write_result = FSA_WriteFile(fsaHandle, io_buffer, 1, IO_BUFFER_SIZE, file, 0);
            if (write_result != IO_BUFFER_SIZE) {
                gfx_printf(20, y_offset + 10, 0, "mlc: Failed to write %d bytes to file %s (result: %d)!", IO_BUFFER_SIZE, file, filename, write_result);
                goto error;
            }
            offset += (IO_BUFFER_SIZE / MLC_BYTES_PER_SECTOR);
            if ((offset % 0x400000) == 0) {
                FSA_CloseFile(fsaHandle, file);
                file = 0;
            }
        }
    }
    while(offset < mlc_end); //! TODO: make define MLC32_SECTOR_COUNT:

    result = 0;

error:
    if (file) {
         FSA_CloseFile(fsaHandle, file);
    }
    // last print to show "done"
    gfx_printf(20, y_offset, 0, "mlc         = %08X / %08X, mlc res %08X, retry %d", offset, mlc_end, mlc_result, retry);

    return result;
}

int check_nand_type(void)
{
    //! check if MLC size is > 8GB
    if( FS_MMC_MLC_STRUCT[0x30/4] > 0x1000000)
    {
        return MLC_NAND_TYPE_32GB;
    }
    else
    {
        return MLC_NAND_TYPE_8GB;
    }
}

void dump_nand_complete(int fsaHandle)
{
    gfx_clear(COLOR_BACKGROUND);
    drawTopBar("SLC + MLC...");
    int offset_y = 10;

    //wait_format_confirmation();

    uint32_t mlc_sector_count = 0;
    mlc_init();
    FS_SLEEP(1000);

    int nand_type = check_nand_type();
    //uint32_t sdio_sector_count = FS_MMC_SDCARD_STRUCT[0x30/4];
    mlc_sector_count = FS_MMC_MLC_STRUCT[0x30/4];
    //uint32_t fat32_partition_offset = (MLC_BASE_SECTORS + mlc_sector_count);

    gfx_printf(20, offset_y, 0, "Detected %d GB MLC NAND type.", (nand_type == MLC_NAND_TYPE_8GB) ? 8 : 32);
    offset_y += 10;
    offset_y += 10;

    if (slc_dump(FS_SLC_PHYS_DEV_STRUCT,     "slc    ", fsaHandle, "/vol/storage_recovsd/slc.bin", offset_y))
        goto error;
    offset_y += 10;
    if (slc_dump(FS_SLCCMPT_PHYS_DEV_STRUCT, "slccmpt", fsaHandle, "/vol/storage_recovsd/slccmpt.bin", offset_y))
        goto error;
    offset_y += 10;
    if (mlc_dump(mlc_sector_count, fsaHandle, offset_y))
        goto error;
    offset_y += 10;
    offset_y += 20;

   gfx_printf(20, offset_y, 0, "Complete!");

error:
    offset_y += 20;
    gfx_printf(20, offset_y, 0, "Error!");
}

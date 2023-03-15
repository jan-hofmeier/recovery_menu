#ifndef _SDIO_H_
#define _SDIO_H_

#define SDIO_WRITE      0
#define SDIO_READ       1

void sdcard_init(void);
void sdcard_lock_mutex(void);
void sdcard_unlock_mutex(void);
int sdcard_readwrite(int is_read, void *data, uint32_t cnt, uint32_t block_size, uint32_t offset_blocks, int * out_callback_arg, int device_id);

#endif // _SDIO_H_

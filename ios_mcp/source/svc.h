#ifndef SVC_H
#define SVC_H

#define SHUTDOWN_TYPE_POWER_OFF             0
#define SHUTDOWN_TYPE_REBOOT                1

typedef struct
{
	void* ptr;
	uint32_t len;
	uint32_t unk;
}iovec_s;

void* svcAlloc(uint32_t heapid, uint32_t size);
void* svcAllocAlign(uint32_t heapid, uint32_t size, uint32_t align);
void svcFree(uint32_t heapid, void* ptr);
int svcOpen(char* name, int mode);
int svcClose(int fd);
int svcIoctl(int fd, uint32_t request, void* input_buffer, uint32_t input_buffer_len, void* output_buffer, uint32_t output_buffer_len);
int svcIoctlv(int fd, uint32_t request, uint32_t vector_count_in, uint32_t vector_count_out, iovec_s* vector);
int svcInvalidateDCache(void* address, uint32_t size);
int svcFlushDCache(void* address, uint32_t size);

void svcShutdown(int shutdown_type);
uint32_t svcRead32(uint32_t addr);

#endif

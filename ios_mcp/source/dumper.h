#ifndef DUMPER_H
#define DUMPER_H

void dump_nand_complete(int fsaHandle);
int mlc_clone(int fsaHandle, int y_offset);
int slc_dump(int fsaHandle, int y_offset, char *filename);
int unmount_mlc(int fsaHandle, int y_offset);
int unmount_slc(int fsaHandle, int y_offset);

#endif

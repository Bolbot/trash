#ifndef __UTILS_H__
#define __UTILS_H__

#include "defines.h"

char directory[PATHSIZE];	

size_t get_file_size(const char *fpath);
int prepare_file_to_send(const char *rel_path, size_t *file_size, char *mime_type);
FILE *redirect_output(const char *file_addr);
void setsignals();
int get_file_mime_type(const char *fpath, char *mimetype);

#endif

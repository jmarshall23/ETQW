/*
 * Internal MiniZip layouts used by the ETQW filesystem implementation.
 * The retail PDB records the 1.01e/zlib-1.2.3 Win32 layouts below.
 */
#ifndef ETQW_MINIZIP_UNZIP_INTERNAL_H
#define ETQW_MINIZIP_UNZIP_INTERNAL_H

#include "unzip.h"

typedef struct unz_file_info_internal_s {
	uLong offset_curfile;
} unz_file_info_internal;

typedef struct file_in_zip_read_info_s {
	char*				read_buffer;
	z_stream			stream;
	uLong				pos_in_zipfile;
	uLong				stream_initialised;
	uLong				offset_local_extrafield;
	uInt				size_local_extrafield;
	uLong				pos_local_extrafield;
	uLong				crc32;
	uLong				crc32_wait;
	uLong				rest_read_compressed;
	uLong				rest_read_uncompressed;
	zlib_filefunc_def	z_filefunc;
	voidpf				filestream;
	uLong				compression_method;
	uLong				byte_before_the_zipfile;
	int					raw;
} file_in_zip_read_info_s;

typedef struct unz_s {
	zlib_filefunc_def			z_filefunc;
	voidpf						filestream;
	unz_global_info				gi;
	uLong						byte_before_the_zipfile;
	uLong						num_file;
	uLong						pos_in_central_dir;
	uLong						current_file_ok;
	uLong						central_pos;
	uLong						size_central_dir;
	uLong						offset_central_dir;
	unz_file_info				cur_file_info;
	unz_file_info_internal		cur_file_info_internal;
	file_in_zip_read_info_s*	pfile_in_zip_read;
	int							encrypted;
#ifndef NOUNCRYPT
	unsigned long				keys[ 3 ];
	const unsigned long*		pcrc_32_tab;
#endif
} unz_s;

#endif

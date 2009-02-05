/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 * All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *	  must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *	  may be used to endorse or promote products derived from this software
 *	  without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.	IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)webdavd.h	8.1 (Berkeley) 6/5/93
 *
 * $Id: webdavd.h,v 1.25 2004/06/03 20:59:03 lutherj Exp $
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <stdio.h>
#include "../webdav_fs.kextproj/webdav_fs.kmodproj/webdav.h"
#include "webdav_memcache.h"
#include "webdav_inode.h"
#include <pthread.h>

/* webdav process structures */

struct webdav_put_struct
{
	int fd;
	char *locktoken;
};

struct webdav_lock_struct
{
	/* If you modify this structure, keep it word aligned. */
	int refresh;
	char *locktoken;
};

struct file_array_element
{
	/* Note that the download status field is first in the hopes that
	 * that will ensure that it is word aligned.  This field will
	 * be used for multithread synchronization so it needs to be
	 * word aligned to ensure that the loads and stores are atomic
	 * on multiprocessor machines.	If you modify the structure,
	 * keep it word aligned as this structure appears inside of
	 * an array.
	 */

	int download_status;
	int fd;
	uid_t uid;
	int deleted;								/* flag to indicate whether this file was deleted */
	char *uri;
	int32_t modtime;
	int32_t cachetime;
	int32_t lastvalidtime;
	webdav_filetype_t file_type;
	struct webdav_lock_struct lockdata;
};

#define CLEAR_GFILE_ENTRY(i) \
	{ \
		if (gfile_array[i].fd != -1) \
		{ \
			(void)close(gfile_array[i].fd); \
		} \
		gfile_array[i].fd = -1; \
		gfile_array[i].uid = 0; \
		gfile_array[i].deleted = 0; \
		gfile_array[i].download_status = 0; \
		gfile_array[i].file_type = 0; \
		if (gfile_array[i].uri) \
		{ \
			(void)free(gfile_array[i].uri); \
		} \
		gfile_array[i].uri = NULL; \
		gfile_array[i].cachetime = 0; \
		gfile_array[i].lastvalidtime = 0; \
		gfile_array[i].modtime = 0; \
	}

/* Note that gfile_array[index].fd needs to be valid when DEL_EXPIRED_CACHE()
   is called. */
#define DEL_EXPIRED_CACHE(index, current_time, timeout) \
	{ \
		if (gfile_array[index].cachetime) \
		{ \
			if (gfile_array[index].deleted) \
			{ \
				CLEAR_GFILE_ENTRY(index); \
			} \
			else \
			{ \
				if (current_time > (gfile_array[index].cachetime + timeout)) \
				{ \
					/* time to clear out this file */ \
					int error = webdav_set_file_handle(gfile_array[index].uri, \
						strlen(gfile_array[index].uri), -1); \
					if (!error || (error == ENOENT)) \
					{ \
						CLEAR_GFILE_ENTRY(index); \
						/* else if we can't clear out the file handle, \
							don't delete the cache, just move on */ \
					} \
				} \
			} \
		} \
	}

struct webdav_lookup_info
{
	enum filetype
	{
		dir, file
	} filetype;
};

struct webdav_stat_struct
{
	const char *orig_uri;
	struct stat *statbuf;
	int uid;
	int add_cache;
};

struct webdav_refreshdir_struct
{
	struct file_array_element *file_array_elem;
	int cache_appledoubleheader;	/* true if appledoubleheader property should be asked for and cached */
};

struct webdav_auth_struct
{
	char *webdav_auth_info;
	char *webdav_proxy_auth_info;
};

struct webdav_read_byte_info
{
	off_t byte_start;
	off_t num_bytes;
	char *uri;
	char *byte_addr;
	off_t num_read_bytes;
};

/*
 * webdav functions
 */
extern int webdav_lookup(struct webdav_request_lookup *request_lookup,
		struct webdav_reply_lookup *reply_lookup,
		int proxy_ok, int *a_socket);

extern int webdav_create(struct webdav_request_create *request_create,
		struct webdav_reply_create *reply_create,
		int proxy_ok, int *a_socket);

extern int webdav_open(struct webdav_request_open *request_open,
		struct webdav_reply_open *reply_open,
		int proxy_ok, int *a_socket);

extern int webdav_close(struct webdav_request_close *request_close,
		int proxy_ok, int *a_socket);

extern int webdav_getattr(struct webdav_request_getattr *request_getattr,
		struct webdav_reply_getattr *reply_getattr,
		int proxy_ok, int *a_socket);

extern int webdav_read(struct webdav_request_read *request_read,
		char **a_byte_addr, size_t *a_size,
		int proxy_ok, int *a_socket);

extern int webdav_fsync(struct webdav_request_fsync *request_fsync,
		int proxy_ok, int *a_socket);

extern int webdav_remove(struct webdav_request_remove *request_remove,
		int proxy_ok, int *a_socket);

extern int webdav_rename(struct webdav_request_rename *request_rename,
		int proxy_ok, int *a_socket);

extern int webdav_mkdir(struct webdav_request_mkdir *request_mkdir,
		struct webdav_reply_mkdir *reply_mkdir,
		int proxy_ok, int *a_socket);
		
extern int webdav_rmdir(struct webdav_request_rmdir *request_rmdir,
		int proxy_ok, int *a_socket);

extern int webdav_readdir(struct webdav_request_readdir *request_readdir,
		int proxy_ok, int *a_socket);

extern int webdav_statfs(struct webdav_request_statfs *request_statfs,
		struct webdav_reply_statfs *reply_statfs,
		int proxy_ok, int *a_socket);

extern int webdav_invalidate_caches(struct webdav_request_invalcaches *request_invalcaches);

extern int webdav_mount(char *key, int *a_mount_args,
		int proxy_ok, int *a_socket);

extern int webdav_lock(struct file_array_element *file_array_elem,
		int proxy_ok, int *a_socket);

extern int webdav_cachefile_init(void);

/* Global Defines */

#define WEBDAV_MAX_OPEN_FILES 512
#define WEBDAV_REQUEST_THREADS 5

/* WEBDAV_RLIMIT_NOFILE needs to be large enough for us to have all cache files
 * open (WEBDAV_MAX_OPEN_FILES), plus some for the defaults (stdin/out, etc),
 * the sockets opened by threads, the dup'd file descriptors passed back to
 * the kext, the socket used to communicate with the kext, and a few extras for
 * libraries we call that might need a few. The most I've ever seen in use is
 * just under 530, so 1024 is more than enough.
 */
#define WEBDAV_RLIMIT_NOFILE 1024

#define MAX_HTTP_LINELEN 4096		/* large enough for fully escaped path + 1K for other data */
#define SHORT_HTTP_LINELEN 1024		/* large enough for the shorter headers we generate */
#define WEBDAV_MAX_USERNAME_LEN 256
#define WEBDAV_MAX_PASSWORD_LEN 256
#define WEBDAV_STOP_DL_TIMEOUT 10000	/* 10 milliseconds */

#define WEBDAV_FS_DONT_CLOSE 1
#define WEBDAV_FS_CLOSE 0

#define WEBDAV_DOWNLOAD_IN_PROGRESS 1
#define WEBDAV_DOWNLOAD_FINISHED 2
#define WEBDAV_DOWNLOAD_TERMINATED 3
#define WEBDAV_DOWNLOAD_ABORTED 4

#define PRIVATE_LOAD_COMMAND "/System/Library/Extensions/webdav_fs.kext/Contents/Resources/load_webdav"
#define PRIVATE_UNMOUNT_COMMAND "/sbin/umount"
#define PRIVATE_UNMOUNT_FLAGS "-f"

/* WEBDAV_IO_TIMEOUT is the amount of time we'll wait for a server to
 * send a response.
 */
#define WEBDAV_IO_TIMEOUT 100			/* seconds */

#define WEBDAV_STATFS_TIMEOUT 60		/* Number of seconds gstatfsbuf is valid */
#define WEBDAV_PULSE_TIMEOUT "600"		/* Default time out = 10 minutes */
#define WEBDAV_CACHE_TIMEOUT 3600		/* 1 hour */
#define WEBDAV_CACHE_LOW_TIMEOUT 300	/* 5 minutes */
#define WEBDAV_CACHE_VALID_TIMEOUT 60	/* Number of seconds file is valid from lastvalidtime */

#define APPLEDOUBLEHEADER_LENGTH 82		/* length of AppleDouble header property */

/*
 * Global functions
 */
extern void activate(int so, int proxy_ok, int *socketptr);
extern void webdav_pulse_thread(void *arg);
extern void webdav_kill(int message);
extern int resolve_http_hostaddr(void);


/* Global variables */
extern int glast_array_element;
extern FILE * logfile;
extern struct file_array_element gfile_array[];
extern pthread_mutex_t garray_lock;
extern unsigned int gtimeout_val;
extern char * gtimeout_string;
extern webdav_memcache_header_t gmemcache_header;
extern char gmountpt[MAXPATHLEN];
extern struct statfs gstatfsbuf;
extern time_t gstatfstime;
extern char *http_hostname, *proxy_server, *dest_server;
extern char dest_path[MAXPATHLEN + 1];
extern char *append_to_file;
extern struct sockaddr_in http_sin;
extern int proxy_ok, proxy_exception, dest_port, host_port, proxy_port;
extern off_t webdav_first_read_len;
extern char *gUserAgentHeader;
extern uid_t process_uid;
extern int gSuppressAllUI;
extern char webdavcache_path[MAXPATHLEN + 1];
extern int gvfc_typenum;
extern webdav_file_record_t gfilerec;

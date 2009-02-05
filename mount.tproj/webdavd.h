/*
 * Copyright (c) 2000-2004 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#ifndef _WEBDAVD_H_INCLUDE
#define _WEBDAVD_H_INCLUDE

/*
 * DEBUG (which defines the state of DEBUG_ASSERT_PRODUCTION_CODE),
 * DEBUG_ASSERT_COMPONENT_NAME_STRING, and DEBUG_ASSERT_MESSAGE must be
 * defined before including AssertMacros.h
 */
/* we want non-quiet asserts to be logged */
#define DEBUG_ASSERT_PRODUCTION_CODE 0
/* and we want them logged as errors */
#define WEBDAV_LOG_LEVEL LOG_ERR
#define DEBUG_ASSERT_COMPONENT_NAME_STRING "webdavfs"
#define DEBUG_ASSERT_MESSAGE(componentNameString, assertionString, exceptionLabelString, errorString, fileName, lineNumber, errorCode) \
	webdav_debug_assert(componentNameString, assertionString, exceptionLabelString, errorString, fileName, lineNumber, errorCode)

#include <AssertMacros.h>

#include <sys/types.h>
#include <sys/param.h>
#include <sys/syslog.h>
#include "../webdav_fs.kextproj/webdav_fs.kmodproj/webdav.h"
#include <errno.h>
#include <mach/boolean.h>
#include <unistd.h>

/* Global Defines */

/*
 * WEBDAV_RLIMIT_NOFILE needs to be large enough for us to have WEBDAV_MAX_OPEN_FILES cache files
 * open, plus some for the defaults (stdin/out, etc), the sockets opened by threads,
 * the dup'd file descriptors passed back to the kext, the socket used to
 * communicate with the kext, and a few extras for libraries we call that might
 * need a few. The most I've ever seen in use is just under 530, so 1024 is
 * more than enough.
 */
#define WEBDAV_RLIMIT_NOFILE 1024
#define WEBDAV_MAX_OPEN_FILES 512

/* the number of threads available to handle requests from the kernel file system and downloads */
#define WEBDAV_REQUEST_THREADS 5

#define PRIVATE_CERT_UI_COMMAND "/System/Library/Filesystems/webdav.fs/Support/webdav_cert_ui.app/Contents/MacOS/webdav_cert_ui"
#define PRIVATE_LOAD_COMMAND "/System/Library/Extensions/webdav_fs.kext/Contents/Resources/load_webdav"
#define PRIVATE_UNMOUNT_COMMAND "/sbin/umount"
#define PRIVATE_UNMOUNT_FLAGS "-f"

/* the time interval (in seconds) for holding LOCKs on the server. The pulse thread runs at doublew this rate. */
#define WEBDAV_PULSE_TIMEOUT "600"		/* Default time out = 10 minutes */

#define APPLEDOUBLEHEADER_LENGTH 82		/* length of AppleDouble header property */

/* special file ID values */
#define WEBDAV_ROOTPARENTFILEID 2
#define WEBDAV_ROOTFILEID 3

/* sizes passed to the kernel file system */
#define WEBDAV_DIR_SIZE 2048			/* the directory size -- a made up value */
#define WEBDAV_IOSIZE (4*1024)			/* should be < PIPSIZ (8K) */

/* Global functions */
extern void webdav_debug_assert(const char *componentNameString, const char *assertionString, 
	const char *exceptionLabelString, const char *errorString, 
	const char *fileName, long lineNumber, int errorCode);
extern void webdav_kill(int message);

/* Global variables */
extern unsigned int gtimeout_val;		/* the pulse_thread runs at double this rate */
extern char * gtimeout_string;			/* the length of time LOCKs are held on on the server */
extern int gWebdavfsDebug;				/* TRUE if the WEBDAVFS_DEBUG environment variable is set */
extern uid_t gProcessUID;				/* the daemon's UID */
extern int gSuppressAllUI;				/* if TRUE, the mount requested that all UI be supressed */
extern char gWebdavCachePath[MAXPATHLEN + 1]; /* the current path to the cache directory */
extern int gSecureConnection;			/* if TRUE, the connection is secure */

/*
 * filesystem functions
 */
// there should be a filesystem.h and these prototypes should be moved there
#include "webdav_cache.h"

extern int filesystem_lookup(struct webdav_request_lookup *request_lookup,
		struct webdav_reply_lookup *reply_lookup);

extern int filesystem_create(struct webdav_request_create *request_create,
		struct webdav_reply_create *reply_create);

extern int filesystem_open(struct webdav_request_open *request_open,
		struct webdav_reply_open *reply_open);

extern int filesystem_close(struct webdav_request_close *request_close);

extern int filesystem_getattr(struct webdav_request_getattr *request_getattr,
		struct webdav_reply_getattr *reply_getattr);

extern int filesystem_read(struct webdav_request_read *request_read,
		char **a_byte_addr, size_t *a_size);

extern int filesystem_fsync(struct webdav_request_fsync *request_fsync);

extern int filesystem_remove(struct webdav_request_remove *request_remove);

extern int filesystem_rename(struct webdav_request_rename *request_rename);

extern int filesystem_mkdir(struct webdav_request_mkdir *request_mkdir,
		struct webdav_reply_mkdir *reply_mkdir);
		
extern int filesystem_rmdir(struct webdav_request_rmdir *request_rmdir);

extern int filesystem_readdir(struct webdav_request_readdir *request_readdir);

extern int filesystem_statfs(struct webdav_request_statfs *request_statfs,
		struct webdav_reply_statfs *reply_statfs);

extern int filesystem_invalidate_caches(struct webdav_request_invalcaches *request_invalcaches);

extern int filesystem_mount(int *a_mount_args);

extern int filesystem_lock(struct node_entry *node);

extern int filesystem_init(int typenum);

#endif /*ifndef _WEBDAVD_H_INCLUDE */

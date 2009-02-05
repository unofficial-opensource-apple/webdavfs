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
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)activate.c	8.3 (Berkeley) 4/28/95
 *
 *	$Id: activate.c,v 1.16 2004/06/03 20:59:03 lutherj Exp $
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/time.h>

#include "../webdav_fs.kextproj/webdav_fs.kmodproj/webdav.h"
#include "webdavd.h"
#include "webdav_requestqueue.h"

/*****************************************************************************/

static int get_request(int so, int *operation, void *key, size_t klen)
{
	int error;
	struct iovec iov[2];
	struct msghdr msg;
	int n;
	
	iov[0].iov_base = (caddr_t)operation;
	iov[0].iov_len = sizeof(int);
	iov[1].iov_base = key;
	iov[1].iov_len = klen;
	
	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = iov;
	msg.msg_iovlen = 2;
	
	n = recvmsg(so, &msg, 0);
	
	if ( n >= (int)(sizeof(int) + sizeof(struct webdav_cred)) )
	{
		/* the message received is large enough to contain operation and webdav_cred */
		error = 0;
		/* terminate the string (if any) at the end of the key */
		n -= sizeof(int);
		((char *)key)[n] = '\0';
	}
	else if ( n < 0 )
	{
		/* error from recvmsg */
		error = errno;
		syslog(LOG_ERR, "get_request: recvmsg(): %s", strerror(error));
	}
	else
	{
		/* the message was too short */
		error = EINVAL;
		syslog(LOG_ERR, "get_request: short message");
	}
	
	return ( error );
}

/*****************************************************************************/

static void send_reply(int so, void *data, size_t size, int error)
{
	int n;
	struct iovec iov[2];
	struct msghdr msg;
	int send_error = error;
	
	/* if the connection is down, let the kernel know */
	if ( get_gconnectionstate() == WEBDAV_CONNECTION_DOWN )
	{
		send_error |= WEBDAV_CONNECTION_DOWN_MASK;
	}
	
	iov[0].iov_base = (caddr_t)&send_error;
	iov[0].iov_len = sizeof(send_error);
	if ( size != 0 )
	{
		iov[1].iov_base = (caddr_t)data;
		iov[1].iov_len = size;
	}
	
	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = iov;
	
	if ( size != 0 )
	{
		msg.msg_iovlen = 2;
	}
	else
	{
		msg.msg_iovlen = 1;
	}
	
	n = sendmsg(so, &msg, 0);
	if (n < 0)
	{
		syslog(LOG_ERR, "send_reply: sendmsg(): %s", strerror(errno));
	}
}

/*****************************************************************************/

void activate(int so, int proxy_ok, int *socketptr)
{
	int error;
	int operation;
	char key[(NAME_MAX + 1) + sizeof(union webdav_request)];
	size_t num_bytes;
	char *bytes;
	union webdav_reply reply;
	
	/* get the request from the socket */
	error = get_request(so, &operation, key, sizeof(key));
	if ( !error )
	{
#ifdef DEBUG	
		syslog(LOG_ERR, "activate: %s(%d)",
				(operation==WEBDAV_LOOKUP) ? "LOOKUP" :
				(operation==WEBDAV_CREATE) ? "CREATE" :
				(operation==WEBDAV_OPEN) ? "OPEN" :
				(operation==WEBDAV_CLOSE) ? "CLOSE" :
				(operation==WEBDAV_GETATTR) ? "GETATTR" :
				(operation==WEBDAV_SETATTR) ? "SETATTR" :
				(operation==WEBDAV_READ) ? "READ" :
				(operation==WEBDAV_WRITE) ? "WRITE" :
				(operation==WEBDAV_FSYNC) ? "FSYNC" :
				(operation==WEBDAV_REMOVE) ? "REMOVE" :
				(operation==WEBDAV_RENAME) ? "RENAME" :
				(operation==WEBDAV_MKDIR) ? "MKDIR" :
				(operation==WEBDAV_RMDIR) ? "RMDIR" :
				(operation==WEBDAV_READDIR) ? "READDIR" :
				(operation==WEBDAV_STATFS) ? "STATFS" :
				(operation==WEBDAV_UNMOUNT) ? "UNMOUNT" :
				(operation==WEBDAV_INVALCACHES) ? "INVALCACHES" :
				"???",
				operation
				);
#endif

		/* call the function to handle the request */
		switch ( operation )
		{
			case WEBDAV_LOOKUP:
				error = webdav_lookup((struct webdav_request_lookup *)key,
					(struct webdav_reply_lookup *)&reply, proxy_ok, socketptr);
				send_reply(so, (void *)&reply, sizeof(struct webdav_reply_lookup), error);
				break;

			case WEBDAV_CREATE:
				error = webdav_create((struct webdav_request_create *)key,
					(struct webdav_reply_create *)&reply, proxy_ok, socketptr);
				send_reply(so, (void *)&reply, sizeof(struct webdav_reply_create), error);
				break;

			case WEBDAV_OPEN:
				error = webdav_open((struct webdav_request_open *)key,
					(struct webdav_reply_open *)&reply, proxy_ok, socketptr);
				send_reply(so, (void *)&reply, sizeof(struct webdav_reply_open), error);
				break;

			case WEBDAV_CLOSE:
				error = webdav_close((struct webdav_request_close *)key,
					proxy_ok, socketptr);
				send_reply(so, (void *)0, 0, error);
				break;

			case WEBDAV_GETATTR:
				error = webdav_getattr((struct webdav_request_getattr *)key,
					(struct webdav_reply_getattr *)&reply, proxy_ok, socketptr);
				send_reply(so, (void *)&reply, sizeof(struct webdav_reply_getattr), error);
				break;

			case WEBDAV_READ:
				error = webdav_read((struct webdav_request_read *)key,
					&bytes, &num_bytes, proxy_ok, socketptr);
				send_reply(so, (void *)bytes, (int)num_bytes, error);
				if (bytes)
				{
					free(bytes);
				}
				break;

			case WEBDAV_FSYNC:
				error = webdav_fsync((struct webdav_request_fsync *)key,
					proxy_ok, socketptr);
				send_reply(so, (void *)0, 0, error);
				break;

			case WEBDAV_REMOVE:
				error = webdav_remove((struct webdav_request_remove *)key,
					proxy_ok, socketptr);
				send_reply(so, (void *)0, 0, error);
				break;

			case WEBDAV_RENAME:
				error = webdav_rename((struct webdav_request_rename *)key,
					proxy_ok, socketptr);
				send_reply(so, (void *)0, 0, error);
				break;

			case WEBDAV_MKDIR:
				error = webdav_mkdir((struct webdav_request_mkdir *)key,
					(struct webdav_reply_mkdir *)&reply, proxy_ok, socketptr);
				send_reply(so, (void *)&reply, sizeof(struct webdav_reply_mkdir), error);
				break;

			case WEBDAV_RMDIR:
				error = webdav_rmdir((struct webdav_request_rmdir *)key,
					proxy_ok, socketptr);
				send_reply(so, (void *)0, 0, error);
				break;

			case WEBDAV_READDIR:
				error = webdav_readdir((struct webdav_request_readdir *)key,
					proxy_ok, socketptr);
				send_reply(so, (void *)0, 0, error);
				break;

			case WEBDAV_STATFS:
				error = webdav_statfs((struct webdav_request_statfs *)key,
					(struct webdav_reply_statfs *)&reply, proxy_ok, socketptr);
				send_reply(so, (void *)&reply, sizeof(struct webdav_reply_statfs), error);
				break;
			
			case WEBDAV_UNMOUNT:
				webdav_kill(-2);	/* tell the main select loop to exit */
				send_reply(so, (void *)0, 0, error);
				break;

			case WEBDAV_INVALCACHES:
				error = webdav_invalidate_caches((struct webdav_request_invalcaches *)key);
				send_reply(so, (void *)0, 0, error);
				break;

			default:
				error = EOPNOTSUPP;
				break;
		}

#ifdef DEBUG
		if (error)
		{
			syslog(LOG_ERR, "activate: error %d, %s(%d)", error,
					(operation==WEBDAV_LOOKUP) ? "LOOKUP" :
					(operation==WEBDAV_CREATE) ? "CREATE" :
					(operation==WEBDAV_OPEN) ? "OPEN" :
					(operation==WEBDAV_CLOSE) ? "CLOSE" :
					(operation==WEBDAV_GETATTR) ? "GETATTR" :
					(operation==WEBDAV_SETATTR) ? "SETATTR" :
					(operation==WEBDAV_READ) ? "READ" :
					(operation==WEBDAV_WRITE) ? "WRITE" :
					(operation==WEBDAV_FSYNC) ? "FSYNC" :
					(operation==WEBDAV_REMOVE) ? "REMOVE" :
					(operation==WEBDAV_RENAME) ? "RENAME" :
					(operation==WEBDAV_MKDIR) ? "MKDIR" :
					(operation==WEBDAV_RMDIR) ? "RMDIR" :
					(operation==WEBDAV_READDIR) ? "READDIR" :
					(operation==WEBDAV_STATFS) ? "STATFS" :
					(operation==WEBDAV_UNMOUNT) ? "UNMOUNT" :
					(operation==WEBDAV_INVALCACHES) ? "INVALCACHES" :
					"???",
					operation
					);
		}
#endif
	}
	else
	{
		send_reply(so, NULL, 0, error);
	}

	close(so);
}

/*****************************************************************************/

void webdav_pulse_thread(void *arg)
{
	struct timeval tv;
	struct timezone tz;
	int i, error, proxy_ok;
	int mysocket;
	
	proxy_ok = *((int *)arg);

	mysocket = socket(PF_INET, SOCK_STREAM, 0);
	if ( mysocket >= 0 )
	{
		while ( TRUE )
		{
			error = pthread_mutex_lock(&garray_lock);
			if ( error )
			{
				syslog(LOG_ERR, "webdav_pulse_thread: pthread_mutex_lock(): %s", strerror(error));
				webdav_kill(-1);	/* tell the main select loop to force unmount */
				break;
			}
			
			gettimeofday(&tv, &tz);

#ifdef DEBUG
			syslog(LOG_ERR, "Pulse thread running at %s", ctime((long *) &tv.tv_sec));
#endif

			for ( i = 0; i < WEBDAV_MAX_OPEN_FILES; ++i )
			{
				if ( (gfile_array[i].fd != -1) &&
					 gfile_array[i].lockdata.locktoken &&
					 gfile_array[i].lockdata.refresh )
				{
					error = webdav_lock(&(gfile_array[i]), proxy_ok, &mysocket);
					if ( error )
					{
						/*
						 * if error, then we've lost our lock and exclusive access
						 * to the resource on the WebDAV server. Clear lock token or
						 * just let things fail at the fsync/close.
						 *
						 * XXX we should attempt to reacquire the lock.
						 */
#ifdef DEBUG
						syslog(LOG_INFO, "webdav_pulse_thread: webdav_lock(): %s", strerror(error));
#endif
					}
				}
				
				/*
				 * even if there was an error, move on to the
				 * next one and try to refresh it.
				 */
				if ( (gfile_array[i].fd != -1) && gfile_array[i].cachetime )
				{
					if ( gfile_array[i].deleted )
					{
						CLEAR_GFILE_ENTRY(i);
					}
					else
					{
						if ( tv.tv_sec > (gfile_array[i].cachetime + WEBDAV_CACHE_TIMEOUT) )
						{
							/* time to clear out this file */
							if ( !webdav_set_file_handle(gfile_array[i].uri, strlen(gfile_array[i].uri), -1) )
							{
								CLEAR_GFILE_ENTRY(i);
							}
							/* else if we can't clear out the file handle -- don't delete the cache, just move on */
						}
					}
				}
			} /* end for loop */

			error = pthread_mutex_unlock(&garray_lock);
			if ( error )
			{
				syslog(LOG_ERR, "webdav_pulse_thread: pthread_mutex_unlock(): %s", strerror(error));
				webdav_kill(-1);	/* tell the main select loop to force unmount */
				break;
			}
			
			(void) sleep(gtimeout_val/2);
		}
	}
	else
	{
		syslog(LOG_ERR, "webdav_pulse_thread: socket(): %s", strerror(errno));
		webdav_kill(-1);	/* tell the main select loop to force unmount */
	}
	
}

/*****************************************************************************/

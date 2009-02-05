/* Copyright (c) 1995 NeXT Computer, Inc. All Rights Reserved */
/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * The NEXTSTEP Software License Agreement specifies the terms
 * and conditions for redistribution.
 *
 *	@(#)webdav_vfsops.c 8.6 (Berkeley) 1/21/94
 */

/*
 * webdav Filesystem
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/socket.h>
#include <sys/ubc.h>
#include <sys/fcntl.h>
#include <sys/unistd.h>
#include <sys/ioccom.h>
#include <vfs/vfs_support.h>
#include <sys/vnode_if.h>
#include <sys/sysctl.h>

#include "webdav.h"

/*****************************************************************************/

/*
 * Global variables defined in other modules:
 */
extern struct vnodeopv_desc webdav_vnodeop_opv_desc;

/*
 * webdav File System globals:
 */

char webdav_name[MFSNAMELEN] = "webdav";
static long webdav_mnt_cnt = 0;
/*
 *¥ vfs_fsadd: second parameter should be (void **)?
 * If so, then the following should be a (void *).
 */
static struct vfsconf webdav_vfsconf;


#define WEBDAV_MAX_REFS   256
static struct open_associatecachefile *webdav_ref_table[WEBDAV_MAX_REFS];


static struct vnodeopv_desc *webdav_vnodeop_opv_desc_list[1] =
{
	&webdav_vnodeop_opv_desc
};

/*****************************************************************************/

/* initialize the webdav_ref_table */
static void webdav_init_ref_table(void)
{
	int ref;
	
	for ( ref = 0; ref < WEBDAV_MAX_REFS; ++ref )
	{
		webdav_ref_table[ref] = NULL;
	}
}

/*****************************************************************************/

/* assign a entry in the webdav_ref_table to associatecachefile and return ref */
__private_extern__
int webdav_assign_ref(struct open_associatecachefile *associatecachefile, int *ref)
{
	int i;
	int error;
	
	while ( TRUE )
	{
		for ( i = 0; i < WEBDAV_MAX_REFS; ++i )
		{
			if ( webdav_ref_table[i] == NULL )
			{
				webdav_ref_table[i] = associatecachefile;
				*ref = i;
				return ( 0 );
			}
		}
		
		/* table is completely used... sleep a little and then try again */
		error = tsleep(&lbolt, PCATCH, "webdav_get_open_ref", 0);
		if ( error && (error != EWOULDBLOCK) )
		{
			/* bail out on errors -- the user probably hit control-c */
			*ref = -1;
			return ( EIO );
		}
	}
}

/*****************************************************************************/

/* translate a ref to a pointer to struct open_associatecachefile */
static int webdav_translate_ref(int ref, struct open_associatecachefile **associatecachefile)
{
	/* range check ref */
	if ( (ref < 0) || (ref >= WEBDAV_MAX_REFS) )
	{
		return ( EIO );
	}
	else
	{
		/* translate */
		*associatecachefile = webdav_ref_table[ref];
		if ( *associatecachefile == NULL )
		{
			/* ref wasn't valid */
			return ( EIO );
		}
		else
		{
			/* ref was valid */
			return ( 0 );
		}
	}
}

/*****************************************************************************/

/* release a ref */
__private_extern__
void webdav_release_ref(int ref)
{
	if ( (ref >= 0) && (ref < WEBDAV_MAX_REFS) )
	{
		webdav_ref_table[ref] = NULL;
	}
}

/*****************************************************************************/

/*
 * Called once from vfs_fsadd() to allow us to initialize.
 */
static int webdav_init(struct vfsconf *vfsp)
{
	#pragma unused(vfsp)
	
	START_MARKER("webdav_init");
	
	webdav_init_ref_table();
	webdav_hashinit();  /* webdav_hashdestroy() is called from webdav_fs_module_stop() */
	
	RET_ERR("webdav_init", 0);
}

/*****************************************************************************/

/*
 * Mount a file system
 */
static int webdav_mount(struct mount *mp, vnode_t devvp, caddr_t data, vfs_context_t context)
//static int webdav_mount(struct mount *mp, vnode_t devvp, caddr_t data, struct proc *p)
{
	#pragma unused(devvp, context)
	struct webdav_args args;
	struct webdavmount *fmp = NULL;
	vnode_t rvp;
	size_t size;
	int error;
	struct timeval tv;
	struct timespec ts;

	START_MARKER("webdav_mount");
	
	++webdav_mnt_cnt;

	/*
	 * Update is a no-op
	 */
	if (vfs_isupdate(mp))
	{
		error = EOPNOTSUPP;
		goto bad;
	}

	/* Hammer in noexec so that the wild web won't endanger our users */
	vfs_setflags(mp, MNT_NOEXEC);

	/*
	 * copy in the mount arguments
	 */
	error = copyin(CAST_USER_ADDR_T(data), (caddr_t) & args, sizeof(struct webdav_args));
	if (error)
	{
		goto bad;
	}
	
	if (args.pa_version != 1)
	{
		/* invalid version argument */
		error = EINVAL;
		goto bad;
	}

	/*
	 * create the webdavmount
	 */

	MALLOC(fmp, struct webdavmount *, sizeof(struct webdavmount), M_TEMP, M_WAITOK);
	bzero(fmp, sizeof(struct webdavmount));

	fmp->pm_status = WEBDAV_MOUNT_SUPPORTS_STATFS;	/* assume yes until told no */
	if ( args.pa_flags & WEBDAV_SUPPRESSALLUI )
	{
		/* suppress UI when connection is lost */
		fmp->pm_status |= WEBDAV_MOUNT_SUPPRESS_ALL_UI;
	}
	fmp->pm_mountp = mp;
	
	/* Get the volume name from the args and store it for webdav_packvolattr() */
	MALLOC(fmp->pm_vol_name, caddr_t, NAME_MAX + 1, M_TEMP, M_WAITOK);
	bzero(fmp->pm_vol_name, NAME_MAX + 1);
	error = copyinstr(CAST_USER_ADDR_T(args.pa_vol_name), fmp->pm_vol_name, NAME_MAX, &size);
	if (error)
	{
		goto bad;
	}

	/* Get the server sockaddr from the args */
	MALLOC(fmp->pm_socket_name, struct sockaddr *, args.pa_socket_namelen, M_TEMP, M_WAITOK);
	error = copyin(CAST_USER_ADDR_T(args.pa_socket_name), fmp->pm_socket_name, args.pa_socket_namelen);
	if (error)
	{
		goto bad;
	}

	fmp->pm_dir_size = args.pa_dir_size;
	fmp->pm_lookup_timeout = args.pa_lookup_timeout;

	/* copy pathconf values from the args */
	fmp->pm_link_max = args.pa_link_max;
	fmp->pm_name_max = args.pa_name_max;
	fmp->pm_path_max = args.pa_path_max;
	fmp->pm_pipe_buf = args.pa_pipe_buf;
	fmp->pm_chown_restricted = args.pa_chown_restricted;
	fmp->pm_no_trunc = args.pa_no_trunc;

	vfs_setfsprivate(mp, (void *)fmp);
	vfs_getnewfsid(mp);
	
	(void)copyinstr(CAST_USER_ADDR_T(args.pa_mntfromname), vfs_statfs(mp)->f_mntfromname, MNAMELEN - 1, &size);
	bzero(vfs_statfs(mp)->f_mntfromname + size, MNAMELEN - size);

	/*
	 * create the root vnode
	 */
	
	microtime(&tv);
	TIMEVAL_TO_TIMESPEC(&tv, &ts);
	
	error = webdav_get(mp, NULLVP, 1, NULL,
		args.pa_root_obj_ref, args.pa_root_fileid, VDIR, ts, ts, ts, fmp->pm_dir_size, &rvp);
	if (error)
	{
		goto bad;
	}
	/* hold on to rvp until unmount */
	error = vnode_ref(rvp);
	(void) vnode_put(rvp);
	if (error)
	{
		goto bad;
	}
	
	fmp->pm_root = rvp;
	
	return (0);

bad:

	--webdav_mnt_cnt;

	/* free any memory allocated before failure */
	if ( fmp != NULL )
	{
		if ( fmp->pm_vol_name != NULL )
		{
			FREE(fmp->pm_vol_name, M_TEMP);
		}
		if ( fmp->pm_socket_name != NULL )
		{
			FREE(fmp->pm_socket_name, M_TEMP);
		}
		FREE(fmp, M_TEMP);
		
		/* clear the webdavmount in the mount point so anyone looking at it will see it's gone */
		vfs_setfsprivate(mp, NULL);
	}
	
	RET_ERR("webdav_mount", error);
}

/*****************************************************************************/

/*
 * Called just after VFS_MOUNT(9) but before first access.
 */
static int webdav_start(struct mount *mp, int flags, vfs_context_t context)
{
	#pragma unused(mp, flags, context)
	
	START_MARKER("webdav_start");
	
	RET_ERR("webdav_start", 0);
}

/*****************************************************************************/

/*
 * Unmount a file system
 */
static int webdav_unmount(struct mount *mp, int mntflags, vfs_context_t context)
{
	#pragma unused(context)
	vnode_t rootvp = VFSTOWEBDAV(mp)->pm_root;
	int error = 0;
	int flags = 0;
	struct webdavmount *fmp;
	int server_error;
	struct webdav_request_unmount request_unmount;

	START_MARKER("webdav_unmount");
	
	fmp = VFSTOWEBDAV(mp);

	if (mntflags & MNT_FORCE)
	{
		flags |= FORCECLOSE;
		fmp->pm_status |= WEBDAV_MOUNT_FORCE;   /* let other code know to stop trying */
	}

	error = vflush(mp, rootvp, flags);
	if (error)
	{
		return (error);
	}

	if ( vnode_isinuse(rootvp, 1) && !(flags & FORCECLOSE) )
	{
		return (EBUSY);
	}

	webdav_copy_creds(context, &request_unmount.pcr);

	/* send the message and ignore errors */
	(void) webdav_sendmsg(WEBDAV_UNMOUNT, fmp,
		&request_unmount, sizeof(struct webdav_request_unmount), 
		NULL, 0, 
		&server_error, NULL, 0);

	/*
	 * Release reference on underlying root vnode
	 */
	vnode_get(rootvp);
	vnode_rele(rootvp);   /* reference taken in webdav_mount() */
	vnode_recycle(rootvp);
	vnode_put(rootvp);

	/* clear the webdavmount in the mount point so anyone looking at it will see it's gone */
	vfs_setfsprivate(mp, NULL);
		
	/*
	 * Throw away the webdavmount structure and related allocated memory
	 */
	FREE(fmp->pm_vol_name, M_TEMP);
	FREE(fmp->pm_socket_name, M_TEMP);
	FREE(fmp, M_TEMP);
	
	--webdav_mnt_cnt;
	
	RET_ERR("webdav_unmount", error);
}

/*****************************************************************************/

/*
 * Get (vnode_get) the vnode for the root directory of the file system.
 */
static int webdav_root(struct mount *mp, struct vnode **vpp, vfs_context_t context)
{
	#pragma unused(context)
	int error;
	vnode_t vp;

	START_MARKER("webdav_root");
	
	/*
	 * Return locked reference to root.
	 */
	vp = VFSTOWEBDAV(mp)->pm_root;
	error = vnode_get(vp);
	if ( error )
	{
		*vpp = NULLVP;
	}
	else
	{
		*vpp = vp;
	}
	
	RET_ERR("webdav_root", error);
}

/*****************************************************************************/

/*
 * Return information about a mounted file system.
 */
static int webdav_statfs(struct mount *mp, struct vfsstatfs *sbp, vfs_context_t context)
{
	struct webdavmount *fmp;
	struct webdav_reply_statfs reply_statfs;
	int error = 0;
	int server_error = 0;
	struct webdav_request_statfs request_statfs;

	START_MARKER("webdav_statfs");
	
	fmp = VFSTOWEBDAV(mp);

	bzero(&reply_statfs, sizeof(struct webdav_reply_statfs));

	/* get the values from the server if we can.  If not, make them up */

	if (fmp->pm_status & WEBDAV_MOUNT_SUPPORTS_STATFS)
	{
		/* while there's a WEBDAV_STATFS request outstanding, sleep */
		while (fmp->pm_status & WEBDAV_MOUNT_STATFS)
		{
			fmp->pm_status |= WEBDAV_MOUNT_STATFS_WANTED;
			(void) tsleep((caddr_t)&fmp->pm_status, PRIBIO, "webdav_statfs", 0);
		}
		
		/* we're making a request so grab the token */
		fmp->pm_status |= WEBDAV_MOUNT_STATFS;

		webdav_copy_creds(context, &request_statfs.pcr);
		request_statfs.root_obj_ref = VTOWEBDAV(VFSTOWEBDAV(mp)->pm_root)->pt_obj_ref;

		error = webdav_sendmsg(WEBDAV_STATFS, fmp,
			&request_statfs, sizeof(struct webdav_request_statfs), 
			NULL, 0,
			&server_error, &reply_statfs, sizeof(struct webdav_reply_statfs));

		/* we're done, so release the token */
		fmp->pm_status &= ~WEBDAV_MOUNT_STATFS;
		
		/* if anyone else is waiting, wake them up */
		if ( fmp->pm_status & WEBDAV_MOUNT_STATFS_WANTED )
		{
			fmp->pm_status &= ~WEBDAV_MOUNT_STATFS_WANTED;
			wakeup((caddr_t)&fmp->pm_status);
		}
		/* now fall through */
	}

	/* Note, at this point error is set to the value we want to
	  return,  Don't set error without restructuring the routine
	  Note also that we are not returning server_error.	*/

	sbp->f_flags = 0;
	if (!reply_statfs.fs_attr.f_bsize)
	{
		sbp->f_bsize = S_BLKSIZE;
	}
	else
	{
		sbp->f_bsize = reply_statfs.fs_attr.f_bsize;
	}

	if (!reply_statfs.fs_attr.f_iosize)
	{
		sbp->f_iosize = WEBDAV_IOSIZE;
	}
	else
	{
		sbp->f_iosize = reply_statfs.fs_attr.f_iosize;
	}
	
	if (!reply_statfs.fs_attr.f_blocks)
	{
		/* Did we actually get f_blocks back from the WebDAV server? */
		if ( error == 0 && server_error == 0 )
		{
			/* server must not support getting quotas so stop trying */
			fmp->pm_status &= ~WEBDAV_MOUNT_SUPPORTS_STATFS;
		}
		sbp->f_blocks = WEBDAV_NUM_BLOCKS;
		sbp->f_bfree = sbp->f_bavail = WEBDAV_FREE_BLOCKS;
	}
	else
	{
		sbp->f_blocks = reply_statfs.fs_attr.f_blocks;
		sbp->f_bfree = reply_statfs.fs_attr.f_bfree;
		if (!reply_statfs.fs_attr.f_bavail)
		{
			sbp->f_bavail = sbp->f_bfree;
		}
		else
		{
			sbp->f_bavail = reply_statfs.fs_attr.f_bavail;
		}
	}

	if (!reply_statfs.fs_attr.f_files)
	{
		sbp->f_files = WEBDAV_NUM_FILES;
	}

	if (!reply_statfs.fs_attr.f_ffree)
	{
		sbp->f_ffree = WEBDAV_FREE_FILES;
	}
	else
	{
		sbp->f_ffree = reply_statfs.fs_attr.f_ffree;
	}

	RET_ERR("webdav_statfs", error);
}

/*****************************************************************************/

/*
 * webdav_sysctl handles the VFS_CTL_QUERY request which tells interested
 * parties if the connection with the remote server is up or down.
 * It also handles receiving and converting cache file descriptors to vnode_t
 * for webdav_open().
 */
static int webdav_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
	void *newp, size_t newlen, vfs_context_t context)
{
	#pragma unused(oldlenp, newp, newlen)
	int error;
	struct sysctl_req *req;
	struct vfsidctl vc;
	struct mount *mp;
	struct webdavmount *fmp;
	struct vfsquery vq;

	START_MARKER("webdav_sysctl");
	
	switch ( name[0] )
	{
		case WEBDAV_ASSOCIATECACHEFILE_SYSCTL:
			{
				struct webdavcachefileref cachefileref;
				struct open_associatecachefile *associatecachefile;
				vnode_t vp;
				
				if ( namelen > 3 )
				{
					error = ENOTDIR;	/* overloaded */
				}
				
				/* make sure there is no incoming data */
				if ( newlen != 0 )
				{
					printf("webdav_sysctl: newlen != 0\n");
					error = EINVAL;
					break;
				}
				
				cachefileref.ref = name[1];
				cachefileref.fd = name[2];
								
				error = webdav_translate_ref(cachefileref.ref, &associatecachefile);
				if ( error != 0 )
				{
					printf("webdav_sysctl: webdav_translate_ref() failed\n");
					break;
				}
				
				error = file_vnode(cachefileref.fd, &vp);
				if ( error != 0 )
				{
					printf("webdav_sysctl: file_vnode() failed\n");
					break;
				}
				
				/* take a reference on it so that it won't go away (reference released by webdav_close_nommap()) */
				vnode_get(vp);
				vnode_ref(vp);
				vnode_put(vp);
				
				(void) file_drop(cachefileref.fd);

				/* store the cache file's vnode in the webdavnode */
				associatecachefile->cachevp = vp;
				
				/* store the PID of the process that called us for validation in webdav_open */
				associatecachefile->pid = vfs_context_proc(context)->p_pid;
				
				/* success */
				error = 0;
			}
			break;
			
		case VFS_CTL_QUERY:
			if ( namelen > 1 )
			{
				error = ENOTDIR;	/* overloaded */
			}
			req = oldp;	/* we're new style vfs sysctl. */
			error = SYSCTL_IN(req, &vc, sizeof(vc));
			if ( !error )
			{
				mp = vfs_getvfs(&vc.vc_fsid);
				if ( mp == NULL )
				{
					error = ENOENT;
				}
				else
				{
					fmp = VFSTOWEBDAV(mp);
					bzero(&vq, sizeof(vq));
					if ( fmp != NULL )
					{
						if ( fmp->pm_status & WEBDAV_MOUNT_TIMEO )
						{
							vq.vq_flags |= VQ_NOTRESP;
						}
						if ( fmp->pm_status & WEBDAV_MOUNT_DEAD )
						{
							vq.vq_flags |= VQ_DEAD;
						}
					}
					error = SYSCTL_OUT(req, &vq, sizeof(vq));
				}
			}
			break;
			
		default:
			error = EOPNOTSUPP;
			break;
	}

	RET_ERR("webdav_sysctl", error);
}

/*****************************************************************************/

/* unsupported VFS operations */

#define webdav_quotactl ((int (*)(struct mount *mp, int cmds, uid_t uid, \
		caddr_t arg, enum uio_seg segflg, vfs_context_t context)) eopnotsupp)

#define webdav_sync ((int (*)(struct mount *mp, int waitfor, \
		vfs_context_t context)) nullop)

#define webdav_vget	((int (*)(struct mount *mp, void *ino, struct vnode **vpp, \
		vfs_context_t context)) eopnotsupp)
		
#define webdav_fhtovp ((int (*)(struct mount *mp, struct fid *fhp, \
		struct vnode **vpp, vfs_context_t context)) eopnotsupp)

#define webdav_vptofh ((int (*)(struct vnode *vp, struct fid *fhp, \
		vfs_context_t context)) eopnotsupp)

/*****************************************************************************/

struct vfsops webdav_vfsops = {
	webdav_mount,
	webdav_start,
	webdav_unmount,
	webdav_root,
	webdav_quotactl,
	webdav_statfs,
	webdav_sync,
	webdav_vget,
	webdav_fhtovp,
	webdav_vptofh,
	webdav_init,
	webdav_sysctl,
};

/*****************************************************************************/

__private_extern__
kern_return_t webdav_fs_module_start(struct kmod_info *ki, void *data)
{
	#pragma unused(ki, data)
	errno_t error;
	struct vfs_fsentry vfe;

	bzero(&vfe, sizeof(struct vfs_fsentry));
	vfe.vfe_vfsops = &webdav_vfsops;	/* vfs operations */
	vfe.vfe_vopcnt = 1;					/* # of vnodeopv_desc being registered (reg, spec, fifo ...) */
	vfe.vfe_opvdescs = webdav_vnodeop_opv_desc_list; /* null terminated;  */
	vfe.vfe_fstypenum = 0;				/* historic file system type number (we have none)*/
	strncpy(vfe.vfe_fsname, webdav_name, strlen(webdav_name));
	vfe.vfe_flags = VFS_TBLNOTYPENUM; /* defines the FS capabilities */

	error = vfs_fsadd(&vfe, &webdav_vfsconf);

	return (error ? KERN_FAILURE : KERN_SUCCESS);
}

/*****************************************************************************/

__private_extern__
kern_return_t webdav_fs_module_stop(struct kmod_info *ki, void *data)
{
	#pragma unused(ki, data)
	int error;

	if (webdav_mnt_cnt == 0)
	{
		error = vfs_fsremove(&webdav_vfsconf);
		if ( error == 0 )
		{
			/* free up any memory allocated */
			webdav_hashdestroy();
		}
	}
	else
	{
		error = EBUSY;
	}
	
	return (error ? KERN_FAILURE : KERN_SUCCESS);
}

/*****************************************************************************/

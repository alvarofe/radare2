/* radare2 - LGPL - Copyright 2017 - condret, pancake */

#include <r_io.h>
#include <sdb.h>
#include <string.h>

R_API bool r_io_desc_init(RIO* io) {
	if (!io || io->files) {
		return false;
	}
	//fd is signed
	io->files = r_id_storage_new (3, 0x80000000);   
	if (!io->files) {
		return false;
	}
	return true;
}

//shall be used by plugins for creating descs
//XXX kill mode
R_API RIODesc* r_io_desc_new(RIO* io, RIOPlugin* plugin, const char* uri,
			      int flags, int mode, void* data) {
	ut32 fd32 = 0;
	RIODesc* desc;
	// this is because emscript is a bitch
	if (!io || !plugin || !uri || !io->files) {
		return NULL;
	}
	if (!(desc = R_NEW0 (RIODesc))) {
		return NULL;
	}
	if (!r_id_pool_grab_id (io->files->pool, &fd32)) {
		free (desc);
		return NULL;
	}
	desc->fd = fd32;
	desc->io = io;
	desc->plugin = plugin;
	desc->data = data;
	desc->flags = flags;
	//because the uri-arg may live on the stack
	desc->uri = strdup (uri);
	return desc;
}

R_API void r_io_desc_free(RIODesc* desc) {
	if (desc) {
		free (desc->uri);
		free (desc->referer);
		free (desc->name);
		if (desc->io && desc->io->files) {
			r_id_storage_delete (desc->io->files, desc->fd);
		}
//		free (desc->plugin);
	}
	free (desc);
}

R_API bool r_io_desc_add(RIO* io, RIODesc* desc) {
	if (!desc || !io) {
		return false;
	}
	//just for the case when plugins cannot use r_io_desc_new
	if (!desc->io) {
		desc->io = io;                                          
	}
	if (!r_id_storage_set (io->files, desc, desc->fd)) {
		eprintf ("You are using this API incorrectly\n");
		eprintf ("fd %d was probably not generated by this RIO-instance\n", desc->fd);
		r_sys_backtrace ();
		return false;
	}
	return true;
}

R_API bool r_io_desc_del(RIO* io, int fd) {
	RIODesc* desc;
	if (!io || !io->files || !(desc = r_id_storage_get (io->files, fd))) {
		return false;
	}
	r_io_desc_free (desc);
	if (desc == io->desc) {
		io->desc = NULL;
	}
	return true;
}

R_API RIODesc* r_io_desc_get(RIO* io, int fd) {
	if (!io || !io->files) {
		return NULL;
	}
	return (RIODesc*) r_id_storage_get (io->files, fd);
}

R_API ut64 r_io_desc_seek(RIODesc* desc, ut64 offset, int whence) {
	if (!desc || !desc->plugin || !desc->plugin->lseek) {
		return (ut64) - 1;
	}
	return desc->plugin->lseek (desc->io, desc, offset, whence);
}

R_API ut64 r_io_desc_size(RIODesc* desc) {
	ut64 off, ret;
	if (!desc || !desc->plugin || !desc->plugin->lseek) {
		return 0LL;
	}
	off = desc->plugin->lseek (desc->io, desc, 0LL, R_IO_SEEK_CUR);
	ret = desc->plugin->lseek (desc->io, desc, 0LL, R_IO_SEEK_END);
	//what to do if that seek fails?
	desc->plugin->lseek (desc->io, desc, off, R_IO_SEEK_CUR);
	return ret;
}

R_API bool r_io_desc_exchange(RIO* io, int fd, int fdx) {
	RIODesc* desc, * descx;
	SdbListIter* iter;
	RIOMap* map;
	if (!(desc = r_io_desc_get (io, fd)) || !(descx = r_io_desc_get (io, fdx)) || !io->maps) {
		return false;
	}
	desc->fd = fdx;
	descx->fd = fd;
	r_id_storage_set (io->files, desc,  fdx);
	r_id_storage_set (io->files, descx, fd);
	if (io->p_cache) {
		Sdb* cache = desc->cache;
		desc->cache = descx->cache;
		descx->cache = cache;
		r_io_desc_cache_cleanup (desc);
		r_io_desc_cache_cleanup (descx);
	}
	ls_foreach (io->maps, iter, map) {
		if (map->fd == fdx) {
			map->flags &= (desc->flags | R_IO_EXEC);
		} else if (map->fd == fd) {
			map->flags &= (descx->flags | R_IO_EXEC);
		}
	}
	return true;
}

R_API int r_io_desc_get_pid(RIO* io, int fd) {
	RIODesc* desc;
	if (!io || !io->files) {
		//-1 is reserved for plugin internal errors
		return -2;
	}
	if (!(desc = r_io_desc_get (io, fd))) {
		return -3;
	}
	if (!desc->plugin) {
		return -4;
	}
	if (!desc->plugin->isdbg) {
		return -5;
	}
	if (!desc->plugin->getpid) {
		return -6;
	}
	return desc->plugin->getpid (desc);
}

R_API int r_io_desc_get_tid(RIO* io, int fd) {
	RIODesc* desc;
	if (!io || !io->files) {
		return -2;              //-1 is reserved for plugin internal errors
	}
	if (!(desc = r_io_desc_get (io, fd))) {
		return -3;
	}
	if (!desc->plugin) {
		return -4;
	}
	if (!desc->plugin->isdbg) {
		return -5;
	}
	if (!desc->plugin->gettid) {
		return -6;
	}
	return desc->plugin->gettid (desc);
}

static bool desc_fini_cb(void* user, void* data, ut32 id) {
	RIODesc* desc = (RIODesc*) data;
	if (desc->plugin && desc->plugin->close) {
		desc->plugin->close (desc);
	}
	return true;
}

//closes all descs and frees all descs and io->files
R_API bool r_io_desc_fini(RIO* io) {
	if (!io || !io->files) {
		return false;
	}
	r_id_storage_foreach (io->files, desc_fini_cb, io);
	r_id_storage_free (io->files);
	io->files = NULL;
	//no map-cleanup here, to keep it modular useable
	io->desc = NULL;
	return true;
}

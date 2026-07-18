/* hostfs.c - host filesystem virtual ISA device for lilpc */
/* Made by a machine. PUBLIC DOMAIN (CC0-1.0) */

#include "hostfs.h"
#include "lilpc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#ifndef __EMSCRIPTEN__
#include <sys/statvfs.h>
#endif
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <strings.h>

#define TRACE(pc, ...) do { \
	if ((pc)->debug & DBG_HOSTFS) fprintf(stderr, "hostfs: " __VA_ARGS__); \
} while (0)

/* ---- helpers ---- */

static int alloc_file(hostfs_t *hfs)
{
	for (int i = 0; i < HOSTFS_MAX_FILES; i++) {
		if (hfs->files[i].fd < 0)
			return i;
	}
	return -1;
}

static int alloc_search(hostfs_t *hfs)
{
	for (int i = 0; i < HOSTFS_MAX_SEARCHES; i++) {
		if (hfs->searches[i].cache_slot < 0)
			return i;
	}
	return -1;
}

static int dircache_get(hostfs_t *hfs, const char *dir_path);

/* build a host path from endpoint base + DOS filename in xfer_buf.
 * resolves each path component through the directory cache for
 * case-insensitive matching of 8.3 names to real filenames. */
static int build_path(hostfs_t *hfs, int ep, char *out, size_t out_sz)
{
    if (ep < 0 || ep >= HOSTFS_MAX_ENDPOINTS || !hfs->ep[ep].path[0])
        return -1;

    char dos_name[HOSTFS_XFER_SIZE];
    int nlen = 0;
    for (int i = 0; i < HOSTFS_XFER_SIZE - 1; i++) {
        if (hfs->xfer_buf[i] == '\0')
            break;
        dos_name[nlen++] = hfs->xfer_buf[i];
    }
    dos_name[nlen] = '\0';

    char *p = dos_name;
    if (nlen >= 2 && p[1] == ':')
        p += 2;
    if (*p == '\\')
        p++;

    int off = snprintf(out, out_sz, "%s", hfs->ep[ep].path);
    if (off > 0 && out[off - 1] == '/')
        off--;
    out[off] = '\0';

    while (*p) {
        char component[256];
        int ci = 0;
        while (*p && *p != '\\' && ci < 255)
            component[ci++] = *p++;
        component[ci] = '\0';
        if (*p == '\\')
            p++;
        if (!component[0])
            continue;

        char upper[256];
        for (int i = 0; i <= ci; i++)
            upper[i] = toupper((unsigned char)component[i]);

        int slot = dircache_get(hfs, out);
        const char *resolved = NULL;
        if (slot >= 0) {
            hostfs_dircache_t *dc = &hfs->dircache[slot];
            for (int i = 0; i < dc->count; i++) {
                if (strcasecmp(dc->entries[i].dos_name, upper) == 0) {
                    resolved = dc->entries[i].real_name;
                    break;
                }
            }
        }

        if (resolved) {
            off += snprintf(out + off, out_sz - off, "/%s", resolved);
        } else {
            char lower[256];
            for (int i = 0; i <= ci; i++)
                lower[i] = tolower((unsigned char)component[i]);
            off += snprintf(out + off, out_sz - off, "/%s", lower);
        }
    }

    if (strstr(out, "/../") || strstr(out, "/.."))
        return -1;

    return 0;
}

/* convert Unix time_t to DOS date/time packed format */
static void time_to_dos(time_t t, uint16_t *dos_time, uint16_t *dos_date)
{
	struct tm *tm = localtime(&t);
	if (!tm) {
		*dos_time = 0;
		*dos_date = 0;
		return;
	}
	*dos_time = (tm->tm_sec / 2) | (tm->tm_min << 5) | (tm->tm_hour << 11);
	*dos_date = tm->tm_mday | ((tm->tm_mon + 1) << 5) |
		((tm->tm_year - 80) << 9);
}

/* convert stat mode to DOS attribute byte */
static uint8_t mode_to_attr(mode_t m)
{
	uint8_t attr = 0;
	if (S_ISDIR(m))
		attr |= HOSTFS_ATTR_DIR;
	if (!(m & S_IWUSR))
		attr |= HOSTFS_ATTR_READONLY;
	if (!S_ISDIR(m))
		attr |= HOSTFS_ATTR_ARCHIVE;
	return attr;
}

/* ---- 8.3 name generation and directory cache ----
 * TODO: DOSLFN "tunnel effect" - when a program creates a file using a
 * short name, the redirector should map it back to a long filename if
 * one existed previously. See DOSLFN doslfn.txt for details. */

static bool is_dos_char(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '0' && c <= '9') return true;
    if (c >= 128) return true;
    if (strchr("!#$%&'()-@^_`{}~", c)) return true;
    return false;
}

static bool gen_short_name(const char *name, char base[9], char ext[4])
{
    bool lossy = false;
    const char *p = name;

    while (*p == '.') { p++; lossy = true; }
    if (!*p) {
        base[0] = '\0';
        ext[0] = '\0';
        return true;
    }

    const char *dot = strrchr(p, '.');
    const char *base_end = dot ? dot : p + strlen(p);
    const char *ext_start = dot ? dot + 1 : NULL;

    int bi = 0;
    for (const char *s = p; s < base_end; s++) {
        unsigned char c = *s;
        if (c == ' ' || c == '.') { lossy = true; continue; }
        unsigned char uc = toupper(c);
        if (!is_dos_char(uc)) { uc = '_'; lossy = true; }
        if (bi < 8)
            base[bi++] = uc;
        else
            lossy = true;
    }
    base[bi] = '\0';

    int ei = 0;
    if (ext_start) {
        for (const char *s = ext_start; *s; s++) {
            unsigned char c = *s;
            if (c == ' ' || c == '.') { lossy = true; continue; }
            unsigned char uc = toupper(c);
            if (!is_dos_char(uc)) { uc = '_'; lossy = true; }
            if (ei < 3)
                ext[ei++] = uc;
            else
                lossy = true;
        }
    }
    ext[ei] = '\0';

    return lossy;
}

static void assign_dos_names(hostfs_dirent_t *entries, int count)
{
    for (int i = 0; i < count; i++) {
        char base[9], ext[4];
        bool lossy = gen_short_name(entries[i].real_name, base, ext);

        if (ext[0])
            snprintf(entries[i].dos_name, 13, "%s.%s", base, ext);
        else
            snprintf(entries[i].dos_name, 13, "%s", base);

        bool collision = false;
        if (!lossy) {
            for (int j = 0; j < i; j++) {
                if (strcasecmp(entries[i].dos_name, entries[j].dos_name) == 0) {
                    collision = true;
                    break;
                }
            }
        }

        if (lossy || collision) {
            for (int n = 1; n <= 999; n++) {
                char suffix[5];
                snprintf(suffix, sizeof(suffix), "~%d", n);
                int slen = strlen(suffix);
                int blen = strlen(base);
                if (blen > 8 - slen)
                    blen = 8 - slen;

                char tbase[9];
                memcpy(tbase, base, blen);
                memcpy(tbase + blen, suffix, slen);
                tbase[blen + slen] = '\0';

                if (ext[0])
                    snprintf(entries[i].dos_name, 13, "%s.%s", tbase, ext);
                else
                    snprintf(entries[i].dos_name, 13, "%s", tbase);

                bool dup = false;
                for (int j = 0; j < i; j++) {
                    if (strcasecmp(entries[i].dos_name, entries[j].dos_name) == 0) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) break;
            }
        }
    }
}

static int dircache_populate(hostfs_t *hfs, int slot, const char *dir_path,
    const struct stat *dir_st)
{
    DIR *d = opendir(dir_path);
    if (!d)
        return -1;

    hostfs_dircache_t *dc = &hfs->dircache[slot];
    snprintf(dc->path, PATH_MAX, "%s", dir_path);
    dc->dev = dir_st->st_dev;
    dc->ino = dir_st->st_ino;
    dc->mtime = dir_st->st_mtime;
    dc->access_seq = ++hfs->cache_seq;

    int cap = 64;
    hostfs_dirent_t *entries = malloc(cap * sizeof(*entries));
    if (!entries) {
        closedir(d);
        return -1;
    }
    int count = 0;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' && (de->d_name[1] == '\0' ||
            (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;
        if (count >= HOSTFS_DIRENT_MAX)
            break;
        if (count >= cap) {
            cap *= 2;
            if (cap > HOSTFS_DIRENT_MAX) cap = HOSTFS_DIRENT_MAX;
            hostfs_dirent_t *tmp = realloc(entries, cap * sizeof(*entries));
            if (!tmp) break;
            entries = tmp;
        }

        char entry_path[PATH_MAX];
        snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, de->d_name);
        struct stat st;
        if (stat(entry_path, &st) < 0)
            continue;

        hostfs_dirent_t *ent = &entries[count];
        snprintf(ent->real_name, sizeof(ent->real_name), "%s", de->d_name);
        ent->attr = mode_to_attr(st.st_mode);
        ent->size = (uint32_t)st.st_size;
        time_to_dos(st.st_mtime, &ent->time, &ent->date);
        ent->dos_name[0] = '\0';
        count++;
    }
    closedir(d);

    dc->entries = entries;
    dc->count = count;
    assign_dos_names(entries, count);

    return slot;
}

static void dircache_invalidate_dir(hostfs_t *hfs, const char *path)
{
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir)
        *slash = '\0';
    else
        return;

    for (int i = 0; i < HOSTFS_DIRCACHE_COUNT; i++) {
        if (!hfs->dircache[i].path[0])
            continue;
        if (strcmp(hfs->dircache[i].path, dir) != 0)
            continue;
        for (int j = 0; j < HOSTFS_MAX_SEARCHES; j++) {
            if (hfs->searches[j].cache_slot == i) {
                hfs->searches[j].cache_slot = -1;
                hfs->dircache[i].pin_count--;
            }
        }
        free(hfs->dircache[i].entries);
        hfs->dircache[i].entries = NULL;
        hfs->dircache[i].count = 0;
        hfs->dircache[i].path[0] = '\0';
    }
}

static int dircache_get(hostfs_t *hfs, const char *dir_path)
{
    struct stat st;
    if (stat(dir_path, &st) < 0)
        return -1;

    for (int i = 0; i < HOSTFS_DIRCACHE_COUNT; i++) {
        if (!hfs->dircache[i].path[0])
            continue;
        if (strcmp(hfs->dircache[i].path, dir_path) != 0)
            continue;

        if (hfs->dircache[i].dev == st.st_dev &&
            hfs->dircache[i].ino == st.st_ino &&
            hfs->dircache[i].mtime == st.st_mtime) {
            hfs->dircache[i].access_seq = ++hfs->cache_seq;
            return i;
        }
        if (hfs->dircache[i].pin_count > 0) {
            hfs->dircache[i].access_seq = ++hfs->cache_seq;
            return i;
        }
        free(hfs->dircache[i].entries);
        hfs->dircache[i].entries = NULL;
        hfs->dircache[i].count = 0;
        hfs->dircache[i].path[0] = '\0';
        return dircache_populate(hfs, i, dir_path, &st);
    }

    int victim = -1;
    unsigned long oldest = (unsigned long)-1;
    for (int i = 0; i < HOSTFS_DIRCACHE_COUNT; i++) {
        if (!hfs->dircache[i].path[0]) {
            victim = i;
            break;
        }
        if (hfs->dircache[i].pin_count == 0 &&
            hfs->dircache[i].access_seq < oldest) {
            oldest = hfs->dircache[i].access_seq;
            victim = i;
        }
    }
    if (victim < 0)
        return -1;

    free(hfs->dircache[victim].entries);
    hfs->dircache[victim].entries = NULL;
    hfs->dircache[victim].count = 0;
    hfs->dircache[victim].path[0] = '\0';

    return dircache_populate(hfs, victim, dir_path, &st);
}

/* match a filename against a DOS wildcard pattern (case-insensitive) */
static void name_to_fcb(const char *name, uint8_t fcb[11])
{
    memset(fcb, ' ', 11);
    const char *dot = strchr(name, '.');
    int base_len = dot ? (int)(dot - name) : (int)strlen(name);
    if (base_len > 8) base_len = 8;
    for (int i = 0; i < base_len; i++)
        fcb[i] = toupper((unsigned char)name[i]);
    if (dot) {
        const char *ext = dot + 1;
        for (int i = 0; i < 3 && ext[i]; i++)
            fcb[8 + i] = toupper((unsigned char)ext[i]);
    }
}

static void pattern_to_fcb(const char *pattern, uint8_t fcb[11])
{
    memset(fcb, ' ', 11);
    int pos = 0;
    const char *p = pattern;
    while (*p && *p != '.' && pos < 8) {
        if (*p == '*') {
            while (pos < 8)
                fcb[pos++] = '?';
            p++;
        } else {
            fcb[pos++] = toupper((unsigned char)*p++);
        }
    }
    while (*p && *p != '.')
        p++;
    if (*p == '.')
        p++;
    pos = 8;
    while (*p && pos < 11) {
        if (*p == '*') {
            while (pos < 11)
                fcb[pos++] = '?';
            p++;
        } else {
            fcb[pos++] = toupper((unsigned char)*p++);
        }
    }
}

static bool dos_match(const char *pattern, const char *name)
{
    uint8_t pat_fcb[11], name_fcb[11];
    pattern_to_fcb(pattern, pat_fcb);
    name_to_fcb(name, name_fcb);
    for (int i = 0; i < 11; i++) {
        if (pat_fcb[i] == '?')
            continue;
        if (pat_fcb[i] != name_fcb[i])
            return false;
    }
    return true;
}

/* put a 16-bit LE value into xfer_buf at current position */
static void xfer_put16(hostfs_t *hfs, uint16_t v)
{
	if (hfs->xfer_len + 2 <= HOSTFS_XFER_SIZE) {
		hfs->xfer_buf[hfs->xfer_len++] = v & 0xFF;
		hfs->xfer_buf[hfs->xfer_len++] = (v >> 8) & 0xFF;
	}
}

/* put a 32-bit LE value into xfer_buf at current position */
static void xfer_put32(hostfs_t *hfs, uint32_t v)
{
	if (hfs->xfer_len + 4 <= HOSTFS_XFER_SIZE) {
		hfs->xfer_buf[hfs->xfer_len++] = v & 0xFF;
		hfs->xfer_buf[hfs->xfer_len++] = (v >> 8) & 0xFF;
		hfs->xfer_buf[hfs->xfer_len++] = (v >> 16) & 0xFF;
		hfs->xfer_buf[hfs->xfer_len++] = (v >> 24) & 0xFF;
	}
}

/* format a 32-byte FAT directory entry into xfer_buf.
 * FreeDOS kernel reads SearchDir as a raw FAT entry and reformats it
 * into the DTA found entry via pop_dmp(). */
static void xfer_put_findentry(hostfs_t *hfs, const hostfs_dirent_t *ent)
{
    hfs->xfer_len = 0;
    hfs->xfer_pos = 0;

    uint8_t fcb[11];
    memset(fcb, ' ', 11);
    const char *dot = strchr(ent->dos_name, '.');
    int blen = dot ? (int)(dot - ent->dos_name) : (int)strlen(ent->dos_name);
    if (blen > 8) blen = 8;
    for (int i = 0; i < blen; i++)
        fcb[i] = toupper((unsigned char)ent->dos_name[i]);
    if (dot) {
        const char *ext = dot + 1;
        int elen = strlen(ext);
        if (elen > 3) elen = 3;
        for (int i = 0; i < elen; i++)
            fcb[8 + i] = toupper((unsigned char)ext[i]);
    }

    for (int i = 0; i < 11; i++)                    /* +0: FCB name 8+3 */
        hfs->xfer_buf[hfs->xfer_len++] = fcb[i];
    hfs->xfer_buf[hfs->xfer_len++] = ent->attr;     /* +11: attribute */
    for (int i = 0; i < 10; i++)                     /* +12..+21: reserved */
        hfs->xfer_buf[hfs->xfer_len++] = 0;
    xfer_put16(hfs, ent->time);                      /* +22: time */
    xfer_put16(hfs, ent->date);                      /* +24: date */
    xfer_put16(hfs, 0);                              /* +26: first cluster */
    xfer_put32(hfs, ent->size);                      /* +28: file size */
}

/* ---- commands ---- */

static void cmd_findnext(hostfs_t *hfs, lilpc_t *pc);

static void cmd_ping(hostfs_t *hfs, lilpc_t *pc)
{
	(void)pc;
	hfs->status = HOSTFS_ERR_OK;
}

static void cmd_mount_info(hostfs_t *hfs, lilpc_t *pc)
{
	int ep = hfs->endpoint;

	if (ep >= HOSTFS_MAX_ENDPOINTS || !hfs->ep[ep].path[0]) {
		hfs->status = HOSTFS_ERR_BADDRV;
		hfs->len = 0;
		return;
	}

	size_t plen = strlen(hfs->ep[ep].path);
	if (plen > HOSTFS_XFER_SIZE)
		plen = HOSTFS_XFER_SIZE;

	hfs->xfer_len = (uint16_t)plen;
	hfs->xfer_pos = 0;
	memcpy(hfs->xfer_buf, hfs->ep[ep].path, plen);
	hfs->len = (uint16_t)plen;
	hfs->status = HOSTFS_ERR_OK;

	TRACE(pc, "mount_info ep=%d path=%s\n", ep, hfs->ep[ep].path);
}

static void cmd_open(hostfs_t *hfs, lilpc_t *pc)
{
	char path[PATH_MAX];
	int ep = hfs->endpoint;

	if (build_path(hfs, ep, path, sizeof(path)) < 0) {
		hfs->status = HOSTFS_ERR_NOPATH;
		return;
	}

	int slot = alloc_file(hfs);
	if (slot < 0) {
		hfs->status = HOSTFS_ERR_TOOMANY;
		return;
	}

	int fd = open(path, O_RDWR);
	if (fd < 0 && errno == EACCES)
		fd = open(path, O_RDONLY);
	if (fd < 0) {
		TRACE(pc, "open '%s' failed: %s\n", path, strerror(errno));
		hfs->status = (errno == ENOENT) ? HOSTFS_ERR_NOTFOUND :
			HOSTFS_ERR_DENIED;
		return;
	}

	hfs->files[slot].fd = fd;
	hfs->files[slot].endpoint = ep;
	hfs->files[slot].pos = 0;
	hfs->handle = slot;
	hfs->status = HOSTFS_ERR_OK;

	TRACE(pc, "open '%s' -> handle %d\n", path, slot);
}

static void cmd_close(hostfs_t *hfs, lilpc_t *pc)
{
	int h = hfs->handle;

	if (h < 0 || h >= HOSTFS_MAX_FILES || hfs->files[h].fd < 0) {
		hfs->status = HOSTFS_ERR_BADHANDLE;
		return;
	}

	TRACE(pc, "close handle %d\n", h);

	close(hfs->files[h].fd);
	hfs->files[h].fd = -1;
	hfs->status = HOSTFS_ERR_OK;
}

static void cmd_read(hostfs_t *hfs, lilpc_t *pc)
{
	int h = hfs->handle;

	if (h < 0 || h >= HOSTFS_MAX_FILES || hfs->files[h].fd < 0) {
		hfs->status = HOSTFS_ERR_BADHANDLE;
		return;
	}

	uint16_t want = hfs->len;
	if (want > HOSTFS_XFER_SIZE)
		want = HOSTFS_XFER_SIZE;

	off_t off = lseek(hfs->files[h].fd, hfs->pos, SEEK_SET);
	if (off < 0) {
		hfs->status = HOSTFS_ERR_DENIED;
		return;
	}

	ssize_t got = read(hfs->files[h].fd, hfs->xfer_buf, want);
	if (got < 0) {
		TRACE(pc, "read handle %d failed: %s\n", h, strerror(errno));
		hfs->status = HOSTFS_ERR_DENIED;
		return;
	}

	hfs->xfer_len = (uint16_t)got;
	hfs->xfer_pos = 0;
	hfs->len = (uint16_t)got;
	hfs->files[h].pos = hfs->pos + got;
	hfs->pos = hfs->files[h].pos;
	hfs->status = HOSTFS_ERR_OK;

	TRACE(pc, "read handle %d: %d bytes\n", h, (int)got);
}

static void cmd_write(hostfs_t *hfs, lilpc_t *pc)
{
	int h = hfs->handle;

	if (h < 0 || h >= HOSTFS_MAX_FILES || hfs->files[h].fd < 0) {
		hfs->status = HOSTFS_ERR_BADHANDLE;
		return;
	}

	uint16_t want = hfs->len;
	if (want > hfs->xfer_pos)
		want = hfs->xfer_pos;

	off_t off = lseek(hfs->files[h].fd, hfs->pos, SEEK_SET);
	if (off < 0) {
		hfs->status = HOSTFS_ERR_DENIED;
		return;
	}

	ssize_t wrote = write(hfs->files[h].fd, hfs->xfer_buf, want);
	if (wrote < 0) {
		TRACE(pc, "write handle %d failed: %s\n", h, strerror(errno));
		hfs->status = HOSTFS_ERR_DENIED;
		return;
	}

	hfs->len = (uint16_t)wrote;
	hfs->files[h].pos = hfs->pos + wrote;
	hfs->pos = hfs->files[h].pos;
	hfs->status = HOSTFS_ERR_OK;

	/* clear xfer_buf after write completes */
	hfs->xfer_len = 0;
	hfs->xfer_pos = 0;

	TRACE(pc, "write handle %d: %d bytes\n", h, (int)wrote);
}

static void cmd_seek(hostfs_t *hfs, lilpc_t *pc)
{
	int h = hfs->handle;

	if (h < 0 || h >= HOSTFS_MAX_FILES || hfs->files[h].fd < 0) {
		hfs->status = HOSTFS_ERR_BADHANDLE;
		return;
	}

	off_t off = lseek(hfs->files[h].fd, hfs->pos, SEEK_SET);
	if (off < 0) {
		hfs->status = HOSTFS_ERR_DENIED;
		return;
	}

	hfs->files[h].pos = (uint32_t)off;
	hfs->pos = (uint32_t)off;
	hfs->status = HOSTFS_ERR_OK;

	TRACE(pc, "seek handle %d -> %u\n", h, hfs->pos);
}

static void cmd_stat(hostfs_t *hfs, lilpc_t *pc)
{
	char path[PATH_MAX];
	int ep = hfs->endpoint;

	if (build_path(hfs, ep, path, sizeof(path)) < 0) {
		hfs->status = HOSTFS_ERR_NOPATH;
		return;
	}

	struct stat st;
	if (stat(path, &st) < 0) {
		hfs->status = (errno == ENOENT) ? HOSTFS_ERR_NOTFOUND :
			HOSTFS_ERR_DENIED;
		return;
	}

	hfs->param = mode_to_attr(st.st_mode);
	hfs->pos = (uint32_t)st.st_size;

	uint16_t dt, dd;
	time_to_dos(st.st_mtime, &dt, &dd);
	hfs->xfer_len = 0;
	hfs->xfer_pos = 0;
	xfer_put16(hfs, dt);
	xfer_put16(hfs, dd);

	hfs->status = HOSTFS_ERR_OK;

	TRACE(pc, "stat '%s': attr=0x%02x size=%u\n", path, hfs->param,
		hfs->pos);
}

static void cmd_create(hostfs_t *hfs, lilpc_t *pc)
{
	char path[PATH_MAX];
	int ep = hfs->endpoint;

	if (build_path(hfs, ep, path, sizeof(path)) < 0) {
		hfs->status = HOSTFS_ERR_NOPATH;
		return;
	}

	int slot = alloc_file(hfs);
	if (slot < 0) {
		hfs->status = HOSTFS_ERR_TOOMANY;
		return;
	}

	int flags = O_RDWR | O_CREAT | O_TRUNC;
	int fd = open(path, flags, 0666);
	if (fd < 0) {
		TRACE(pc, "create '%s' failed: %s\n", path, strerror(errno));
		hfs->status = HOSTFS_ERR_DENIED;
		return;
	}

	hfs->files[slot].fd = fd;
	hfs->files[slot].endpoint = ep;
	hfs->files[slot].pos = 0;
	hfs->handle = slot;
	hfs->status = HOSTFS_ERR_OK;
	dircache_invalidate_dir(hfs, path);

	TRACE(pc, "create '%s' -> handle %d\n", path, slot);
}

static void cmd_delete(hostfs_t *hfs, lilpc_t *pc)
{
	char path[PATH_MAX];
	int ep = hfs->endpoint;

	if (build_path(hfs, ep, path, sizeof(path)) < 0) {
		hfs->status = HOSTFS_ERR_NOPATH;
		return;
	}

	if (unlink(path) < 0) {
		TRACE(pc, "delete '%s' failed: %s\n", path, strerror(errno));
		hfs->status = (errno == ENOENT) ? HOSTFS_ERR_NOTFOUND :
			HOSTFS_ERR_DENIED;
		return;
	}

	hfs->status = HOSTFS_ERR_OK;
	dircache_invalidate_dir(hfs, path);
	TRACE(pc, "delete '%s'\n", path);
}

static void cmd_rename(hostfs_t *hfs, lilpc_t *pc)
{
	int ep = hfs->endpoint;
	if (ep < 0 || ep >= HOSTFS_MAX_ENDPOINTS || !hfs->ep[ep].path[0]) {
		hfs->status = HOSTFS_ERR_BADDRV;
		return;
	}

	/* xfer_buf contains old\0new\0 */
	char *old_dos = (char *)hfs->xfer_buf;
	size_t old_len = strnlen(old_dos, hfs->xfer_pos);
	if (old_len >= hfs->xfer_pos) {
		hfs->status = HOSTFS_ERR_NOPATH;
		return;
	}
	char *new_dos = old_dos + old_len + 1;

	/* temporarily swap xfer_buf content to build paths */
	char old_path[PATH_MAX], new_path[PATH_MAX];

	/* build old path */
	uint16_t saved_pos = hfs->xfer_pos;
	hfs->xfer_pos = (uint16_t)(old_len + 1);
	if (build_path(hfs, ep, old_path, sizeof(old_path)) < 0) {
		hfs->xfer_pos = saved_pos;
		hfs->status = HOSTFS_ERR_NOPATH;
		return;
	}

	/* build new path: put new name into xfer_buf start */
	size_t new_len = strnlen(new_dos, saved_pos - old_len - 1);
	memmove(hfs->xfer_buf, new_dos, new_len + 1);
	hfs->xfer_pos = (uint16_t)(new_len + 1);
	if (build_path(hfs, ep, new_path, sizeof(new_path)) < 0) {
		hfs->xfer_pos = saved_pos;
		hfs->status = HOSTFS_ERR_NOPATH;
		return;
	}
	hfs->xfer_pos = saved_pos;

	if (rename(old_path, new_path) < 0) {
		TRACE(pc, "rename '%s' -> '%s' failed: %s\n", old_path, new_path,
			strerror(errno));
		hfs->status = (errno == ENOENT) ? HOSTFS_ERR_NOTFOUND :
			HOSTFS_ERR_DENIED;
		return;
	}

	hfs->status = HOSTFS_ERR_OK;
	dircache_invalidate_dir(hfs, old_path);
	dircache_invalidate_dir(hfs, new_path);
	TRACE(pc, "rename '%s' -> '%s'\n", old_path, new_path);
}

static void cmd_mkdir(hostfs_t *hfs, lilpc_t *pc)
{
	char path[PATH_MAX];
	int ep = hfs->endpoint;

	if (build_path(hfs, ep, path, sizeof(path)) < 0) {
		hfs->status = HOSTFS_ERR_NOPATH;
		return;
	}

	if (mkdir(path, 0777) < 0) {
		TRACE(pc, "mkdir '%s' failed: %s\n", path, strerror(errno));
		hfs->status = (errno == EEXIST) ? HOSTFS_ERR_EXISTS :
			HOSTFS_ERR_DENIED;
		return;
	}

	hfs->status = HOSTFS_ERR_OK;
	dircache_invalidate_dir(hfs, path);
	TRACE(pc, "mkdir '%s'\n", path);
}

static void cmd_rmdir(hostfs_t *hfs, lilpc_t *pc)
{
	char path[PATH_MAX];
	int ep = hfs->endpoint;

	if (build_path(hfs, ep, path, sizeof(path)) < 0) {
		hfs->status = HOSTFS_ERR_NOPATH;
		return;
	}

	if (rmdir(path) < 0) {
		TRACE(pc, "rmdir '%s' failed: %s\n", path, strerror(errno));
		hfs->status = (errno == ENOENT) ? HOSTFS_ERR_NOTFOUND :
			HOSTFS_ERR_DENIED;
		return;
	}

	hfs->status = HOSTFS_ERR_OK;
	dircache_invalidate_dir(hfs, path);
	TRACE(pc, "rmdir '%s'\n", path);
}

static void cmd_findfrst(hostfs_t *hfs, lilpc_t *pc)
{
    int ep = hfs->endpoint;
    if (ep < 0 || ep >= HOSTFS_MAX_ENDPOINTS || !hfs->ep[ep].path[0]) {
        hfs->status = HOSTFS_ERR_BADDRV;
        return;
    }

    int slot = alloc_search(hfs);
    if (slot < 0) {
        hfs->status = HOSTFS_ERR_TOOMANY;
        return;
    }

    char full[PATH_MAX];
    if (build_path(hfs, ep, full, sizeof(full)) < 0) {
        hfs->status = HOSTFS_ERR_NOPATH;
        return;
    }

    char *sep = strrchr(full, '/');
    char dir_path[PATH_MAX];
    char pattern[14];

    if (sep) {
        size_t dlen = sep - full;
        if (dlen == 0)
            dlen = 1;
        memcpy(dir_path, full, dlen);
        dir_path[dlen] = '\0';
        snprintf(pattern, sizeof(pattern), "%.*s",
                 (int)(sizeof(pattern) - 1), sep + 1);
    } else {
        snprintf(dir_path, sizeof(dir_path), "%s", hfs->ep[ep].path);
        snprintf(pattern, sizeof(pattern), "%.*s",
                 (int)(sizeof(pattern) - 1), full);
    }

    for (int i = 0; pattern[i]; i++)
        pattern[i] = toupper((unsigned char)pattern[i]);
    if (!pattern[0])
        snprintf(pattern, sizeof(pattern), "*.*");

    int cache_slot = dircache_get(hfs, dir_path);
    if (cache_slot < 0) {
        TRACE(pc, "findfrst dir '%s' cache failed\n", dir_path);
        hfs->status = HOSTFS_ERR_NOPATH;
        return;
    }

    hostfs_search_t *s = &hfs->searches[slot];
    s->cache_slot = cache_slot;
    s->next_entry = 0;
    s->endpoint = ep;
    s->attr_mask = hfs->param & 0xFF;
    snprintf(s->pattern, sizeof(s->pattern), "%.*s",
             (int)(sizeof(s->pattern) - 1), pattern);
    hfs->dircache[cache_slot].pin_count++;
    hfs->handle = slot;

    TRACE(pc, "findfrst ep=%d dir='%s' pat='%s' -> search %d (cache %d)\n",
        ep, dir_path, pattern, slot, cache_slot);

    if (hfs->guest_sda && ((pc)->debug & DBG_HOSTFS)) {
        uint16_t sda_seg = (uint16_t)(hfs->guest_sda >> 16);
        uint16_t sda_off = (uint16_t)(hfs->guest_sda & 0xFFFF);
        uint32_t sda_lin = ((uint32_t)sda_seg << 4) + sda_off;
        uint32_t dta_lin = sda_lin + 0x0C;
        uint16_t dta_off = bus_read8(&pc->bus, dta_lin) |
            ((uint16_t)bus_read8(&pc->bus, dta_lin + 1) << 8);
        uint16_t dta_seg = bus_read8(&pc->bus, dta_lin + 2) |
            ((uint16_t)bus_read8(&pc->bus, dta_lin + 3) << 8);
        fprintf(stderr, "hostfs: SDA=%04x:%04x DTA=%04x:%04x\n",
            sda_seg, sda_off, dta_seg, dta_off);
    }

    cmd_findnext(hfs, pc);
    hfs->handle = slot;
}

static void cmd_findnext(hostfs_t *hfs, lilpc_t *pc)
{
    int h = hfs->handle;

    if (h < 0 || h >= HOSTFS_MAX_SEARCHES || hfs->searches[h].cache_slot < 0) {
        hfs->status = HOSTFS_ERR_BADHANDLE;
        return;
    }

    hostfs_search_t *s = &hfs->searches[h];
    hostfs_dircache_t *dc = &hfs->dircache[s->cache_slot];

    while (s->next_entry < dc->count) {
        hostfs_dirent_t *ent = &dc->entries[s->next_entry];
        s->next_entry++;

        if (ent->real_name[0] == '.' && !(s->attr_mask & HOSTFS_ATTR_HIDDEN))
            continue;

        if (!dos_match(s->pattern, ent->dos_name))
            continue;

        if ((ent->attr & HOSTFS_ATTR_DIR) && !(s->attr_mask & HOSTFS_ATTR_DIR))
            continue;

        xfer_put_findentry(hfs, ent);
        hfs->status = HOSTFS_ERR_OK;

        TRACE(pc, "findnext search %d: '%s' -> '%s' "
            "attr=%02x size=%u\n",
            h, ent->real_name, ent->dos_name,
            ent->attr, ent->size);
        return;
    }

    hfs->status = HOSTFS_ERR_NOMORE;
    TRACE(pc, "findnext search %d: no more\n", h);
}

static void cmd_findclose(hostfs_t *hfs, lilpc_t *pc)
{
    int h = hfs->handle;

    if (h < 0 || h >= HOSTFS_MAX_SEARCHES || hfs->searches[h].cache_slot < 0) {
        hfs->status = HOSTFS_ERR_BADHANDLE;
        return;
    }

    TRACE(pc, "findclose search %d\n", h);

    int cs = hfs->searches[h].cache_slot;
    if (cs >= 0 && hfs->dircache[cs].pin_count > 0)
        hfs->dircache[cs].pin_count--;
    hfs->searches[h].cache_slot = -1;
    hfs->status = HOSTFS_ERR_OK;
}

static void cmd_diskfree(hostfs_t *hfs, lilpc_t *pc)
{
	int ep = hfs->endpoint;
	if (ep < 0 || ep >= HOSTFS_MAX_ENDPOINTS || !hfs->ep[ep].path[0]) {
		hfs->status = HOSTFS_ERR_BADDRV;
		return;
	}

#ifdef __EMSCRIPTEN__
	/* MEMFS has no real capacity; report 32 MB with 16 MB free */
	uint64_t free_bytes = 16ULL * 1024 * 1024;
	uint64_t total = 32ULL * 1024 * 1024;
#else
	struct statvfs sv;
	if (statvfs(hfs->ep[ep].path, &sv) < 0) {
		hfs->status = HOSTFS_ERR_DENIED;
		return;
	}

	uint64_t free_bytes = (uint64_t)sv.f_bavail * sv.f_bsize;
	uint64_t total = (uint64_t)sv.f_blocks * sv.f_bsize;
#endif

	/* report in DOS format:
	 * pos = free bytes (clamped to 32-bit)
	 * param low = sectors per cluster
	 * param high = bytes per sector */
	if (free_bytes > 0xFFFFFFFFULL)
		free_bytes = 0xFFFFFFFFULL;
	hfs->pos = (uint32_t)free_bytes;

	/* sectors per cluster in low byte, bytes per sector / 16 in high byte
	 * (TSR multiplies high byte by 16 to recover bytes per sector) */
	hfs->param = 64 | ((512 / 16) << 8);

	/* total clusters in len register, free clusters in xfer */
	if (total > 0xFFFFFFFFULL)
		total = 0xFFFFFFFFULL;
	uint32_t total_clust = (uint32_t)(total / (64 * 512));
	uint32_t free_clust = (uint32_t)(free_bytes / (64 * 512));
	if (total_clust > 0xFFFF)
		total_clust = 0xFFFF;
	if (free_clust > 0xFFFF)
		free_clust = 0xFFFF;
	hfs->len = (uint16_t)total_clust;

	hfs->xfer_len = 0;
	hfs->xfer_pos = 0;
	xfer_put16(hfs, (uint16_t)free_clust);

	hfs->status = HOSTFS_ERR_OK;

	TRACE(pc, "diskfree ep=%d: %u bytes free\n", ep, hfs->pos);
}

static void cmd_truncate(hostfs_t *hfs, lilpc_t *pc)
{
	int h = hfs->handle;

	if (h < 0 || h >= HOSTFS_MAX_FILES || hfs->files[h].fd < 0) {
		hfs->status = HOSTFS_ERR_BADHANDLE;
		return;
	}

	if (ftruncate(hfs->files[h].fd, hfs->pos) < 0) {
		TRACE(pc, "truncate handle %d to %u failed: %s\n",
			h, hfs->pos, strerror(errno));
		hfs->status = HOSTFS_ERR_DENIED;
		return;
	}

	hfs->status = HOSTFS_ERR_OK;
	TRACE(pc, "truncate handle %d to %u\n", h, hfs->pos);
}

static void cmd_setattr(hostfs_t *hfs, lilpc_t *pc)
{
	char path[PATH_MAX];
	int ep = hfs->endpoint;

	if (build_path(hfs, ep, path, sizeof(path)) < 0) {
		hfs->status = HOSTFS_ERR_NOPATH;
		return;
	}

	struct stat st;
	if (stat(path, &st) < 0) {
		hfs->status = (errno == ENOENT) ? HOSTFS_ERR_NOTFOUND :
			HOSTFS_ERR_DENIED;
		return;
	}

	mode_t mode = st.st_mode;
	if (hfs->param & HOSTFS_ATTR_READONLY)
		mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
	else
		mode |= S_IWUSR;

	if (chmod(path, mode) < 0) {
		hfs->status = HOSTFS_ERR_DENIED;
		return;
	}

	hfs->status = HOSTFS_ERR_OK;
	TRACE(pc, "setattr '%s' attr=0x%02x\n", path, hfs->param & 0xFF);
}

static void cmd_getszp(hostfs_t *hfs, lilpc_t *pc)
{
	int h = hfs->handle;

	if (h < 0 || h >= HOSTFS_MAX_FILES || hfs->files[h].fd < 0) {
		hfs->status = HOSTFS_ERR_BADHANDLE;
		return;
	}

	struct stat st;
	if (fstat(hfs->files[h].fd, &st) < 0) {
		hfs->status = HOSTFS_ERR_DENIED;
		return;
	}

	hfs->pos = (uint32_t)st.st_size;
	hfs->status = HOSTFS_ERR_OK;

	TRACE(pc, "getszp handle %d: size=%u\n", h, hfs->pos);
}

static void cmd_init(hostfs_t *hfs, lilpc_t *pc)
{
    hfs->guest_sda = hfs->pos;
    hfs->status = HOSTFS_ERR_OK;
    TRACE(pc, "init: SDA=%04x:%04x\n",
        (uint16_t)(hfs->guest_sda >> 16),
        (uint16_t)(hfs->guest_sda & 0xFFFF));
}

/* ---- command dispatch ---- */

static void hostfs_exec_cmd(hostfs_t *hfs, lilpc_t *pc, uint8_t cmd)
{
	TRACE(pc, "cmd=0x%02x ep=%d h=%d len=%d pos=%u\n",
		cmd, hfs->endpoint, hfs->handle, hfs->len, hfs->pos);

	switch (cmd) {
	case HOSTFS_CMD_PING:		cmd_ping(hfs, pc); break;
	case HOSTFS_CMD_MOUNT_INFO:	cmd_mount_info(hfs, pc); break;
	case HOSTFS_CMD_OPEN:		cmd_open(hfs, pc); break;
	case HOSTFS_CMD_CLOSE:		cmd_close(hfs, pc); break;
	case HOSTFS_CMD_READ:		cmd_read(hfs, pc); break;
	case HOSTFS_CMD_WRITE:		cmd_write(hfs, pc); break;
	case HOSTFS_CMD_SEEK:		cmd_seek(hfs, pc); break;
	case HOSTFS_CMD_STAT:		cmd_stat(hfs, pc); break;
	case HOSTFS_CMD_CREATE:		cmd_create(hfs, pc); break;
	case HOSTFS_CMD_DELETE:		cmd_delete(hfs, pc); break;
	case HOSTFS_CMD_RENAME:		cmd_rename(hfs, pc); break;
	case HOSTFS_CMD_MKDIR:		cmd_mkdir(hfs, pc); break;
	case HOSTFS_CMD_RMDIR:		cmd_rmdir(hfs, pc); break;
	case HOSTFS_CMD_FINDFRST:	cmd_findfrst(hfs, pc); break;
	case HOSTFS_CMD_FINDNEXT:	cmd_findnext(hfs, pc); break;
	case HOSTFS_CMD_FINDCLOSE:	cmd_findclose(hfs, pc); break;
	case HOSTFS_CMD_DISKFREE:	cmd_diskfree(hfs, pc); break;
	case HOSTFS_CMD_TRUNCATE:	cmd_truncate(hfs, pc); break;
	case HOSTFS_CMD_SETATTR:	cmd_setattr(hfs, pc); break;
	case HOSTFS_CMD_GETSZP:		cmd_getszp(hfs, pc); break;
	case HOSTFS_CMD_INIT:		cmd_init(hfs, pc); break;
	default:
		TRACE(pc, "unknown command 0x%02x\n", cmd);
		hfs->status = HOSTFS_ERR_GENERAL;
		break;
	}

	/* reset xfer pointer after command so results are read from start */
	hfs->xfer_pos = 0;
}

/* ---- I/O port handlers ---- */

static uint8_t hostfs_read(lilpc_t *pc, uint16_t port)
{
	hostfs_t *hfs = &pc->hostfs;
	int reg = port - hfs->base_port;

	switch (reg) {
	case HOSTFS_REG_CMD:		return hfs->status;
	case HOSTFS_REG_ENDPOINT:	return hfs->endpoint;
	case HOSTFS_REG_HANDLE_LO:	return hfs->handle & 0xFF;
	case HOSTFS_REG_HANDLE_HI:	return (hfs->handle >> 8) & 0xFF;
	case HOSTFS_REG_PARAM_LO:	return hfs->param & 0xFF;
	case HOSTFS_REG_PARAM_HI:	return (hfs->param >> 8) & 0xFF;
	case HOSTFS_REG_POS0:		return hfs->pos & 0xFF;
	case HOSTFS_REG_POS1:		return (hfs->pos >> 8) & 0xFF;
	case HOSTFS_REG_POS2:		return (hfs->pos >> 16) & 0xFF;
	case HOSTFS_REG_POS3:		return (hfs->pos >> 24) & 0xFF;
	case HOSTFS_REG_LEN_LO:	return hfs->len & 0xFF;
	case HOSTFS_REG_LEN_HI:	return (hfs->len >> 8) & 0xFF;
	case HOSTFS_REG_XFER:
	{
		uint8_t v = 0;
		if (hfs->xfer_pos == 0 && hfs->xfer_len > 0 &&
		    (pc->debug & DBG_HOSTFS)) {
			cpu286_t *cpu = &pc->cpu;
			uint32_t dest = cpu->seg[SEG_ES].base + cpu->di;
			fprintf(stderr,
			    "hostfs: XFER read start: ES:DI=%04x:%04x "
			    "(lin %05x) CX=%u len=%u DF=%d\n",
			    cpu->seg[SEG_ES].sel, cpu->di,
			    dest, cpu->cx, hfs->xfer_len,
			    !!(cpu->flags & FLAG_DF));
		}
		if (hfs->xfer_pos < hfs->xfer_len)
			v = hfs->xfer_buf[hfs->xfer_pos];
		if (hfs->xfer_pos < HOSTFS_XFER_SIZE)
			hfs->xfer_pos++;
		return v;
	}
	case HOSTFS_REG_ID0:		return 'H';
	case HOSTFS_REG_ID1:		return 'F';
	case HOSTFS_REG_ID2:		return 'S';
	}
	return 0xFF;
}

static void hostfs_write(lilpc_t *pc, uint16_t port, uint8_t val)
{
	hostfs_t *hfs = &pc->hostfs;
	int reg = port - hfs->base_port;

	switch (reg) {
	case HOSTFS_REG_CMD:
		hostfs_exec_cmd(hfs, pc, val);
		break;
	case HOSTFS_REG_ENDPOINT:
		hfs->endpoint = val;
		break;
	case HOSTFS_REG_HANDLE_LO:
		hfs->handle = (hfs->handle & 0xFF00) | val;
		break;
	case HOSTFS_REG_HANDLE_HI:
		hfs->handle = (hfs->handle & 0x00FF) | ((uint16_t)val << 8);
		break;
	case HOSTFS_REG_PARAM_LO:
		hfs->param = (hfs->param & 0xFF00) | val;
		break;
	case HOSTFS_REG_PARAM_HI:
		hfs->param = (hfs->param & 0x00FF) | ((uint16_t)val << 8);
		break;
	case HOSTFS_REG_POS0:
		hfs->pos = (hfs->pos & 0xFFFFFF00) | val;
		break;
	case HOSTFS_REG_POS1:
		hfs->pos = (hfs->pos & 0xFFFF00FF) | ((uint32_t)val << 8);
		break;
	case HOSTFS_REG_POS2:
		hfs->pos = (hfs->pos & 0xFF00FFFF) | ((uint32_t)val << 16);
		break;
	case HOSTFS_REG_POS3:
		hfs->pos = (hfs->pos & 0x00FFFFFF) | ((uint32_t)val << 24);
		break;
	case HOSTFS_REG_LEN_LO:
		hfs->len = (hfs->len & 0xFF00) | val;
		break;
	case HOSTFS_REG_LEN_HI:
		hfs->len = (hfs->len & 0x00FF) | ((uint16_t)val << 8);
		break;
	case HOSTFS_REG_XFER:
		if (hfs->xfer_len > 0) {
			hfs->xfer_pos = 0;
			hfs->xfer_len = 0;
		}
		if (hfs->xfer_pos < HOSTFS_XFER_SIZE)
			hfs->xfer_buf[hfs->xfer_pos++] = val;
		break;
	}
}

/* ---- public API ---- */

void hostfs_init(hostfs_t *hfs, lilpc_t *pc)
{
    memset(hfs, 0, sizeof(*hfs));
    hfs->base_port = HOSTFS_PORT_BASE;
    hfs->mount_count = 0;

    for (int i = 0; i < HOSTFS_MAX_FILES; i++)
        hfs->files[i].fd = -1;
    for (int i = 0; i < HOSTFS_MAX_SEARCHES; i++)
        hfs->searches[i].cache_slot = -1;

    bus_register_io(&pc->bus, hfs->base_port, HOSTFS_PORT_COUNT,
        hostfs_read, hostfs_write);
}

void hostfs_cleanup(hostfs_t *hfs)
{
    for (int i = 0; i < HOSTFS_MAX_FILES; i++) {
        if (hfs->files[i].fd >= 0) {
            close(hfs->files[i].fd);
            hfs->files[i].fd = -1;
        }
    }
    for (int i = 0; i < HOSTFS_MAX_SEARCHES; i++)
        hfs->searches[i].cache_slot = -1;
    for (int i = 0; i < HOSTFS_DIRCACHE_COUNT; i++) {
        free(hfs->dircache[i].entries);
        hfs->dircache[i].entries = NULL;
        hfs->dircache[i].count = 0;
        hfs->dircache[i].path[0] = '\0';
    }
}

void hostfs_mount(hostfs_t *hfs, int ep, const char *path)
{
	if (ep < 0 || ep >= HOSTFS_MAX_ENDPOINTS)
		return;

	/* ensure path ends with / */
	size_t len = strlen(path);
	if (len > 0 && path[len - 1] == '/')
		snprintf(hfs->ep[ep].path, PATH_MAX, "%s", path);
	else
		snprintf(hfs->ep[ep].path, PATH_MAX, "%s/", path);

	hfs->mount_count++;
	fprintf(stderr, "hostfs: endpoint %d -> %s\n", ep, hfs->ep[ep].path);
}


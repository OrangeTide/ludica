/* hostfs.h - host filesystem virtual ISA device for lilpc */
/* Made by a machine. PUBLIC DOMAIN (CC0-1.0) */
#ifndef HOSTFS_H
#define HOSTFS_H

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/types.h>

struct lilpc;

#define HOSTFS_PORT_BASE	0x0240
#define HOSTFS_PORT_COUNT	16

#define HOSTFS_MAX_ENDPOINTS	16
#define HOSTFS_MAX_FILES	64
#define HOSTFS_MAX_SEARCHES	16
#define HOSTFS_XFER_SIZE	4096
#define HOSTFS_DIRCACHE_COUNT	8
#define HOSTFS_DIRENT_MAX	512

/* port offsets from base */
#define HOSTFS_REG_CMD		0x00	/* W: command, R: status */
#define HOSTFS_REG_ENDPOINT	0x01
#define HOSTFS_REG_HANDLE_LO	0x02
#define HOSTFS_REG_HANDLE_HI	0x03
#define HOSTFS_REG_PARAM_LO	0x04
#define HOSTFS_REG_PARAM_HI	0x05
#define HOSTFS_REG_POS0		0x06
#define HOSTFS_REG_POS1		0x07
#define HOSTFS_REG_POS2		0x08
#define HOSTFS_REG_POS3		0x09
#define HOSTFS_REG_LEN_LO	0x0A
#define HOSTFS_REG_LEN_HI	0x0B
#define HOSTFS_REG_XFER		0x0C
#define HOSTFS_REG_ID0		0x0D
#define HOSTFS_REG_ID1		0x0E
#define HOSTFS_REG_ID2		0x0F

/* commands */
#define HOSTFS_CMD_PING		0x00
#define HOSTFS_CMD_MOUNT_INFO	0x01
#define HOSTFS_CMD_OPEN		0x02
#define HOSTFS_CMD_CLOSE	0x03
#define HOSTFS_CMD_READ		0x04
#define HOSTFS_CMD_WRITE	0x05
#define HOSTFS_CMD_SEEK		0x06
#define HOSTFS_CMD_STAT		0x07
#define HOSTFS_CMD_CREATE	0x08
#define HOSTFS_CMD_DELETE	0x09
#define HOSTFS_CMD_RENAME	0x0A
#define HOSTFS_CMD_MKDIR	0x0B
#define HOSTFS_CMD_RMDIR	0x0C
#define HOSTFS_CMD_FINDFRST	0x0D
#define HOSTFS_CMD_FINDNEXT	0x0E
#define HOSTFS_CMD_FINDCLOSE	0x0F
#define HOSTFS_CMD_DISKFREE	0x10
#define HOSTFS_CMD_TRUNCATE	0x11
#define HOSTFS_CMD_SETATTR	0x12
#define HOSTFS_CMD_GETSZP	0x13
#define HOSTFS_CMD_INIT		0x14	/* guest .COM stores SDA ptr */

/* DOS error codes returned in status register */
#define HOSTFS_ERR_OK		0x00
#define HOSTFS_ERR_NOTFOUND	0x02
#define HOSTFS_ERR_NOPATH	0x03
#define HOSTFS_ERR_TOOMANY	0x04
#define HOSTFS_ERR_DENIED	0x05
#define HOSTFS_ERR_BADHANDLE	0x06
#define HOSTFS_ERR_NOMEM	0x08
#define HOSTFS_ERR_BADDRV	0x0F
#define HOSTFS_ERR_NOMORE	0x12
#define HOSTFS_ERR_EXISTS	0x50
#define HOSTFS_ERR_GENERAL	0x1F

/* DOS file attribute bits */
#define HOSTFS_ATTR_READONLY	0x01
#define HOSTFS_ATTR_HIDDEN	0x02
#define HOSTFS_ATTR_SYSTEM	0x04
#define HOSTFS_ATTR_LABEL	0x08
#define HOSTFS_ATTR_DIR		0x10
#define HOSTFS_ATTR_ARCHIVE	0x20

/* debug flag: 'g' = bit 6 */
#define DBG_HOSTFS	((uint64_t)1 << 6)

typedef struct hostfs_file {
	int fd;			/* host file descriptor, -1 = unused */
	uint8_t endpoint;
	uint32_t pos;
} hostfs_file_t;

typedef struct hostfs_dirent {
	char real_name[256];
	char dos_name[13];
	uint8_t attr;
	uint32_t size;
	uint16_t time;
	uint16_t date;
} hostfs_dirent_t;

typedef struct hostfs_dircache {
	char path[PATH_MAX];
	dev_t dev;
	ino_t ino;
	time_t mtime;
	hostfs_dirent_t *entries;
	int count;
	int pin_count;
	unsigned long access_seq;
} hostfs_dircache_t;

typedef struct hostfs_search {
	int cache_slot;		/* -1 = unused */
	int next_entry;
	uint8_t endpoint;
	char pattern[13];	/* DOS 8.3 wildcard pattern */
	uint8_t attr_mask;
} hostfs_search_t;

typedef struct hostfs_endpoint {
	char path[PATH_MAX];	/* host directory, empty = not mounted */
} hostfs_endpoint_t;

typedef struct hostfs {
	hostfs_endpoint_t ep[HOSTFS_MAX_ENDPOINTS];
	hostfs_file_t files[HOSTFS_MAX_FILES];
	hostfs_search_t searches[HOSTFS_MAX_SEARCHES];

	/* transfer buffer */
	uint8_t xfer_buf[HOSTFS_XFER_SIZE];
	uint16_t xfer_pos;	/* auto-increment pointer */
	uint16_t xfer_len;	/* valid bytes for read-back */

	/* port-accessible registers */
	uint8_t status;
	uint8_t endpoint;
	uint16_t handle;
	uint16_t param;
	uint32_t pos;
	uint16_t len;

	uint16_t base_port;
	int mount_count;	/* number of mounted endpoints */

	/* SDA pointer set by guest HOSTFS.COM via CMD_INIT */
	uint32_t guest_sda;	/* seg:off packed as (seg<<16)|off */

	/* directory cache for 8.3 name generation */
	hostfs_dircache_t dircache[HOSTFS_DIRCACHE_COUNT];
	unsigned long cache_seq;
} hostfs_t;

void hostfs_init(hostfs_t *hfs, struct lilpc *pc);
void hostfs_cleanup(hostfs_t *hfs);
void hostfs_mount(hostfs_t *hfs, int ep, const char *path);

#endif

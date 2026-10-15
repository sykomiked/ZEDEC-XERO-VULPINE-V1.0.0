/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarmnet.h — fetch the missing swarm discs over the network, on bare metal.
 *
 * Disc 1 (the core disc) boots alone. Its /ZXV/SWARM.LST names the swarm
 * discs it expects and /ZXV/UPSTREAM.CID the bucket they live in and the
 * gateways that serve it (hw_models.h). When a listed disc is not attached
 * and a blank drive is, this module brings the network up and writes the disc
 * onto that drive, entirely in ZXV's own code: the hwdrv NIC drivers, the
 * kernel/src/net stack (ARP, IPv4, UDP, DHCP, DNS, TCP) and the
 * kernel/src/ipfs_node block walker.
 *
 *   1. link   the first NIC whose driver reports link up (STATUS.LU), bound to
 *             the stack as eth0                                [NET] ... link up
 *   2. DHCP   DISCOVER/OFFER/REQUEST/ACK, 5 s per attempt, 3 attempts
 *                                                              [NET] DHCP lease
 *      no link, no NIC or no lease:
 *             [NET] Link down or no DHCP response.
 *             [SWARM] Falling back to local storage scan: Attach Disc N USB / drive.
 *   3. HTTP   HTTP/1.1 GET /ipfs/<cid>?format=raw to each http:// gateway in
 *             UPSTREAM.CID order, one raw block per request, keep-alive.
 *             Every block is hashed against the CID asked for before use, so
 *             a gateway cannot change a byte; a block one gateway cannot serve
 *             intact is asked of the next. https:// gateways are skipped
 *             (no TLS in this path yet) and said so.
 *   4. find   manifest.txt from the bucket root (mk_bucket.py), the line for
 *             the disc, and the disc's CID through the bucket's directories,
 *             which must equal the manifest's.
 *   5. write  onto a drive that is blank (no MBR signature, no GPT header),
 *             512-byte sectors and large enough. The image streams to the
 *             drive with a rolling SHA-256; its first 34 sectors (protective
 *             MBR, GPT header and partition entries) are held in memory and
 *             written last, only when size and SHA-256 match the manifest.
 *             A failed or partial download therefore never looks like a disc.
 *                                       [SWARM_FETCH] ... %, [SWARM_FETCHED]
 *                                       or [SWARM_FETCH_FAIL] ...; nothing committed
 *
 * The fetcher runs in the kernel's idle context for now; it moves to a ring-3
 * process when user programs have network and disk system calls (gaps 7, 17).
 * Native Bitswap needs the libp2p handshake (multistream-select, Noise, yamux)
 * as well as its protobuf messages, and is not here; neither is HTTPS.
 *
 * Freestanding C11, integer only, no allocation: all state is in the structs
 * the caller provides. */
#ifndef ZXV_SWARMNET_H
#define ZXV_SWARMNET_H

#include <stdint.h>
#include "../net/net.h"
#include "../hwdrv/hw_models.h"
#include "../ipfs_node/ipfs_node.h"

#define SN_DHCP_TRIES   3u
#define SN_DHCP_MS      5000u
#define SN_ARP_MS       2000u
#define SN_CONNECT_MS   5000u
#define SN_STALL_MS     30000u /* no byte of a response for this long: give up on it */
#define SN_TICK_MS      200u   /* TCP retransmit pacing */
#define SN_HOLD_SECTORS 34u    /* protective MBR + GPT header + 32 sectors of entries */
#define SN_STAGE_BYTES  65536u
#define SN_MANIFEST_MAX 16384u
#define SN_HOST_MAX     64u
#define SN_CACHE_MAX    262144u /* one full raw leaf (mk_bucket's chunk size) */

/* Time and the idle hook (the heartbeat) the long loops call. */
typedef struct sn_clock {
    uint64_t (*ms)(void *ctx);
    void (*idle)(void *ctx);
    void *ctx;
} sn_clock_t;

/* ---- the link: one NIC bound to the stack ---------------------------------- */
typedef struct sn_link {
    net_state_t net;
    net_interface_t *eth;
    hw_nic_t *nic;
    const hw_plat_t *p;
    sn_clock_t clk;
    uint64_t next_tick;
    uint8_t frame[2048];
} sn_link_t;

/* Bind the first NIC with link up, then DHCP. 1 = leased; 0 = no NIC, no link
 * or no lease (each logged). */
int sn_link_up(sn_link_t *l, const hw_plat_t *p, hw_registry_t *r, const sn_clock_t *clk,
               uint32_t (*entropy)(void));
/* Drain received frames into the stack, pace TCP, call the idle hook. */
void sn_pump(sn_link_t *l);

/* ---- HTTP/1.1, pure parts ----------------------------------------------------- */
/* "http://host[:port][/]" -> host and port (80 default). 1 = ok, 0 = not an
 * http:// URL this client can use (https:// included). */
int sn_url_parse(const char *url, char *host, uint32_t host_cap, uint16_t *port);

typedef struct sn_head {
    uint32_t status;
    uint64_t length; /* Content-Length */
    uint8_t has_length;
    uint8_t chunked; /* Transfer-Encoding: chunked (refused) */
    uint8_t close;   /* Connection: close */
} sn_head_t;

/* Parse a response head in b[0..n). Returns the head's length (through the
 * blank line), 0 when more bytes are needed, -1 when malformed. */
int32_t sn_http_head(const uint8_t *b, uint32_t n, sn_head_t *h);

/* ---- HTTP/1.1 over the link -------------------------------------------------- */
typedef struct sn_http {
    sn_link_t *l;
    char host[SN_HOST_MAX];
    uint16_t port;
    uint8_t ip[4];
    uint8_t resolved;
    int32_t sock; /* -1 when not connected */
    uint8_t head[2048];
} sn_http_t;

/* GET path into buf. 0 = 200 with the whole body in buf (*len); otherwise
 * negative with *why set. A failed request closes the connection. */
int sn_http_get(sn_http_t *h, const char *path, uint8_t *buf, uint32_t cap, uint32_t *len,
                const char **why);

/* ---- the bucket's manifest.txt ------------------------------------------------ */
/* One line per disc or file:
 *   <kind> <arch> <disc|-> <GROUP|-> <bytes> <sha256> <cid> <dir>/<file>
 * kind core|swarm|cartridge, written by build_system/mk_bucket.py. */
typedef struct sn_entry {
    uint32_t disc;
    char group[17];
    uint64_t bytes;
    uint8_t sha[32];
    char cid[121];
    char path[128];
} sn_entry_t;

/* Find the swarm line for disc `disc`, group `group`. 0 = found; else a
 * reason, and *line the line it concerns (0 for the whole file). */
const char *sn_manifest_find(const char *t, uint32_t n, uint32_t disc, const char *group,
                             sn_entry_t *e, uint32_t *line);

/* ---- writing a disc --------------------------------------------------------- */
/* 1 when b can take an image of `bytes`: 512-byte sectors, large enough, and
 * blank (no 0x55AA at the end of LBA 0, no "EFI PART" at LBA 1). sector is a
 * 512-byte scratch. */
int sn_drive_blank(hw_blk_t *b, uint64_t bytes, uint8_t *sector);

typedef struct sn_writer {
    hw_blk_t *b;
    uint64_t want;    /* image bytes */
    uint64_t got;     /* bytes received */
    uint64_t written; /* bytes on the drive past the held head */
    uint32_t fill;    /* bytes in stage */
    sha256_ctx_t sha;
    int err; /* sticky: a write failed or the image ran long */
    void (*progress)(void *ctx, uint64_t got, uint64_t want);
    void *progress_ctx;
    uint8_t hold[SN_HOLD_SECTORS * 512u];
    uint8_t stage[SN_STAGE_BYTES];
} sn_writer_t;

void sn_writer_init(sn_writer_t *w, hw_blk_t *b, uint64_t want);
/* ipfsn_write_fn: takes the image in order. */
int sn_writer_put(void *ctx, const uint8_t *data, uint32_t len);
/* Flush, compare size and SHA-256, and only then write the held head.
 * 0 = committed; otherwise *why says what failed and the head is not written. */
int sn_writer_commit(sn_writer_t *w, const uint8_t sha[32], const char **why);

/* ---- the fetch ------------------------------------------------------------- */
typedef struct sn_fetch {
    sn_link_t *link; /* NULL in host tests, where `get` is supplied */
    sn_http_t http[HW_GATEWAY_MAX];
    uint32_t nhttp;
    ipfsn_get_fn get; /* block source: gateways, or a test's */
    void *get_ctx;
    const hw_plat_t *p;
    uint32_t pct; /* last progress decile logged */
    sn_entry_t e;
    ipfsn_walk_t walk;
    sn_writer_t wr;
    /* The last block fetched, kept so a run of identical leaves (the zero
     * padding a FAT32 swarm disc carries) costs one request, not one each.
     * The walker hashes every block it is handed, cached or not. */
    ipfsn_cid_t last_cid;
    uint32_t last_len; /* 0 = nothing cached */
    uint8_t last[SN_CACHE_MAX];
    uint8_t dirbuf[IPFSN_NODE_MAX];
    char manifest[SN_MANIFEST_MAX];
    uint8_t leaf[IPFSN_BLOCK_MAX];
} sn_fetch_t;

/* The http:// gateways of m as the block source (logs each https:// one
 * skipped). Returns how many gateways can be used. */
uint32_t sn_fetch_gateways(sn_fetch_t *f, sn_link_t *l, const hw_models_t *m);

/* ipfsn_get_fn over the gateways, in order, each block verified. */
int sn_gateway_get(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len);

/* Fetch every swarm disc m expects and lacks onto blank drives of r, in disc
 * order. Returns how many were committed. m must have been indexed. */
uint32_t sn_fetch_missing(sn_fetch_t *f, hw_models_t *m, hw_registry_t *r);

/* The air-gap lines, for the first expected disc that is missing. */
void sn_log_offline(const hw_plat_t *p, const hw_models_t *m);

/* ---- the post-hydration seal ---------------------------------------------- */
/* Once every swarm disc the core expects is attached after a fetch, the
 * network that carried them is severed, as /ZXV/UPSTREAM.CID's "seal" line
 * says (hw_models.h):
 *   POWERDOWN_NIC    the driver stops the MAC and powers the PHY down, then
 *                    PCI Bus Master and Memory Space are cleared and read back
 *                    (hw_nic_power_off): no DMA, no register access until reset
 *   LOCAL_MESH_ONLY  the NIC stays powered; the stack's WAN state (address,
 *                    routes, DNS, DHCP lease, sockets) is wiped with the rest
 *   PROMPT_USER      (default) asks on the console for p or m; no answer in
 *                    SN_SEAL_PROMPT_MS powers the NIC down
 * In every mode the stack's state (gateway connections included) and the
 * fetch buffers are zeroed, and the hydration code (io->code) overwritten with
 * INT3 and read back, so no code path that fetches or writes a disc image is
 * left; sn_sealed() is then 1. The receipt is printed last, after each step
 * has been checked, so it only states what happened:
 *   [SEAL] Swarm Discs 2..N successfully hydrated and verified.
 *   [SEAL] Master CID: bafy...
 *   [SEAL] Network hydration channel severed: NIC bus mastering disabled.
 *   [SEAL] Operating system is now immutable and air-gapped.
 * (LOCAL_MESH_ONLY says what it removed and that the mesh remains.) A step
 * that fails prints [SEAL_FAIL] and no receipt; returns 0 sealed, -1 not. */
#define SN_SEAL_PROMPT_MS 30000u

typedef struct sn_seal_io {
    int (*key)(void *ctx); /* an ASCII key from any console, 0 for none */
    void *ctx;
    uint8_t *code; /* the hydration code to overwrite */
    uint32_t code_len;
} sn_seal_io_t;

int sn_seal(sn_link_t *l, sn_fetch_t *f, const hw_models_t *m, const sn_seal_io_t *io);
int sn_sealed(void);

#endif

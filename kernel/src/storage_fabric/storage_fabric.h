/* storage_fabric.h — ZXV Storage Fabric Compound Module
 *
 * The Storage Fabric unifies all storage, filesystem, and data persistence
 * modules into a single coherent fabric for content-addressable, replicated,
 * and economically-incentivized storage.
 *
 * Sub-modules integrated:
 *   1. VFS — Virtual filesystem switch
 *   2. ZXVFS — ZXV native filesystem (content-addressable)
 *   3. IPFS — InterPlanetary File System integration
 *   4. Blockdev — Block device abstraction
 *   5. ATA — ATA/SATA driver
 *   6. NVMe — NVMe driver
 *   7. Fat32 — FAT32 filesystem
 *   8. Mesh Net — P2P replication via trade routes
 *   9. Financial Fabric — Storage pricing & settlement
 *   10. Identity Fabric — Access control via credentials
 *   11. Orbital Fabric — Schema translation for storage events
 *
 * Design principles:
 * - Content-addressable storage (CID-based)
 * - Replication factor enforced economically
 * - Storage priced in Vino vouchers (Form 6: Material)
 * - Access controlled via Identity Fabric credentials
 * - Erasure coding for durability
 * - Mesh Net for geo-distributed replication
 * - Paraconsistent logic (LPRES) for all attestations
 * - M5 coverage hyperbola enforcement on all storage operations
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef STORAGE_FABRIC_H
#define STORAGE_FABRIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "vfs.h"
#include "zxvfs.h"
#include "ipfs.h"
#include "blockdev.h"
#include "ata.h"
#include "nvme.h"
#include "fat32.h"
#include "mesh_net.h"
#include "financial_fabric.h"
#include "identity_fabric.h"
#include "orbital_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ===== Constants ===== */

#define SF_MAX_VOLUMES           64
#define SF_MAX_FILES             4096
#define SF_MAX_REPLICAS          8
#define SF_MAX_BLOCK_DEVICES     32
#define SF_MAX_NAME_LEN          128
#define SF_CID_LEN               32

/* ===== Storage Volume ===== */

typedef enum {
    SF_VOL_UNUSED      = 0,
    SF_VOL_ZXVFS       = 1,   /* ZXV native filesystem */
    SF_VOL_FAT32       = 2,   /* FAT32 */
    SF_VOL_IPFS        = 3,   /* IPFS mount */
    SF_VOL_BLOCK       = 4,   /* Raw block device */
    SF_VOL_MESH        = 5    /* Mesh-replicated volume */
} sf_volume_type_t;

typedef struct sf_volume {
    uint32_t id;
    char name[SF_MAX_NAME_LEN];
    sf_volume_type_t type;
    
    /* Block device backing */
    uint32_t block_device_id;
    uint64_t offset;
    uint64_t size;
    
    /* Filesystem */
    void *fs_handle;           /* zxvfs_t*, fat32_t*, ipfs_node_t* */
    
    /* Replication */
    uint8_t replication_factor;
    uint32_t replica_volume_ids[SF_MAX_REPLICAS];
    uint32_t num_replicas;
    
    /* Mesh replication */
    uint32_t mesh_network_id;
    uint32_t trade_route_ids[SF_MAX_REPLICAS];
    uint32_t num_trade_routes;
    
    /* Pricing */
    uint64_t price_per_gb_per_month;  /* Vino vouchers (Form 6) */
    uint8_t pricing_form;             /* Capital form for pricing */
    
    /* Access control */
    uint32_t credential_id;           /* Identity Fabric credential */
    bool encrypted;
    uint8_t encryption_key[32];
    
    /* Statistics */
    uint64_t total_bytes_written;
    uint64_t total_bytes_read;
    uint64_t total_files;
    uint64_t used_bytes;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
    bool mounted;
} sf_volume_t;

/* ===== Block Device ===== */

typedef enum {
    SF_BDEV_UNUSED   = 0,
    SF_BDEV_ATA      = 1,
    SF_BDEV_NVME     = 2,
    SF_BDEV_SD       = 3,
    SF_BDEV_USB      = 4,
    SF_BDEV_VIRTUAL  = 5,
    SF_BDEV_RAM      = 6
} sf_bdev_type_t;

typedef struct sf_block_device {
    uint32_t id;
    char name[SF_MAX_NAME_LEN];
    sf_bdev_type_t type;
    
    /* Hardware */
    void *hw_handle;           /* ata_device_t*, nvme_controller_t*, etc. */
    
    /* Geometry */
    uint64_t sector_size;
    uint64_t num_sectors;
    uint64_t total_size;
    
    /* Partitions */
    struct {
        uint64_t start_sector;
        uint64_t num_sectors;
        uint8_t type;
        bool active;
    } partitions[16];
    uint32_t num_partitions;
    
    /* Health */
    struct {
        uint64_t read_errors;
        uint64_t write_errors;
        uint64_t smart_attrs[32];
        bool healthy;
    } health;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
} sf_block_device_t;

/* ===== File ===== */

typedef struct sf_file {
    uint32_t id;
    uint32_t volume_id;
    char path[SF_MAX_NAME_LEN];
    uint8_t cid[SF_CID_LEN];       /* Content ID (SHA-256) */
    
    /* Metadata */
    uint64_t size;
    uint64_t created_tick;
    uint64_t modified_tick;
    uint32_t permissions;          /* Unix-style */
    uint32_t owner_id;             /* Identity Fabric identity */
    
    /* Replication */
    uint8_t replication_factor;
    uint32_t replica_cids[SF_MAX_REPLICAS];
    uint32_t num_replicas;
    
    /* Erasure coding */
    uint8_t ec_k;                  /* Data shards */
    uint8_t ec_m;                  /* Parity shards */
    
    /* Mesh replication */
    uint32_t mesh_network_id;
    uint32_t trade_route_ids[SF_MAX_REPLICAS];
    
    /* Access */
    uint32_t credential_id;
    bool encrypted;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
} sf_file_t;

/* ===== Storage Fabric ===== */

typedef struct storage_fabric {
    /* Core sub-modules */
    vfs_t vfs;                     /* Virtual Filesystem */
    zxvfs_t zxvfs;                 /* ZXV Filesystem */
    ipfs_node_t ipfs;              /* IPFS Node */
    blockdev_subsystem_t blockdev; /* Block Device Subsystem */
    ata_subsystem_t ata;           /* ATA Subsystem */
    nvme_subsystem_t nvme;         /* NVMe Subsystem */
    fat32_fs_t fat32;              /* FAT32 Filesystem */
    
    /* Integration references */
    mesh_net_t *mesh;              /* Mesh Net */
    financial_fabric_t *financial; /* Financial Fabric */
    identity_fabric_t *identity;   /* Identity Fabric */
    orbital_fabric_t *orbital;     /* Orbital Fabric */
    compute_fabric_t *compute;     /* Compute Fabric */
    network_fabric_t *network;     /* Network Fabric */
    
    /* Fabric-level state */
    sf_volume_t volumes[SF_MAX_VOLUMES];
    uint32_t num_volumes;
    uint32_t next_volume_id;
    
    sf_block_device_t block_devices[SF_MAX_BLOCK_DEVICES];
    uint32_t num_block_devices;
    
    sf_file_t files[SF_MAX_FILES];
    uint32_t num_files;
    
    /* Global statistics */
    struct {
        uint64_t total_volumes_created;
        uint64_t total_files_stored;
        uint64_t total_bytes_stored;
        uint64_t total_bytes_retrieved;
        uint64_t total_replication_ops;
        uint64_t total_mesh_replication_revenue;
        uint64_t total_storage_fees_collected;
        uint64_t total_block_devices;
    } stats;
    
    /* Paraconsistent global state */
    lpres_state_t global_attestation;
    bool global_safety_gate;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;
    
    /* Configuration */
    struct {
        uint8_t default_replication_factor;
        uint8_t default_ec_k;
        uint8_t default_ec_m;
        bool require_encryption;
        bool auto_mesh_replication;
        surplus_real_t min_durability;  /* 0.999999 = 6 nines */
    } config;
    
    bool initialized;
} storage_fabric_t;

/* ===== API ===== */

/* Initialize the Storage Fabric */
void sf_init(storage_fabric_t *fabric,
             mesh_net_t *mesh,
             financial_fabric_t *financial,
             identity_fabric_t *identity,
             orbital_fabric_t *orbital,
             compute_fabric_t *compute,
             network_fabric_t *network);

/* Register built-in storage modules */
void sf_register_builtins(storage_fabric_t *fabric);

/* ===== Block Device Management ===== */

int32_t sf_register_block_device(storage_fabric_t *fabric,
                                 const char *name, sf_bdev_type_t type,
                                 void *hw_handle,
                                 uint64_t sector_size, uint64_t num_sectors);

sf_block_device_t *sf_get_block_device(storage_fabric_t *fabric, uint32_t device_id);

/* ===== Volume Management ===== */

int32_t sf_create_volume(storage_fabric_t *fabric,
                         const char *name, sf_volume_type_t type,
                         uint32_t block_device_id,
                         uint64_t offset, uint64_t size,
                         uint8_t replication_factor);

int32_t sf_mount_volume(storage_fabric_t *fabric, uint32_t volume_id);
int32_t sf_unmount_volume(storage_fabric_t *fabric, uint32_t volume_id);

sf_volume_t *sf_get_volume(storage_fabric_t *fabric, uint32_t volume_id);

/* ===== File Operations ===== */

int32_t sf_create_file(storage_fabric_t *fabric,
                       uint32_t volume_id, const char *path,
                       uint32_t owner_id, uint32_t permissions);

int32_t sf_write_file(storage_fabric_t *fabric, uint32_t file_id,
                      const uint8_t *data, uint64_t offset, uint64_t len);

int32_t sf_read_file(storage_fabric_t *fabric, uint32_t file_id,
                     uint8_t *buffer, uint64_t offset, uint64_t len);

int32_t sf_delete_file(storage_fabric_t *fabric, uint32_t file_id);

sf_file_t *sf_get_file(storage_fabric_t *fabric, uint32_t file_id);
sf_file_t *sf_find_file_by_path(storage_fabric_t *fabric, uint32_t volume_id, const char *path);

/* ===== Replication ===== */

int32_t sf_replicate_file(storage_fabric_t *fabric,
                          uint32_t file_id,
                          uint32_t mesh_network_id,
                          uint8_t replication_factor);

int32_t sf_verify_replication(storage_fabric_t *fabric, uint32_t file_id);

/* ===== Mesh Replication Settlement ===== */

int32_t sf_settle_replication(storage_fabric_t *fabric,
                              uint32_t trade_route_id,
                              uint64_t current_cycle);

/* ===== Health & Attestation ===== */

int32_t sf_check_volume_health(storage_fabric_t *fabric,
                               uint32_t volume_id,
                               void *health_out);

int32_t sf_check_block_device_health(storage_fabric_t *fabric,
                                     uint32_t device_id,
                                     void *health_out);

int32_t sf_check_global_health(storage_fabric_t *fabric);

bool sf_global_safety_gate(storage_fabric_t *fabric);

lpres_state_t sf_attest(storage_fabric_t *fabric, uint32_t volume_id,
                        uint32_t op_id, void *args, int32_t result);

/* Coverage enforcement */
void sf_update_coverage(storage_fabric_t *fabric);
bool sf_enforce_coverage(storage_fabric_t *fabric, surplus_real_t min_ratio);

/* Statistics */
void sf_get_stats(storage_fabric_t *fabric, void *stats_out);

/* Paraconsistent state */
lpres_state_t sf_get_attestation(storage_fabric_t *fabric, uint32_t volume_id);
void sf_set_attestation(storage_fabric_t *fabric, uint32_t volume_id, lpres_state_t state);

/* Utility */
const char *sf_lpres_state_name(lpres_state_t state);
const char *sf_volume_type_name(sf_volume_type_t type);
const char *sf_bdev_type_name(sf_bdev_type_t type);

#endif /* STORAGE_FABRIC_H */

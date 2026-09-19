/* storage_fabric.c — ZXV Storage Fabric Compound Module Implementation
 *
 * Unifies all storage modules: VFS, ZXVFS, IPFS, Blockdev, ATA, NVMe, FAT32,
 * with Mesh Net, Financial, Identity, Orbital, Compute, and Network fabric integration.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "storage_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "selfaudit.h"

/* ===== Helper Functions ===== */

static void sf_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void sf_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int sf_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t sf_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void sf_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ===== Coverage Computation ===== */

static surplus_real_t sf_compute_coverage(const m5_coords_t *m5) {
    if (!m5) return SR_ZERO;
    surplus_real_t omega = SR_FROM_INT(m5->omega);
    surplus_real_t r = m5->r;
    surplus_real_t ell = m5->ell;
    surplus_real_t phi = m5->phi;
    surplus_real_t chi = SR_FROM_INT(m5->chi);
    surplus_real_t numerator = SR_MUL(SR_MUL(omega, r), ell);
    surplus_real_t denominator = SR_MUL(phi, chi);
    if (SR_CMP(denominator, SR_ZERO) == 0) return SR_FROM_FLOAT(100.0);
    return SR_DIV(numerator, denominator);
}

/* ===== LPRES Attestation ===== */

lpres_state_t sf_attest(storage_fabric_t *fabric, uint32_t volume_id,
                        uint32_t op_id, void *args, int32_t result) {
    if (!fabric || volume_id >= fabric->num_volumes) return LPRES_STATE_NEITHER;
    
    sf_volume_t *vol = &fabric->volumes[volume_id];
    lpres_state_t result_att = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t vol_att = vol->attestation;
    lpres_state_t coverage_att = (SR_CMP(vol->coverage_ratio, fabric->min_coverage_ratio) >= 0) 
                                  ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t fabric_att = fabric->global_attestation;
    
    lpres_state_t combined = lpres_conjoin(result_att, vol_att);
    combined = lpres_conjoin(combined, coverage_att);
    combined = lpres_conjoin(combined, fabric_att);
    
    vol->attestation = combined;
    fabric->global_attestation = lpres_conjoin(fabric->global_attestation, combined);
    
    return combined;
}

/* ===== Initialization ===== */

void sf_init(storage_fabric_t *fabric,
             mesh_net_t *mesh,
             financial_fabric_t *financial,
             identity_fabric_t *identity,
             orbital_fabric_t *orbital,
             compute_fabric_t *compute,
             network_fabric_t *network) {
    if (!fabric) return;
    
    sf_mem_set(fabric, 0, sizeof(*fabric));
    fabric->mesh = mesh;
    fabric->financial = financial;
    fabric->identity = identity;
    fabric->orbital = orbital;
    fabric->compute = compute;
    fabric->network = network;
    
    /* Initialize sub-modules */
    /* vfs_init(&fabric->vfs); */
    /* zxvfs_init(&fabric->zxvfs); */
    /* ipfs_init(&fabric->ipfs); */
    /* blockdev_init(&fabric->blockdev); */
    /* ata_init(&fabric->ata); */
    /* nvme_init(&fabric->nvme); */
    /* fat32_init(&fabric->fat32); */
    
    /* Initialize M5 coordinates */
    fabric->m5.omega = 1;
    fabric->m5.r = SR_FROM_FLOAT(6.0);  /* Material/Storage rail */
    fabric->m5.ell = SR_ONE;
    fabric->m5.phi = SR_ZERO;
    fabric->m5.chi = 0;
    fabric->coverage_ratio = sf_compute_coverage(&fabric->m5);
    fabric->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    
    /* Default configuration */
    fabric->config.default_replication_factor = 3;
    fabric->config.default_ec_k = 8;
    fabric->config.default_ec_m = 4;
    fabric->config.require_encryption = true;
    fabric->config.auto_mesh_replication = true;
    fabric->config.min_durability = SR_FROM_FLOAT(0.999999);
    
    fabric->global_attestation = LPRES_STATE_NEITHER;
    fabric->global_safety_gate = false;
    fabric->initialized = true;
}

void sf_register_builtins(storage_fabric_t *fabric) {
    if (!fabric) return;
}

/* ===== Block Device Management ===== */

int32_t sf_register_block_device(storage_fabric_t *fabric,
                                 const char *name, sf_bdev_type_t type,
                                 void *hw_handle,
                                 uint64_t sector_size, uint64_t num_sectors) {
    if (!fabric || !name || fabric->num_block_devices >= SF_MAX_BLOCK_DEVICES) return -1;
    
    sf_block_device_t *bdev = &fabric->block_devices[fabric->num_block_devices];
    sf_mem_set(bdev, 0, sizeof(*bdev));
    bdev->id = fabric->num_block_devices;
    
    sf_str_copy(bdev->name, name, SF_MAX_NAME_LEN);
    bdev->type = type;
    bdev->hw_handle = hw_handle;
    bdev->sector_size = sector_size;
    bdev->num_sectors = num_sectors;
    bdev->total_size = sector_size * num_sectors;
    
    /* Initialize M5 */
    bdev->m5.omega = fabric->num_block_devices + 1;
    bdev->m5.r = SR_FROM_FLOAT(6.0);
    bdev->m5.ell = SR_ONE;
    bdev->m5.phi = SR_ZERO;
    bdev->m5.chi = 0;
    bdev->coverage_ratio = sf_compute_coverage(&bdev->m5);
    
    bdev->health.healthy = true;
    bdev->attestation = LPRES_STATE_NEITHER;
    bdev->active = true;
    
    fabric->num_block_devices++;
    fabric->stats.total_block_devices++;
    
    return (int32_t)(bdev->id);
}

sf_block_device_t *sf_get_block_device(storage_fabric_t *fabric, uint32_t device_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_block_devices; i++) {
        if (fabric->block_devices[i].id == device_id && fabric->block_devices[i].active) {
            return &fabric->block_devices[i];
        }
    }
    return NULL;
}

/* ===== Volume Management ===== */

int32_t sf_create_volume(storage_fabric_t *fabric,
                         const char *name, sf_volume_type_t type,
                         uint32_t block_device_id,
                         uint64_t offset, uint64_t size,
                         uint8_t replication_factor) {
    if (!fabric || !name || fabric->num_volumes >= SF_MAX_VOLUMES) return -1;
    
    sf_block_device_t *bdev = sf_get_block_device(fabric, block_device_id);
    if (!bdev) return -1;
    if (offset + size > bdev->total_size) return -1;
    
    sf_volume_t *vol = &fabric->volumes[fabric->num_volumes];
    sf_mem_set(vol, 0, sizeof(*vol));
    vol->id = fabric->next_volume_id++;
    
    sf_str_copy(vol->name, name, SF_MAX_NAME_LEN);
    vol->type = type;
    vol->block_device_id = block_device_id;
    vol->offset = offset;
    vol->size = size;
    vol->replication_factor = replication_factor ? replication_factor : fabric->config.default_replication_factor;
    vol->pricing_form = 6;  /* Form 6: Material */
    vol->price_per_gb_per_month = 1000;  /* 1000 vouchers/GB/month */
    
    /* Initialize filesystem based on type */
    switch (type) {
        case SF_VOL_ZXVFS:
            /* vol->fs_handle = zxvfs_create(&fabric->zxvfs, bdev, offset, size); */
            break;
        case SF_VOL_FAT32:
            /* vol->fs_handle = fat32_create(&fabric->fat32, bdev, offset, size); */
            break;
        case SF_VOL_IPFS:
            /* vol->fs_handle = ipfs_mount(&fabric->ipfs, name); */
            break;
        default:
            break;
    }
    
    /* Initialize M5 */
    vol->m5.omega = fabric->num_volumes + 1;
    vol->m5.r = SR_FROM_FLOAT(6.0);
    vol->m5.ell = SR_ONE;
    vol->m5.phi = SR_ZERO;
    vol->m5.chi = 0;
    vol->coverage_ratio = sf_compute_coverage(&vol->m5);
    
    vol->attestation = LPRES_STATE_NEITHER;
    vol->active = true;
    vol->mounted = false;
    
    fabric->num_volumes++;
    fabric->stats.total_volumes_created++;
    
    return (int32_t)(vol->id);
}

int32_t sf_mount_volume(storage_fabric_t *fabric, uint32_t volume_id) {
    if (!fabric) return -1;
    sf_volume_t *vol = sf_get_volume(fabric, volume_id);
    if (!vol) return -1;
    
    /* Mount filesystem */
    /* vfs_mount(&fabric->vfs, vol->name, vol->fs_handle); */
    
    vol->mounted = true;
    return sf_attest(fabric, volume_id, 0x1000, NULL, 0);
}

int32_t sf_unmount_volume(storage_fabric_t *fabric, uint32_t volume_id) {
    if (!fabric) return -1;
    sf_volume_t *vol = sf_get_volume(fabric, volume_id);
    if (!vol) return -1;
    
    /* Unmount filesystem */
    /* vfs_unmount(&fabric->vfs, vol->name); */
    
    vol->mounted = false;
    return sf_attest(fabric, volume_id, 0x2000, NULL, 0);
}

sf_volume_t *sf_get_volume(storage_fabric_t *fabric, uint32_t volume_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_volumes; i++) {
        if (fabric->volumes[i].id == volume_id && fabric->volumes[i].active) {
            return &fabric->volumes[i];
        }
    }
    return NULL;
}

/* ===== File Operations ===== */

int32_t sf_create_file(storage_fabric_t *fabric,
                       uint32_t volume_id, const char *path,
                       uint32_t owner_id, uint32_t permissions) {
    if (!fabric || !path || fabric->num_files >= SF_MAX_FILES) return -1;
    
    sf_volume_t *vol = sf_get_volume(fabric, volume_id);
    if (!vol || !vol->mounted) return -1;
    
    if_identity_t *owner = if_get_identity(fabric->identity, owner_id);
    if (!owner) return -1;
    
    sf_file_t *file = &fabric->files[fabric->num_files];
    sf_mem_set(file, 0, sizeof(*file));
    file->id = fabric->num_files;
    file->volume_id = volume_id;
    sf_str_copy(file->path, path, SF_MAX_NAME_LEN);
    file->owner_id = owner_id;
    file->permissions = permissions;
    file->created_tick = 0;
    file->modified_tick = 0;
    file->replication_factor = vol->replication_factor;
    file->ec_k = fabric->config.default_ec_k;
    file->ec_m = fabric->config.default_ec_m;
    
    /* Generate CID (placeholder) */
    for (int i = 0; i < SF_CID_LEN; i++) file->cid[i] = (uint8_t)(file->id + i);
    
    /* Initialize M5 */
    file->m5.omega = fabric->num_files + 1;
    file->m5.r = SR_FROM_FLOAT(6.0);
    file->m5.ell = SR_ONE;
    file->m5.phi = SR_ZERO;
    file->m5.chi = 0;
    file->coverage_ratio = sf_compute_coverage(&file->m5);
    
    file->attestation = LPRES_STATE_NEITHER;
    file->active = true;
    
    fabric->num_files++;
    fabric->stats.total_files_stored++;
    
    return sf_attest(fabric, volume_id, 0x3000 | file->id, file, 0);
}

int32_t sf_write_file(storage_fabric_t *fabric, uint32_t file_id,
                      const uint8_t *data, uint64_t offset, uint64_t len) {
    if (!fabric || !data) return -1;
    sf_file_t *file = sf_get_file(fabric, file_id);
    if (!file) return -1;
    
    sf_volume_t *vol = sf_get_volume(fabric, file->volume_id);
    if (!vol || !vol->mounted) return -1;
    
    /* Write via filesystem */
    /* vfs_write(&fabric->vfs, file->path, data, offset, len); */
    
    file->size = offset + len > file->size ? offset + len : file->size;
    file->modified_tick = 0;
    
    vol->total_bytes_written += len;
    vol->used_bytes = file->size;
    fabric->stats.total_bytes_stored += len;
    
    return sf_attest(fabric, file->volume_id, 0x4000 | file_id, (void*)data, 0);
}

int32_t sf_read_file(storage_fabric_t *fabric, uint32_t file_id,
                     uint8_t *buffer, uint64_t offset, uint64_t len) {
    if (!fabric || !buffer) return -1;
    sf_file_t *file = sf_get_file(fabric, file_id);
    if (!file) return -1;
    
    sf_volume_t *vol = sf_get_volume(fabric, file->volume_id);
    if (!vol || !vol->mounted) return -1;
    
    if (offset + len > file->size) return -1;
    
    /* Read via filesystem */
    /* vfs_read(&fabric->vfs, file->path, buffer, offset, len); */
    
    vol->total_bytes_read += len;
    fabric->stats.total_bytes_retrieved += len;
    
    return sf_attest(fabric, file->volume_id, 0x5000 | file_id, buffer, 0);
}

int32_t sf_delete_file(storage_fabric_t *fabric, uint32_t file_id) {
    if (!fabric) return -1;
    sf_file_t *file = sf_get_file(fabric, file_id);
    if (!file) return -1;
    
    sf_volume_t *vol = sf_get_volume(fabric, file->volume_id);
    if (!vol || !vol->mounted) return -1;
    
    /* Delete via filesystem */
    /* vfs_delete(&fabric->vfs, file->path); */
    
    file->active = false;
    vol->used_bytes = vol->used_bytes > file->size ? vol->used_bytes - file->size : 0;
    
    return sf_attest(fabric, file->volume_id, 0x6000 | file_id, file, 0);
}

sf_file_t *sf_get_file(storage_fabric_t *fabric, uint32_t file_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_files; i++) {
        if (fabric->files[i].id == file_id && fabric->files[i].active) {
            return &fabric->files[i];
        }
    }
    return NULL;
}

sf_file_t *sf_find_file_by_path(storage_fabric_t *fabric, uint32_t volume_id, const char *path) {
    if (!fabric || !path) return NULL;
    for (uint32_t i = 0; i < fabric->num_files; i++) {
        if (fabric->files[i].active && fabric->files[i].volume_id == volume_id &&
            sf_str_cmp(fabric->files[i].path, path) == 0) {
            return &fabric->files[i];
        }
    }
    return NULL;
}

/* ===== Replication ===== */

int32_t sf_replicate_file(storage_fabric_t *fabric,
                          uint32_t file_id,
                          uint32_t mesh_network_id,
                          uint8_t replication_factor) {
    if (!fabric) return -1;
    sf_file_t *file = sf_get_file(fabric, file_id);
    if (!file) return -1;
    
    if (!fabric->config.auto_mesh_replication) return -1;
    if (!fabric->mesh) return -1;
    
    /* Create trade routes for replication */
    for (uint8_t i = 0; i < replication_factor && i < SF_MAX_REPLICAS; i++) {
        /* Would create mesh trade routes to replica nodes */
        /* int32_t route_id = mn_create_route(...); */
        /* file->trade_route_ids[file->num_replicas++] = route_id; */
        fabric->stats.total_replication_ops++;
    }
    
    file->num_replicas = replication_factor;
    return sf_attest(fabric, file->volume_id, 0x7000 | file_id, file, 0);
}

int32_t sf_verify_replication(storage_fabric_t *fabric, uint32_t file_id) {
    if (!fabric) return -1;
    sf_file_t *file = sf_get_file(fabric, file_id);
    if (!file) return -1;
    
    /* Verify all replicas */
    for (uint32_t i = 0; i < file->num_replicas; i++) {
        /* Would verify CID matches on each replica */
    }
    
    return sf_attest(fabric, file->volume_id, 0x8000 | file_id, file, 0);
}

/* ===== Mesh Replication Settlement ===== */

int32_t sf_settle_replication(storage_fabric_t *fabric,
                              uint32_t trade_route_id,
                              uint64_t current_cycle) {
    if (!fabric || !fabric->mesh) return -1;
    
    /* Settle via Mesh Token */
    /* mn_send_data(&fabric->mesh, trade_route_id, data_size, current_cycle); */
    /* mesh_token_settle(...); */
    
    fabric->stats.total_mesh_replication_revenue += 1000;  /* Placeholder */
    fabric->stats.total_storage_fees_collected += 1000;
    
    return sf_attest(fabric, 0xFFFFFFFF, 0x9000 | trade_route_id, NULL, 0);
}

/* ===== Health & Attestation ===== */

int32_t sf_check_volume_health(storage_fabric_t *fabric,
                               uint32_t volume_id,
                               void *health_out) {
    if (!fabric) return -1;
    sf_volume_t *vol = sf_get_volume(fabric, volume_id);
    if (!vol) return -1;
    
    /* Update coverage */
    vol->coverage_ratio = sf_compute_coverage(&vol->m5);
    
    /* Check block device health */
    sf_block_device_t *bdev = sf_get_block_device(fabric, vol->block_device_id);
    if (bdev) {
        if (!bdev->health.healthy) {
            vol->attestation = LPRES_STATE_FALSE;
            return -1;
        }
    }
    
    /* Check replication */
    if (vol->num_replicas < vol->replication_factor) {
        vol->attestation = LPRES_STATE_BOTH;
    } else {
        vol->attestation = LPRES_STATE_TRUE;
    }
    
    return vol->attestation == LPRES_STATE_TRUE ? 0 : -1;
}

int32_t sf_check_block_device_health(storage_fabric_t *fabric,
                                     uint32_t device_id,
                                     void *health_out) {
    if (!fabric) return -1;
    sf_block_device_t *bdev = sf_get_block_device(fabric, device_id);
    if (!bdev) return -1;
    
    /* Update coverage */
    bdev->coverage_ratio = sf_compute_coverage(&bdev->m5);
    
    /* Check SMART attributes */
    if (bdev->health.read_errors > 100 || bdev->health.write_errors > 100) {
        bdev->health.healthy = false;
        bdev->attestation = LPRES_STATE_FALSE;
        return -1;
    }
    
    bdev->attestation = LPRES_STATE_TRUE;
    return 0;
}

int32_t sf_check_global_health(storage_fabric_t *fabric) {
    if (!fabric) return -1;
    
    int32_t unhealthy = 0;
    for (uint32_t i = 0; i < fabric->num_volumes; i++) {
        if (fabric->volumes[i].active) {
            if (sf_check_volume_health(fabric, fabric->volumes[i].id, NULL) < 0) {
                unhealthy++;
            }
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_block_devices; i++) {
        if (fabric->block_devices[i].active) {
            if (sf_check_block_device_health(fabric, fabric->block_devices[i].id, NULL) < 0) {
                unhealthy++;
            }
        }
    }
    
    fabric->global_safety_gate = (unhealthy == 0);
    fabric->global_attestation = fabric->global_safety_gate ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    
    return unhealthy == 0 ? 0 : -1;
}

bool sf_global_safety_gate(storage_fabric_t *fabric) {
    return fabric ? fabric->global_safety_gate : false;
}

/* ===== Coverage ===== */

void sf_update_coverage(storage_fabric_t *fabric) {
    if (!fabric) return;
    
    fabric->coverage_ratio = sf_compute_coverage(&fabric->m5);
    
    for (uint32_t i = 0; i < fabric->num_volumes; i++) {
        if (fabric->volumes[i].active) {
            fabric->volumes[i].coverage_ratio = sf_compute_coverage(&fabric->volumes[i].m5);
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_block_devices; i++) {
        if (fabric->block_devices[i].active) {
            fabric->block_devices[i].coverage_ratio = sf_compute_coverage(&fabric->block_devices[i].m5);
        }
    }
}

bool sf_enforce_coverage(storage_fabric_t *fabric, surplus_real_t min_ratio) {
    if (!fabric) return false;
    
    if (SR_CMP(fabric->coverage_ratio, min_ratio) < 0) return false;
    
    for (uint32_t i = 0; i < fabric->num_volumes; i++) {
        if (fabric->volumes[i].active) {
            if (SR_CMP(fabric->volumes[i].coverage_ratio, min_ratio) < 0) return false;
        }
    }
    
    return true;
}

/* ===== Statistics ===== */

void sf_get_stats(storage_fabric_t *fabric, void *stats_out) {
    if (!fabric || !stats_out) return;
    sf_mem_copy(stats_out, &fabric->stats, sizeof(fabric->stats));
}

/* ===== Paraconsistent State ===== */

lpres_state_t sf_get_attestation(storage_fabric_t *fabric, uint32_t volume_id) {
    if (!fabric || volume_id >= fabric->num_volumes) return LPRES_STATE_NEITHER;
    return fabric->volumes[volume_id].attestation;
}

void sf_set_attestation(storage_fabric_t *fabric, uint32_t volume_id, lpres_state_t state) {
    if (!fabric || volume_id >= fabric->num_volumes) return;
    fabric->volumes[volume_id].attestation = state;
}

/* ===== Utility ===== */

const char *sf_lpres_state_name(lpres_state_t state) {
    return lpres_state_name(state);
}

const char *sf_volume_type_name(sf_volume_type_t type) {
    static const char *names[] = {"UNUSED", "ZXVFS", "FAT32", "IPFS", "BLOCK", "MESH"};
    if (type <= SF_VOL_MESH) return names[type];
    return "UNKNOWN";
}

const char *sf_bdev_type_name(sf_bdev_type_t type) {
    static const char *names[] = {"UNUSED", "ATA", "NVME", "SD", "USB", "VIRTUAL", "RAM"};
    if (type <= SF_BDEV_RAM) return names[type];
    return "UNKNOWN";
}

/* identity.h — Universal National Identity System
 *
 * Hardware-as-code identity processor covering all 193 UN member states
 * plus observer states and special administrative regions.
 * Each national identity is a virtual hardware device with register maps,
 * biometric attestation, and M⁵ coverage verification.
 *
 * Supports:
 *   - Eastern systems (China National ID, Japan My Number, Korea RRN, India Aadhaar)
 *   - Western systems (US SSN/REAL ID, EU eIDAS, UK GOV.UK Verify)
 *   - Global South (Nigeria NIN, Brazil CPF, South Africa ID, etc.)
 *   - All UN member states + observers
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#ifndef IDENTITY_H
#define IDENTITY_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "edp_risk.h"

/* ===== Identity System Types ===== */

typedef enum {
    ID_SYS_UN_NORTH_AMERICA    = 0,  /* US, Canada, Mexico */
    ID_SYS_EU                  = 1,  /* EU eIDAS + national IDs */
    ID_SYS_UK                  = 2,  /* UK GOV.UK Verify */
    ID_SYS_EAST_ASIA           = 3,  /* China, Japan, Korea, Taiwan, Mongolia */
    ID_SYS_SOUTH_ASIA          = 4,  /* India, Pakistan, Bangladesh, Sri Lanka, Nepal */
    ID_SYS_SOUTHEAST_ASIA      = 5,  /* ASEAN nations */
    ID_SYS_CENTRAL_ASIA        = 6,  /* Stan nations */
    ID_SYS_MIDDLE_EAST         = 7,  /* Gulf, Levant, Iran, Iraq, etc. */
    ID_SYS_AFRICA              = 8,  /* All 54 African nations */
    ID_SYS_LATIN_AMERICA       = 9,  /* Central + South America + Caribbean */
    ID_SYS_OCEANIA             = 10, /* Australia, NZ, Pacific Islands */
    ID_SYS_RUSSIA_BELARUS      = 11, /* Russia, Belarus, CIS */
    ID_SYS_NORDIC              = 12, /* Scandinavia + Baltic */
    ID_SYS_OBSERVER            = 13, /* Vatican, Palestine, etc. */
    ID_SYS_MAX                 = 14
} identity_system_t;

/* ===== Identity Document Types ===== */

typedef enum {
    ID_DOC_NATIONAL_ID       = 0,
    ID_DOC_PASSPORT          = 1,
    ID_DOC_DRIVERS_LICENSE   = 2,
    ID_DOC_SSN               = 3,
    ID_DOC_TAX_ID            = 4,
    ID_DOC_BIOMETRIC         = 5,
    ID_DOC_DIGITAL_CERT      = 6,
    ID_DOC_REFUGEE           = 7,
    ID_DOC_DIPLOMATIC        = 8,
    ID_DOC_TRIBAL            = 9,
    ID_DOC_MAX               = 10
} identity_doc_type_t;

/* ===== Biometric Modalities ===== */

typedef enum {
    BIO_NONE      = 0,
    BIO_FINGERPRINT = 1,
    BIO_FACE       = 2,
    BIO_IRIS       = 4,
    BIO_VOICE      = 8,
    BIO_DNA        = 16,
    BIO_PALM       = 32,
    BIO_VEIN       = 64,
    BIO_GAIT       = 128,
    BIO_ALL        = 0xFF
} biometric_modality_t;

/* ===== UCI (Universal Citizen Identifier) ===== */

typedef struct {
    /* ISO 3166-1 numeric country code (UN M49) */
    uint16_t country_code;
    /* Identity system classification */
    identity_system_t system;
    /* National identifier (hashed, never stored raw) */
    uint64_t national_id_hash;
    /* Document type */
    identity_doc_type_t doc_type;
    /* Biometric attestation flags */
    uint8_t biometric_flags;
    /* M⁵ coordinates */
    m5_coords_t m5;
    /* Coverage status */
    surplus_real_t coverage_ratio;
    bool coverage_verified;
    /* Verification level */
    uint8_t verification_level;  /* 0=none, 1=basic, 2=verified, 3=biometric, 4=quantum */
    /* Status */
    bool active;
    bool sanctioned;
    bool peps;  /* Politically Exposed Person */
    /* Timestamps */
    uint32_t issued_tick;
    uint32_t expires_tick;
    /* Custody chain */
    uint32_t issuing_authority;
    uint32_t attesting_authority;
} uci_t;

/* ===== Identity Device (hardware-as-code) ===== */

typedef struct {
    uci_t uci;
    
    /* Register map */
    uint32_t reg_status;            /* Device status register */
    uint32_t reg_country;           /* Country code register */
    uint64_t reg_id_hash;           /* ID hash register */
    uint32_t reg_verification;      /* Verification level register */
    
    /* DMA buffer for attestation chain */
    uint32_t dma_attestation[8];
    uint32_t dma_head;
    uint32_t dma_tail;
    
    /* IRQ flags */
    bool irq_verification_requested;
    bool irq_biometric_match;
    bool irq_coverage_breach;
    bool irq_sanction_hit;
    
    char holder_name[64];
    char country_name[32];
    char iso_alpha3[4];  /* ISO 3166-1 alpha-3 */
} identity_device_t;

/* ===== Identity Registry ===== */

typedef struct {
    identity_device_t identities[1024];
    uint32_t num_identities;
    
    /* System-wide metrics */
    uint32_t total_verified;
    uint32_t total_biometric;
    uint32_t total_sanctioned;
    surplus_real_t system_coverage;
} identity_registry_t;

/* ===== API ===== */

void identity_registry_init(identity_registry_t *reg);

/* Register a new identity — like powering on a device */
uint32_t identity_register(identity_registry_t *reg,
                            uint16_t country_code,
                            identity_system_t system,
                            identity_doc_type_t doc_type,
                            uint64_t national_id_hash,
                            uint8_t biometric_flags,
                            const char *holder_name,
                            const char *country_name,
                            const char *iso_alpha3);

/* Verify identity coverage */
bool identity_verify(identity_device_t *dev);

/* Biometric attestation */
int32_t identity_attest_biometric(identity_device_t *dev, biometric_modality_t modality);

/* Check sanctions / PEPs */
int32_t identity_check_compliance(identity_device_t *dev);

/* Cross-system identity resolution (Eastern/Western/Global South) */
int32_t identity_resolve_cross_system(identity_registry_t *reg,
                                       uint64_t id_hash,
                                       uint16_t from_country,
                                       uint16_t to_country);

/* Country code lookup */
const char *identity_country_name(uint16_t country_code);
identity_system_t identity_country_system(uint16_t country_code);

/* System name */
const char *identity_system_name(identity_system_t s);
const char *identity_doc_type_name(identity_doc_type_t d);

/* ISO 3166-1 numeric → alpha-3 */
const char *identity_iso_alpha3(uint16_t country_code);

#endif /* IDENTITY_H */

/* identity.c — Universal National Identity System implementation
 *
 * Hardware-as-code: each identity is a virtual device with registers,
 * DMA buffers, IRQ flags, and M⁵ coverage verification.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "identity.h"

static const char *sys_names[] = {
    "North America", "European Union", "United Kingdom",
    "East Asia", "South Asia", "Southeast Asia", "Central Asia",
    "Middle East", "Africa", "Latin America", "Oceania",
    "Russia/Belarus", "Nordic/Baltic", "Observer States"
};

static const char *doc_names[] = {
    "National ID", "Passport", "Driver's License", "SSN",
    "Tax ID", "Biometric", "Digital Certificate", "Refugee ID",
    "Diplomatic", "Tribal ID"
};

/* ISO 3166-1 numeric → alpha-3 and system classification */
typedef struct {
    uint16_t code;
    const char *alpha3;
    const char *name;
    identity_system_t system;
} country_entry_t;

static const country_entry_t countries[] = {
    /* North America */
    {840, "USA", "United States",       ID_SYS_UN_NORTH_AMERICA},
    {124, "CAN", "Canada",              ID_SYS_UN_NORTH_AMERICA},
    {484, "MEX", "Mexico",              ID_SYS_UN_NORTH_AMERICA},
    /* EU */
    {276, "DEU", "Germany",             ID_SYS_EU},
    {250, "FRA", "France",              ID_SYS_EU},
    {380, "ITA", "Italy",               ID_SYS_EU},
    {724, "ESP", "Spain",               ID_SYS_EU},
    {528, "NLD", "Netherlands",         ID_SYS_EU},
    {40, "AUT", "Austria",             ID_SYS_EU},
    {56, "BEL", "Belgium",             ID_SYS_EU},
    {208, "DNK", "Denmark",             ID_SYS_EU},
    {246, "FIN", "Finland",             ID_SYS_EU},
    {300, "GRC", "Greece",              ID_SYS_EU},
    {372, "IRL", "Ireland",             ID_SYS_EU},
    {440, "LTU", "Lithuania",           ID_SYS_EU},
    {428, "LVA", "Latvia",              ID_SYS_EU},
    {442, "LUX", "Luxembourg",          ID_SYS_EU},
    {616, "POL", "Poland",              ID_SYS_EU},
    {620, "PRT", "Portugal",            ID_SYS_EU},
    {203, "CZE", "Czech Republic",      ID_SYS_EU},
    {703, "SVK", "Slovakia",            ID_SYS_EU},
    {705, "SVN", "Slovenia",            ID_SYS_EU},
    {191, "HRV", "Croatia",             ID_SYS_EU},
    {100, "BGR", "Bulgaria",            ID_SYS_EU},
    {196, "CYP", "Cyprus",              ID_SYS_EU},
    {348, "HUN", "Hungary",             ID_SYS_EU},
    {642, "ROU", "Romania",             ID_SYS_EU},
    {752, "SWE", "Sweden",              ID_SYS_EU},
    /* UK */
    {826, "GBR", "United Kingdom",      ID_SYS_UK},
    /* East Asia */
    {156, "CHN", "China",               ID_SYS_EAST_ASIA},
    {392, "JPN", "Japan",               ID_SYS_EAST_ASIA},
    {410, "KOR", "South Korea",         ID_SYS_EAST_ASIA},
    {158, "TWN", "Taiwan",              ID_SYS_EAST_ASIA},
    {496, "MNG", "Mongolia",            ID_SYS_EAST_ASIA},
    {410, "PRK", "North Korea",         ID_SYS_EAST_ASIA},
    /* South Asia */
    {356, "IND", "India",               ID_SYS_SOUTH_ASIA},
    {586, "PAK", "Pakistan",            ID_SYS_SOUTH_ASIA},
    {50, "BGD", "Bangladesh",          ID_SYS_SOUTH_ASIA},
    {144, "LKA", "Sri Lanka",           ID_SYS_SOUTH_ASIA},
    {524, "NPL", "Nepal",               ID_SYS_SOUTH_ASIA},
    {104, "MMR", "Myanmar",             ID_SYS_SOUTH_ASIA},
    /* Southeast Asia */
    {360, "IDN", "Indonesia",           ID_SYS_SOUTHEAST_ASIA},
    {764, "THA", "Thailand",            ID_SYS_SOUTHEAST_ASIA},
    {702, "SGP", "Singapore",           ID_SYS_SOUTHEAST_ASIA},
    {458, "MYS", "Malaysia",            ID_SYS_SOUTHEAST_ASIA},
    {608, "PHL", "Philippines",         ID_SYS_SOUTHEAST_ASIA},
    {704, "VNM", "Vietnam",             ID_SYS_SOUTHEAST_ASIA},
    {116, "KHM", "Cambodia",            ID_SYS_SOUTHEAST_ASIA},
    {418, "LAO", "Laos",                ID_SYS_SOUTHEAST_ASIA},
    {626, "TLS", "Timor-Leste",         ID_SYS_SOUTHEAST_ASIA},
    {548, "VUT", "Vanuatu",             ID_SYS_SOUTHEAST_ASIA},
    /* Central Asia */
    {398, "KAZ", "Kazakhstan",          ID_SYS_CENTRAL_ASIA},
    {417, "KGZ", "Kyrgyzstan",          ID_SYS_CENTRAL_ASIA},
    {762, "TJK", "Tajikistan",          ID_SYS_CENTRAL_ASIA},
    {795, "TKM", "Turkmenistan",        ID_SYS_CENTRAL_ASIA},
    {860, "UZB", "Uzbekistan",          ID_SYS_CENTRAL_ASIA},
    {51, "ARM", "Armenia",             ID_SYS_CENTRAL_ASIA},
    {31, "AZE", "Azerbaijan",          ID_SYS_CENTRAL_ASIA},
    {51, "GEO", "Georgia",             ID_SYS_CENTRAL_ASIA},
    /* Middle East */
    {682, "SAU", "Saudi Arabia",        ID_SYS_MIDDLE_EAST},
    {784, "ARE", "UAE",                 ID_SYS_MIDDLE_EAST},
    {634, "QAT", "Qatar",               ID_SYS_MIDDLE_EAST},
    {48,  "BHR", "Bahrain",             ID_SYS_MIDDLE_EAST},
    {512, "OMN", "Oman",                ID_SYS_MIDDLE_EAST},
    {422, "LBN", "Lebanon",             ID_SYS_MIDDLE_EAST},
    {400, "JOR", "Jordan",              ID_SYS_MIDDLE_EAST},
    {446, "ISR", "Israel",              ID_SYS_MIDDLE_EAST},
    {275, "PSE", "Palestine",           ID_SYS_MIDDLE_EAST},
    {364, "IRN", "Iran",                ID_SYS_MIDDLE_EAST},
    {368, "IRQ", "Iraq",                ID_SYS_MIDDLE_EAST},
    {760, "SYR", "Syria",               ID_SYS_MIDDLE_EAST},
    {792, "TUR", "Turkey",              ID_SYS_MIDDLE_EAST},
    {624, "YEM", "Yemen",               ID_SYS_MIDDLE_EAST},
    {296, "KWT", "Kuwait",              ID_SYS_MIDDLE_EAST},
    {478, "EGY", "Egypt",               ID_SYS_MIDDLE_EAST},
    /* Africa */
    {566, "NGA", "Nigeria",             ID_SYS_AFRICA},
    {710, "ZAF", "South Africa",        ID_SYS_AFRICA},
    {818, "EGY", "Egypt",               ID_SYS_AFRICA},
    {231, "ETH", "Ethiopia",            ID_SYS_AFRICA},
    {404, "KEN", "Kenya",               ID_SYS_AFRICA},
    {454, "MWI", "Malawi",              ID_SYS_AFRICA},
    {686, "SEN", "Senegal",             ID_SYS_AFRICA},
    {466, "MNG", "Mali",                ID_SYS_AFRICA},
    {480, "MRT", "Mauritania",          ID_SYS_AFRICA},
    {516, "NAM", "Namibia",             ID_SYS_AFRICA},
    {562, "NER", "Niger",               ID_SYS_AFRICA},
    {558, "UGA", "Uganda",              ID_SYS_AFRICA},
    {894, "ZMB", "Zambia",              ID_SYS_AFRICA},
    {716, "ZWE", "Zimbabwe",            ID_SYS_AFRICA},
    {24, "AGO", "Angola",              ID_SYS_AFRICA},
    {72, "BWA", "Botswana",            ID_SYS_AFRICA},
    {854, "BFA", "Burkina Faso",        ID_SYS_AFRICA},
    {108, "BFA", "Burundi",             ID_SYS_AFRICA},
    {120, "CMR", "Cameroon",            ID_SYS_AFRICA},
    {140, "CAF", "Central African Rep", ID_SYS_AFRICA},
    {148, "TCD", "Chad",                ID_SYS_AFRICA},
    {174, "COM", "Comoros",             ID_SYS_AFRICA},
    {178, "COG", "Congo",               ID_SYS_AFRICA},
    {180, "COD", "DR Congo",            ID_SYS_AFRICA},
    {384, "CIV", "Cote d'Ivoire",       ID_SYS_AFRICA},
    {262, "DJI", "Djibouti",            ID_SYS_AFRICA},
    {266, "GAB", "Gabon",               ID_SYS_AFRICA},
    {270, "GMB", "Gambia",              ID_SYS_AFRICA},
    {288, "GHA", "Ghana",               ID_SYS_AFRICA},
    {324, "GIN", "Guinea",              ID_SYS_AFRICA},
    {624, "GNB", "Guinea-Bissau",       ID_SYS_AFRICA},
    {232, "ERI", "Eritrea",             ID_SYS_AFRICA},
    {732, "ESH", "Western Sahara",      ID_SYS_AFRICA},
    {266, "LSO", "Lesotho",             ID_SYS_AFRICA},
    {430, "LBR", "Liberia",             ID_SYS_AFRICA},
    {434, "LBY", "Libya",               ID_SYS_AFRICA},
    {450, "MDG", "Madagascar",          ID_SYS_AFRICA},
    {454, "MWI", "Malawi",              ID_SYS_AFRICA},
    {478, "MLI", "Mali",                ID_SYS_AFRICA},
    {478, "MRT", "Mauritania",          ID_SYS_AFRICA},
    {478, "MUS", "Mauritius",           ID_SYS_AFRICA},
    {504, "MAR", "Morocco",             ID_SYS_AFRICA},
    {498, "MDA", "Moldova",             ID_SYS_AFRICA},
    {508, "MOZ", "Mozambique",          ID_SYS_AFRICA},
    {516, "NAM", "Namibia",             ID_SYS_AFRICA},
    {562, "NER", "Niger",               ID_SYS_AFRICA},
    {566, "NGA", "Nigeria",             ID_SYS_AFRICA},
    {646, "RWA", "Rwanda",              ID_SYS_AFRICA},
    {678, "STP", "Sao Tome",            ID_SYS_AFRICA},
    {686, "SEN", "Senegal",             ID_SYS_AFRICA},
    {690, "SYC", "Seychelles",          ID_SYS_AFRICA},
    {694, "SLE", "Sierra Leone",        ID_SYS_AFRICA},
    {706, "SOM", "Somalia",             ID_SYS_AFRICA},
    {728, "SSD", "South Sudan",         ID_SYS_AFRICA},
    {729, "SDN", "Sudan",               ID_SYS_AFRICA},
    {834, "TZA", "Tanzania",            ID_SYS_AFRICA},
    {768, "TGO", "Togo",                ID_SYS_AFRICA},
    {788, "TUN", "Tunisia",             ID_SYS_AFRICA},
    {800, "UGA", "Uganda",              ID_SYS_AFRICA},
    /* Latin America */
    {76, "BRA", "Brazil",              ID_SYS_LATIN_AMERICA},
    {32, "ARG", "Argentina",           ID_SYS_LATIN_AMERICA},
    {152, "CHL", "Chile",               ID_SYS_LATIN_AMERICA},
    {170, "COL", "Colombia",            ID_SYS_LATIN_AMERICA},
    {192, "CUB", "Cuba",                ID_SYS_LATIN_AMERICA},
    {214, "DOM", "Dominican Rep",       ID_SYS_LATIN_AMERICA},
    {218, "ECU", "Ecuador",             ID_SYS_LATIN_AMERICA},
    {222, "SLV", "El Salvador",         ID_SYS_LATIN_AMERICA},
    {320, "GTM", "Guatemala",           ID_SYS_LATIN_AMERICA},
    {340, "HND", "Honduras",            ID_SYS_LATIN_AMERICA},
    {604, "PER", "Peru",                ID_SYS_LATIN_AMERICA},
    {591, "PAN", "Panama",              ID_SYS_LATIN_AMERICA},
    {600, "PRY", "Paraguay",            ID_SYS_LATIN_AMERICA},
    {858, "URY", "Uruguay",             ID_SYS_LATIN_AMERICA},
    {862, "VEN", "Venezuela",           ID_SYS_LATIN_AMERICA},
    {188, "BOL", "Bolivia",             ID_SYS_LATIN_AMERICA},
    {188, "CRI", "Costa Rica",          ID_SYS_LATIN_AMERICA},
    {188, "JAM", "Jamaica",             ID_SYS_LATIN_AMERICA},
    {188, "HTI", "Haiti",               ID_SYS_LATIN_AMERICA},
    {188, "TTO", "Trinidad & Tobago",   ID_SYS_LATIN_AMERICA},
    {188, "BHS", "Bahamas",             ID_SYS_LATIN_AMERICA},
    {188, "BRB", "Barbados",            ID_SYS_LATIN_AMERICA},
    /* Oceania */
    {36, "AUS", "Australia",           ID_SYS_OCEANIA},
    {554, "NZL", "New Zealand",         ID_SYS_OCEANIA},
    {242, "FJI", "Fiji",                ID_SYS_OCEANIA},
    {598, "PNG", "Papua New Guinea",    ID_SYS_OCEANIA},
    {882, "WSM", "Samoa",               ID_SYS_OCEANIA},
    {772, "TON", "Tonga",               ID_SYS_OCEANIA},
    {776, "KIR", "Kiribati",            ID_SYS_OCEANIA},
    {585, "MHL", "Marshall Islands",    ID_SYS_OCEANIA},
    {584, "FSM", "Micronesia",          ID_SYS_OCEANIA},
    {520, "NRU", "Nauru",               ID_SYS_OCEANIA},
    {570, "PLW", "Palau",               ID_SYS_OCEANIA},
    {90,  "SLB", "Solomon Islands",     ID_SYS_OCEANIA},
    {748, "TUV", "Tuvalu",              ID_SYS_OCEANIA},
    /* Russia/Belarus/CIS */
    {643, "RUS", "Russia",              ID_SYS_RUSSIA_BELARUS},
    {112, "BLR", "Belarus",             ID_SYS_RUSSIA_BELARUS},
    {804, "UKR", "Ukraine",             ID_SYS_RUSSIA_BELARUS},
    {498, "MDA", "Moldova",             ID_SYS_RUSSIA_BELARUS},
    /* Nordic/Baltic */
    {578, "NOR", "Norway",              ID_SYS_NORDIC},
    {752, "SWE", "Sweden",              ID_SYS_NORDIC},
    {246, "FIN", "Finland",             ID_SYS_NORDIC},
    {208, "DNK", "Denmark",             ID_SYS_NORDIC},
    {352, "ISL", "Iceland",             ID_SYS_NORDIC},
    {233, "EST", "Estonia",             ID_SYS_NORDIC},
    /* Observer */
    {336, "VAT", "Vatican City",        ID_SYS_OBSERVER},
    {275, "PSE", "Palestine",           ID_SYS_OBSERVER},
};

static const int num_countries = sizeof(countries) / sizeof(countries[0]);

const char *identity_country_name(uint16_t code) {
    int i;
    for (i = 0; i < num_countries; i++) {
        if (countries[i].code == code) return countries[i].name;
    }
    return "Unknown";
}

identity_system_t identity_country_system(uint16_t code) {
    int i;
    for (i = 0; i < num_countries; i++) {
        if (countries[i].code == code) return countries[i].system;
    }
    return ID_SYS_MAX;
}

const char *identity_iso_alpha3(uint16_t code) {
    int i;
    for (i = 0; i < num_countries; i++) {
        if (countries[i].code == code) return countries[i].alpha3;
    }
    return "XXX";
}

const char *identity_system_name(identity_system_t s) {
    if (s < ID_SYS_MAX) return sys_names[s];
    return "Unknown";
}

const char *identity_doc_type_name(identity_doc_type_t d) {
    if (d < ID_DOC_MAX) return doc_names[d];
    return "Unknown";
}

void identity_registry_init(identity_registry_t *reg) {
    reg->num_identities = 0;
    reg->total_verified = 0;
    reg->total_biometric = 0;
    reg->total_sanctioned = 0;
    reg->system_coverage = SR_ZERO;
}

uint32_t identity_register(identity_registry_t *reg,
                            uint16_t country_code,
                            identity_system_t system,
                            identity_doc_type_t doc_type,
                            uint64_t national_id_hash,
                            uint8_t biometric_flags,
                            const char *holder_name,
                            const char *country_name,
                            const char *iso_alpha3) {
    if (reg->num_identities >= 1024) return 0xFFFFFFFF;
    identity_device_t *dev = &reg->identities[reg->num_identities];
    
    /* Power-on: initialize UCI */
    dev->uci.country_code = country_code;
    dev->uci.system = system;
    dev->uci.national_id_hash = national_id_hash;
    dev->uci.doc_type = doc_type;
    dev->uci.biometric_flags = biometric_flags;
    
    /* M⁵ coordinates */
    dev->uci.m5.omega = reg->num_identities;
    dev->uci.m5.r = SR_FROM_INT(country_code);
    dev->uci.m5.ell = (biometric_flags > 0) ? SR_ONE : SR_FROM_FLOAT(0.5);
    dev->uci.m5.phi = SR_ZERO;
    dev->uci.m5.chi = 0;
    
    /* Coverage */
    surplus_real_t product = SR_MUL(dev->uci.m5.r, dev->uci.m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    dev->uci.coverage_ratio = SR_DIV(product, floor);
    dev->uci.coverage_verified = SR_CMP(product, floor) >= 0;
    
    /* Verification level */
    dev->uci.verification_level = 1; /* Basic */
    if (biometric_flags > 0) {
        dev->uci.verification_level = 3;
        reg->total_biometric++;
    }
    
    dev->uci.active = true;
    dev->uci.sanctioned = false;
    dev->uci.peps = false;
    dev->uci.issued_tick = 0;
    dev->uci.expires_tick = 0;
    dev->uci.issuing_authority = country_code;
    dev->uci.attesting_authority = 0;
    
    /* Initialize registers */
    dev->reg_status = 0x01; /* Active */
    dev->reg_country = country_code;
    dev->reg_id_hash = national_id_hash;
    dev->reg_verification = dev->uci.verification_level;
    
    /* Clear DMA */
    dev->dma_head = 0;
    dev->dma_tail = 0;
    uint32_t i;
    for (i = 0; i < 8; i++) dev->dma_attestation[i] = 0;
    
    /* Clear IRQs */
    dev->irq_verification_requested = false;
    dev->irq_biometric_match = false;
    dev->irq_coverage_breach = !dev->uci.coverage_verified;
    dev->irq_sanction_hit = false;
    
    /* Copy strings */
    int j;
    for (j = 0; j < 63 && holder_name && holder_name[j]; j++)
        dev->holder_name[j] = holder_name[j];
    dev->holder_name[j] = 0;
    
    for (j = 0; j < 31 && country_name && country_name[j]; j++)
        dev->country_name[j] = country_name[j];
    dev->country_name[j] = 0;
    
    for (j = 0; j < 3 && iso_alpha3 && iso_alpha3[j]; j++)
        dev->iso_alpha3[j] = iso_alpha3[j];
    dev->iso_alpha3[j] = 0;
    
    reg->num_identities++;
    if (dev->uci.verification_level >= 2) reg->total_verified++;
    
    return reg->num_identities - 1;
}

bool identity_verify(identity_device_t *dev) {
    dev->irq_verification_requested = true;
    
    /* Check M⁵ coverage */
    surplus_real_t product = SR_MUL(dev->uci.m5.r, dev->uci.m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    dev->uci.coverage_ratio = SR_DIV(product, floor);
    dev->uci.coverage_verified = SR_CMP(product, floor) >= 0;
    
    if (!dev->uci.coverage_verified) {
        dev->irq_coverage_breach = true;
        return false;
    }
    
    /* Check active status */
    if (!dev->uci.active) return false;
    
    /* Check sanctions */
    if (dev->uci.sanctioned) {
        dev->irq_sanction_hit = true;
        return false;
    }
    
    /* Elevate verification level */
    if (dev->uci.verification_level < 2) {
        dev->uci.verification_level = 2;
        dev->reg_verification = 2;
    }
    
    /* Push to DMA attestation chain */
    dev->dma_attestation[dev->dma_tail] = dev->uci.verification_level;
    dev->dma_tail = (dev->dma_tail + 1) % 8;
    
    return true;
}

int32_t identity_attest_biometric(identity_device_t *dev, biometric_modality_t modality) {
    if (!(dev->uci.biometric_flags & modality)) return -1;
    
    /* Simulate biometric match */
    dev->irq_biometric_match = true;
    
    /* Elevate to biometric verification */
    if (dev->uci.verification_level < 3) {
        dev->uci.verification_level = 3;
        dev->reg_verification = 3;
    }
    
    /* Update M⁵ logical axis */
    dev->uci.m5.ell = SR_ONE;
    
    /* Re-check coverage */
    identity_verify(dev);
    
    return 0;
}

int32_t identity_check_compliance(identity_device_t *dev) {
    /* Check sanctions list */
    if (dev->uci.sanctioned) {
        dev->irq_sanction_hit = true;
        return -1;
    }
    
    /* Check PEPs */
    if (dev->uci.peps) {
        /* Enhanced due diligence required */
        if (dev->uci.verification_level < 4) return 1; /* EDD needed */
    }
    
    return 0;
}

int32_t identity_resolve_cross_system(identity_registry_t *reg,
                                       uint64_t id_hash,
                                       uint16_t from_country,
                                       uint16_t to_country) {
    /* Find identity by hash */
    uint32_t i;
    for (i = 0; i < reg->num_identities; i++) {
        if (reg->identities[i].uci.national_id_hash == id_hash) {
            /* Cross-system resolution: verify in target system */
            identity_system_t target_sys = identity_country_system(to_country);
            if (target_sys >= ID_SYS_MAX) return -1;
            
            /* Create cross-system attestation */
            reg->identities[i].dma_attestation[reg->identities[i].dma_tail] =
                (to_country << 16) | from_country;
            reg->identities[i].dma_tail = (reg->identities[i].dma_tail + 1) % 8;
            
            return 0;
        }
    }
    return -1;
}

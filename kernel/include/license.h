/* license.h — Apache-2.0 license header
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef LICENSE_H
#define LICENSE_H

#include <stdbool.h>

/* Apache-2.0 License constants */
#define LICENSE_VERSION "Apache-2.0"
#define LICENSE_FULL_NAME "Apache License 2.0"
#define LICENSE_SPDX "Apache-2.0"
#define LICENSE_URL "http://www.apache.org/licenses/LICENSE-2.0"

/* License properties (for programmatic access) */
#define LICENSE_NON_EXCLUSIVE    1
#define LICENSE_IRREVOCABLE      1
#define LICENSE_WORLDWIDE        1
#define LICENSE_SELF_ENFORCING   1

/* Attribution required by Apache-2.0 */
#define LICENSE_ATTRIBUTION_AUTHOR  "Michael Laurence Curzi"
#define LICENSE_ATTRIBUTION_ENTITY  "36N9 Genetics, LLC"
#define LICENSE_ATTRIBUTION_EMAIL   "admin@zedec.ai"

/* License ID */
typedef enum {
    LICENSE_APACHE_20 = 0,
} license_id_t;

void license_print(void);
void license_print_all(void);
const char *license_get_version(void);
const char *license_get_issuer(void);
const char *license_get_name(license_id_t id);
const char *license_get_full_text(license_id_t id);
const char *license_get_attribution(void);

/* The SPDX expression */
const char *license_spdx_bundle(void);

/* Apache-2.0 is permissive, not copyleft */
bool license_is_share_alike(license_id_t id);

/* Apache-2.0 derivatives may be relicensed */
bool license_travels_together(license_id_t id);

/* Precedence rank (single license) */
int license_precedence_rank(license_id_t id);

#endif /* LICENSE_H */

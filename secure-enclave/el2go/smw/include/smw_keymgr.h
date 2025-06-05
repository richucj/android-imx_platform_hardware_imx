/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright 2020-2025 NXP
 */

#ifndef __SMW_KEYMGR_H__
#define __SMW_KEYMGR_H__

#include <stdbool.h>

#include "smw/attr.h"
#include "smw/names.h"
#include "smw_status.h"

/*
 * Define the NXP and NXP's EdgeLock 2GO key/data storage identifier
 * bit[23]    = PSA Vendor bit
 * bit[22]    = Vendor NXP identifier
 * bit[21]    = NXP's EdgeLock 2GO identifier
 * bit[20:16] = Reserved must be 0
 * bit[15]    = Key/Data object (Key=0/Data=1)
 * bit[14:8]  = NXP's enclave storage ID
 * bit[7:0]   = NPX's enclave identifier
 */
#define NXP_KEY_DATA_STORAGE_ID_MASK (BIT(23) | BIT(22))
#define NXP_EL2GO_STORAGE_ID_MASK (NXP_KEY_DATA_STORAGE_ID_MASK | BIT(21))
#define NXP_EL2GO_KEY NXP_EL2GO_STORAGE_ID_MASK
#define NXP_EL2GO_DATA (NXP_EL2GO_STORAGE_ID_MASK | BIT(15))
#define NXP_IS_EL2GO_OBJECT(val) ((val) & (NXP_EL2GO_STORAGE_ID_MASK | BIT(15)))
#define NXP_IS_EL2GO_KEY(val) (NXP_IS_EL2GO_OBJECT(val) == NXP_EL2GO_KEY)
#define NXP_IS_EL2GO_DATA(val) (NXP_IS_EL2GO_OBJECT(val) == NXP_EL2GO_DATA)

#endif /* __SMW_KEYMGR_H__ */

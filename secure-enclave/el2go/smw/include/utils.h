/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2025 NXP
 */

#ifndef __UTILS_H__
#define __UTILS_H__

#include <psa/crypto.h>

#define KEY_STORE_ID (0x1111)
#define KEY_STORE_NONCE (1000)
#define ELE_UUID_LENGTH (16)
#define MU_CHANNEL_ELE MU_CHANNEL_PLAT_HSM

psa_status_t eleErrorToPSAStatus(ErrorType error);
ErrorType openDeviceSession(EleOperation *ops);
void closeKeystoreAndMgt(EleOperation *ops, uint32_t keyStoreHandler, uint32_t keyMgtHandle);
ErrorType openKeystoreAndMgt(EleOperation *ops, uint32_t *keyStoreHandler, uint32_t *keyMgtHandle);
void closeKeystoreAndSign(EleOperation *ops, uint32_t keyStoreHandler, uint32_t signGenHandle);
ErrorType openKeystoreAndSign(EleOperation *ops, uint32_t *keyStoreHandler,
                              uint32_t *signGenHandle);
void closeKeystoreAndDataStore(EleOperation *ops, uint32_t keyStoreHandler,
                               uint32_t dataStoreHandler);
ErrorType openKeystoreAndDataStore(EleOperation *ops, uint32_t *keyStoreHandler,
                                   uint32_t *dataStoreHandler);

#endif

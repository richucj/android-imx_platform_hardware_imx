/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2025 NXP
 */

#define LOG_TAG "smw"

#include <EleMessage.h>
#include <EleOperation.h>
#include <utils.h>

psa_status_t eleErrorToPSAStatus(ErrorType error) {
    if (error == ELE_NO_ERROR)
        return PSA_SUCCESS;
    else if (error == ELE_INVALID_ARGS || error == ELE_COMMAND_INVALID_ADDRESS ||
             error == ELE_COMMAND_INVALID_PARAM)
        return PSA_ERROR_INVALID_ARGUMENT;
    else if (error == ELE_COMMAND_OUTPUT_TOO_SMALL)
        return PSA_ERROR_BUFFER_TOO_SMALL;
    else if (error == ELE_COMMAND_INVALID_ID)
        return PSA_ERROR_INVALID_HANDLE;
    else if (error == ELE_COMMAND_NVM_ERROR)
        return PSA_ERROR_STORAGE_FAILURE;
    else if (error == ELE_COMMAND_ERR_DELETE_PERMANENT_KEY)
        return PSA_ERROR_NOT_PERMITTED;
    else
        return PSA_ERROR_GENERIC_ERROR;
}

ErrorType openDeviceSession(EleOperation *ops) {
    ErrorType ret;

    ret = ops->eleOpenDeviceNode();
    if (ret != ELE_NO_ERROR) {
        ALOGE("Failed to open ELE device nodes!");
        return ret;
    }

    ret = ops->eleOpenSession();
    if (ret != ELE_NO_ERROR) {
        ALOGE("Failed to open ELE session!");
    }

    return ret;
}

void closeKeystoreAndMgt(EleOperation *ops, uint32_t keyStoreHandler, uint32_t keyMgtHandle) {
    if (ops == NULL)
        return;

    if (keyMgtHandle != 0)
        ops->eleCloseKeyManagement(keyMgtHandle);
    if (keyStoreHandler != 0)
        ops->eleCloseKeyStore(keyStoreHandler);

    ops->eleCloseSession();
}

ErrorType openKeystoreAndMgt(EleOperation *ops, uint32_t *keyStoreHandler, uint32_t *keyMgtHandle) {
    ErrorType ret;

    do {
        ret = openDeviceSession(ops);
        if (ret != ELE_NO_ERROR)
            break;

        ret = ops->eleOpenKeyStore(KEY_STORE_ID, KEY_STORE_NONCE,
                                   KEY_STORE_OPERATION_CREATE | OPERATION_SYNC, keyStoreHandler);
        if (ret == ELE_COMMAND_KEYSTORE_CONFLICT) {
            ALOGE("Keystore already existed, loading it...");
            ret = ops->eleOpenKeyStore(KEY_STORE_ID, KEY_STORE_NONCE, KEY_STORE_OPERATION_LOAD,
                                       keyStoreHandler);
            if (ret != ELE_NO_ERROR) {
                ALOGE("Keystore load failed!");
                break;
            }
        } else if (ret != ELE_NO_ERROR) {
            ALOGE("Keystore open failed!");
            break;
        }

        ret = ops->eleOpenKeyManagement(*keyStoreHandler, keyMgtHandle);
        if (ret != ELE_NO_ERROR) {
            ALOGE("key management open failed!");
            break;
        }
    } while (0);

    if (ret != ELE_NO_ERROR) {
        // clean up
        closeKeystoreAndMgt(ops, *keyStoreHandler, *keyMgtHandle);
    }
    return ret;
}

void closeKeystoreAndSign(EleOperation *ops, uint32_t keyStoreHandler, uint32_t signGenHandle) {
    if (ops == NULL)
        return;

    if (signGenHandle != 0)
        ops->eleSignGenerateClose(signGenHandle);
    if (keyStoreHandler != 0)
        ops->eleCloseKeyStore(keyStoreHandler);

    ops->eleCloseSession();
}

ErrorType openKeystoreAndSign(EleOperation *ops, uint32_t *keyStoreHandler,
                              uint32_t *signGenHandle) {
    ErrorType ret;

    do {
        ret = openDeviceSession(ops);
        if (ret != ELE_NO_ERROR)
            break;

        ret = ops->eleOpenKeyStore(KEY_STORE_ID, KEY_STORE_NONCE,
                                   KEY_STORE_OPERATION_CREATE | OPERATION_SYNC, keyStoreHandler);
        if (ret == ELE_COMMAND_KEYSTORE_CONFLICT) {
            ALOGE("Keystore already existed, loading it...");
            ret = ops->eleOpenKeyStore(KEY_STORE_ID, KEY_STORE_NONCE, KEY_STORE_OPERATION_LOAD,
                                       keyStoreHandler);
            if (ret != ELE_NO_ERROR) {
                ALOGE("Keystore load failed!");
                break;
            }
        } else if (ret != ELE_NO_ERROR) {
            ALOGE("Keystore open failed!");
            break;
        }

        ret = ops->eleSignGenerateOpen(*keyStoreHandler, signGenHandle);
        if (ret != ELE_NO_ERROR) {
            ALOGE("Sign generate session open failed!");
            break;
        }
    } while (0);

    if (ret != ELE_NO_ERROR) {
        // clean up
        closeKeystoreAndSign(ops, *keyStoreHandler, *signGenHandle);
    }
    return ret;
}

void closeKeystoreAndDataStore(EleOperation *ops, uint32_t keyStoreHandler,
                               uint32_t dataStoreHandler) {
    if (ops == NULL)
        return;

    if (dataStoreHandler != 0)
        ops->eleDataStorageClose(dataStoreHandler);
    if (keyStoreHandler != 0)
        ops->eleCloseKeyStore(keyStoreHandler);

    ops->eleCloseSession();
}

ErrorType openKeystoreAndDataStore(EleOperation *ops, uint32_t *keyStoreHandler,
                                   uint32_t *dataStoreHandler) {
    ErrorType ret;

    do {
        ret = openDeviceSession(ops);
        if (ret != ELE_NO_ERROR)
            break;

        ret = ops->eleOpenKeyStore(KEY_STORE_ID, KEY_STORE_NONCE,
                                   KEY_STORE_OPERATION_CREATE | OPERATION_SYNC, keyStoreHandler);
        if (ret == ELE_COMMAND_KEYSTORE_CONFLICT) {
            ALOGE("Keystore already existed, loading it...");
            ret = ops->eleOpenKeyStore(KEY_STORE_ID, KEY_STORE_NONCE, KEY_STORE_OPERATION_LOAD,
                                       keyStoreHandler);
            if (ret != ELE_NO_ERROR) {
                ALOGE("Keystore load failed!");
                break;
            }
        } else if (ret != ELE_NO_ERROR) {
            ALOGE("Keystore open failed!");
            break;
        }

        ret = ops->eleDataStorageOpen(*keyStoreHandler, dataStoreHandler);
        if (ret != ELE_NO_ERROR) {
            ALOGE("Data store open failed!");
            break;
        }
    } while (0);

    if (ret != ELE_NO_ERROR) {
        // clean up
        closeKeystoreAndDataStore(ops, *keyStoreHandler, *dataStoreHandler);
    }

    return ret;
}

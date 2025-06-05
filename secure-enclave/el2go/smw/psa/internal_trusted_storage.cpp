/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2025 NXP
 */

#define LOG_TAG "smw"

#include <EleMessage.h>
#include <EleOperation.h>
#include <psa/internal_trusted_storage.h>
#include <smw_keymgr.h>
#include <utils.h>

/* Single data should not be larger than 2KB */
#define DATA_STORAGE_MAX_SIZE 2048

psa_status_t psa_its_set(psa_storage_uid_t uid, size_t data_length, const void *p_data,
                         psa_storage_create_flags_t create_flags) {
    ErrorType error = ELE_NO_ERROR;
    psa_status_t psa_status = PSA_ERROR_STORAGE_FAILURE;
    uint32_t keyStoreHandler = 0;
    uint32_t dataStorageHandler = 0;
    data_storage_attr attr = {0};
    struct psa_storage_info_t info = {0};
    uint8_t *data = nullptr;

    if (!data_length || !p_data)
        return PSA_ERROR_INVALID_ARGUMENT;

    if (data_length > DATA_STORAGE_MAX_SIZE)
        return PSA_ERROR_INSUFFICIENT_STORAGE;

    /* ELE doesn't provide permanent data storage */
    if (create_flags & PSA_STORAGE_FLAG_WRITE_ONCE)
        return PSA_ERROR_NOT_SUPPORTED;

    EleOperation ops(MU_CHANNEL_ELE);
    do {
        error = openKeystoreAndDataStore(&ops, &keyStoreHandler, &dataStorageHandler);
        if (error != ELE_NO_ERROR) {
            break;
        }

        /* store data */
        attr.data_id = uid;
        attr.data_lsb_addr = (uint8_t *)p_data;
        attr.data_size = data_length;
        attr.flags = ELE_DATA_STORAGE_STORE;
        error = ops.eleDataStorage(dataStorageHandler, &attr);
        if (error != ELE_NO_ERROR) {
            break;
        }

        psa_status = PSA_SUCCESS;
    } while (0);

    closeKeystoreAndDataStore(&ops, keyStoreHandler, dataStorageHandler);
    if (psa_status != PSA_SUCCESS)
        ALOGE("Failed to store data!");

    return psa_status;
}

psa_status_t psa_its_get(psa_storage_uid_t uid, size_t data_offset, size_t data_size, void *p_data,
                         size_t *p_data_length) {
    ErrorType error = ELE_NO_ERROR;
    psa_status_t psa_status = PSA_ERROR_STORAGE_FAILURE;
    uint32_t keyStoreHandler = 0;
    uint32_t dataStorageHandler = 0;
    data_storage_attr attr = {0};
    uint8_t *data = nullptr;

    /* check input parameters */
    if (!data_size || p_data == nullptr || p_data_length == nullptr)
        return PSA_ERROR_INVALID_ARGUMENT;

    if (data_size + data_offset > DATA_STORAGE_MAX_SIZE)
        return PSA_ERROR_INVALID_ARGUMENT;

    EleOperation ops(MU_CHANNEL_ELE);
    do {
        error = openKeystoreAndDataStore(&ops, &keyStoreHandler, &dataStorageHandler);
        if (error != ELE_NO_ERROR) {
            break;
        }

        data = (uint8_t *)malloc(DATA_STORAGE_MAX_SIZE);
        if (data == nullptr) {
            ALOGE("Failed to allocate memory for data");
            break;
        }

        /* retrieve data */
        attr.data_id = uid;
        attr.data_lsb_addr = data;
        attr.data_size = DATA_STORAGE_MAX_SIZE;
        attr.flags = ELE_DATA_STORAGE_RETRIEVE;
        error = ops.eleDataStorage(dataStorageHandler, &attr);
        if (error == ELE_NO_ERROR) {
            if (data_offset + data_size > attr.data_size) {
                psa_status = PSA_ERROR_INVALID_ARGUMENT;
                break;
            }
            memcpy(p_data, data + data_offset, data_size);
            *p_data_length = data_size;
            psa_status = PSA_SUCCESS;
        } else if (error == ELE_COMMAND_INVALID_ID) {
            /* data not found */
            psa_status = PSA_ERROR_DOES_NOT_EXIST;
        } else {
            /* general error */
            psa_status = PSA_ERROR_STORAGE_FAILURE;
        }
    } while (0);

    closeKeystoreAndDataStore(&ops, keyStoreHandler, dataStorageHandler);
    if (data != nullptr) {
        memset(data, 0, DATA_STORAGE_MAX_SIZE);
        free(data);
    }

    if (psa_status != PSA_SUCCESS)
        ALOGE("Failed to retrieve data!");

    return psa_status;
}

psa_status_t psa_its_get_info(psa_storage_uid_t uid, struct psa_storage_info_t *p_info) {
    ErrorType error = ELE_NO_ERROR;
    psa_status_t psa_status = PSA_ERROR_STORAGE_FAILURE;
    uint32_t keyStoreHandler = 0;
    uint32_t dataStorageHandler = 0;
    data_storage_attr attr = {0};
    uint8_t *data = nullptr;

    /* check input parameters */
    if (p_info == nullptr)
        return PSA_ERROR_INVALID_ARGUMENT;

    EleOperation ops(MU_CHANNEL_ELE);
    do {
        error = openKeystoreAndDataStore(&ops, &keyStoreHandler, &dataStorageHandler);
        if (error != ELE_NO_ERROR) {
            break;
        }

        data = (uint8_t *)malloc(DATA_STORAGE_MAX_SIZE);
        if (data == nullptr) {
            ALOGE("Failed to allocate memory for data");
            break;
        }

        /* retrieve data */
        attr.data_id = uid;
        attr.data_lsb_addr = data;
        attr.data_size = DATA_STORAGE_MAX_SIZE;
        attr.flags = ELE_DATA_STORAGE_RETRIEVE;
        error = ops.eleDataStorage(dataStorageHandler, &attr);
        if (error == ELE_NO_ERROR) {
            p_info->size = attr.data_size;
            p_info->flags = PSA_STORAGE_FLAG_NONE;
            psa_status = PSA_SUCCESS;
        } else if (error == ELE_COMMAND_INVALID_ID) {
            /* data not found */
            psa_status = PSA_ERROR_DOES_NOT_EXIST;
        } else {
            /* general error */
            psa_status = PSA_ERROR_STORAGE_FAILURE;
        }
    } while (0);

    closeKeystoreAndDataStore(&ops, keyStoreHandler, dataStorageHandler);
    if (data != nullptr) {
        memset(data, 0, DATA_STORAGE_MAX_SIZE);
        free(data);
    }

    if (psa_status != PSA_SUCCESS)
        ALOGE("Failed to get data info!");

    return psa_status;
}

psa_status_t psa_its_remove(psa_storage_uid_t uid) {
    ErrorType error = ELE_NO_ERROR;
    uint32_t keyStoreHandler = 0;
    uint32_t dataStorageHandler = 0;
    struct psa_storage_info_t info = {0};
    psa_status_t psa_status = PSA_ERROR_STORAGE_FAILURE;

    /* check if data exist */
    psa_status = psa_its_get_info(uid, &info);
    if (psa_status != PSA_SUCCESS)
        return psa_status;

    psa_status = PSA_ERROR_STORAGE_FAILURE;
    EleOperation ops(MU_CHANNEL_ELE);
    do {
        error = openKeystoreAndDataStore(&ops, &keyStoreHandler, &dataStorageHandler);
        if (error != ELE_NO_ERROR) {
            break;
        }

        /* delete data */
        error = ops.eleDataStorageDelete(dataStorageHandler, uid);
        if (error != ELE_NO_ERROR) {
            break;
        }

        psa_status = PSA_SUCCESS;
    } while (0);

    closeKeystoreAndDataStore(&ops, keyStoreHandler, dataStorageHandler);

    if (psa_status != PSA_SUCCESS)
        ALOGE("Failed to delete data!");

    return psa_status;
}

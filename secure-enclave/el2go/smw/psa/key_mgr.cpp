/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2025 NXP
 */

#define LOG_TAG "smw"

#include <EleMessage.h>
#include <EleOperation.h>
#include <psa/crypto.h>
#include <smw_keymgr.h>
#include <utils.h>

#include "psa/storage_common.h"

/*
 * OEM SRKH Key Identifier - Hardcoded value
 * This object is imported by EL2GO as RAW Type but must use the import key
 * operation.
 */
#define ELE_OEM_SRKH_KEY_ID 0x7FFF817A

void psa_reset_key_attributes(psa_key_attributes_t *attributes) {
    *attributes = PSA_KEY_ATTRIBUTES_INIT;
}

psa_status_t psa_get_key_attributes(psa_key_id_t key, psa_key_attributes_t *attributes) {
    ErrorType error = ELE_NO_ERROR;
    uint32_t keyStoreHandler = 0;
    uint32_t keyMgtHandle = 0;
    key_attribute key_attr = {0};

    if (!attributes || !key)
        return PSA_ERROR_INVALID_ARGUMENT;

    psa_reset_key_attributes(attributes);

    EleOperation ops(MU_CHANNEL_ELE);
    do {
        error = openKeystoreAndMgt(&ops, &keyStoreHandler, &keyMgtHandle);
        if (error != ELE_NO_ERROR)
            break;

        /* Get key attributes */
        memset(&key_attr, 0, sizeof(key_attribute));
        error = ops.eleGetKeyAttr(keyMgtHandle, key, &key_attr);
        if (error != ELE_NO_ERROR) {
            ALOGE("Get key attribute failed! error: 0x%x", error);
            break;
        } else {
            psa_set_key_id(attributes, key);
            psa_set_key_type(attributes, key_attr.type);
            psa_set_key_bits(attributes, key_attr.size_bits);
            psa_set_key_usage_flags(attributes, key_attr.usage);
            psa_set_key_algorithm(attributes, key_attr.permit_algo);
            attributes->lifetime = key_attr.lifetime;
        }
    } while (0);

    closeKeystoreAndMgt(&ops, keyStoreHandler, keyMgtHandle);

    return eleErrorToPSAStatus(error);
}

psa_status_t psa_export_public_key(psa_key_id_t key, uint8_t *data, size_t data_size,
                                   size_t *data_length) {
    psa_status_t psa_status = PSA_ERROR_INVALID_ARGUMENT;
    ErrorType error = ELE_NO_ERROR;
    uint32_t keyStoreHandler = 0;
    uint32_t keyMgtHandle = 0;
    key_attribute key_attr;
    pubkey_export pubkey_export_args = {0};
    uint8_t *tmp_buf = NULL;

    if (!key || !data || !data_size || !data_length)
        return PSA_ERROR_INVALID_ARGUMENT;

    EleOperation ops(MU_CHANNEL_ELE);
    do {
        error = openKeystoreAndMgt(&ops, &keyStoreHandler, &keyMgtHandle);
        if (error != ELE_NO_ERROR) {
            psa_status = eleErrorToPSAStatus(error);
            break;
        }

        /* Get key attributes */
        memset(&key_attr, 0, sizeof(key_attribute));
        error = ops.eleGetKeyAttr(keyMgtHandle, key, &key_attr);
        if (error != ELE_NO_ERROR) {
            ALOGE("Get key attribute failed! error: 0x%x", error);
            psa_status = eleErrorToPSAStatus(error);
            break;
        }

        /* TODO Only ECC key type is supported now, other key types will follow */
        if (key_attr.type != KEY_TYPE_ECC_BP_R1 && key_attr.type != KEY_TYPE_ECC_NIST) {
            ALOGE("Key type(%04x) not supported!", key_attr.type);
            psa_status = PSA_ERROR_NOT_SUPPORTED;
            break;
        }

        /* Export the public key */
        tmp_buf = (uint8_t *)malloc(data_size);
        if (!tmp_buf) {
            ALOGE("Failed to allocate memory for public key export!");
            psa_status = PSA_ERROR_INSUFFICIENT_MEMORY;
            break;
        }
        memset(tmp_buf, 0, data_size);
        memset(&pubkey_export_args, 0, sizeof(pubkey_export_args));
        pubkey_export_args.key_id = key;
        pubkey_export_args.out_key = tmp_buf;
        pubkey_export_args.out_key_size = data_size;
        error = ops.elePubkeyExport(keyStoreHandler, &pubkey_export_args);
        if (error != ELE_NO_ERROR) {
            ALOGE("Export public key failed! error: 0x%x", error);
            psa_status = eleErrorToPSAStatus(error);
            break;
        }

        /* Covert the data from ELE to be PSA compliant */
        /* Different story for RSA */
        *data = 0x04;
        memcpy(data + 1, tmp_buf, pubkey_export_args.out_key_size);
        *data_length = pubkey_export_args.out_key_size + 1;

        psa_status = PSA_SUCCESS;
    } while (0);

    closeKeystoreAndMgt(&ops, keyStoreHandler, keyMgtHandle);
    if (tmp_buf != NULL)
        free(tmp_buf);

    return psa_status;
}

psa_status_t psa_import_key(const psa_key_attributes_t *attributes, const uint8_t *data,
                            size_t data_length, psa_key_id_t *key) {
    ErrorType error = ELE_NO_ERROR;
    import_key_attr import_key_attr_args = {0};
    data_storage_attr import_data_attr = {0};
    uint32_t keyStoreHandler = 0;
    uint32_t keyMgtHandle = 0;
    uint32_t dataStorageHandler = 0;
    uint32_t location = 0;
    uint32_t assetID = 0;
    bool isKeyAsset = false;

    if (!attributes || !data || !data_length || !key)
        return PSA_ERROR_INVALID_ARGUMENT;

    location = PSA_KEY_LIFETIME_GET_LOCATION(attributes->lifetime);
    assetID = psa_get_key_id(attributes);
    if (NXP_IS_EL2GO_KEY(location) ||
        (NXP_IS_EL2GO_OBJECT(location) && assetID == ELE_OEM_SRKH_KEY_ID)) {
        isKeyAsset = true;
    } else if (NXP_IS_EL2GO_DATA(location)) {
        isKeyAsset = false;
    } else {
        ALOGE("Not EL2GO key/data asset!");
        return PSA_ERROR_NOT_SUPPORTED;
    }

    EleOperation ops(MU_CHANNEL_ELE);
    do {
        if (isKeyAsset) {
            /* Asset is a key */
            error = openKeystoreAndMgt(&ops, &keyStoreHandler, &keyMgtHandle);
            if (error != ELE_NO_ERROR)
                break;

            /* Import the key */
            memset(&import_key_attr_args, 0, sizeof(import_key_attr_args));
            import_key_attr_args.flags = IMPORT_EL2GO_OPTION | OPERATION_SYNC;
            import_key_attr_args.input_addr = (uint8_t *)data;
            import_key_attr_args.input_size = data_length;
            error = ops.eleImportKey(keyMgtHandle, &import_key_attr_args, key);
            if (error != ELE_NO_ERROR) {
                ALOGE("Import key failed with error: 0x%x", error);
                break;
            }
        } else {
            /* Asset is data */
            error = openKeystoreAndDataStore(&ops, &keyStoreHandler, &dataStorageHandler);
            if (error != ELE_NO_ERROR) {
                break;
            }
            /* data_id is not needed for EL2GO option, it will use the one in TLV blob */
            import_data_attr.data_lsb_addr = (uint8_t *)data;
            import_data_attr.data_size = data_length;
            import_data_attr.flags = ELE_DATA_STORAGE_STORE | ELE_DATA_STORAGE_EL2GO_OPTION;
            error = ops.eleDataStorage(dataStorageHandler, &import_data_attr);
            if (error != ELE_NO_ERROR) {
                ALOGE("Import data failed with error: 0x%x", error);
                break;
            }
            // TODO assign the data id back to "key"
        }
    } while (0);

    if (isKeyAsset)
        closeKeystoreAndMgt(&ops, keyStoreHandler, keyMgtHandle);
    else
        closeKeystoreAndDataStore(&ops, keyStoreHandler, dataStorageHandler);

    return eleErrorToPSAStatus(error);
}

psa_status_t psa_destroy_key(psa_key_id_t key) {
    ErrorType error = ELE_NO_ERROR;
    uint32_t keyStoreHandler = 0;
    uint32_t keyMgtHandle = 0;

    /* Do nothing for PSA_KEY_ID_NULL */
    if (key == PSA_KEY_ID_NULL)
        return PSA_SUCCESS;

    EleOperation ops(MU_CHANNEL_ELE);
    do {
        error = openKeystoreAndMgt(&ops, &keyStoreHandler, &keyMgtHandle);
        if (error != ELE_NO_ERROR)
            break;

        error = ops.eleDeleteKey(keyMgtHandle, key, OPERATION_SYNC);
        if (error != ELE_NO_ERROR) {
            ALOGE("Delete key failed with error: 0x%x", error);
            break;
        }
    } while (0);

    closeKeystoreAndMgt(&ops, keyStoreHandler, keyMgtHandle);

    return eleErrorToPSAStatus(error);
}

/*
 * Copyright 2024 NXP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "EleOperation.h"

#define KEY_STORE_ID (0x1111)
#define KEY_STORE_NONCE (1000)
#define KEY_GROUP 1
#define KEY_ID 0
#define KEY_PUBKEY_SIZE 64
#define DATA_STORAGE_ID (0x10000001)

/* ID fixed only for testing */
#define EL2GO_IMPORTED_KEYPAIR_ID (0x13470001)
#define EL2GO_IMPORTED_DATA_STORAGE_ID (0x20000001)
#define EL2GO_IMPORTED_CLAIMCODE_ID (0xF00000E0)

uint8_t hash_data[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
                         0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                         0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};

uint8_t iv_data[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                       0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

uint8_t iv_data_aead[12] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
};

uint8_t aad_data_aead[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};

uint8_t test_claimcode[] = "NzgwMTU2NTItYjRhNi00ZWQ3LWFjM2YtOGZhMzViNjAyNGVhMDAwMDAwNjk=";

int test_signature_generate_verification(EleOperation *ops, uint32_t keyStoreHandler,
                                         uint32_t keyID, uint8_t *pubKey) {
    struct verify_sign_attr sign_verify_attr;
    struct gen_sign_attr sign_generate_attr;
    uint32_t signGenHandle = 0;
    uint32_t signVerifyHandle = 0;
    uint8_t signature[128];
    ErrorType error;
    int ret = 0;

    /* open signature generation session */
    error = ops->eleSignGenerateOpen(keyStoreHandler, &signGenHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to open sign generate session!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Succeed to open sign generate session, ID:%d!", signGenHandle);
    }

    /* generate signature */
    memset(&sign_generate_attr, 0, sizeof(sign_generate_attr));
    memset(signature, 0, sizeof(signature));
    sign_generate_attr.key_id = keyID;
    sign_generate_attr.msg_lsb_addr = hash_data;
    sign_generate_attr.sign_lsb_addr = signature;
    sign_generate_attr.msg_size = sizeof(hash_data);
    sign_generate_attr.sign_size = sizeof(signature);
    sign_generate_attr.flags = ELE_SIGN_FLAGS_MESSAGE;
    sign_generate_attr.sign_scheme = PERMITTED_ALGO_ECDSA_SHA256;
    sign_generate_attr.salt_len = 0;
    error = ops->eleSignGenerate(signGenHandle, &sign_generate_attr);
    if (error != ELE_NO_ERROR) {
        ALOGE("Generate signature failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Generate signature succeed!");
        ALOGE("======== dump signature ========");
        for (int i = 0; i < sign_generate_attr.sign_size; i++) ALOGE("%02x", signature[i]);
        ALOGE("=================================");
    }

    /* open signature verification session */
    error = ops->eleSignVerifyOpen(&signVerifyHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to open sign verify session!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Succeed to open sign verify session, ID:%d!", signGenHandle);
    }

    /* verify signature */
    memset(&sign_verify_attr, 0, sizeof(sign_verify_attr));
    sign_verify_attr.key_lsb_addr = pubKey;
    sign_verify_attr.msg_lsb_addr = hash_data;
    sign_verify_attr.sign_lsb_addr = signature;
    sign_verify_attr.msg_size = sizeof(hash_data);
    sign_verify_attr.sign_size = sign_generate_attr.sign_size;
    sign_verify_attr.key_size = KEY_PUBKEY_SIZE;
    sign_verify_attr.key_security_size = KEY_SIZE_ECC_NIST_256;
    sign_verify_attr.key_type = PUBKEY_TYPE_ECC_NIST;
    sign_verify_attr.flags = ELE_SIGN_FLAGS_MESSAGE;
    sign_verify_attr.sign_scheme = PERMITTED_ALGO_ECDSA_SHA256;
    sign_verify_attr.salt_len = 0;
    error = ops->eleSignVerify(signVerifyHandle, &sign_verify_attr);
    if (error != ELE_NO_ERROR) {
        ALOGE("Verify signature failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Verify signature succeed!");
    }

    ret = 0;
exit:
    /* close signature generation session */
    error = ops->eleSignGenerateClose(signGenHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to close signature generation session!");
        ret = -1;
    } else {
        ALOGE("Succeed to close signature generation session!");
    }

    /* close verify signature session */
    error = ops->eleSignVerifyClose(signVerifyHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to close signature verify session!");
        ret = -1;
    } else {
        ALOGE("Succeed to close signature verify session!");
    }

    return ret;
}

int test_keypair_mangement_and_signature(EleOperation *ops, uint32_t keyStoreHandler,
                                         uint32_t keyMgtHandle) {
    gen_key_attribute keyAttribute;
    key_attribute keyAttr;
    pubkey_export pubkey_export_args;
    uint32_t keyID = 0;
    ErrorType error;
    uint8_t *key_buf = nullptr;
    int ret = 0;

    /* generate key pair */
    key_buf = (uint8_t *)malloc(KEY_PUBKEY_SIZE);
    if (!key_buf) {
        ALOGE("failed to allocate memory!");
        ret = -1;
        goto exit;
    }

    memset(&keyAttribute, 0, sizeof(gen_key_attribute));
    /* let the ELE choose key id automatically */
    keyID = KEY_ID;
    keyAttribute.pub_key_size = KEY_PUBKEY_SIZE;
    keyAttribute.key_group = KEY_GROUP;
    keyAttribute.type = KEY_TYPE_ECC_NIST;
    keyAttribute.size_bits = KEY_SIZE_ECC_NIST_256;
    keyAttribute.lifetime = STD_PERSISTENT;
    keyAttribute.usage = KEY_USAGE_SIGN_MSG | KEY_USAGE_VERIFY_MSG;
    keyAttribute.permit_algo = PERMITTED_ALGO_ECDSA_SHA256;
    keyAttribute.lifecycle = LIFE_CYCLE_CURRENT;
    keyAttribute.flags = OPERATION_SYNC;
    keyAttribute.pub_key_lsb_addr = key_buf;
    error = ops->eleGenerateKey(keyMgtHandle, &keyID, &keyAttribute);
    if (error != ELE_NO_ERROR) {
        ALOGE("generate key pair failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("key pair generated successfully with ID: 0x%x!", keyID);
        ALOGE("======== dump public key ========");
        for (int i = 0; i < KEY_PUBKEY_SIZE; i++) ALOGE("%02x", key_buf[i]);
        ALOGE("=================================");
    }

    /* get key attribure */
    memset(&keyAttr, 0, sizeof(key_attribute));
    error = ops->eleGetKeyAttr(keyMgtHandle, keyID, &keyAttr);
    if (error != ELE_NO_ERROR) {
        ALOGE("get key pair attribute failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("get key pair attribute successfully!");
        ALOGE("asymmetric key type: 0x%x, size: 0x%x, lifetime: 0x%x, usage: 0x%x, algo: 0x%x, lifecycle: 0x%x",
              keyAttr.type, keyAttr.size_bits, keyAttr.lifetime, keyAttr.usage, keyAttr.permit_algo,
              keyAttr.lifecycle);
    }

    /* export public key */
    memset(&pubkey_export_args, 0, sizeof(pubkey_export_args));
    memset(key_buf, 0, KEY_PUBKEY_SIZE);
    pubkey_export_args.key_id = keyID;
    pubkey_export_args.out_key = key_buf;
    pubkey_export_args.out_key_size = KEY_PUBKEY_SIZE;
    error = ops->elePubkeyExport(keyStoreHandler, &pubkey_export_args);
    if (error != ELE_NO_ERROR) {
        ALOGE("export public key failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("export public key successfully!");
        ALOGE("======== dump public key ========");
        for (int i = 0; i < KEY_PUBKEY_SIZE; i++) ALOGE("%02x", key_buf[i]);
        ALOGE("=================================");
    }

    /* test signature generation and verification */
    ret = test_signature_generate_verification(ops, keyStoreHandler, keyID, key_buf);
    if (ret < 0) {
        ALOGE("Signature generate and verify failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Signature generate and verify succeed!");
    }

    ret = 0;

exit:
    /* delete the keypair */
    error = ops->eleDeleteKey(keyMgtHandle, keyID, OPERATION_SYNC);
    if (error != ELE_NO_ERROR) {
        ALOGE("delete key pair failed!");
        ret = -1;
    } else {
        ALOGE("delete key pair successfully!");
    }

    if (key_buf)
        free(key_buf);

    return ret;
}

int test_cipher_operation(EleOperation *ops, uint32_t keyStoreHandler, uint32_t keyMgtHandle) {
    cipher_operation_attr sym_key_op_attr;
    gen_key_attribute keyAttribute;
    ErrorType error;
    uint32_t cipherHandle = 0;
    uint8_t ciphered_data[32];
    uint8_t deciphered_data[32];
    uint32_t keyID = 0;
    int ret = 0;

    /* generate symmetric key for AES */
    memset(&keyAttribute, 0, sizeof(gen_key_attribute));
    /* let the ELE choose key id automatically */
    keyID = KEY_ID;
    keyAttribute.key_group = KEY_GROUP;
    keyAttribute.type = KEY_TYPE_AES;
    keyAttribute.size_bits = KEY_SIZE_AES_256;
    keyAttribute.lifetime = STD_PERSISTENT;
    keyAttribute.usage = KEY_USAGE_ENCRYPT | KEY_USAGE_DECRYPT;
    keyAttribute.permit_algo = PERMITTED_ALGO_ALL_CIPHER;
    keyAttribute.lifecycle = LIFE_CYCLE_CURRENT;
    keyAttribute.flags = OPERATION_SYNC;
    error = ops->eleGenerateKey(keyMgtHandle, &keyID, &keyAttribute);
    if (error != ELE_NO_ERROR) {
        ALOGE("generate symmetric key failed!");
        return -1;
    } else {
        ALOGE("symmetric key generated successfully with ID: 0x%x!", keyID);
    }

    /* open key cipher */
    error = ops->eleOpenCipher(keyStoreHandler, &cipherHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("open key cipher failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("open key cipher succeed!");
    }

    /* encrypt with symmetric key */
    memset(&sym_key_op_attr, 0, sizeof(cipher_operation_attr));
    sym_key_op_attr.key_id = keyID;
    sym_key_op_attr.iv_addr = iv_data;
    sym_key_op_attr.iv_size = sizeof(iv_data);
    sym_key_op_attr.flags = CIPHER_FLAGS_ENCRYPT;
    sym_key_op_attr.algo = PERMITTED_ALGO_CBC_NO_PADDING;
    sym_key_op_attr.input_addr = hash_data;
    sym_key_op_attr.input_size = sizeof(hash_data);
    sym_key_op_attr.output_addr = ciphered_data;
    sym_key_op_attr.output_size = sizeof(ciphered_data);
    error = ops->eleCipherOperation(cipherHandle, &sym_key_op_attr);
    if (error != ELE_NO_ERROR) {
        ALOGE("cipher encrypt failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("cipher encrypt succeed!");
        ALOGE("======== dump encrypted data ========");
        for (int i = 0; i < sizeof(ciphered_data); i++) ALOGE("%02x", ciphered_data[i]);
        ALOGE("=================================");
    }

    /* decrypt with symmetric key */
    memset(&sym_key_op_attr, 0, sizeof(cipher_operation_attr));
    sym_key_op_attr.key_id = keyID;
    sym_key_op_attr.iv_addr = iv_data;
    sym_key_op_attr.iv_size = sizeof(iv_data);
    sym_key_op_attr.flags = CIPHER_FLAGS_DECRYPT;
    sym_key_op_attr.algo = PERMITTED_ALGO_CBC_NO_PADDING;
    sym_key_op_attr.input_addr = ciphered_data;
    sym_key_op_attr.input_size = sizeof(ciphered_data);
    sym_key_op_attr.output_addr = deciphered_data;
    sym_key_op_attr.output_size = sizeof(deciphered_data);
    error = ops->eleCipherOperation(cipherHandle, &sym_key_op_attr);
    if (error != ELE_NO_ERROR) {
        ALOGE("cipher decrypt failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("cipher decrypt succeed!");
        ALOGE("======== dump decrypted data ========");
        for (int i = 0; i < sizeof(deciphered_data); i++) ALOGE("%02x", deciphered_data[i]);
        ALOGE("=================================");

        ALOGE("======== dump original data ========");
        for (int i = 0; i < sizeof(hash_data); i++) ALOGE("%02x", hash_data[i]);
        ALOGE("=================================");

        if (memcmp(deciphered_data, hash_data, sizeof(hash_data))) {
            ALOGE("The decrypted data is not equal with the original data!");
            ret = -1;
            goto exit;
        } else {
            ALOGE("The decrypted data matches original data!");
        }
    }

    ret = 0;

exit:
    /* delete the key */
    error = ops->eleDeleteKey(keyMgtHandle, keyID, OPERATION_SYNC);
    if (error != ELE_NO_ERROR) {
        ALOGE("delete symmetric cipher key failed!");
    } else {
        ALOGE("delete symmetric cipher key successfully!");
    }

    /* close cipher */
    error = ops->eleCloseCipher(cipherHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("key cipher close failed!");
    } else {
        ALOGE("key cipher close successfully!");
    }

    return ret;
}

int test_cipher_aead_operation(EleOperation *ops, uint32_t keyStoreHandler, uint32_t keyMgtHandle) {
    cipher_aead_operation_attr sym_key_aead_op_addr;
    gen_key_attribute keyAttribute;
    ErrorType error;
    uint32_t cipherHandle = 0;
    uint8_t ciphered_aead_data[48];
    uint8_t deciphered_aead_data[32];
    uint8_t tag_aead_data[16];
    uint32_t keyID = 0;
    int ret = 0;

    /* generate symmetric key for AES CBC/GCM */
    memset(&keyAttribute, 0, sizeof(gen_key_attribute));
    /* let the ELE choose key id automatically */
    keyID = KEY_ID;
    keyAttribute.key_group = KEY_GROUP;
    keyAttribute.type = KEY_TYPE_AES;
    keyAttribute.size_bits = KEY_SIZE_AES_256;
    keyAttribute.lifetime = STD_PERSISTENT;
    keyAttribute.usage = KEY_USAGE_ENCRYPT | KEY_USAGE_DECRYPT;
    keyAttribute.permit_algo = PERMITTED_ALGO_ALL_AEAD;
    keyAttribute.lifecycle = LIFE_CYCLE_CURRENT;
    keyAttribute.flags = OPERATION_SYNC;
    error = ops->eleGenerateKey(keyMgtHandle, &keyID, &keyAttribute);
    if (error != ELE_NO_ERROR) {
        ALOGE("generate cipher aead key failed!");
        return -1;
    } else {
        ALOGE("cipher aead key generated successfully with ID: 0x%x!", keyID);
    }

    /* open key cipher */
    error = ops->eleOpenCipher(keyStoreHandler, &cipherHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("open key cipher failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("open key cipher succeed!");
    }

    /* encryption */
    memset(&sym_key_aead_op_addr, 0, sizeof(sym_key_aead_op_addr));
    sym_key_aead_op_addr.key_id = keyID;
    sym_key_aead_op_addr.iv_in_addr = iv_data_aead;
    sym_key_aead_op_addr.iv_size = sizeof(iv_data_aead);
    sym_key_aead_op_addr.iv_out_addr = NULL;
    sym_key_aead_op_addr.flags = AEAD_FLAGS_ENCRYPT | AEAD_FLAGS_ONE_SHOT;
    sym_key_aead_op_addr.algo = PERMITTED_ALGO_CCM;
    sym_key_aead_op_addr.tag_addr = tag_aead_data;
    sym_key_aead_op_addr.tag_size = sizeof(tag_aead_data);
    sym_key_aead_op_addr.aad_addr = aad_data_aead;
    sym_key_aead_op_addr.aad_size = sizeof(aad_data_aead);
    sym_key_aead_op_addr.input_addr = hash_data;
    sym_key_aead_op_addr.input_size = sizeof(hash_data);
    sym_key_aead_op_addr.output_addr = ciphered_aead_data;
    sym_key_aead_op_addr.output_size = sizeof(ciphered_aead_data);
    error = ops->eleCipherAeadOperation(cipherHandle, &sym_key_aead_op_addr);
    if (error != ELE_NO_ERROR) {
        ALOGE("cipher aead encrypt failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("cipher aead encrypt succeed!");
        ALOGE("======== dump encrypted data ========");
        for (int i = 0; i < sizeof(ciphered_aead_data); i++) ALOGE("%02x", ciphered_aead_data[i]);
        ALOGE("=================================");
    }

    /* dencryption */
    memset(&sym_key_aead_op_addr, 0, sizeof(sym_key_aead_op_addr));
    sym_key_aead_op_addr.key_id = keyID;
    sym_key_aead_op_addr.iv_in_addr = iv_data_aead;
    sym_key_aead_op_addr.iv_size = sizeof(iv_data_aead);
    sym_key_aead_op_addr.iv_out_addr = NULL;
    sym_key_aead_op_addr.flags = AEAD_FLAGS_DECRYPT | AEAD_FLAGS_ONE_SHOT;
    sym_key_aead_op_addr.algo = PERMITTED_ALGO_CCM;
    sym_key_aead_op_addr.tag_addr = tag_aead_data;
    sym_key_aead_op_addr.tag_size = sizeof(tag_aead_data);
    sym_key_aead_op_addr.aad_addr = aad_data_aead;
    sym_key_aead_op_addr.aad_size = sizeof(aad_data_aead);
    sym_key_aead_op_addr.input_addr = ciphered_aead_data;
    sym_key_aead_op_addr.input_size = sizeof(hash_data); // the actual ciphertext size
    sym_key_aead_op_addr.output_addr = deciphered_aead_data;
    sym_key_aead_op_addr.output_size = sizeof(deciphered_aead_data);
    error = ops->eleCipherAeadOperation(cipherHandle, &sym_key_aead_op_addr);
    if (error != ELE_NO_ERROR) {
        ALOGE("cipher aead decrypt failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("cipher aead decrypt succeed!");
        ALOGE("======== dump decrypted data ========");
        for (int i = 0; i < sizeof(deciphered_aead_data); i++)
            ALOGE("%02x", deciphered_aead_data[i]);
        ALOGE("=================================");

        ALOGE("======== dump original data ========");
        for (int i = 0; i < sizeof(hash_data); i++) ALOGE("%02x", hash_data[i]);
        ALOGE("=================================");

        if (memcmp(deciphered_aead_data, hash_data, sizeof(hash_data))) {
            ALOGE("The decrypted data is not equal with the original data!");
            ret = -1;
            goto exit;
        } else {
            ALOGE("The decrypted data matches original data!");
        }
    }

    ret = 0;

exit:
    /* delete the key */
    error = ops->eleDeleteKey(keyMgtHandle, keyID, OPERATION_SYNC);
    if (error != ELE_NO_ERROR) {
        ALOGE("delete key failed!");
        ret = -1;
    } else {
        ALOGE("delete key successfully!");
    }

    /* close cipher */
    error = ops->eleCloseCipher(cipherHandle);
    if (error != ELE_NO_ERROR) {
        ret = -1;
        ALOGE("key cipher close failed!");
    } else {
        ALOGE("key cipher close successfully!");
    }

    return ret;
}

int test_mac_operation(EleOperation *ops, uint32_t keyStoreHandler, uint32_t keyMgtHandle) {
    mac_operation_attr mac_op_args;
    gen_key_attribute keyAttribute;
    uint8_t payload[32];
    uint8_t mac[32];
    uint32_t keyID = 0;
    uint32_t macHandle = 0;
    ErrorType error;
    int ret = 0;

    /* open mac session */
    error = ops->eleMacOpen(keyStoreHandler, &macHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("open mac session failed!");
        return -1;
    } else {
        ALOGE("mac session open succeed with handle: 0x%x!", macHandle);
    }

    /* generate cmac key */
    memset(&keyAttribute, 0, sizeof(gen_key_attribute));
    keyID = KEY_ID;
    keyAttribute.key_group = KEY_GROUP;
    keyAttribute.type = KEY_TYPE_AES;
    keyAttribute.size_bits = KEY_SIZE_AES_256;
    keyAttribute.lifetime = STD_PERSISTENT;
    keyAttribute.usage = KEY_USAGE_SIGN_MSG | KEY_USAGE_VERIFY_MSG;
    keyAttribute.permit_algo = PERMITTED_ALGO_CMAC;
    keyAttribute.lifecycle = LIFE_CYCLE_CURRENT;
    keyAttribute.flags = OPERATION_SYNC;
    error = ops->eleGenerateKey(keyMgtHandle, &keyID, &keyAttribute);
    if (error != ELE_NO_ERROR) {
        ALOGE("generate cmac key failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("cmac key generate successfully with ID: 0x%x!", keyID);
    }

    /* do mac test - mac generation */
    mac_op_args.key_id = keyID;
    mac_op_args.payload_addr = payload;
    mac_op_args.mac_addr = mac;
    mac_op_args.payload_size = sizeof(payload);
    mac_op_args.mac_size = MAC_LENGTH_CMAC;
    mac_op_args.flags = MAC_ONE_GO_GENERATION;
    mac_op_args.algo = PERMITTED_ALGO_CMAC;
    error = ops->eleMacOperation(macHandle, &mac_op_args);
    if (error != ELE_NO_ERROR) {
        ALOGE("cmac generation failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("cmac generation succeed!");
        ALOGE("======== dump cmac ========");
        for (int i = 0; i < sizeof(mac); i++) ALOGE("%02x", mac[i]);
        ALOGE("=================================");
    }

    /* do mac test - mac verification */
    mac_op_args.key_id = keyID;
    mac_op_args.payload_addr = payload;
    mac_op_args.mac_addr = mac;
    mac_op_args.payload_size = sizeof(payload);
    mac_op_args.mac_size = MAC_LENGTH_CMAC;
    mac_op_args.flags = MAC_ONE_GO_VERIFICATION;
    mac_op_args.algo = PERMITTED_ALGO_CMAC;
    error = ops->eleMacOperation(macHandle, &mac_op_args);
    if (error != ELE_NO_ERROR || mac_op_args.mac_size != MAC_LENGTH_CMAC) {
        ALOGE("cmac verification failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("cmac verification succeed!");
    }

exit:
    /* delete the key */
    error = ops->eleDeleteKey(keyMgtHandle, keyID, OPERATION_SYNC);
    if (error != ELE_NO_ERROR) {
        ALOGE("delete cmac key failed!");
        ret = -1;
    } else {
        ALOGE("delete cmac key successfully!");
    }

    error = ops->eleMacClose(macHandle);
    if (error != ELE_NO_ERROR) {
        ret = -1;
        ALOGE("close mac operation failed!");
    } else {
        ALOGE("close mac operation successfully!");
    }

    return ret;
}

int test_storage_operation(EleOperation *ops, uint32_t keyStoreHandler, uint32_t keyMgtHandle) {
    data_storage_attr dataStorageAttr;
    data_enc_storage_attr dataEncStorageAttr;
    gen_key_attribute keyAttribute;
    uint32_t encryptKeyID = 0;
    uint32_t signKeyID = 0;
    uint32_t dataStorageHandle = 0;
    uint32_t storedSize = 0;
    uint8_t temp_data[1024];
    ErrorType error;
    int ret = 0;

    /* open data storage session */
    error = ops->eleDataStorageOpen(keyStoreHandler, &dataStorageHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("open data storage failed!");
        return -1;
    } else {
        ALOGE("open data storage session succeed. Handle: 0x%x!", dataStorageHandle);
    }

    /* store data to storage */
    memset(&dataStorageAttr, 0, sizeof(dataStorageAttr));
    dataStorageAttr.data_lsb_addr = hash_data;
    dataStorageAttr.data_size = sizeof(hash_data);
    dataStorageAttr.flags = ELE_DATA_STORAGE_STANDARD_OPTION | ELE_DATA_STORAGE_STORE;
    dataStorageAttr.data_id = DATA_STORAGE_ID;
    error = ops->eleDataStorage(dataStorageHandle, &dataStorageAttr);
    if (error != ELE_NO_ERROR) {
        ALOGE("store data to storage failed!, err: %d", error);
        ret = -1;
        goto exit;
    } else {
        ALOGE("data stored to storage successfully! ID: 0x%x", DATA_STORAGE_ID);
    }

    /* load the stored data and compare */
    memset(&dataStorageAttr, 0, sizeof(dataStorageAttr));
    dataStorageAttr.data_lsb_addr = temp_data;
    dataStorageAttr.data_size = sizeof(hash_data);
    dataStorageAttr.flags = ELE_DATA_STORAGE_STANDARD_OPTION | ELE_DATA_STORAGE_RETRIEVE;
    dataStorageAttr.data_id = DATA_STORAGE_ID;
    error = ops->eleDataStorage(dataStorageHandle, &dataStorageAttr);
    if (error != ELE_NO_ERROR) {
        ALOGE("retrieve data from storage failed!, err: %d", error);
        ret = -1;
        goto exit;
    } else {
        ALOGE("data retrieved storage successfully! ID: 0x%x", DATA_STORAGE_ID);
    }
    /* compare the data */
    if (memcmp(hash_data, temp_data, sizeof(hash_data))) {
        ALOGE("the retrieved data is not equal with the original data!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("the retrieved data matches original data!");
    }

    /* delete the data */
    error = ops->eleDataStorageDelete(dataStorageHandle, DATA_STORAGE_ID);
    if (error != ELE_NO_ERROR) {
        ALOGE("delete data (id: %d) from storage failed!", DATA_STORAGE_ID);
        ret = -1;
        goto exit;
    } else {
        ALOGE("delete data (id: %d) successfully!", DATA_STORAGE_ID);
    }

    /* store encrypted/signed data */
    /* generate encryption key */
    memset(&keyAttribute, 0, sizeof(gen_key_attribute));
    keyAttribute.key_group = KEY_GROUP;
    keyAttribute.type = KEY_TYPE_AES;
    keyAttribute.size_bits = KEY_SIZE_AES_256;
    keyAttribute.lifetime = STD_PERSISTENT;
    keyAttribute.usage = KEY_USAGE_ENCRYPT | KEY_USAGE_DECRYPT;
    keyAttribute.permit_algo = PERMITTED_ALGO_ALL_CIPHER;
    keyAttribute.lifecycle = LIFE_CYCLE_CURRENT;
    keyAttribute.flags = OPERATION_SYNC;
    error = ops->eleGenerateKey(keyMgtHandle, &encryptKeyID, &keyAttribute);
    if (error != ELE_NO_ERROR) {
        ALOGE("generate cipher key failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("cipher key generated successfully with ID: 0x%x!", encryptKeyID);
    }
    /* generate cmac sign key */
    memset(&keyAttribute, 0, sizeof(gen_key_attribute));
    keyAttribute.key_group = KEY_GROUP;
    keyAttribute.type = KEY_TYPE_AES;
    keyAttribute.size_bits = KEY_SIZE_AES_256;
    keyAttribute.lifetime = STD_PERSISTENT;
    keyAttribute.usage = KEY_USAGE_SIGN_MSG | KEY_USAGE_VERIFY_MSG;
    keyAttribute.permit_algo = PERMITTED_ALGO_CMAC;
    keyAttribute.lifecycle = LIFE_CYCLE_CURRENT;
    keyAttribute.flags = OPERATION_SYNC;
    error = ops->eleGenerateKey(keyMgtHandle, &signKeyID, &keyAttribute);
    if (error != ELE_NO_ERROR) {
        ALOGE("generate cmac key failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("cmac key generate successfully with ID: 0x%x!", signKeyID);
    }
    /* store data */
    memset(&dataEncStorageAttr, 0, sizeof(dataEncStorageAttr));
    dataEncStorageAttr.data_id = DATA_STORAGE_ID;
    dataEncStorageAttr.data_addr = hash_data;
    dataEncStorageAttr.data_size = sizeof(hash_data);
    dataEncStorageAttr.enc_algo = PERMITTED_ALGO_CBC_NO_PADDING;
    dataEncStorageAttr.enc_key_id = encryptKeyID;
    dataEncStorageAttr.sign_algo = PERMITTED_ALGO_CMAC;
    dataEncStorageAttr.sign_key_id = signKeyID;
    dataEncStorageAttr.iv_addr = iv_data;
    dataEncStorageAttr.iv_size = sizeof(iv_data);
    dataEncStorageAttr.flags = ELE_DATA_ENC_STORAGE_READ_ONCE;
    dataEncStorageAttr.lifecycle = LIFE_CYCLE_CURRENT;
    error = ops->eleDataEncStorage(dataStorageHandle, &dataEncStorageAttr, &storedSize);
    if (error != ELE_NO_ERROR) {
        ALOGE("store encrypted/signed data to storage failed! err=0x%x", error);
        ret = -1;
        goto exit;
    } else {
        ALOGE("store encrypted/signed data to storage successfully! id: 0x%x, size: %d",
              DATA_STORAGE_ID, storedSize);
    }

    /* read stored encrypted/signed data */
    memset(&dataStorageAttr, 0, sizeof(dataStorageAttr));
    dataStorageAttr.data_lsb_addr = temp_data;
    dataStorageAttr.data_size = storedSize;
    dataStorageAttr.flags = ELE_DATA_STORAGE_STANDARD_OPTION | ELE_DATA_STORAGE_RETRIEVE;
    dataStorageAttr.data_id = DATA_STORAGE_ID;
    error = ops->eleDataStorage(dataStorageHandle, &dataStorageAttr);
    if (error != ELE_NO_ERROR) {
        ALOGE("retrieve data from storage failed!, err: %d", error);
        ret = -1;
        goto exit;
    } else {
        ALOGE("data retrieved storage successfully! ID: 0x%x", DATA_STORAGE_ID);
        ALOGE("======== dump encrypted/signed TLV ========");
        for (int i = 0; i < storedSize; i++) ALOGE("%02x", temp_data[i]);
        ALOGE("=================================");
    }

    /* try to read the data again...it should fail this time because ELE_DATA_ENC_STORAGE_READ_ONCE
     * was set */
    memset(&dataStorageAttr, 0, sizeof(dataStorageAttr));
    dataStorageAttr.data_lsb_addr = temp_data;
    dataStorageAttr.data_size = storedSize;
    dataStorageAttr.flags = ELE_DATA_STORAGE_STANDARD_OPTION | ELE_DATA_STORAGE_RETRIEVE;
    dataStorageAttr.data_id = DATA_STORAGE_ID;
    error = ops->eleDataStorage(dataStorageHandle, &dataStorageAttr);
    if (error != ELE_COMMAND_INVALID_ID) {
        ALOGE("retrieve encrypted/signed data succeed, which is not expected. err: %d", error);
        ret = -1;
        goto exit;
    } else {
        ALOGE("previously stored data cannot be retrieved again, which is expected!");
    }

exit:
    /* delete keys */
    if (encryptKeyID != 0) {
        error = ops->eleDeleteKey(keyMgtHandle, encryptKeyID, OPERATION_SYNC);
        if (error != ELE_NO_ERROR) {
            ALOGE("delete cipher aead key failed!");
            ret = -1;
        } else {
            ALOGE("delete cipher aead key successfully!");
        }
    }

    if (signKeyID != 0) {
        error = ops->eleDeleteKey(keyMgtHandle, signKeyID, OPERATION_SYNC);
        if (error != ELE_NO_ERROR) {
            ALOGE("delete cmac key failed!");
            ret = -1;
        } else {
            ALOGE("delete cmac key successfully!");
        }
    }

    error = ops->eleDataStorageClose(dataStorageHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("close data storage failed!");
        ret = -1;
    } else {
        ALOGE("close data storage session succeed!");
    }

    return ret;
}

int test_el2go(EleOperation *ops, uint32_t keyStoreHandler, uint32_t keyMgtHandle) {
    ErrorType error = ELE_NO_ERROR;
    key_attribute keyAttr = {0};
    data_storage_attr dataStorageAttr = {0};
    uint8_t temp_data[1024];
    uint32_t dataStorageHandle = 0;
    int ret = 0;

    /* Get the attributes of imported keypair */
    error = ops->eleGetKeyAttr(keyMgtHandle, EL2GO_IMPORTED_KEYPAIR_ID, &keyAttr);
    if (error != ELE_NO_ERROR) {
        ALOGE("get el2go imported key pair attribute failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("get el2go imported key pair attribute successfully!");
        ALOGE("key id:0x%x key type: 0x%x, size: 0x%x, lifetime: 0x%x, usage: 0x%x, algo: 0x%x, lifecycle: 0x%x",
              EL2GO_IMPORTED_KEYPAIR_ID, keyAttr.type, keyAttr.size_bits, keyAttr.lifetime,
              keyAttr.usage, keyAttr.permit_algo, keyAttr.lifecycle);
    }

    /* Dump the imported data storage */
    /* open data storage session */
    error = ops->eleDataStorageOpen(keyStoreHandler, &dataStorageHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("open data storage failed!");
        goto exit;
    } else {
        ALOGE("open data storage session succeed. Handle: 0x%x!", dataStorageHandle);
    }
    /* Dump data */
    dataStorageAttr.data_lsb_addr = temp_data;
    dataStorageAttr.data_size = sizeof(temp_data);
    dataStorageAttr.flags = ELE_DATA_STORAGE_STANDARD_OPTION | ELE_DATA_STORAGE_RETRIEVE;
    dataStorageAttr.data_id = EL2GO_IMPORTED_DATA_STORAGE_ID;
    error = ops->eleDataStorage(dataStorageHandle, &dataStorageAttr);
    if (error != ELE_NO_ERROR) {
        ALOGE("retrieve el2go imported data from storage failed!, err: %d", error);
        ret = -1;
        goto exit;
    } else {
        ALOGE("el2go imported data retrieved from storage successfully! ID: 0x%x",
              EL2GO_IMPORTED_DATA_STORAGE_ID);
        ALOGE("======== dump el2go imported data ========");
        for (int i = 0; i < dataStorageAttr.data_size; i++) ALOGE("%02x", temp_data[i]);
        ALOGE("==========================================");
    }

exit:
    if (dataStorageHandle != 0) {
        error = ops->eleDataStorageClose(dataStorageHandle);
        if (error != ELE_NO_ERROR) {
            ALOGE("close data storage failed!");
            ret = -1;
        } else {
            ALOGE("close data storage session succeed!");
        }
    }

    return ret;
}

int test_inject_claim_code(EleOperation *ops, uint32_t keyStoreHandler, uint32_t keyMgtHandle) {
    ErrorType error = ELE_NO_ERROR;
    uint32_t dataStorageHandle = 0;
    uint32_t storedSize = 0;
    data_enc_storage_attr dataEncStorageAttr = {0};
    int ret = 0;

    /* open data storage session */
    error = ops->eleDataStorageOpen(keyStoreHandler, &dataStorageHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("open data storage failed!");
        return -1;
    } else {
        ALOGE("open data storage session succeed. Handle: 0x%x!", dataStorageHandle);
    }

    /* Inject claimcode */
    dataEncStorageAttr.data_id = EL2GO_IMPORTED_CLAIMCODE_ID;
    dataEncStorageAttr.data_addr = test_claimcode;
    dataEncStorageAttr.data_size = sizeof(test_claimcode) - 1;
    /* If the data id is claimcode(0xF00000E0), enc_algo, enc_key_id, sign_algo and sign_key_id
     * would set by default, "ELE_DATA_ENC_STORAGE_READ_ONCE" would be set in the flags and the
     * the lifecycle would be set to all lifecycles (OPEN | CLOSED | CLOSED and LOCKED).
     */
    /* use random IV */
    dataEncStorageAttr.flags = ELE_USE_INTERNAL_RAMDOM_IV;
    error = ops->eleDataEncStorage(dataStorageHandle, &dataEncStorageAttr, &storedSize);
    if (error != ELE_NO_ERROR) {
        ALOGE("inject claimcode failed!, err: %d", error);
        ret = -1;
        goto exit;
    } else {
        ALOGE("inject claim code successfully! Stored size: %d", storedSize);
    }

exit:
    if (dataStorageHandle != 0) {
        error = ops->eleDataStorageClose(dataStorageHandle);
        if (error != ELE_NO_ERROR) {
            ALOGE("close data storage failed!");
            ret = -1;
        } else {
            ALOGE("close data storage session succeed!");
        }
    }

    return ret;
}

void print_usage() {
    printf("Usage: ele-test <command>\n"
           "Commands:\n"
           "  all             - Run all tests except for el2go/injectclaimcode test\n"
           "  el2go           - Run el2go test(all materials should be provisioned in advance).\n"
           "  injectclaimcode - Inject test claim code to the device.\n"
           "  keypair         - Test keypair management and signature\n"
           "  cipher          - Test symmetric cipher operations\n"
           "  cipher-aead     - Test authenticated encryption/decryption operations\n"
           "  mac             - Test MAC operations\n"
           "  storage         - Test data storage operations\n"
           "  info            - Get device info only\n"
           "  help            - Show this message\n");
}

int main(int argc, const char *argv[]) {
    uint32_t keyStoreHandler = 0;
    uint32_t keyMgtHandle = 0;
    struct device_info info;
    ErrorType error;
    int ret = 0;

    // Check arguments
    if (argc != 2) {
        print_usage();
        return -1;
    }

    const char *cmd = argv[1];

    // Check for help command first
    if (strcmp(cmd, "help") == 0) {
        print_usage();
        return 0;
    }

    // Initialize ELE operations
    EleOperation ops(MU_CHANNEL_PLAT_HSM);
    if (ops.eleOpenDeviceNode() != ELE_NO_ERROR) {
        ALOGE("Failed to open ELE device nodes!");
        return -1;
    }
    ALOGE("Succeed to open ELE device nodes!");

    if (ops.eleOpenSession() != ELE_NO_ERROR) {
        ALOGE("Failed to open ELE session!");
        ret = -1;
        goto cleanup_session;
    }
    ALOGE("Succeed to open ELE session!");

    // Get device info for all commands
    if (ops.eleGetDeviceInfo(&info) != ELE_NO_ERROR) {
        ALOGE("Failed to retrieve device info!");
        ret = -1;
        goto cleanup_session;
    }
    ALOGE("Device Info:");
    ALOGE("user id: 0x%08x", info.user_sab_id);
    ALOGE("device uuid: 0x%08x%08x%08x%08x", info.uid_w0, info.uid_w1, info.uid_w2, info.uid_w3);
    ALOGE("monotonic_counter: 0x%04x", info.monotonic_counter);
    ALOGE("lifecycle: 0x%04x", info.lifecycle);
    ALOGE("fips_mode: 0x%02x", info.fips_mode);

    // Return if only device info was requested
    if (strcmp(cmd, "info") == 0) {
        ret = 0;
        goto cleanup_session;
    }

    // Initialize keystore and key management
    error = ops.eleOpenKeyStore(KEY_STORE_ID, KEY_STORE_NONCE,
                                KEY_STORE_OPERATION_CREATE | OPERATION_SYNC, &keyStoreHandler);
    if (error == ELE_COMMAND_KEYSTORE_CONFLICT) {
        ALOGE("Keystore already existed, loading it...");
        error = ops.eleOpenKeyStore(KEY_STORE_ID, KEY_STORE_NONCE, KEY_STORE_OPERATION_LOAD,
                                    &keyStoreHandler);
    }
    if (error != ELE_NO_ERROR) {
        ALOGE("Keystore open/load failed!");
        ret = -1;
        goto cleanup_session;
    }

    error = ops.eleOpenKeyManagement(keyStoreHandler, &keyMgtHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Key management open failed!");
        ret = -1;
        goto cleanup_keystore;
    }

    /* Execute requested command */
    if (strcmp(cmd, "all") == 0) {
        ret = test_keypair_mangement_and_signature(&ops, keyStoreHandler, keyMgtHandle);
        if (ret == 0)
            ret = test_cipher_operation(&ops, keyStoreHandler, keyMgtHandle);
        if (ret == 0)
            ret = test_cipher_aead_operation(&ops, keyStoreHandler, keyMgtHandle);
        if (ret == 0)
            ret = test_mac_operation(&ops, keyStoreHandler, keyMgtHandle);
        if (ret == 0)
            ret = test_storage_operation(&ops, keyStoreHandler, keyMgtHandle);
    } else if (strcmp(cmd, "keypair") == 0) {
        ret = test_keypair_mangement_and_signature(&ops, keyStoreHandler, keyMgtHandle);
    } else if (strcmp(cmd, "cipher") == 0) {
        ret = test_cipher_operation(&ops, keyStoreHandler, keyMgtHandle);
    } else if (strcmp(cmd, "cipher-aead") == 0) {
        ret = test_cipher_aead_operation(&ops, keyStoreHandler, keyMgtHandle);
    } else if (strcmp(cmd, "mac") == 0) {
        ret = test_mac_operation(&ops, keyStoreHandler, keyMgtHandle);
    } else if (strcmp(cmd, "storage") == 0) {
        ret = test_storage_operation(&ops, keyStoreHandler, keyMgtHandle);
    } else if (strcmp(cmd, "el2go") == 0) {
        ret = test_el2go(&ops, keyStoreHandler, keyMgtHandle);
    } else if (strcmp(cmd, "injectclaimcode") == 0) {
        ret = test_inject_claim_code(&ops, keyStoreHandler, keyMgtHandle);
    } else {
        ALOGE("Unknown command: %s", cmd);
        print_usage();
        ret = -1;
    }
    if (ret == 0) {
        ALOGE("********************************");
        ALOGE("******** ALL TESTS PASS ********");
        ALOGE("********************************");
    } else {
        ALOGE("********************************");
        ALOGE("******** SOME TESTS FAIL ********");
        ALOGE("********************************");
    }

    // Cleanup
    ops.eleCloseKeyManagement(keyMgtHandle);
cleanup_keystore:
    ops.eleCloseKeyStore(keyStoreHandler);
cleanup_session:
    ops.eleCloseSession();

    return ret;
}

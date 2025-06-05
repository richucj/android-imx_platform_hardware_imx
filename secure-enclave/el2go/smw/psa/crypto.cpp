/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2025 NXP
 */

#define LOG_TAG "smw"

#include <EleMessage.h>
#include <EleOperation.h>
#include <psa/crypto.h>
#include <utils.h>

psa_status_t psa_sign_hash(psa_key_id_t key, psa_algorithm_t alg, const uint8_t *hash,
                           size_t hash_length, uint8_t *signature, size_t signature_size,
                           size_t *signature_length) {
    ErrorType error = ELE_NO_ERROR;
    uint32_t keyStoreHandler = 0;
    uint32_t signGenHandle = 0;
    struct gen_sign_attr sign_generate_attr = {0};

    if (!key || !alg || !hash || !hash_length || !signature || !signature_size || !signature_length)
        return PSA_ERROR_INVALID_ARGUMENT;

    EleOperation ops(MU_CHANNEL_ELE);
    do {
        error = openKeystoreAndSign(&ops, &keyStoreHandler, &signGenHandle);
        if (error != ELE_NO_ERROR)
            break;

        /* ELE doesn't support deterministic ECDSA, so clear the deterministic bit */
        if (PSA_ALG_IS_ECDSA(alg)) {
            alg = (alg) & ~PSA_ALG_ECDSA_DETERMINISTIC_FLAG;
        }

        /* Generate hash signature */
        memset(&sign_generate_attr, 0, sizeof(sign_generate_attr));
        sign_generate_attr.key_id = key;
        sign_generate_attr.msg_lsb_addr = (uint8_t *)hash;
        sign_generate_attr.sign_lsb_addr = (uint8_t *)signature;
        sign_generate_attr.msg_size = hash_length;
        sign_generate_attr.sign_size = signature_size;
        sign_generate_attr.flags = ELE_SIGN_FLAGS_DIGEST;
        sign_generate_attr.sign_scheme = alg;
        sign_generate_attr.salt_len = 0;
        error = ops.eleSignGenerate(signGenHandle, &sign_generate_attr);
        if (error != ELE_NO_ERROR) {
            ALOGE("Failed to generate hash signature!");
            break;
        } else {
            *signature_length = sign_generate_attr.sign_size;
        }
    } while (0);

    closeKeystoreAndSign(&ops, keyStoreHandler, signGenHandle);

    return eleErrorToPSAStatus(error);
}

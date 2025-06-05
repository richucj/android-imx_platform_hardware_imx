/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2025 NXP
 */

#define LOG_TAG "smw"

#include <EleMessage.h>
#include <EleOperation.h>
#include <smw_device.h>
#include <smw_osal.h>
#include <utils.h>

enum smw_status_code smw_osal_lib_init(void) {
    return SMW_STATUS_OK;
}

enum smw_status_code smw_device_get_uuid(struct smw_device_uuid_args *args) {
    ErrorType error = ELE_NO_ERROR;
    struct device_info info;

    if (args->uuid == NULL) {
        ALOGE("empty input uuid buffer!");
        return SMW_STATUS_INVALID_PARAM;
    }

    EleOperation ops(MU_CHANNEL_ELE);
    do {
        error = openDeviceSession(&ops);
        if (error != ELE_NO_ERROR)
            break;

        /* get device info */
        error = ops.eleGetDeviceInfo(&info);
        if (error != ELE_NO_ERROR) {
            ALOGE("Failed to retrieve device info!");
            break;
        } else {
            memcpy(args->uuid, &(info.uid_w0), ELE_UUID_LENGTH);
            args->uuid_length = ELE_UUID_LENGTH;
        }
    } while (0);

    if (ops.eleCloseSession() != ELE_NO_ERROR) {
        ALOGE("Failed to close ELE session!");
    }

    if (error != ELE_NO_ERROR)
        return SMW_STATUS_OPERATION_FAILURE;
    else
        return SMW_STATUS_OK;
}

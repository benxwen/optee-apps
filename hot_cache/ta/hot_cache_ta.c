/*
 * Copyright (c) 2017, Linaro Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <inttypes.h>
#include <string.h>

#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include <hot_cache_ta.h>

#define AES128_KEY_BIT_SIZE		128
#define AES128_KEY_BYTE_SIZE		(AES128_KEY_BIT_SIZE / 8)
#define AES256_KEY_BIT_SIZE		256
#define AES256_KEY_BYTE_SIZE		(AES256_KEY_BIT_SIZE / 8)

typedef struct aes_cipher {
    uint32_t algo;
    uint32_t mode;
    uint32_t key_size;
    TEE_OperationHandle op_handle;
    TEE_ObjectHandle key_handle;
} aes_cipher;

static TEE_Result alloc_resources(void *session, uint32_t mode)
{
    printf("MQTTZ: Started AES Resource Allocation\n");
    aes_cipher *sess;
    TEE_Attribute attr;
    TEE_Result res;
    sess = (aes_cipher *)session;
    sess->algo = TEE_ALG_AES_CBC_NOPAD;
    sess->key_size = TA_AES_KEY_SIZE;
    printf("MQTTZ: Loaded algo and key size\n");
    switch (mode) {
        case TA_AES_MODE_ENCODE:
            sess->mode = TEE_MODE_ENCRYPT;
            break;
        case TA_AES_MODE_DECODE:
            sess->mode = TEE_MODE_DECRYPT;
            break;
        default:
            return TEE_ERROR_BAD_PARAMETERS;
    }
    // Free previous operation handle
    if (sess->op_handle != TEE_HANDLE_NULL)
        TEE_FreeOperation(sess->op_handle);
    // Allocate operation
    res = TEE_AllocateOperation(&sess->op_handle, sess->algo, sess->mode,
            sess->key_size * 8);
    if (res != TEE_SUCCESS)
    {
        printf("MQTTZ-ERROR: TEE_AllocateOperation failed!\n");
        sess->op_handle = TEE_HANDLE_NULL;
        goto err;
    }
    printf("MQTTZ: Allocated Operation Handle\n");
    // Free Previous Key Handle
    if (sess->key_handle != TEE_HANDLE_NULL)
        TEE_FreeTransientObject(sess->key_handle);
    res = TEE_AllocateTransientObject(TEE_TYPE_AES, sess->key_size * 8,
            &sess->key_handle);
    if (res != TEE_SUCCESS)
    {
        printf("MQTTZ-ERROR: TEE_AllocateTransitionObject failed\n");
        sess->key_handle = TEE_HANDLE_NULL;
        goto err;
    }
    printf("MQTTZ: Allocated Key Handle\n");
    // Load Dummy Key 
    char *key;
    key = TEE_Malloc(sess->key_size, 0);
    if (!key)
    {
        printf("MQTTZ-ERROR: Out of memory!\n");
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto err;
    }
    TEE_InitRefAttribute(&attr, TEE_ATTR_SECRET_VALUE, key, sess->key_size);
    printf("MQTTZ: InitRef\n");
    res = TEE_PopulateTransientObject(sess->key_handle, &attr, 1);
    printf("MQTTZ: Populate\n");
    if (res != TEE_SUCCESS)
    {
        printf("MQTTZ-ERROR: TEE_PopulateTransientObject failed!\n");
        goto err;
    }
    printf("MQTTZ: Reset Operation\n");
    res = TEE_SetOperationKey(sess->op_handle, sess->key_handle);
    if (res != TEE_SUCCESS)
    {
        printf("MQTTZ-ERROR: TEE_SetOperationKey failed!\n");
        goto err;
    }
    printf("MQTTZ: Set Operation\n");
    return res;
err:
    if (sess->op_handle != TEE_HANDLE_NULL)
        TEE_FreeOperation(sess->op_handle);
    sess->op_handle = TEE_HANDLE_NULL;
    if (sess->key_handle != TEE_HANDLE_NULL)
        TEE_FreeTransientObject(sess->key_handle);
    sess->key_handle = TEE_HANDLE_NULL;
    return res;
}

static TEE_Result set_aes_key(void *session, char *key)
{
    aes_cipher *sess;
    TEE_Attribute attr;
    TEE_Result res;
    sess = (aes_cipher *)session;

    /*
    if (key_size != sess->key_size)
    {
        prinf("MQTTZ-ERROR: Wrong Key Size!\n");
        return TEE_ERROR_BAD_PARAMETERS;
    }*/

    // Load Key Another Time 
    TEE_InitRefAttribute(&attr, TEE_ATTR_SECRET_VALUE, key, sess->key_size);
    TEE_ResetTransientObject(sess->key_handle);
    res = TEE_PopulateTransientObject(sess->key_handle, &attr, 1);
    if (res != TEE_SUCCESS)
    {
        printf("MQTTZ-ERROR: TEE_PopulateTransientObject Failed\n");
        return res;
    }
    TEE_ResetOperation(sess->op_handle);
    res = TEE_SetOperationKey(sess->op_handle, sess->key_handle);
    if (res != TEE_SUCCESS)
    {
        printf("MQTTZ-ERROR: TEE_SetOperationKey failed\n");
        return res;
    }
    return res;
}

static TEE_Result set_aes_iv(void *session, char *iv)
{
    aes_cipher *sess;
    sess = (aes_cipher *)session;
    // Load IV
    TEE_CipherInit(sess->op_handle, iv, TA_AES_IV_SIZE);
    return TEE_SUCCESS;
}

/*
 * Read Raw Object from Secure Storage within TA
 *
 * This method reads an object from Secure Storage but is always invoked
 * from within a TA. Hence why we don't check the parameter types.
 */
static TEE_Result read_raw_object(char *cli_id, size_t cli_id_size, char *data,
        size_t data_sz)
{
	const uint32_t exp_param_types =
		TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
				TEE_PARAM_TYPE_MEMREF_OUTPUT,
				TEE_PARAM_TYPE_NONE,
				TEE_PARAM_TYPE_NONE);
	TEE_ObjectHandle object;
	TEE_ObjectInfo object_info;
	TEE_Result res;
	uint32_t read_bytes;
    // Check if object is in memory
	res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
					cli_id, cli_id_size,
					TEE_DATA_FLAG_ACCESS_READ |
					TEE_DATA_FLAG_SHARE_READ,
					&object);
	if (res != TEE_SUCCESS) {
		EMSG("Failed to open persistent object, res=0x%08x", res);
		TEE_Free(cli_id);
		return res;
	}
	res = TEE_GetObjectInfo1(object, &object_info);
	if (res != TEE_SUCCESS) {
		EMSG("Failed to create persistent object, res=0x%08x", res);
		goto exit;
	}
	if (object_info.dataSize > data_sz) {
		/*
		 * Provided buffer is too short.
		 * Return the expected size together with status "short buffer"
		 */
		data_sz = object_info.dataSize;
		res = TEE_ERROR_SHORT_BUFFER;
		goto exit;
	}
	res = TEE_ReadObjectData(object, data, object_info.dataSize,
				 &read_bytes);
	if (res != TEE_SUCCESS || read_bytes != object_info.dataSize) {
		EMSG("TEE_ReadObjectData failed 0x%08x, read %" PRIu32 " over %u",
				res, read_bytes, object_info.dataSize);
		goto exit;
	}
	data_sz = read_bytes;
exit:
	TEE_CloseObject(object);
	return res;
}

static TEE_Result cipher_buffer(void *sess, char *enc_data,
        size_t enc_data_size, char *dec_data, size_t *dec_data_size)
{
    printf("MQTTZ: Starting AES Cipher!\n");
    aes_cipher *session;
    session = (aes_cipher *) sess;
    if (session->op_handle == TEE_HANDLE_NULL)
        return TEE_ERROR_BAD_STATE;
    printf("MQTTZ: Starting CipherUpdate with the following parameters:\n");
    printf("\t- Enc Data: %s\n", enc_data);
    printf("\t- Enc Data Size: %li\n", enc_data_size);
    printf("\t- Dec Data: %s\n", dec_data);
    printf("\t- Dec Data Size: %li\n", dec_data_size);
    return TEE_CipherUpdate(session->op_handle, enc_data, enc_data_size,
            dec_data, dec_data_size);
}

static int get_key(char *cli_id, char *cli_key)
{
    // TODO Implement Cache Logic
    size_t read_bytes;
    if ((read_raw_object(cli_id, strlen(cli_id), cli_key, read_bytes) 
            != TEE_SUCCESS))// || (read_bytes != TA_AES_KEY_SIZE))
    {
        // If no data, create random key FIXME
        char fake_key[TA_AES_KEY_SIZE] = "11111111111111111111111111111111";
        printf("MQTTZ: Key not found! Created fake key: %s\n", fake_key);
        //cli_key = (char *) TEE_Malloc(sizeof *cli_key * (TA_AES_KEY_SIZE + 1), 0);
        //memset(cli_key, 0xa5, TA_AES_KEY_SIZE);
        strcpy(cli_key, fake_key);
        cli_key[TA_AES_KEY_SIZE] = '\0';
        return 0;
    }
    return 0;
}

static TEE_Result payload_reencryption(void *session, uint32_t param_types,
        TEE_Param params[4])
{
    TEE_Result res;
    uint32_t exp_param_types = TEE_PARAM_TYPES(
            TEE_PARAM_TYPE_MEMREF_INPUT,
            TEE_PARAM_TYPE_MEMREF_INOUT,
            TEE_PARAM_TYPE_NONE,
            TEE_PARAM_TYPE_NONE);
    if (param_types != exp_param_types)
        return TEE_ERROR_BAD_PARAMETERS;
    printf("MQTTZ: Entered SW\n");
    // TODO
    // 1. Decrypt from Origin
    char *ori_cli_id;
    char *ori_cli_iv;
    char *ori_cli_data;
    size_t data_size = params[0].memref.size - TA_MQTTZ_CLI_ID_SZ 
            - TA_AES_IV_SIZE;
    ori_cli_id = (char *) TEE_Malloc(sizeof *ori_cli_id 
            * (TA_MQTTZ_CLI_ID_SZ + 1), 0);
    ori_cli_iv = (char *) TEE_Malloc(sizeof *ori_cli_id 
            * (TA_AES_IV_SIZE + 1), 0);
    ori_cli_data = (char *) TEE_Malloc(sizeof *ori_cli_id 
            * (TA_AES_KEY_SIZE + 1), 0);
    if (!(ori_cli_id && ori_cli_iv && ori_cli_data))
    {
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto exit;
    }
    printf("MQTTZ: Allocated input args\n");
    TEE_MemMove(ori_cli_id, (char *) params[0].memref.buffer, TA_MQTTZ_CLI_ID_SZ);
    ori_cli_id[TA_MQTTZ_CLI_ID_SZ] = '\0';
    TEE_MemMove(ori_cli_iv, (char *) params[0].memref.buffer + TA_MQTTZ_CLI_ID_SZ,
            TA_AES_IV_SIZE);
    ori_cli_iv[TA_AES_IV_SIZE] = '\0';
    TEE_MemMove(ori_cli_data, (char *) params[0].memref.buffer + TA_MQTTZ_CLI_ID_SZ
            + TA_AES_IV_SIZE, data_size);
    printf("MQTTZ: Loaded Values\n");
    printf("\t- Cli id: %s\n", ori_cli_id);
    printf("\t- Cli iv: %s\n", ori_cli_iv);
    printf("\t- Cli data: %s\n", ori_cli_data);
    // 2. Read key from secure storage
    char *ori_cli_key;
    ori_cli_key = (char *) TEE_Malloc(sizeof *ori_cli_key * (TA_AES_KEY_SIZE + 1), 0);
    printf("MQTTZ: Allocated key\n");
    if (get_key(ori_cli_id, ori_cli_key) != 0)
    {
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto exit;
    }
    printf("MQTTZ: Got key! %s\n", ori_cli_key);
    // 2. Encrypt to Destination
    if (alloc_resources(session, TA_AES_MODE_DECODE) != TEE_SUCCESS)
    {
        res = TEE_ERROR_GENERIC;
        goto exit;
    }
    printf("MQTTZ: Initialized AES Session!\n");
    if (set_aes_key(session, ori_cli_key) != TEE_SUCCESS)
    {
        printf("MQTTZ-ERROR: set_aes_key failed\n");
        res = TEE_ERROR_GENERIC;
        goto exit;
    }
    if (set_aes_iv(session, ori_cli_iv) != TEE_SUCCESS)
    {
        printf("MQTTZ-ERROR: set_aes_iv failed\n");
        res = TEE_ERROR_GENERIC;
        goto exit;
    }
    char *dec_data;
    dec_data = (char *) TEE_Malloc(sizeof *dec_data * data_size, 0);
    if (!dec_data)
    {
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto exit;
    }
    printf("MQTTZ: Allocated decrypted data!\n");
    if (cipher_buffer(session, ori_cli_data, data_size,
            params[1].memref.buffer, &params[1].memref.size) != TEE_SUCCESS)
    {
        res = TEE_ERROR_GENERIC;
        goto exit;
    }
    printf("MQTTZ: Finished buffer ciphering!\n");
    goto exit;
exit:
    TEE_Free(ori_cli_id);
    TEE_Free(ori_cli_iv);
    TEE_Free(ori_cli_data);
    TEE_Free(ori_cli_key);
    return res;
}

TEE_Result TA_CreateEntryPoint(void)
{
	/* Nothing to do */
	return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void)
{
	/* Nothing to do */
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t __unused param_types,
				    TEE_Param __unused params[4],
				    void __unused **session)
{
    aes_cipher *sess;
    sess = TEE_Malloc(sizeof *sess, 0);
    if (!sess)
        return TEE_ERROR_OUT_OF_MEMORY;
    sess->key_handle = TEE_HANDLE_NULL;
    sess->op_handle = TEE_HANDLE_NULL;
    *session = (void *)sess;
	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void __unused *session)
{
    aes_cipher *sess;
    sess = (aes_cipher *) session;
    if (sess->key_handle != TEE_HANDLE_NULL)
        TEE_FreeTransientObject(sess->key_handle);
    if (sess->op_handle != TEE_HANDLE_NULL)
        TEE_FreeOperation(sess->op_handle);
    TEE_Free(sess);
}

TEE_Result TA_InvokeCommandEntryPoint(void __unused *session,
				      uint32_t command,
				      uint32_t param_types,
				      TEE_Param params[4])
{
	switch (command) {
        case TA_REENCRYPT:
            return payload_reencryption(session, param_types, params);
	default:
		EMSG("Command ID 0x%x is not supported", command);
		return TEE_ERROR_NOT_SUPPORTED;
	}
}

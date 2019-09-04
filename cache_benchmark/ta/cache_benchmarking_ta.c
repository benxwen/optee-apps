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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <utee_defines.h>

#include <cache_benchmarking_ta.h>

#define NUM_TESTS               2 //100
#define TA_AES_KEY_SIZE         32
#define TA_MQTTZ_CLI_ID_SZ      12
#define TOTAL_ELEMENTS          1024
// To change every experiment
#define CACHE_SIZE              1024

typedef struct Node {
    char *id;
    char *data;
    struct Node *next;
    struct Node *prev;
} Node;

typedef struct Queue {
    Node *first;
    Node *last;
    int size;
    int max_size;
} Queue;

typedef struct Hash {
    int capacity;
    Node **array;
} Hash;

static Node* init_node(char *id, char *data)
{
    Node *tmp = (Node *) TEE_Malloc(sizeof(Node), 0);
    tmp->id = (char *) TEE_Malloc(sizeof(char) * (TA_MQTTZ_CLI_ID_SZ + 1), 0);
    strncpy(tmp->id, id, TA_MQTTZ_CLI_ID_SZ);
    tmp->id[TA_MQTTZ_CLI_ID_SZ] = '\0';
    tmp->data = (char *) TEE_Malloc(sizeof(char) * (TA_AES_KEY_SIZE + 1), 0);
    strncpy(tmp->data, data, TA_AES_KEY_SIZE);
    tmp->data[TA_AES_KEY_SIZE] = '\0';
    tmp->prev = tmp->next = NULL;
    return tmp;
}

static int free_node(Node *node)
{
    TEE_Free((void *) node->id);
    TEE_Free((void *) node->data);
    TEE_Free((void *) node); 
    return 0;
}

static Queue* init_queue(int max_size)
{
    Queue *queue = (Queue *) TEE_Malloc(sizeof(Queue), 0);
    queue->first = NULL;
    queue->last = NULL;
    queue->size = 0;
    queue->max_size = max_size;
    return queue;
}

static int free_queue(Queue *queue)
{
    TEE_Free((void *) queue->first);
    TEE_Free((void *) queue->last);
    TEE_Free((void *) queue);
    return 0;
}

static Hash* init_hash(int capacity)
{
    Hash *hash = (Hash *) TEE_Malloc(sizeof(Hash), 0);
    hash->capacity = capacity;
    hash->array = (Node **) TEE_Malloc(sizeof(Node*) * hash->capacity, 0);
    unsigned int i;
    for (i = 0; i < hash->capacity; i++)
        hash->array[i] = NULL;
    return hash;
}

int free_hash(Hash *hash)
{
    TEE_Free((void *) hash->array);
    TEE_Free((void *) hash);
    return 0;
}

Node* queue_pop(Queue *queue)
{
    // FIFO policy for the time being
    if (queue->size == 0)
        return NULL;
    Node *old_last = queue->last;
    queue->last = queue->last->prev;
    queue->last->next = NULL;
    queue->size -= 1;
    return old_last;
}

int queue_push(Queue *queue, Node *node)
{
    // FIFO policy for the time being
    if (queue->size == CACHE_SIZE)
        queue_pop(queue);
    if (queue->first != NULL)
    {
        queue->first->prev = node;
        node->next = queue->first;
    }
    else
    {
        queue->last = node;
    }
    queue->first = node;
    queue->size += 1;
    return 0;
}

int queue_to_front(Queue *queue, Node *node)
{
    if (queue->first == node)
        return 0;
    if (queue->last == node)
        queue->last = node->prev;
    else
        node->next->prev = node->prev;
    node->prev->next = node->next;
    node->prev = NULL;
    node->next = queue->first;
    queue->first->prev = node;
    queue->first = node;
    return 0;
}

Node* cache_query(Hash *hash, Queue *queue, char *obj_id)
{
    int page = atoi(obj_id) % hash->capacity;
    Node *reqPage = hash->array[page];
    if (reqPage == NULL)
    {
        // Cache Miss
        // Load from Secure Storage FIXME FIXME TODO
        // We do this instead for testing!
        reqPage = init_node(obj_id, "1111111111111111");
        if (queue->size == CACHE_SIZE)
        {
            Node *tmp = queue_pop(queue);
            int tmp_index = atoi(tmp->id) % hash->capacity;
            hash->array[tmp_index] = NULL;
            free_node(tmp);
        }
        queue_push(queue, reqPage);
        hash->array[page] = reqPage;
        return reqPage;
    }
    queue_to_front(queue, reqPage);
    return reqPage;
}


void print_queue_status(Queue *queue)
{
    printf("-----------------------\n");
    printf("Current Queue Status:\n\t- Queue Size: %i\n\t- Elements:\n",
            queue->size);
    int i;
    Node *current = queue->first;
    for (i = 0; i < queue->size; i++)
    {
        printf("\t\t%i -> %s\n", i, current->data);
        current = current->next;
    }
    printf("-----------------------\n");
}

void print_cache_status(Hash *hash)
{
    printf("-----------------------\n");
    printf("Current Hash Status:\n\t- Table Size: %i\n", hash->capacity);
    printf("\t- Cache Size: %i\n\t- Elements:\n", CACHE_SIZE);
    unsigned int i;
    for (i = 0; i < hash->capacity; i++)
    {
        if (hash->array[i] != NULL)
            printf("\t\t%i -> %s\n", i, hash->array[i]->data);
        else
            printf("\t\t%i -> \n", i);
    }
    printf("-----------------------\n");
}

static TEE_Result read_raw_object(char *cli_id, size_t cli_id_size, char *data,
        size_t data_sz)
{
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

static int save_key(char *cli_id, char *cli_key)
{
    uint32_t obj_data_flag;
    TEE_Result res;
    TEE_ObjectHandle object;
    obj_data_flag = TEE_DATA_FLAG_ACCESS_READ | TEE_DATA_FLAG_ACCESS_WRITE
        | TEE_DATA_FLAG_ACCESS_WRITE_META | TEE_DATA_FLAG_OVERWRITE;
    res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE, cli_id,
            strlen(cli_id), obj_data_flag, TEE_HANDLE_NULL, NULL, 0, &object);
    if (res != TEE_SUCCESS)
        return 1;
    res = TEE_WriteObjectData(object, cli_key, strlen(cli_key));
    if (res != TEE_SUCCESS)
    {
        TEE_CloseAndDeletePersistentObject1(object);
        return 1;
    }
    TEE_CloseObject(object);
    return 0;
}

static int get_key(char *cli_id, char *cli_key, int key_mode)
{
    // TODO Implement Cache Logic
    char fke_key[TA_AES_KEY_SIZE + 1] = "11111111111111111111111111111111";
    char my_id[TA_MQTTZ_CLI_ID_SZ + 1];
    strncpy(my_id, cli_id, TA_MQTTZ_CLI_ID_SZ);
    my_id[TA_MQTTZ_CLI_ID_SZ] = '\0';
    printf("My ID: %s\n", my_id);
    size_t read_bytes;
    int res;
    if (key_mode == 0)
        goto keyinmem;
    //if ((read_raw_object(cli_id, strlen(cli_id), cli_key, read_bytes) 
    if ((read_raw_object(my_id, TA_MQTTZ_CLI_ID_SZ, cli_key, read_bytes) 
            != TEE_SUCCESS))// || (read_bytes != TA_AES_KEY_SIZE))
    {
        printf("MQTTZ: Key not found! Saving it to persistent storage.\n");
        res = save_key(my_id, cli_key);
        if (res != 0)
        {
            printf("MQTTZ: Error saving key to persistent storage.\n");
            printf(" Using a fake one...\n");
            goto keyinmem;
        }
        printf("MQTTZ: Succesfully stored key in SS!\n");
    }
    return 0;
keyinmem:
    strcpy(cli_key, fke_key);
    cli_key[TA_AES_KEY_SIZE] = '\0';
    return 0;
}

static TEE_Result cache_benchmarking(void *session, uint32_t param_types,
        TEE_Param params[4])
{
    TEE_Result res;
    TEE_Time t1, t2, t_aux;
    char fke_key[TA_AES_KEY_SIZE + 1] = "11111111111111111111111111111111";
    uint32_t exp_param_types = TEE_PARAM_TYPES(
            TEE_PARAM_TYPE_NONE,
            TEE_PARAM_TYPE_NONE,
            TEE_PARAM_TYPE_NONE,
            TEE_PARAM_TYPE_NONE);
    if (param_types != exp_param_types)
        return TEE_ERROR_BAD_PARAMETERS;
    Queue *queue = init_queue(CACHE_SIZE);
    Hash *hash = init_hash(TOTAL_ELEMENTS);
    printf("Initialized Queue!\n");
    print_cache_status(hash);
    cache_query(hash, queue, "000000000000");
    print_cache_status(hash);
    cache_query(hash, queue, "000000000001");
    print_cache_status(hash);
    cache_query(hash, queue, "000000000002");
    print_cache_status(hash);
    cache_query(hash, queue, "000000000003");
    print_cache_status(hash);
    free_queue(queue);
    free_hash(hash);
    res = TEE_SUCCESS;
    return res;
}

/*
static TEE_Result payload_reencryption(void *session, uint32_t param_types,
        TEE_Param params[4])
{
    TEE_Result res;
    TEE_Time t1, t2;
    TEE_Time t_aux;
    uint32_t exp_param_types = TEE_PARAM_TYPES(
            TEE_PARAM_TYPE_MEMREF_INPUT,
            TEE_PARAM_TYPE_MEMREF_INOUT,
            TEE_PARAM_TYPE_MEMREF_INOUT,
            TEE_PARAM_TYPE_VALUE_INPUT);
    if (param_types != exp_param_types)
        return TEE_ERROR_BAD_PARAMETERS;
    printf("MQTTZ: Entered SW\n");
    // TODO
    // 1. Decrypt from Origin
    // char *ori_cli_id;
    // char *ori_cli_iv;
    //char *ori_cli_data;
    //char *tmp_buffer;
    //tmp_buffer = (char *) TEE_Malloc(sizeof *tmp_buffer
    //        * (100 + 1), 0);
    size_t data_size = params[0].memref.size - TA_MQTTZ_CLI_ID_SZ 
            - TA_AES_IV_SIZE;
    // ori_cli_id = (char *) TEE_Malloc(sizeof *ori_cli_id 
    //        * (TA_MQTTZ_CLI_ID_SZ + 1), 0);
    //ori_cli_iv = (char *) TEE_Malloc(sizeof *ori_cli_iv 
    //        * (TA_AES_IV_SIZE + 1), 0);
    //ori_cli_data = (char *) TEE_Malloc(sizeof *ori_cli_data 
    //        * (TA_MQTTZ_MAX_MSG_SZ + 1), 0);
    //if (!(ori_cli_id && ori_cli_iv && ori_cli_data))
    //if (!(ori_cli_iv))
    //{
    //    res = TEE_ERROR_OUT_OF_MEMORY;
    //    goto exit;
    //}
    printf("MQTTZ: Allocated input args\n");
    //TEE_MemMove(ori_cli_id, (char *) params[0].memref.buffer,
    //        TA_MQTTZ_CLI_ID_SZ);
    //ori_cli_id[TA_MQTTZ_CLI_ID_SZ] = '\0';
    //TEE_MemMove(ori_cli_iv, (char *) params[0].memref.buffer
    //  + TA_MQTTZ_CLI_ID_SZ, TA_AES_IV_SIZE);
    //ori_cli_iv[TA_AES_IV_SIZE] = '\0';
    //TEE_MemMove(ori_cli_data, (char *) params[0].memref.buffer 
    //        + TA_MQTTZ_CLI_ID_SZ + TA_AES_IV_SIZE, data_size);
    //ori_cli_data[data_size] = '\0';
    printf("MQTTZ: Loaded Values\n");
    //printf("\t- Cli id: %s\n", ori_cli_id);
    //printf("\t- Cli iv: %s\n", ori_cli_iv);
    //printf("\t- Cli data: %s\n", ori_cli_data);
    // 2. Read key from secure storage
    TEE_GetSystemTime(&t1);
    char *ori_cli_key;
    ori_cli_key = (char *) TEE_Malloc(sizeof *ori_cli_key 
            * (TA_AES_KEY_SIZE + 1), 0);
    printf("MQTTZ: Allocated Origin Cli Key\n");
    //if (get_key(ori_cli_id, ori_cli_key, params[3].value.a) != 0)
    if (get_key((char *) params[0].memref.buffer, ori_cli_key,
                params[3].value.a) != 0)
    {
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto exit;
    }
    printf("MQTTZ: Got Origin Key! %s\n", ori_cli_key);
    TEE_GetSystemTime(&t2);
    TEE_TIME_SUB(t2, t1, t_aux);
    //sprintf(params[2].memref.buffer, "%s%i", params[2].memref.buffer, t_aux.seconds * 1000 + t_aux.millis);
    //snprintf(tmp_buffer, 100, "%s%i,", tmp_buffer,
    //        t_aux.seconds * 1000 + t_aux.millis);
    snprintf((char *) params[2].memref.buffer, 100, "%s%i,", 
             (char *) params[2].memref.buffer,
             t_aux.seconds * 1000 + t_aux.millis);
    TEE_GetSystemTime(&t1);
    // 2. Decrypt Inbound Traffic w/ Origin Key
    // FIXME FIXME FIXME
    //if (alloc_resources(session, TA_AES_MODE_DECODE) != TEE_SUCCESS)
    if (alloc_resources(session, TA_AES_MODE_ENCODE) != TEE_SUCCESS)
    {
        res = TEE_ERROR_GENERIC;
        goto exit;
    }
    printf("MQTTZ: Initialized AES Session!\n");
    if (set_aes_key(session, ori_cli_key) != TEE_SUCCESS)
    {
        printf("MQTTZ-ERROR: set_aes_key failed\n");
        res = TEE_ERROR_GENERIC;
        TEE_Free((void *) ori_cli_key);
        goto exit;
    }
    TEE_Free((void *) ori_cli_key);
    //if (set_aes_iv(session, ori_cli_iv) != TEE_SUCCESS)
    if (set_aes_iv(session, (char *) params[0].memref.buffer +
            TA_MQTTZ_CLI_ID_SZ) != TEE_SUCCESS)
    {
        printf("MQTTZ-ERROR: set_aes_iv failed\n");
        res = TEE_ERROR_GENERIC;
        goto exit;
    }
    char *dec_data;
    size_t dec_data_size = TA_MQTTZ_MAX_MSG_SZ;
    dec_data = (char *) TEE_Malloc(sizeof *dec_data * dec_data_size, 0);
    if (!dec_data)
    {
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto exit;
    }
    printf("MQTTZ: Allocated decrypted data!\n");
    // FIXME This is gonna fail, most likely
//    if (cipher_buffer(session, ori_cli_data, data_size, dec_data, 
    if (cipher_buffer(session,
        (char *) params[0].memref.buffer + TA_MQTTZ_CLI_ID_SZ + TA_AES_IV_SIZE,
        data_size, dec_data, &dec_data_size) != TEE_SUCCESS)
    {
        res = TEE_ERROR_GENERIC;
        goto exit;
    }
    printf("MQTTZ: Finished decrypting, now we encrypt with the other key!\n");
    //printf("MQTTZ: Decrypted data: %s\n", dec_data);
    TEE_GetSystemTime(&t2);
    TEE_TIME_SUB(t2, t1, t_aux);
    //sprintf(params[2].memref.buffer, "%s%i", params[2].memref.buffer, t_aux.seconds * 1000 + t_aux.millis);
    //snprintf(tmp_buffer, 100, "%s%i,", tmp_buffer,
    //        t_aux.seconds * 1000 + t_aux.millis);
    snprintf((char *) params[2].memref.buffer, 100, "%s%i,", 
             (char *) params[2].memref.buffer,
             t_aux.seconds * 1000 + t_aux.millis);
    // 3. Encrypt outbound traffic with destination key
    //TEE_Free((void *) ori_cli_id);
    //TEE_Free((void *) ori_cli_iv);
    //TEE_Free((void *) ori_cli_data);
    //TEE_Free((void *) ori_cli_key);
    printf("MQTTZ: Freed previous resources we don't need anymore.\n");
    //char *dest_cli_id;
    char *dest_cli_iv;
    //char *dest_cli_data;
    //dest_cli_id = (char *) TEE_Malloc(sizeof *dest_cli_id 
    //        * (TA_MQTTZ_CLI_ID_SZ + 1), 0);
    dest_cli_iv = (char *) TEE_Malloc(sizeof *dest_cli_iv 
            * (TA_AES_IV_SIZE + 1), 0);
    //dest_cli_data = (char *) TEE_Malloc(sizeof *dest_cli_data 
    //        * (TA_MQTTZ_MAX_MSG_SZ + 1), 0);
    if (!(dest_cli_iv))
    {
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto exit;
    }
    printf("MQTTZ: Allocated Destination Cli Data. \n");
    //TEE_MemMove(dest_cli_id, (char *) params[1].memref.buffer,
    //        TA_MQTTZ_CLI_ID_SZ);
    // 4. Get Destination Client Key from Secure Storage
    TEE_GetSystemTime(&t1);
    char *dest_cli_key;
    dest_cli_key = (char *) TEE_Malloc(sizeof *dest_cli_key
            * (TA_AES_KEY_SIZE + 1), 0);
    printf("MQTTZ: Allocated Destination Cli Key\n");
    //if (get_key(dest_cli_id, dest_cli_key, (int) params[3].value.a) != 0)
    if (get_key((char *) params[1].memref.buffer, dest_cli_key,
                (int) params[3].value.a) != 0)
    {
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto exit;
    }
    printf("MQTTZ: Got Destination Key! %s\n", dest_cli_key);
    TEE_GetSystemTime(&t2);
    TEE_TIME_SUB(t2, t1, t_aux);
    snprintf((char *) params[2].memref.buffer, 100, "%s%i,", 
             (char *) params[2].memref.buffer,
             t_aux.seconds * 1000 + t_aux.millis);
    //snprintf(tmp_buffer, 100, "%s%i,", tmp_buffer,
    //        t_aux.seconds * 1000 + t_aux.millis);
    TEE_GetSystemTime(&t1);
    // FIXME 
    //if (alloc_resources(session, TA_AES_MODE_ENCODE) != TEE_SUCCESS)
    if (alloc_resources(session, TA_AES_MODE_DECODE) != TEE_SUCCESS)
    {
        res = TEE_ERROR_GENERIC;
        goto exit;
    }
    printf("MQTTZ: Initialized AES ENCODE Session!\n");
    if (set_aes_key(session, dest_cli_key) != TEE_SUCCESS)
    {
        printf("MQTTZ-ERROR: set_aes_key failed\n");
        res = TEE_ERROR_GENERIC;
        goto exit;
    }
    printf("MQTTZ: Set Destination Key in Session\n");
    // Set random IV for encryption TODO
    char fake_iv[TA_AES_IV_SIZE + 1] = "1111111111111111";
    strcpy(dest_cli_iv, fake_iv);
    printf("This is the initial IV: %s\n", dest_cli_iv);
    if (set_aes_iv(session, dest_cli_iv) != TEE_SUCCESS)
    {
        printf("MQTTZ-ERROR: set_aes_iv failed\n");
        res = TEE_ERROR_GENERIC;
        goto exit;
    }
    size_t enc_data_size = TA_MQTTZ_MAX_MSG_SZ;
    if (cipher_buffer(session, dec_data, dec_data_size, 
        (char *) params[1].memref.buffer + TA_MQTTZ_CLI_ID_SZ + TA_AES_IV_SIZE, 
        &enc_data_size) != TEE_SUCCESS)
    {
        printf("MQTTZ-ERROR: Error in cipher_buffer Encrypting!\n");
        res = TEE_ERROR_GENERIC;
        goto exit;
    }
    printf("MQTTZ: Finished encrypting!\n");
    //printf("MQTTZ: Encrypted Data: %s\n", dest_cli_data);
    printf("MQTTZ: This is the final IV: %s\n", dest_cli_iv);
    TEE_GetSystemTime(&t2);
    TEE_TIME_SUB(t2, t1, t_aux);
    //sprintf(params[2].memref.buffer, "%s%i", params[2].memref.buffer, t_aux.seconds * 1000 + t_aux.millis);
    snprintf((char *) params[2].memref.buffer, 100, "%s%i,", 
             (char *) params[2].memref.buffer,
             t_aux.seconds * 1000 + t_aux.millis);
    //printf("MQTTZ: Time: %i\n%s\n", t2.seconds * 1000 + t2.millis, tmp_buffer);
    //printf("MQTTZ: Time elapsed: %i\n", jeje.seconds * 1000 + jeje.millis); 
    // Rebuild the return value
    strcpy((char *) params[1].memref.buffer + TA_MQTTZ_CLI_ID_SZ, dest_cli_iv);
    //strcpy((char *) params[1].memref.buffer + TA_MQTTZ_CLI_ID_SZ 
    //        + TA_AES_IV_SIZE, dest_cli_data);
    //strcpy((char *) params[2].memref.buffer, tmp_buffer);
    res = TEE_SUCCESS;
    //printf("This fails?\n");
    //TEE_Free((void *) dest_cli_id);
    TEE_Free((void *) dest_cli_iv);
    //TEE_Free((void *) dest_cli_data);
    TEE_Free((void *) dest_cli_key);
    TEE_Free((void *) dec_data);
    goto exit;
exit:
    return res;
}*/

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
	/* Nothing to do */
	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void __unused *session)
{
	/* Nothing to do */
}

TEE_Result TA_InvokeCommandEntryPoint(void __unused *session,
				      uint32_t command,
				      uint32_t param_types,
				      TEE_Param params[4])
{
	switch (command) {
        case TA_CACHE_BENCHMARK:
            return cache_benchmarking(session, param_types, params);
	default:
		EMSG("Command ID 0x%x is not supported", command);
		return TEE_ERROR_NOT_SUPPORTED;
	}
}

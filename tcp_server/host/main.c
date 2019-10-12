#include <arpa/inet.h>
#include <err.h>
#include <math.h>
#include <netinet/in.h> 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h> 
#include <sys/time.h>
#include <unistd.h>

/* OP-TEE TEE client API (built by optee_client) */
#include <tee_client_api.h>

/* TA API: UUID and command IDs */
#include <tcp_server_ta.h>

/* TEE resources */
struct test_ctx {
	TEEC_Context ctx;
	TEEC_Session sess;
};

#define BUFFER_SIZE 1024
#define MAX         80
#define PORT        9999
#define REMOTE_IP   "10.0.2.2"

void prepare_tee_session(struct test_ctx *ctx)
{
	TEEC_UUID uuid = TA_TCP_SERVER_UUID;
	uint32_t origin;
	TEEC_Result res;

	/* Initialize a context connecting us to the TEE */
	res = TEEC_InitializeContext(NULL, &ctx->ctx);
	if (res != TEEC_SUCCESS)
		errx(1, "TEEC_InitializeContext failed with code 0x%x", res);

	/* Open a session with the TA */
	res = TEEC_OpenSession(&ctx->ctx, &ctx->sess, &uuid,
			       TEEC_LOGIN_PUBLIC, NULL, NULL, &origin);
	if (res != TEEC_SUCCESS)
		errx(1, "TEEC_Opensession failed with code 0x%x origin 0x%x",
			res, origin);
}

void terminate_tee_session(struct test_ctx *ctx)
{
	TEEC_CloseSession(&ctx->sess);
	TEEC_FinalizeContext(&ctx->ctx);
}

void ree_tcp_server(int sockfd, char *buffer)
{
    int n;
    // Infinite Loop
    for (;;) {
        memset(buffer, '\0', BUFFER_SIZE);
        n = 0;
        // Read message from client and copy it in buffer
        read(sockfd, buffer, sizeof(buffer));
        printf("From client: %s\t\nTo client:", buffer);
        memset(buffer, '\0', BUFFER_SIZE);
        // Read input and send message to client
        while ((buffer[n++] = getchar()) != '\n');
        write (sockfd, buffer, sizeof buffer);
        if (strncmp("exit", buffer, 4) == 0) {
            printf("Server Exit...\n");
            break;
        }
    }
}

int ree_tcp_socket(char *buffer)
{
    int server_fd, new_socket, valread;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    // Creating Socket File Descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket Failed");
        return 1;
    }
    // Attaching to port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                                                            sizeof(opt))) {
        perror("Set Socket Options Failed");
        return 1;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons( PORT );
    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
        perror("Bind Failed");
        return 1;
    }
    if (listen(server_fd, 3) < 0) {
        perror("Listen Failed");
        return 1;
    }
    if ((new_socket = accept(server_fd, (struct sockaddr *) &address,
                    (socklen_t *) &addrlen)) < 0) {
        perror("Accept Failed");
        return 1;
    }
    // Chatting between server and client
    ree_tcp_server(new_socket, buffer);
    // Close the socket when finished
    close(server_fd);
    return 0;
}

TEEC_Result tee_tcp_socket(struct test_ctx *ctx, buffer)
{
    TEEC_Operation op;
    TEEC_Result res;
    uint32_t ori;

    memset(&op, 0 sizeof op);
    op.paramTypes = TEEC_PARAM_TYPES(
            TEEC_MEMREF_TEMP_INPUT,
            TEEC_NONE,
            TEEC_NONE,
            TEEC_NONE);
    
    strcpy(buffer, "Hello World!");
    op.params[0].tmpref.buffer = buffer;
    op.params[0].tmpref.size = BUFFER_SIZE;

    res = TEEC_InvokeCommand(&ctx->sess, TA_TCP_SOCKET, &op, &ori);

    return res;
}

int main(int argc, char *argv[])
{
	struct test_ctx ctx;
    struct timeval t1, t2;
	TEEC_Result res;

    // Dummy TEE Context to check if all files are OK
	prepare_tee_session(&ctx);

    char *buffer; 
    buffer = (char *) malloc(sizeof buffer * BUFFER_SIZE);
    memset(buffer, '\0', sizeof buffer * BUFFER_SIZE);
    if (ree_tcp_socket(buffer) != 0)
    {
        printf("REE TCP Socket failure!\n");
        return 1;
    }

    // Terminate Dummy TEE Context
	terminate_tee_session(&ctx);
	return 0;
}

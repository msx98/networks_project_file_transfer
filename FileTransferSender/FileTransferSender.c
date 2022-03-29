// FileTransferSender.c : This file contains the 'main' function. Program execution begins and ends there.
//

#include "FileTransferCommon/common.h"

int socket_send_file(const SOCKET* sock, const char* file_name, uint64_t* file_size, uint64_t* file_total_sent) {
    unsigned char buf_send[4], buf_hold[1], buf_encode[4], buf_read[4], buf_send_enc[31];
    uint8_t buf_raw[26], buf_enc[31];
    uint8_t buf_enc_tmp[31];
    uint64_t file_size_left;
    errno_t err;
    FILE* fp;

    *file_size       = 0;
    *file_total_sent = 0;

    if (fopen_s(&fp, file_name, "r") != 0) {
        return STATUS_ERR_FILE_READ;
    }

    fseek(fp, 0, SEEK_END);
    *file_size = ftell(fp); // bytes
    // first byte will tell us how many bytes were added to the actual data
    // next 8 bytes will tell us the size of the transmission
    fseek(fp, 0, SEEK_SET);

    uint8_t bytes_missing_for_26 = (26 - ((*file_size) % 26)) % 26;
    //if (((*file_size) % 26) == 0) bytes_missing_for_26 = 0;
    //else                          bytes_missing_for_26 = 26 - ((*file_size) % 26);
    //int bytes_missing_for_26 = (26 - (1+8+(*(file_size)) % 26)) % 26;
    printf("bytes_missing_for_26=%d\n", bytes_missing_for_26);
    int total_zeros_added = bytes_missing_for_26;
    printf("total bytes added: %d\n", total_zeros_added);
    int buf_size = (*file_size) + 26+total_zeros_added;
    
    // at this point, we know what the transmission size will be
    // raw=26m bytes = 8*26m bits, so m=raw.bytes/26=buf_size/26, encoded=8*31m bits = 31m bytes
    if (floor(buf_size / 26) != ceil(buf_size / 26)) return STATUS_ERR_BUF_SIZE;
    uint64_t m = buf_size / 26;
    uint64_t expected_transmission_size = 31 * m;

    int total_bytes_added_not_sent = expected_transmission_size;
    uint64_t bytes_not_sent = expected_transmission_size;

    /////////////////
    // send header
    /////////////////
    buf_raw[0] = ((uint64_t)(0xFF)) & ((uint64_t)total_zeros_added);
    buf_raw[1] = ((uint64_t)(0xFF)) & (expected_transmission_size >> (8 * 7));
    buf_raw[2] = ((uint64_t)(0xFF)) & (expected_transmission_size >> (8 * 6));
    buf_raw[3] = ((uint64_t)(0xFF)) & (expected_transmission_size >> (8 * 5));
    buf_raw[4] = ((uint64_t)(0xFF)) & (expected_transmission_size >> (8 * 4));
    buf_raw[5] = ((uint64_t)(0xFF)) & (expected_transmission_size >> (8 * 3));
    buf_raw[6] = ((uint64_t)(0xFF)) & (expected_transmission_size >> (8 * 2));
    buf_raw[7] = ((uint64_t)(0xFF)) & (expected_transmission_size >> (8 * 1));
    buf_raw[8] = ((uint64_t)(0xFF)) & (expected_transmission_size >> (8 * 0));
    for (int i = 9; i < 26; i++) buf_raw[i] = 0;
    encode_26_block_to_31(buf_enc, buf_raw);
    safe_send(sock, buf_enc, 31);
    bytes_not_sent -= 31;

    /////////////////////////////////////////
    // send everything except the last block
    /////////////////////////////////////////
    printf("raw: \n");
    while (bytes_not_sent > 31) {
        fread(buf_raw, sizeof(uint8_t), 26, fp);
        for (int i=0; i<26; i++) printf("%02x ", buf_raw[i]);
        printf("\n");
        encode_26_block_to_31(buf_enc, buf_raw);
        safe_send(sock, buf_enc, 31);
        bytes_not_sent -= 31;
    }

    ///////////////////////////////////////////////////
    // send the last block padded by zeros from the end
    ///////////////////////////////////////////////////
    if (bytes_not_sent > 0) {
        fread(buf_raw, sizeof(uint8_t), (uint8_t)26 - bytes_missing_for_26, fp);
        for (int i = (26-bytes_missing_for_26); i < 26; i++) buf_raw[i] = 0;
        for (int i = 0; i < 26; i++) printf("%02x ", buf_raw[i]);
        printf("\n");
        encode_26_block_to_31(buf_enc, buf_raw);
        safe_send(sock, buf_enc, 31);
        bytes_not_sent -= 31;
    }
    printf("\n");
    printf("closed\n");

    fclose(fp);
    return 0;
}

int main(const int argc, const char *argv[])
{
    char*   remote_addr;
    u_short remote_port;
    SOCKET sock;
    WSADATA wsaData;
    int status;

    char file_name[MAX_PERMITTED_FILE_PATH_LENGTH];
    uint64_t file_size, file_total_sent;

    if (argc != 3) {
        remote_addr = CHANNEL_ADDR;
        remote_port = CHANNEL_PORT_SENDER;
        printd("WARNING: proper syntax is as follows:\n");
        printd("         %s IP PORT\n", argv[0]);
        printd("         an invalid number of arguments was specified, so using IP=%s, PORT=%d\n", remote_addr, remote_port);
    } else {
        remote_addr = argv[1];
        remote_port = (u_short) atoi(argv[2]);
    }

    if (socket_initialize(&wsaData) != NO_ERROR) {
        printf(MSG_ERR_WSASTARTUP);
        return 1;
    }

    printd("Attempting connection to %s:%d\n", remote_addr, remote_port);
    status = socket_connect(&sock, remote_addr, remote_port);
    if (status == SOCKET_ERROR) {
        printf(MSG_ERR_CONNECTING, status, remote_addr, remote_port);
#if FLAG_IGNORE_SOCKET != 1
        return 1;
#endif
    }
    printd("Connected to %s:%d\n", remote_addr, remote_port);


    while (1) {
        printf(MSG_ENTER_FILENAME);
#if FLAG_SKIP_FILENAME==1
        strncpy_s(file_name, 100, DEBUG_FILE_PATH, strlen(DEBUG_FILE_PATH));
#else
        scanf_s("%s", file_name, MAX_PERMITTED_FILE_PATH_LENGTH);
        if (strcmp(file_name, "quit") == 0) {
            return 0;
        }
#endif

        printd("Sending file...\n");
        status = socket_send_file(sock, file_name, &file_size, &file_total_sent);
        switch (status) {
            case STATUS_SUCCESS: {
                printf(MSG_FILE_LENGTH, file_size);
                printf(MSG_TOTAL_SENT,  file_total_sent);
                break;
            }
            case STATUS_ERR_FILE_READ: {
                printf(MSG_ERR_FILE_READ, file_name);
                break;
            }
            case STATUS_ERR_MALLOC_BUF: {
                printf(MSG_ERR_MALLOC_BUF, file_size);
                break;
            }
            case STATUS_ERR_MALLOC_BUF_ENC: {
                printf(MSG_ERR_MALLOC_BUF_ENC, file_total_sent);
                break;
            }
            case STATUS_ERR_BUF_SIZE: {
                printf(MSG_ERR_BUF_SIZE, file_size);
            }
            default: {
                printf(MSG_ERR_UNKNOWN, status);
                break;
            }
        }
#if FLAG_SINGLE_ITER==1
        break;
#endif
    }

    return 0;
}
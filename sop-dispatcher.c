#include "sop-common.h"

void usage(char *name) { fprintf(stderr, "USAGE: %s port \n", name); }

#define MAX_MSG_LEN 512
#define NAME_SIZE 16
#define BOARD_SIZE 100

int validate_and_parse_spells(char *colon_part2) {
    char copy[MAX_MSG_LEN];
    strncpy(copy, colon_part2, MAX_MSG_LEN - 1);
    copy[MAX_MSG_LEN - 1] = '\0';

    char *saveptr2 = NULL;
    char *saveptr3 = NULL;
    char *spell_token = strtok_r(copy, ";", &saveptr2);
    
    if (!spell_token) {
        return 0;
    }

    while (spell_token) {
        char *spell_name = strtok_r(spell_token, " \t\r\n-", &saveptr3);
        char *x_str = strtok_r(NULL, " \t\r\n,", &saveptr3);
        char *y_str = strtok_r(NULL, " \t\r\n", &saveptr3);

        if (!spell_name || !x_str || !y_str) {
            return 0;
        }

        int x = atoi(x_str);
        int y = atoi(y_str);

        if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
            return 0;
        }

        spell_token = strtok_r(NULL, ";", &saveptr2);
    }

    return 1;
}

void print_spells(char *colon_part2) {
    char copy[MAX_MSG_LEN];
    strncpy(copy, colon_part2, MAX_MSG_LEN - 1);
    copy[MAX_MSG_LEN - 1] = '\0';

    char *saveptr2 = NULL;
    char *saveptr3 = NULL;
    char *spell_token = strtok_r(copy, ";", &saveptr2);

    while (spell_token) {
        char *spell_name = strtok_r(spell_token, " \t\r\n-", &saveptr3);
        char *x_str = strtok_r(NULL, " \t\r\n,", &saveptr3);
        char *y_str = strtok_r(NULL, " \t\r\n", &saveptr3);

        int x = atoi(x_str);
        int y = atoi(y_str);

        printf("  -> Spell: %s at [%d, %d]\n", spell_name, x, y);
        spell_token = strtok_r(NULL, ";", &saveptr2);
    }
}

int process_message(char *buffer) {
    char *saveptr1 = NULL;
    char *saveptr2 = NULL;

    char *colon_part1 = strtok_r(buffer, ":", &saveptr1);
    char *colon_part2 = strtok_r(NULL, ":", &saveptr1);

    if (!colon_part1 || !colon_part2) {
        fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
        return 0;
    }

    char *cmd = strtok_r(colon_part1, " \t\r\n", &saveptr2);
    char *priority_str = strtok_r(NULL, " \t\r\n", &saveptr2);
    char *mage_name = strtok_r(NULL, " \t\r\n", &saveptr2);

    if (!cmd || strcmp(cmd, "SUBMIT") != 0 || !priority_str || !mage_name || strlen(mage_name) > 16) {
        fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
        return 0;
    }

    int priority = atoi(priority_str);
    if (priority < 1 || priority > 5) {
        fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
        return 0;
    }

    if (!validate_and_parse_spells(colon_part2)) {
        fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
        return 0;
    }

    printf("[Received] Priority %d from %s.\n", priority, mage_name);
    print_spells(colon_part2);

    return 1;
}


void server_work(int fd){
    int valid_messages_count = 0;
    char buffer[MAX_MSG_LEN];
    

    while (valid_messages_count < 5) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        ssize_t n = TEMP_FAILURE_RETRY(recvfrom(fd, buffer, sizeof(buffer)-1, 0, (struct sockaddr *)&client_addr, &client_len ));

        if(n<0) ERR("recvfrom");

        buffer[n]='\0';

        if (process_message(buffer)) {
            valid_messages_count++;
        }
    }
}


int main(int argc, char **argv){
    if(argc!=2){
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    uint16_t port = atoi(argv[1]);

    int fd=bind_inet_socket(port, SOCK_DGRAM, 10);


    server_work(fd);

    if(close(fd)<0) ERR("close");
    return EXIT_SUCCESS;
}
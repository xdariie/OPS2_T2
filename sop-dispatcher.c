#include "sop-common.h"

void usage(char *name) { fprintf(stderr, "USAGE: %s port \n", name); }

#define MAX_MSG_LEN 512
#define NAME_SIZE 16
#define BOARD_SIZE 100

typedef struct spell_node {
    char name[32];
    int x;
    int y;
    struct spell_node *next;
} spell_node_t;

typedef struct request_node {
    int priority;
    char mage_name[32];
    spell_node_t *spells_head;
    struct request_node *next;
} request_node_t;

// Global Queue Pointers and Mutex
request_node_t *queue_head = NULL;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

/* --- FUNCTION DEFINITIONS --- */



// 1. Socket Setup Helper
int make_udp_socket(int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) ERR("socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) ERR("bind");
    return fd;
}

// 2. Queue Insertion Logic
void insert_into_priority_queue(request_node_t *new_req) {
    pthread_mutex_lock(&queue_mutex);

    if (queue_head == NULL || new_req->priority < queue_head->priority) {
        new_req->next = queue_head;
        queue_head = new_req;
    } else {
        request_node_t *current = queue_head;
        while (current->next != NULL && current->next->priority <= new_req->priority) {
            current = current->next;
        }
        new_req->next = current->next;
        current->next = new_req;
    }

    pthread_mutex_unlock(&queue_mutex);
}

// 3. Memory Cleanup Helper
void free_spells_list(spell_node_t *head) {
    while (head != NULL) {
        spell_node_t *temp = head;
        head = head->next;
        free(temp);
    }
}

// 4. Header Parser (Validates "SUBMIT Priority Name")
request_node_t* parse_request_header(char *header_str) {
    char *saveptr;
    char *cmd = strtok_r(header_str, " \t\r\n", &saveptr);
    char *prio_str = strtok_r(NULL, " \t\r\n", &saveptr);
    char *name = strtok_r(NULL, " \t\r\n", &saveptr);

    if (!cmd || strcmp(cmd, "SUBMIT") != 0 || !prio_str || !name || strlen(name) > 16) {
        return NULL; // Invalid format
    }

    int priority = atoi(prio_str);
    if (priority < 1 || priority > 5) {
        return NULL; // Invalid priority bounds
    }

    request_node_t *req = malloc(sizeof(request_node_t));
    if (!req) ERR("malloc");
    
    req->priority = priority;
    strncpy(req->mage_name, name, 31);
    req->mage_name[31] = '\0';
    req->spells_head = NULL;
    req->next = NULL;

    return req;
}

// 5. Spells Parser (Builds the inner linked list of spells)
spell_node_t* parse_spells_list(char *spells_str) {
    char *saveptr_sc;
    char *spell_token = strtok_r(spells_str, ";", &saveptr_sc);
    
    if (!spell_token) return NULL; // No spells found

    spell_node_t *head = NULL;
    spell_node_t *tail = NULL;

    while (spell_token) {
        char *saveptr_hy;
        char *spell_name = strtok_r(spell_token, " \t\r\n-", &saveptr_hy);
        char *x_str = strtok_r(NULL, " \t\r\n,", &saveptr_hy);
        char *y_str = strtok_r(NULL, " \t\r\n", &saveptr_hy);

        if (!spell_name || !x_str || !y_str) {
            free_spells_list(head); 
            return NULL; 
        }

        int x = atoi(x_str);
        int y = atoi(y_str);

        if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
            free_spells_list(head); 
            return NULL;
        }

        spell_node_t *new_spell = malloc(sizeof(spell_node_t));
        if (!new_spell) ERR("malloc");
        
        strncpy(new_spell->name, spell_name, 31);
        new_spell->name[31] = '\0';
        new_spell->x = x;
        new_spell->y = y;
        new_spell->next = NULL;

        if (head == NULL) {
            head = new_spell;
            tail = new_spell;
        } else {
            tail->next = new_spell;
            tail = new_spell;
        }

        spell_token = strtok_r(NULL, ";", &saveptr_sc);
    }

    return head;
}

// 6. Thread Worker (Acts as the bridge calling the parsers)
void *worker_thread(void *arg) {
    pthread_detach(pthread_self());
    char *buffer = (char *)arg;
    char *saveptr;

    // Split at the colon
    char *colon_part1 = strtok_r(buffer, ":", &saveptr);
    char *colon_part2 = strtok_r(NULL, ":", &saveptr);

    if (!colon_part1 || !colon_part2) {
        fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
        free(buffer);
        return NULL;
    }

    // Parse the Header
    request_node_t *req = parse_request_header(colon_part1);
    if (req == NULL) {
        fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
        free(buffer);
        return NULL;
    }

    // Parse the Spells
    req->spells_head = parse_spells_list(colon_part2);
    if (req->spells_head == NULL) {
        fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
        free(req);
        free(buffer);
        return NULL;
    }

    // Success! Queue it up.
    insert_into_priority_queue(req);
    printf("[Queued] %s's request safely stored.\n", req->mage_name);

    free(buffer);
    return NULL;
}

// 7. Core Server Loop
void run_server(int fd) {
    char temp_buffer[MAX_MSG_LEN];

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        ssize_t n = TEMP_FAILURE_RETRY(recvfrom(fd, temp_buffer, sizeof(temp_buffer) - 1, 0, (struct sockaddr *)&client_addr, &client_len));
        if (n < 0) ERR("recvfrom");
        temp_buffer[n] = '\0';

        char *thread_buffer = malloc(n + 1);
        if (!thread_buffer) ERR("malloc");
        strcpy(thread_buffer, temp_buffer);

        pthread_t tid;
        if (pthread_create(&tid, NULL, worker_thread, thread_buffer) != 0) {
            ERR("pthread_create");
        }
    }
}

/* --- MAIN --- */

int main(int argc, char **argv) {
    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    int fd = make_udp_socket(port);

    run_server(fd);

    if (close(fd) < 0) ERR("close");
    pthread_mutex_destroy(&queue_mutex);

    return EXIT_SUCCESS;
}
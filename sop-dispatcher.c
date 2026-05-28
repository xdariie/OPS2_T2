#include "sop-common.h"

void usage(char *name) { fprintf(stderr, "USAGE: %s port \n", name); }


#define BOARD_SIZE 100
#define MAX_MSG_LEN 512

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

typedef struct {
    request_node_t *head;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
} priority_queue_t;

typedef struct {
    char *buffer;
    priority_queue_t *pq;
} thread_arg_t;


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

void insert_into_priority_queue(priority_queue_t *pq, request_node_t *new_req) {
    pthread_mutex_lock(&pq->mutex);

    if (pq->head == NULL || new_req->priority < pq->head->priority) {
        new_req->next = pq->head;
        pq->head = new_req;
    } else {
        request_node_t *current = pq->head;
        while (current->next != NULL && current->next->priority <= new_req->priority) {
            current = current->next;
        }
        new_req->next = current->next;
        current->next = new_req;
    }

    pthread_cond_signal(&pq->not_empty);
    pthread_mutex_unlock(&pq->mutex);
}

// Stage 4: Revoke Logic
void revoke_spell(priority_queue_t *pq, const char *mage_name, const char *spell_name) {
    pthread_mutex_lock(&pq->mutex);

    request_node_t *curr_req = pq->head;
    request_node_t *prev_req = NULL;

    while (curr_req != NULL) {
        if (strcmp(curr_req->mage_name, mage_name) == 0) {
            
            spell_node_t *curr_spell = curr_req->spells_head;
            spell_node_t *prev_spell = NULL;

            while (curr_spell != NULL) {
                if (strcmp(curr_spell->name, spell_name) == 0) {
                    
                    if (prev_spell == NULL) {
                        curr_req->spells_head = curr_spell->next;
                    } else {
                        prev_spell->next = curr_spell->next;
                    }
                    free(curr_spell);

                    printf("[Revoked] %s cancelled for %s.\n", spell_name, mage_name);

                    if (curr_req->spells_head == NULL) {
                        if (prev_req == NULL) {
                            pq->head = curr_req->next;
                        } else {
                            prev_req->next = curr_req->next;
                        }
                        free(curr_req);
                    }

                    pthread_mutex_unlock(&pq->mutex);
                    return; 
                }
                prev_spell = curr_spell;
                curr_spell = curr_spell->next;
            }
        }
        prev_req = curr_req;
        curr_req = curr_req->next;
    }

    printf("[Too Late] The spell was already cast.\n");
    pthread_mutex_unlock(&pq->mutex);
}

void free_spells_list(spell_node_t *head) {
    while (head != NULL) {
        spell_node_t *temp = head;
        head = head->next;
        free(temp);
    }
}

spell_node_t* parse_spells_list(char *spells_str) {
    char *saveptr_sc;
    char *spell_token = strtok_r(spells_str, ";", &saveptr_sc);
    
    if (!spell_token) return NULL; 

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

// Updated Worker Thread to handle both SUBMIT and REVOKE
void *worker_thread(void *arg) {
    pthread_detach(pthread_self());
    
    thread_arg_t *t_args = (thread_arg_t *)arg;
    char *buffer = t_args->buffer;
    priority_queue_t *pq = t_args->pq;
    
    char *saveptr1;
    char *colon_part1 = strtok_r(buffer, ":", &saveptr1);
    char *colon_part2 = strtok_r(NULL, ":", &saveptr1);

    if (!colon_part1 || !colon_part2) {
        fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
        free(buffer); free(t_args); return NULL;
    }

    char *saveptr2;
    char *cmd = strtok_r(colon_part1, " \t\r\n", &saveptr2);

    if (!cmd) {
        fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
        free(buffer); free(t_args); return NULL;
    }

    if (strcmp(cmd, "SUBMIT") == 0) {
        char *prio_str = strtok_r(NULL, " \t\r\n", &saveptr2);
        char *name = strtok_r(NULL, " \t\r\n", &saveptr2);

        if (!prio_str || !name || strlen(name) > 16) {
            fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
            free(buffer); free(t_args); return NULL;
        }

        int priority = atoi(prio_str);
        if (priority < 1 || priority > 5) {
            fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
            free(buffer); free(t_args); return NULL;
        }

        request_node_t *req = malloc(sizeof(request_node_t));
        req->priority = priority;
        strncpy(req->mage_name, name, 31);
        req->mage_name[31] = '\0';
        req->spells_head = parse_spells_list(colon_part2);
        req->next = NULL;

        if (req->spells_head == NULL) {
            fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
            free(req); free(buffer); free(t_args); return NULL;
        }

        insert_into_priority_queue(pq, req);
        printf("[Queued] %s's request safely stored.\n", req->mage_name);

    } else if (strcmp(cmd, "REVOKE") == 0) {
        char *name = strtok_r(NULL, " \t\r\n", &saveptr2);
        
        char *saveptr3;
        char *spell_name = strtok_r(colon_part2, " \t\r\n;", &saveptr3);

        if (!name || !spell_name || strlen(name) > 16) {
            fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
            free(buffer); free(t_args); return NULL;
        }

        revoke_spell(pq, name, spell_name);

    } else {
        fprintf(stderr, "[Error] Malformed telepathy from an apprentice\n");
    }

    free(buffer);
    free(t_args);
    return NULL;
}

void *archmage_thread(void *arg) {
    priority_queue_t *pq = (priority_queue_t *)arg;

    while (1) {
        pthread_mutex_lock(&pq->mutex);

        while (pq->head == NULL) {
            pthread_cond_wait(&pq->not_empty, &pq->mutex);
        }

        request_node_t *req = pq->head;
        pq->head = req->next;

        pthread_mutex_unlock(&pq->mutex);

        spell_node_t *curr = req->spells_head;
        while (curr != NULL) {
            usleep(200000); // Wait 200ms
            curr = curr->next;
        }

        printf("[Processed] Archmage completed request for %s.\n", req->mage_name);

        free_spells_list(req->spells_head);
        free(req);
    }
    return NULL;
}

void run_server(int fd, priority_queue_t *pq) {
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

        thread_arg_t *t_args = malloc(sizeof(thread_arg_t));
        if (!t_args) ERR("malloc");
        t_args->buffer = thread_buffer;
        t_args->pq = pq;

        pthread_t tid;
        if (pthread_create(&tid, NULL, worker_thread, t_args) != 0) ERR("pthread_create");
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    int fd = make_udp_socket(port);

    priority_queue_t pq;
    pq.head = NULL;
    if (pthread_mutex_init(&pq.mutex, NULL) != 0) ERR("pthread_mutex_init");
    if (pthread_cond_init(&pq.not_empty, NULL) != 0) ERR("pthread_cond_init");

    pthread_t archmage_tid;
    if (pthread_create(&archmage_tid, NULL, archmage_thread, &pq) != 0) ERR("pthread_create");

    run_server(fd, &pq);

    if (close(fd) < 0) ERR("close");
    pthread_mutex_destroy(&pq.mutex);
    pthread_cond_destroy(&pq.not_empty);

    return EXIT_SUCCESS;
}
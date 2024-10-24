#include <fcntl.h>  // For O_* constants
#include <mqueue.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>  // For mode constants
#include <time.h>      // For time measurement
#include <unistd.h>

#define QUEUE_NAME "/test_queue"
#define SHM_NAME "/shm_comm"

typedef struct {
    long mtype;       // Not used in POSIX message queue, but kept for structure consistency
    char mtext[100];  // Message content
} message_t;

typedef struct {
    int flag;  // 1 for message passing, 2 for shared memory
    union {
        mqd_t mq;        // Message queue descriptor
        char* shm_addr;  // Shared memory address
    } storage;
} mailbox_t;

void send_message(message_t message, mailbox_t* mailbox_ptr) {
    if (mailbox_ptr->flag == 1) {
        if (mq_send(mailbox_ptr->storage.mq, message.mtext, strlen(message.mtext) + 1, 0) == -1) {
            perror("mq_send failed");
            exit(1);
        }
    } else if (mailbox_ptr->flag == 2) {
        // Use shared memory
        strcpy(mailbox_ptr->storage.shm_addr, message.mtext);  // Write message to shared memory
    } else {
        printf("Unknown communication method\n");
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <communication method> <input file>\n", argv[0]);
        exit(1);
    }

    int method = atoi(argv[1]);
    const char* input_file = argv[2];

    mailbox_t mailbox;
    mailbox.flag = method;

    // Create semaphores
    sem_t* sender_sem = sem_open("/sender_sem", O_CREAT, 0644, 1);
    sem_t* receiver_sem = sem_open("/receiver_sem", O_CREAT, 0644, 0);
    if (sender_sem == SEM_FAILED || receiver_sem == SEM_FAILED) {
        perror("sem_open failed");
        exit(1);
    }

    if (mailbox.flag == 1) {
        // Create/open message queue
        struct mq_attr attr;
        attr.mq_flags = 0;                    // Blocking mode
        attr.mq_maxmsg = 10;                  // Maximum number of messages
        attr.mq_msgsize = sizeof(message_t);  // Maximum length of a single message
        attr.mq_curmsgs = 0;                  // Current number of messages

        mailbox.storage.mq = mq_open(QUEUE_NAME, O_CREAT | O_WRONLY, 0644, &attr);
        if (mailbox.storage.mq == (mqd_t)-1) {
            perror("mq_open failed");
            exit(1);
        }
    } else if (mailbox.flag == 2) {
        // Create/open shared memory
        int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
        if (shm_fd == -1) {
            perror("shm_open failed");
            exit(1);
        }
        ftruncate(shm_fd, sizeof(message_t));  // Set shared memory size
        mailbox.storage.shm_addr = mmap(0, sizeof(message_t), PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (mailbox.storage.shm_addr == MAP_FAILED) {
            perror("mmap failed");
            exit(1);
        }
    } else {
        fprintf(stderr, "Invalid communication method. Use 1 for message queue or 2 for shared memory.\n");
        exit(1);
    }

    // Open input file
    FILE* file = fopen(input_file, "r");
    if (!file) {
        perror("fopen failed");
        exit(1);
    }

    struct timespec start, end;
    double time_taken;

    // Start timing
    clock_gettime(CLOCK_MONOTONIC, &start);

    message_t message;
    while (fgets(message.mtext, sizeof(message.mtext), file) != NULL) {
        sem_wait(sender_sem);  // Wait for semaphore permission

        message.mtype = 1;  // Keep this field for structure consistency

        send_message(message, &mailbox);
        sem_post(receiver_sem);  // Notify receiver to read the message
    }

    // Send exit message
    strcpy(message.mtext, "exit");
    sem_wait(sender_sem);
    send_message(message, &mailbox);
    sem_post(receiver_sem);

    fclose(file);

    // End timing
    clock_gettime(CLOCK_MONOTONIC, &end);

    // Calculate total time
    time_taken = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
    printf("Total time taken in sending msg: %f seconds\n", time_taken);

    // Clean up resources
    if (mailbox.flag == 1) {
        mq_close(mailbox.storage.mq);
        mq_unlink(QUEUE_NAME);
    } else if (mailbox.flag == 2) {
        munmap(mailbox.storage.shm_addr, sizeof(message_t));
        shm_unlink(SHM_NAME);
    }

    sem_close(sender_sem);
    sem_close(receiver_sem);

    return 0;
}

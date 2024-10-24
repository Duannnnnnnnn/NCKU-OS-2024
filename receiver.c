#include <fcntl.h>  // For O_* constants
#include <mqueue.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>  // For mode constants
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

void receive_message(mailbox_t* mailbox_ptr, message_t* message) {
    if (mailbox_ptr->flag == 1) {
        if (mq_receive(mailbox_ptr->storage.mq, message->mtext, sizeof(message_t), NULL) == -1) {
            perror("mq_receive failed");
            exit(1);
        }
    } else if (mailbox_ptr->flag == 2) {
        // Use shared memory
        strcpy(message->mtext, mailbox_ptr->storage.shm_addr);  // Read message from shared memory
    } else {
        printf("Unknown communication method\n");
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <communication method>\n", argv[0]);
        exit(1);
    }

    int method = atoi(argv[1]);

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
        // Open message queue
        mailbox.storage.mq = mq_open(QUEUE_NAME, O_CREAT | O_RDONLY, 0644, NULL);
        if (mailbox.storage.mq == (mqd_t)-1) {
            perror("mq_open failed");
            exit(1);
        }
    } else if (mailbox.flag == 2) {
        // Open shared memory
        int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
        if (shm_fd == -1) {
            perror("shm_open failed");
            exit(1);
        }
        mailbox.storage.shm_addr = mmap(0, sizeof(message_t), PROT_READ, MAP_SHARED, shm_fd, 0);
        if (mailbox.storage.shm_addr == MAP_FAILED) {
            perror("mmap failed");
            exit(1);
        }
    } else {
        fprintf(stderr, "Invalid communication method. Use 1 for message queue or 2 for shared memory.\n");
        exit(1);
    }

    message_t message;

    while (1) {
        sem_wait(receiver_sem);  // Wait for sender to send message

        receive_message(&mailbox, &message);
        printf("Received message: %s\n", message.mtext);

        sem_post(sender_sem);  // Notify sender that message has been received

        if (strcmp(message.mtext, "exit") == 0) {
            break;
        }
    }

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

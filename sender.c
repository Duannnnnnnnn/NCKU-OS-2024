#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    long mtype;
    char mtext[100];
} message_t;

typedef struct {
    int flag;  // 1 for message queue, 2 for shared memory
    union {
        int msqid;       // ID for message queue
        char* shm_addr;  // Shared memory address
    } storage;
} mailbox_t;

void send(message_t message, mailbox_t* mailbox_ptr) {
    if (mailbox_ptr->flag == 1) {
        if (msgsnd(mailbox_ptr->storage.msqid, &message, sizeof(message.mtext), 0) == -1) {
            perror("msgsnd failed");
            exit(1);
        }
    } else if (mailbox_ptr->flag == 2) {
        // 使用共享記憶體
        strcpy(mailbox_ptr->storage.shm_addr, message.mtext);  // 將消息寫入共享記憶體
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
    message_t message;
    mailbox.flag = method;

    // 創建信號量
    int semid_sender = semget(IPC_PRIVATE, 1, 0666 | IPC_CREAT);
    if (semid_sender == -1) {
        perror("semget for sender failed");
        exit(1);
    }

    int semid_receiver = semget(IPC_PRIVATE, 1, 0666 | IPC_CREAT);
    if (semid_receiver == -1) {
        perror("semget for receiver failed");
        exit(1);
    }

    // 初始化信號量
    semctl(semid_sender, 0, SETVAL, 1);    // 發送者信號量初始值為 1
    semctl(semid_receiver, 0, SETVAL, 0);  // 接收者信號量初始值為 0

    if (mailbox.flag == 1) {
        key_t key = ftok("receiver.c", 'A');
        if (key == -1) {
            perror("ftok failed");
            exit(1);
        }
        mailbox.storage.msqid = msgget(key, 0666 | IPC_CREAT);
        if (mailbox.storage.msqid == -1) {
            perror("msgget failed");
            exit(1);
        }
    } else if (mailbox.flag == 2) {
        int shm_id = shmget(IPC_PRIVATE, sizeof(message_t), 0666 | IPC_CREAT);
        mailbox.storage.shm_addr = shmat(shm_id, NULL, 0);
    }

    FILE* file = fopen(input_file, "r");
    if (!file) {
        perror("fopen failed");
        exit(1);
    }

    struct timespec start, end;
    double time_taken;

    // 開始計時
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (fgets(message.mtext, sizeof(message.mtext), file) != NULL) {
        struct sembuf p = {0, -1, 0};  // P 操作
        semop(semid_sender, &p, 1);    // 等待發送者信號量

        message.mtype = 1;

        send(message, &mailbox);

        struct sembuf v = {0, 1, 0};   // V 操作
        semop(semid_receiver, &v, 1);  // 釋放接收者信號量
    }

    // 發送退出消息
    strcpy(message.mtext, "exit");
    struct sembuf p = {0, -1, 0};  // P 操作
    semop(semid_sender, &p, 1);    // 等待發送者信號量
    send(message, &mailbox);
    struct sembuf v = {0, 1, 0};   // V 操作
    semop(semid_receiver, &v, 1);  // 釋放接收者信號量

    fclose(file);

    // 結束計時
    clock_gettime(CLOCK_MONOTONIC, &end);

    // 計算總時間
    time_taken = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
    printf("Total time taken in sending msg: %f seconds\n", time_taken);

    // 刪除共享記憶體
    if (mailbox.flag == 2) {
        shmdt(mailbox.storage.shm_addr);                                                   // 分離共享記憶體
        shmctl(shmget(IPC_PRIVATE, sizeof(message_t), 0666 | IPC_CREAT), IPC_RMID, NULL);  // 刪除共享記憶體段
    }

    // 刪除信號量
    semctl(semid_sender, 0, IPC_RMID);    // 刪除發送者信號量
    semctl(semid_receiver, 0, IPC_RMID);  // 刪除接收者信號量

    return 0;
}
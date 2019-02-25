#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/types.h>
#include <semaphore.h>
#include <errno.h>

#define KV_STORE_NAME "/kyilma2020"
#define RW_LOCK "rwlock_kyilmaaaz"
#define MUTEX "mutex_kyilmaaaz"
#define KV_PAIR_COUNT 256
#define MAX_BUCKETS 15
#define MAX_POD_COUNT 500
#define POD_SIZE (MAX_BUCKETS*((KV_PAIR_COUNT*288)+20))
#define TOTAL_SIZE (4+(MAX_POD_COUNT*POD_SIZE))

int kv_store_create(char* name);
int kv_store_write(char *key, char *value);
char *kv_store_read(char* key);
char **kv_store_read_all(char *key);
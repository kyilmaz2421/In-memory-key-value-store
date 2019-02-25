#include "a2_lib.h"

unsigned long hash(unsigned char *str) {
    unsigned long hash = 0;
    int c;

    while('\0' != *str){
        c = *str++;
        hash = c+ (hash << 6)+ (hash<<16) - hash;
    }
    hash = hash % 500;
    return (hash > 0) ? hash : -(hash);
}

int kv_store_create(char *name){

        int fd = shm_open(name,O_CREAT|O_RDWR|O_EXCL,S_IRWXU);

        if(fd<0){
            if (errno != EEXIST) {
                perror("error with shm_open()\n");
                //shared mem has already been created` 
                return -1;
            }
        } else {
                //setting up the key-value store

                /*TOTAL_SIZE comprises of 500 pods each that fit 110 kv pairs each of which has  16 bookingkeeping bytes as well as
                another 4 bookeeping bytes in front of each kvpair that accounts for collision and strores string length of keys.
                The reason for 100 kvpairs is that there may be up to 10  collisions. In the test I ran for 1000 keys for 500 pods could
                create maximuim 10 collisions thus i need to create 10 spaces for each potential collison with a bookeping 4 bytes in front to add efficiency
                so each pod is (MAX_BUCKETS*(4+16+(KV_PAIR_COUNT(256+32))))=POD_SIZE
                furthermore there are another 4 bytes at the very begining that tracks number of readers.
                therefore TOTAL_SIZE is modelled by (4 + 500(POD_SIZE)= TOTAL_SIZE */
                ftruncate(fd,TOTAL_SIZE);
                char *addr = mmap(NULL,TOTAL_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
                memset(addr,'\0',TOTAL_SIZE);

                addr += (4);

                //initializing the pods
                //pod structure as follows
                //20 bytes of bookkeeping data
                //4 bytes for how many blocks in pod free
                //4 bytes for next free block in pod
                //4 bytes for earliest entry 
                //4 bytes next read
                //4 bytes for keylength (done to minimize usage of strcmp when testing for collison)
                //all but 1st 4 bytes are initialized to zero, they will be 20 
                int maxsize = 10;
                int work =0;
                for(int i=0; i<MAX_POD_COUNT; i++){
                        for(int k=0; k<MAX_BUCKETS; k++){
                                memcpy(addr,&maxsize,4);
                                addr= addr + (20+(KV_PAIR_COUNT*288));
                        }
                }
                close(fd);
                munmap(addr,TOTAL_SIZE);
        }

        sem_t *rwlock = sem_open(RW_LOCK, O_CREAT,0664,1);
        sem_t *mutex = sem_open(MUTEX, O_CREAT,0664,1);

        sem_close(rwlock);
        sem_close(mutex);
        return 0;
}


int kv_store_write(char *key, char *value){
        int nextFree=0;
        int earliestEntry=0;
        int numFreeLeft = 10;
        int collisiontest= 1;
        int collisionoffset = 0;

        char *modifiedkey = key;
        char *modifiedvalue = value;

        if((strlen(modifiedkey))>=32){
                modifiedkey[31]='\0';
        }
        else{
                key[strlen(modifiedkey)+1] = '\0';
        }
        if((strlen(modifiedvalue))>=256){
                modifiedvalue[255] = '\0';
        }
        else{
                modifiedvalue[strlen(value)+1] = '\0';
        }

        int keylength = strlen(modifiedkey);
        int podnum = hash((unsigned char*) key);
        sem_t *rwlock = sem_open(RW_LOCK, O_RDWR);

        // Lock the semaphore 
        if((sem_wait(rwlock))==-1){
                perror("semaphore error\n");
                sem_close(rwlock);
                return -1;
        }

        int fd = shm_open("/kyilma2020",O_RDWR,S_IRWXU);

        if(fd<0){
            perror("error with shm_open()\n");
            return -1;
        }

        char *addr = mmap(NULL, TOTAL_SIZE,PROT_WRITE|PROT_READ,MAP_SHARED,fd, 0);

        /* Access to the shared memory area */
        int offset = podnum*POD_SIZE;
        addr += (4+offset);
        int j=0;
        /*we are now testing if there exists a collison and pushoing pointer to a safe place */
        while(collisiontest != 0){
                addr = addr + 16;
                memcpy(&collisiontest, addr,4);

                if((collisiontest==0)||(collisiontest == '\0')){
                        addr=addr-16;
                        collisiontest = 0;
                }
                else if(collisiontest==keylength){
                        addr = addr + 4;
                        char *test = (char*) calloc ((keylength+1), sizeof(char));

                        memcpy(test, addr, keylength);
                        test[keylength] = '\0';
                        collisiontest = strcmp(test,key);

                        if(collisiontest == 0){
                        //we have a match
                                free(test);
                                addr = addr-20;
                        }
                        else{
                        //no match it must be another section   
                                free(test);
                                collisionoffset += (20 + (288*KV_PAIR_COUNT));
                                addr = addr + (288*KV_PAIR_COUNT);
                                j++;
                                continue;
                        }
                }
                else if(j==MAX_BUCKETS){// accounting for collision occuring more than buckets
                addr = addr +4;
                addr = addr - (j*(288*KV_PAIR_COUNT));
                }
                else{
                // if string lengths dont match then keys cant match therfore its another section
                            addr = addr + 4 +(288*KV_PAIR_COUNT);
                collisionoffset += (20 + (288*KV_PAIR_COUNT));
                j++;
                continue;
                }
        }

        //collecting data about pod
        memcpy(&numFreeLeft,addr,4);
        addr = addr + 4; //skipping to nextFree index
        memcpy(&nextFree,addr,4);
        addr = addr + 4;//pushing another 4
        memcpy(&earliestEntry,addr,4);
        addr = addr + 12;

        if((nextFree == 0)&&(numFreeLeft==KV_PAIR_COUNT)){ //1st write ever to a section
                 addr = addr - 4;//pushing to kv slot for  collision bookeeping data     
                 memcpy(addr,&keylength,4);
                 addr = addr + 4;

                 numFreeLeft--;
                 memcpy(addr, modifiedkey, strlen(modifiedkey));
                 addr = addr + 32;
                 memcpy(addr, modifiedvalue, strlen(modifiedvalue));
                 addr = addr - (20 + 32);

                 memcpy(addr,&numFreeLeft,4);
                 addr = addr + 4;//pushing another 4
                 nextFree++;
                 memcpy(addr,&nextFree,4);
                 addr = addr + 4;//pushing another 4
                 memcpy(addr,&earliestEntry,4);

                addr = addr - (offset + collisionoffset + 12);
        }
        else if(numFreeLeft == 0){ //it is full, therefore starting FIFO
                nextFree = earliestEntry;
                earliestEntry++;
                if(earliestEntry == KV_PAIR_COUNT) {
                        earliestEntry = 0;
                }
                addr = addr + (nextFree*288);
                memset(addr,'\0',288);
                memcpy(addr, modifiedkey, strlen(modifiedkey));
                addr = addr + 32;
                memcpy(addr, modifiedvalue, strlen(modifiedvalue));

                addr = addr -( 20 +(nextFree*288)+32);
          nextFree = earliestEntry;
                addr = addr + 4;
                memcpy(addr,&nextFree,4);
                addr = addr + 4;//pushing another 4
                memcpy(addr,&earliestEntry,4);

                addr = addr - (offset + collisionoffset +12);

        }
        else if((numFreeLeft>0)||(numFreeLeft<KV_PAIR_COUNT)){
                 numFreeLeft--;
                 addr = addr + (nextFree*288);
                 memset(addr,'\0',288);
                 memcpy(addr,modifiedkey, strlen(modifiedkey));
                 addr = addr + 32;
                 memcpy(addr, modifiedvalue, strlen(modifiedvalue));
                 addr = addr - (20 +(nextFree*288)+32);
                 nextFree++;
                 memcpy(addr,&numFreeLeft,4);
                 addr = addr + 4;//pushing another 4
                 memcpy(addr,&nextFree,4);
                 addr = addr + 4;//pushing another 4
                 memcpy(addr,&earliestEntry,4);

                 addr = addr - (offset + collisionoffset + 12);
        }

        close(fd);
        munmap(addr,TOTAL_SIZE);
        sem_post(rwlock);
        sem_close(rwlock);
        return 0;
}

char* kv_store_read(char *key){
        int nextRead = 0;
        int readersCount = 0;

        int collisiontest= 1;
        int collisionoffset =0;


        if((strlen(key))>=32){
                key[31]='\0';
        }

        char *modifiedkey = key;
        char *str = (char *) calloc ((256),sizeof(char));
        char *temp = (char *) calloc ((288),sizeof (char));
 int keylength = strlen(modifiedkey);

        int podnum = hash((unsigned char*) key);

        int offset = podnum*POD_SIZE;

        int fd = shm_open("/kyilma2020",O_RDWR,S_IRWXU);

        if(fd<0){
            perror("error with shm_open()\n");
            return NULL;
        }

        char *addr = mmap(NULL, TOTAL_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
         // Access to the shared memory area */
        addr += (4+offset);


        sem_t *rwlock = sem_open(RW_LOCK, O_RDWR);
        sem_t *mutex = sem_open(MUTEX, O_RDWR);

         //Lock the readers count lock semaphore
        if((sem_wait(mutex))==-1){
             perror("semaphore error\n");
             free(temp);
             sem_post(mutex);
             close(fd);
             munmap(addr,TOTAL_SIZE);
             sem_close(mutex);
             sem_close(rwlock);
             return NULL;
        }
        memcpy(&readersCount,addr,4);
        readersCount++;
        memcpy(addr,&readersCount,4);
        memcpy(&readersCount,addr,4);


        if(readersCount==1){

                //releasing the mutex lock
                sem_post(mutex);

                // Lock the read/write lock semaphore 
                if((sem_wait(rwlock))==-1){
                        perror("semaphore error\n");
                        free(temp);
                        sem_post(mutex);
                        close(fd);
                        munmap(addr,TOTAL_SIZE);
                        sem_close(mutex);
   sem_close(rwlock);
                        return NULL;
                }
        }
        else{
                //releasing the mutex lock
                sem_post(mutex);
        }
                //Access to the shared memory area 

        int i=0;
         /*we are now testing if there exists a collison and pushoing pointer to a safe place */
         while(collisiontest != 0){
                addr = addr + 16;
                //checking length of key in this section stored in bookeeping 
                memcpy(&collisiontest, addr,4);
                //if that size is zero then this means that section is empty
                if((collisiontest==0)||(collisiontest == '\0')){
                        addr=addr-16;
                        collisiontest = 0;
                }
                //this means that the length of this key matches that which we are looking for so we will strcmp
                else if(collisiontest==keylength){
                        addr = addr + 4;
                        char *test = (char*) calloc ((keylength+1),sizeof(char));
                        memcpy(test, addr, keylength);
                        test[keylength] = '\0';

                        collisiontest = strcmp(test,key);

                        if(collisiontest == 0){
                        //we have a match
                                free(test);
                                addr = addr-20;
                        }
                        else{
                        //no match it must be another section so we move up 10 blocks to next section
                                free(test);
                                collisionoffset += (20 + (288*KV_PAIR_COUNT));
                                addr = addr + (288*KV_PAIR_COUNT);
                                i++;
                                continue;
                        }
               }
               else if(i==MAX_BUCKETS){// accounting for collision occuring more than buckets
                addr = addr +4;
                addr = addr - (i*(288*KV_PAIR_COUNT));

               }

               else {
                        // if string lengths dont match then keys cant be equal therfore its another section
                        collisionoffset += (20 + (288*KV_PAIR_COUNT));
                        addr = addr + 4 +(288*KV_PAIR_COUNT);
                        i++;
                        continue;
                        }
               }


                addr = addr + 12;

                memcpy(&nextRead,addr,4);

                if(nextRead==KV_PAIR_COUNT){
                        nextRead  = 0;
                }
                addr = addr + 8 + (nextRead*288);
                memcpy(temp,addr,288);
                addr = addr - (8 + (nextRead*288));
         nextRead++;
                memcpy(addr,&nextRead,4);
                addr = addr - (16+offset+collisionoffset);

                temp= temp + 32;
                strcat(str,temp);
                temp= temp - 32;

                if((strlen(str))>=256){
                                str[255]='\0';
                        }
                        else{
                                str[256] = '\0';
                        }
                // exiting critical section  


        //locking readerscount again to account leaving member  
        if((sem_wait(mutex))==-1){
                perror("semaphore error\n");
                free(temp);
                free(modifiedkey);
                sem_post(mutex);
                close(fd);
                munmap(addr,TOTAL_SIZE);
                sem_close(mutex);
                sem_close(rwlock);
                return NULL;
        }

        memcpy(&readersCount,addr,4);
        readersCount--;
        memcpy(addr,&readersCount,4);

        if(readersCount==0){
        // last reader is leaving so we can now open the memory to writers
                 sem_post(rwlock);
        }

        free(temp);
        //free(modifiedkey); 
             sem_post(mutex);
        close(fd);
        munmap(addr,TOTAL_SIZE);
        sem_close(mutex);
        sem_close(rwlock);

   if(*str == '\0'){
                free(str);
                return NULL;
        }
        else{
                return str;
        }

}

char** kv_store_read_all(char *key){
        int collisiontest= 1;
        int collisionoffset =0;

        int readersCount = 0;

        if((strlen(key))>=32){
                key[31]='\0';
        }
        char *modifiedkey = key;


        int keylength = strlen(modifiedkey);

        int podnum = hash((unsigned char *) key);

        int offset = podnum*POD_SIZE;

        int fd = shm_open("/kyilma2020",O_RDWR,S_IRWXU);

        if(fd<0){
            perror("error with shm_open()\n");
            return NULL;
        }

        char *addr = mmap(NULL, TOTAL_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);

        sem_t *rwlock = sem_open(RW_LOCK, O_RDWR);
        sem_t *mutex = sem_open(MUTEX, O_RDWR);

        addr += (4+offset);
           // Lock the readers count lock semaphore 
        if((sem_wait(mutex))==-1){
                perror("semaphore error\n");
                sem_post(mutex);
                //free(modifiedkey);
                close(fd);
                munmap(addr,TOTAL_SIZE);
                sem_close(mutex);
                sem_close(rwlock);
                return NULL;
        }
        memcpy(&readersCount,addr,4);
        readersCount++;
        memcpy(addr,&readersCount,4);
        memcpy(&readersCount,addr,4);


        if(readersCount==1){
                //releasing the mutex lock
                sem_post(mutex);
                // Lock the read/write lock semaphore 

                if((sem_wait(rwlock))==-1){
                        perror("semaphore error\n");
                        sem_post(mutex);
                        //free(modifiedkey);
                        close(fd);
                        munmap(addr,TOTAL_SIZE);
                        sem_close(mutex);
                        sem_close(rwlock);
                        return NULL;
                }

        }
        else{
         //releasing the mutex lock
           sem_post(mutex);
        }


        int i=0;
        /*we are now testing if there exists a collison and pushoing pointer to a safe place */
        while(collisiontest != 0){
                addr = addr + 16;
                //checking length of key in this section stored in bookeeping 
                memcpy(&collisiontest, addr,4);
                //if that size is zero then this means that section is empty
                if((collisiontest==0)||(collisiontest == '\0')){
                        addr=addr-16;
                        collisiontest = 0;
                }
  //this means that the length of this key matches that which we are looking for so we will strcmp
                else if(collisiontest==keylength){
                        addr = addr + 4;
                        char *test = (char*) calloc ((keylength+1),sizeof(char));
                        memcpy(test, addr, keylength);
                        test[keylength] = '\0';

                        collisiontest = strcmp(test,key);

                        if(collisiontest == 0){
                        //we have a match
                                free(test);
                                addr = addr-20;
                        }
                        else{
                        //no match it must be another section so we move up 10 blocks to next section
                                free(test);
                                collisionoffset += (20 + (288*KV_PAIR_COUNT));
                                addr = addr + (288*KV_PAIR_COUNT);
                                i++;
                                continue;
                        }
               }
               else if(i==MAX_BUCKETS){// accounting for collision occuring more than buckets
                addr = addr +4;
                addr = addr - (i*(288*KV_PAIR_COUNT));
               }

               else {
                        // if string lengths dont match then keys cant be equal therfore its another section               
                   collisionoffset += (20 + (288*KV_PAIR_COUNT));
                   addr = addr + 4 +(288*KV_PAIR_COUNT);
                   i++;
                   continue;
                }
        }




        // Access to the shared memory area 
           addr = addr + 20;
           char *temp = (char *) calloc ((288),sizeof(char));
           char **str = malloc(KV_PAIR_COUNT * sizeof(char *));
            int i2 = 0;
           int k = 0;
                for(i2; i2<KV_PAIR_COUNT; i2++){
                     str[i2] = (char *) calloc ((256), sizeof (char));
                     memcpy(temp,addr,288);
                        if(*temp == '\0'){

                        if(k==10){
                                break;
                        }
                        else{
                                k++;
                                continue;

                        }
                     }
                     addr = addr + 32;
                     strncpy(str[i2], addr, strlen(addr));
                     addr = addr - (32) + (288);
                     *temp ='\0';

           }
           str[i2] = NULL;

           addr -= (24+offset+ collisionoffset);

                //critical section over


       //locking readerscount again to account leaving member 
       if((sem_wait(mutex))==-1){
               perror("semaphore error\n");
                close(fd);
                munmap(addr,TOTAL_SIZE);
                sem_close(mutex);
                sem_close(rwlock);
                return NULL;
       }

       memcpy(&readersCount,addr,4);
       readersCount--;
       memcpy(addr,&readersCount,4);

       if(readersCount<=0){
       // last reader is leaving so we can now open the memory to writers
                if((sem_post(rwlock))==-1){
                        perror("semaphore error\n");
                }
                                                }
       sem_post(mutex);

       close(fd);
       munmap(addr,TOTAL_SIZE);
       sem_close(mutex);
       sem_close(rwlock);

       if(*str[0] == '\0'){
                free(str);
                return NULL;
       }
       else{
                return str;
       }
}
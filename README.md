Kaan Yilmaz: 260706265


I built my "kv store" with 500 pods.
My design is governed by the global variables set in "a2_lib.h". I allocate TOTAL_SIZE (TOTAL SIZE is an int that represents the number of bytes allocated) in memory and structure it according to the following design.

I implement 500 pods where  (4 + 500(POD_SIZE)= TOTAL_SIZE. The 4 bytes at the beginning is left for an int that counts the number of readers currently reading from the kv_store (“readersCount”). The semaphores used this number when locking or releasing the RW lock. POD_SIZE is governed by this formula (MAX_BUCKETS*(20+(KV_PAIR_COUNT(256+32)))) = POD_SIZE 
MAX_BUCKETS is the number of buckets each pod has in order to account for collision, I currently have this set to 15. I ran another external program that tested the hash function for collision rate, the max number I got was 11 and am using 15 just to be safe (as this assignment isn’t really testing for efficiency).
Collision is accounted for by when a key is hashed we go to the pod that it points to and then for all the non NULL buckets we iterate through and strcmp the keys until there is a match. If all buckets have been filled and there is no match then it overwrites the 1st bucket 

KV_PAIR_COUNT is the number of blocks allocated per pod (per bucket) to write kv pairs 
each block is then comprised of 288 bytes (32 for key and 256 for value) 
*the tester sets this number to 256 so I did as well

lastly for each bucket the 20 bytes are there for bookkeeping purposes

the 1st set of 4 bytes: “numFreeLeft” determining how many blocks of KV_PAIRS in pod are free still and haven’t been written to, this initialized to KV_PAIR_COUNT up creation of the shared memory

the 2nd set of 4 bytes: “nextFree” determining the next free block in bucket the location that the next write will be to

the 3rd set of 4 bytes: “earliestEntry” used for FIFO this locates the earliest (or oldest ) write in the current bucket

the 4th set of 4 bytes: “nextRead” determining the next block to be read in pod, the location that the read will be from

the 5th set of 4 bytes: “keylength” (done to minimize usage of strcmp when testing for collison),
store the str length of the key so that when dealing with collision it is more efficient to limit number of strcmp executed


Throughout the assignment I traverse through my shared memory using the “addr” (i.e addr += 4 or addr+= strlen(key) pointer that I retrieve when I mmap the space. I follow the above rules I have set out to ensure accurate reads and writes






   

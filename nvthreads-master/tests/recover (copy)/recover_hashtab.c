/*
(c) Copyright [2017] Hewlett Packard Enterprise Development LP

This program is free software; you can redistribute it and/or modify it under 
the terms of the GNU General Public License, version 2 as published by the 
Free Software Foundation.

This program is distributed in the hope that it will be useful, but WITHOUT ANY 
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with 
this program; if not, write to the Free Software Foundation, Inc., 59 Temple 
Place, Suite 330, Boston, MA 02111-1307 USA
*/
// Auther: Terry Hsu
// Verify that aggregate data structures spanning multiple pages can be recovered by NVthreads
// Result: recovery works correctly

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "nvrecovery.h"

pthread_mutex_t gm;
/*#define touch_pages 10
#define table_size 1 * touch_pages
*/
int table_size = 10;
struct hte{
    int key;
    int value;
};
struct hte *ht;

void *t(void *args){
    nvcheckpoint();
    pthread_exit(NULL);
}

void ht_add(int key, int value)
{
    //printf("key = %d", key);
    int hash = key % table_size;
  //  printf("key = %d, touch size = %d, hash=%d cal- %d \n\n",key, table_size, hash, (key%10));
    //printf("ht[hash].key%d\n", ht[hash].key);
    //int count = 0;

  //  printf("curr key value ht[%d].key-> %d, %d\n\n",hash, ht[hash].key, ht[hash].value);
  
    //overwrite
    ht[hash].key = key;
    ht[hash].value = value;
}

int ht_lookup(int key)
{
    int hash = key % table_size;

  /*  while (ht[hash].key != -1 && ht[hash].key != key)
       hash = (hash + 1) % table_size;*/
    
    if (ht[hash].key == -1)
        return -1;
    else
        return ht[hash].value;

}

// ht_delete()
// {}

int main(){
    pthread_mutex_init(&gm, NULL);
    pthread_t tid1;
    
    
    printf("Checking crash status\n");
    if ( isCrashed() ) {
        printf("I need to recover!\n");
        struct hte *c = (struct hte*)malloc(sizeof(struct hte)*table_size);
        nvrecover(c, sizeof(struct hte)*table_size, (char*)"ht");
        for (int i = 0; i < table_size; i++) {
            printf("c[%d] = %c\t", c[i].key, c[i].value);
           
        }
        free(c);
    }
    else{    
        printf("Program did not crash before, continue normal execution.\n");
        pthread_create(&tid1, NULL, t, NULL);

        ht = (struct hte *)nvmalloc(sizeof(struct hte)*table_size, (char*)"ht");

        //init hash table
        for (int i = 0; i < table_size; i++)
        {
            ht[i].key = -1;
            ht[i].value = 68;
        }

        int ascii = 97;
        int i = 0;
        for (i = 0; i < table_size + 4; i++, ascii++) {
            if ( ascii > 122 ) {
                ascii = 97;
            }

           // printf("  ht[%d]-> key, vaue, mod = %d, %c %d\n", i, i, ht[i].value, (i%10));
            ht_add(i, ascii);
          
        }
        
        nvcheckpoint();
        printf("finish writing to values\n");
        for (int i = 0; i < table_size; i++)
        {
             printf("ht[%d] = %c\t", ht[i].key, ht[i].value);
        }

        pthread_join(tid1, NULL);
        printf("internally abort!\n");
        abort(); 
    }

    printf("\n-------------main exits-------------------\n");
    return 0;
}

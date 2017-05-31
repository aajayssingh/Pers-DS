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

/*#define touch_pages 10
#define table_size 1 * touch_pages
*/
#define HASH_TAB_SIZE 4 //16 //power of 2
pthread_mutex_t gm;

//hash table entry
typedef struct hte{
    int key;
    int value;
    bool in_use;
    pthread_mutex_t gmtx;
}hte_t;

//Hash Table
hte_t *ht;

void *t(void *args){
    nvcheckpoint();
    pthread_exit(NULL);
}


int ht_hash(int key)
{
    return(key%HASH_TAB_SIZE);
}

hte_t* ht_probe(int key, int value)
{
   int hash_offset = ht_hash(key);
   int probe_step = 1;//((hash_offset / HASH_TAB_SIZE) % HASH_TAB_SIZE);
  // probe_step |= 1;  // Force step to be odd. That way the loop below eventually visits every entry in the table!

    for (int i = 0; i < HASH_TAB_SIZE; i++) 
    {
        hte* e = &ht[hash_offset];

        // printf("key= %d & value= %d & in use %d\n", e->key, e->value, (int)e->in_use);
        if (!e->in_use)
           return e;
        if ((e->key == key))
            return e;

        hash_offset = (hash_offset + probe_step) % HASH_TAB_SIZE;  // no match found yet. linear probe move on.
        printf("next offset %d\n", hash_offset);
    }
    return NULL; // oh no, the hash table is full
}

bool ht_add(int key, int value)
{

    hte* e = ht_probe(key, value);
    
    if(e == NULL)
    {
        printf("so sorry! bob table is full\n");
        return false;
    }
    printf("gonna ADD key= %d & value= %d & in use %d\n", e->key, e->value, (int)e->in_use);

    if (e != NULL && !e->in_use)
    {
        e->in_use = true;
        e->key = key;        
        e->value = value; //overwrite value because key is same.

    printf("ADDED :) key= %d & value= %d & in use written%d\n", e->key, e->value, (int)e->in_use);
    }
    else //key is in use and matches the key in entry overwrite value
    {
        //e->in_use = true;
        //e->key = key;        
        e->value = value; //overwrite value because key is same.
    }
    return true;
}

bool ht_lookup(int key, int* value)
{
    hte* e = ht_probe(key, value);
    if((e != NULL)&&(e->in_use))
    {
        *value = e->value;
        return true;
    }
    else
    {
        return false;
    }
}

void ht_init()
{
    //init hash table
    for (int i = 0; i < HASH_TAB_SIZE; i++)
    {
        ht[i].key = -1;
        ht[i].value = 36;
        ht[i].in_use = false;
    }
}

void ht_show()
{
    printf("HAST_TABLE:\n");
    for (int i = 0; i < HASH_TAB_SIZE; i++)
    {
        printf("ht[%d] = %c\t", ht[i].key, ht[i].value);
    }
    printf("\n");
}

// ht_delete()
// {}

int main(){
    pthread_mutex_init(&gm, NULL);
    pthread_t tid1;
    
    
    printf("Checking crash status\n");
    if ( isCrashed() ) {
        printf("I need to recover!\n");
        struct hte *c = (struct hte*)malloc(sizeof(struct hte)*HASH_TAB_SIZE);
        nvrecover(c, sizeof(struct hte)*HASH_TAB_SIZE, (char*)"ht",0);
        for (int i = 0; i < HASH_TAB_SIZE; i++) {
            printf("c[%d] = %c\t", c[i].key, c[i].value);
           
        }
        free(c);
    }
    else{    
        printf("Program did not crash before, continue normal execution.\n");
        pthread_create(&tid1, NULL, t, NULL);

        ht = (struct hte *)nvmalloc(sizeof(struct hte)*HASH_TAB_SIZE, (char*)"ht");

        //init hash table
        ht_init();

        ht_add(2, 100);
        ht_show();
        ht_add(2, 101);
        ht_show();
        ht_add(4, 102);
        ht_show();
        ht_add(1, 103);
        ht_show();
        printf("add key 3\n");
        ht_add(3, 104);
        ht_show();

        int val;
        if(ht_lookup(2, &val))
        {
            printf("lookup succeed & value =%c\n", val);            
        }
        else
        {
            printf("failed lookup\n");
        }

        
        // ht_add(5, 105);
        // ht_add(2, 102);
        // ht_add(2, 103);
        // ht_add(2, 106);
        // ht_add(2, 107);
        // ht_add(8, 108);
        // ht_add(2, 109);
        //ht_show();
        
        nvcheckpoint();
        printf("finish writing to values & print HT\n");
        ht_show();

        pthread_join(tid1, NULL);
        printf("internally abort!\n");
        abort(); 
    }

    printf("\n-------------main exits-------------------\n");
    return 0;
}

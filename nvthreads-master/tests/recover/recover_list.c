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
#define touch_pages 10
#define touch_size 4096 * touch_pages
#define num_threads 1 //3

// aggregate data structures
typedef struct node{
    int node_id;
    struct node *next   ;
}node_t;

typedef struct llist 
{
    int arr[5];
    node_t *head; // pointer to the head of the list
} llist_t;


struct thread_data {
    int tid;
    llist_t *local_data;
};

void *t(void *args){        
    nvcheckpoint();
    pthread_exit(NULL);
}

node_t* new_node(int node_id, node_t *next)
{
  printf("New node method\n");
  // allocate node
  node_t* node = (node_t*)nvmalloc(sizeof(node_t), (char*)"f");

  node->node_id = node_id;
  node->next = next;
  return node;
}

llist_t* list_new()
{
    printf("Create list method\n");
  // allocate list
    llist_t *f = (llist_t*)nvmalloc(sizeof(llist_t), (char*)"f");
  // now need to create the sentinel node
  f->head = new_node(10, NULL);
  return f;
}



void *add(void *args){
    struct thread_data *data = (struct thread_data*)args;
    int tid = data->tid;
    llist_t *tmp = data->local_data;

    pthread_mutex_lock(&gm);

    pthread_mutex_unlock(&gm);
 
    free(args);
    pthread_exit(NULL);
}

int main(){
    pthread_mutex_init(&gm, NULL);
    pthread_t tid[num_threads]; 
    pthread_t tid1;   
    
    printf("Checking crash status\n");
    if ( isCrashed() ) {
        printf("I need to recover!\n");
        // Recover aggregate data structure
    //    struct foo *f = (struct foo*)nvmalloc(sizeof(struct foo), (char*)"f");
        llist_t *f = (llist_t*)nvmalloc(sizeof(llist_t), (char*)"f");
        nvrecover(f, sizeof(llist_t), (char *)"f");

        // Verify results
        if(f->head)
            printf("f->head->node_id --> %d\n", f->head->node_id);        
        else
            printf("nil head\n");

        printf("f->arr --> %d, %d, %d, %d\n", f->arr[0], f->arr[1], f->arr[2],f->arr[3]);
//        printf("f->id = %d\n", f->id);
        free(f);
    }
    else{    
        printf("Program did not crash before, continue normal execution.\n");
        pthread_create(&tid1, NULL, t, NULL);

        // Assign magic numbers and character to the aggregate data structure
        //struct foo *f = (struct foo*)nvmalloc(sizeof(struct foo), (char*)"f");
        llist_t *f = list_new();
        f->arr[0] = 20;
        f->arr[1] = 30;
        f->arr[2] = 40;
        f->arr[3] = 50;
        //memset(f->b.c, 0, sizeof(struct foo));
        printf("finish writing to values\n");  
        nvcheckpoint();
        printf("f->arr --> %d, %d, %d, %d\n", f->arr[0], f->arr[1], f->arr[2],f->arr[3]);
        printf("f->head->node_id --> %d\n", f->head->node_id);              
       
       /* int i;
        for (i = 0; i < num_threads; i++) {
            struct thread_data *tmp = (struct thread_data*)malloc(sizeof(struct thread_data));
            tmp->tid = i;
            tmp->local_data = f;
            pthread_create(&tid[i], NULL, t, (void*)tmp);
        }

        pthread_mutex_lock(&gm);
        f->id = 12345;
        f->b.c[10000] = '$';
        printf("main wrote $ to f->b.c[10000]\n");
        pthread_mutex_unlock(&gm);

        pthread_mutex_lock(&gm);
        f->b.c[10000] = '$';
        printf("main wrote $ to f->b.c[10000] again\n");
        pthread_mutex_unlock(&gm);

        for (i = 0; i < num_threads; i++) {
            pthread_join(tid[i], NULL);
        }

        printf("f->b.c[97] = %c\n", f->b.c[97]);        */
        
        // Crash the program
         pthread_join(tid1, NULL);
        printf("internally abort!\n");
        abort(); 
    }

    printf("-------------main exits-------------------\n");
    return 0;
}

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
#if 1
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "nvrecovery.h"

pthread_mutex_t gm;
#define touch_size 4096 * 3

#define PMEM(pmp, ptr_) ((typeof(ptr_))(pmp + (uintptr_t)ptr_))

struct node{
    int id;
    //char c[2];
    struct node *next;
    int i_next;
};

struct list 
{
    int sentinel_id;
   // char arr[5];
    struct node *head; // pointer to the head of the list
} list_t;


void *t(void *args){ 
 //pthread_mutex_lock(&gm);
/*     int *c = (int *)nvmalloc(sizeof(int), (char *)"c");
        *c = 12345;
        printf("c: %d\n", *c);
       

        struct list *nd = (struct list *)nvmalloc(sizeof(struct list), (char *)"z");
        nd->sentinel_id = 99;
        

        //preparing first node
        struct node *nd1 = (struct node *)nvmalloc(sizeof(struct node), (char *)"z");
        nd1->id = 999;

        nd1->next = NULL;

        nd->head = nd1;
        nd->sentinel_id = 2;

        printf("addr nd: %p\n", nd);
        printf("addr nd1: %p\n", nd1);
        printf("&nd->head: %p\n", &(nd->head));
        printf("nd->head: %p\n", nd->head);  
        printf("nd->sentinel_id: %d\n", nd->sentinel_id); 
        printf("nd->head->id: %d\n", nd->head->id);*/
 //pthread_mutex_unlock(&gm);

    nvcheckpoint();
    pthread_exit(NULL);
}


//const char *ajarr = "test";
int main(){
    pthread_mutex_init(&gm, NULL);
    pthread_t tid1;
    
    
    printf("Checking crash status\n");
    if ( isCrashed() ) {
        printf("I need to recover!\n");
        
        void *ptr = malloc(sizeof(int));
        nvrecover(ptr, sizeof(int), (char *)"c", 0);
        printf("Recovered c = %d with add %p\n", *(int*)ptr, (int*)ptr);

        free(ptr);

       //  struct list *ptr_list_head = (struct list*)malloc(sizeof(struct list)/*, (char*)"z"*/);
       //  int node_count = 0;
       //  nvrecover(ptr_list_head, sizeof(struct list), (char*)"z", node_count);
       //  printf("Recovered ptraggr->sentinel_id = %d\n", ptr_list_head->sentinel_id);
        
       //  printf("ptr_list_head: %p\n", ptr_list_head);
       //  printf("ptr_list_head->head: %p\n", ptr_list_head->head);  

       //  //next node
       //  node_count = 1;
       //  struct node *node1 = (struct node *)malloc(sizeof(struct node));
       //  nvrecover(node1, sizeof(struct node), (char*)"z", node_count);

       // ptr_list_head->head = node1;

       //  if(ptr_list_head->head)
       //  {
       //     printf("GONNA ACCESS NEXT.\n");          
       //     printf("Recovered ptr_list_head->head not null = %d\n", (ptr_list_head->head)->id);
       //  }
       
        //free(ptr);
    }
    else{    
        printf("Program did not crash before, continue normal execution.\n");
        pthread_create(&tid1, NULL, t, NULL);
        
        //pthread_mutex_lock(&gm);
        int *c = (int *)nvmalloc(sizeof(int), (char *)"c");
        *c = 12345;
        printf("c: %d add %p\n", *c, c);
       

        // struct list *nd = (struct list *)nvmalloc(sizeof(struct list), (char *)"z");
        // nd->sentinel_id = 99;
        

        // //preparing first node
        // struct node *nd1 = (struct node *)nvmalloc(sizeof(struct node), (char *)"z");
        // nd1->id = 999;

        // nd1->next = NULL;

        // nd->head = nd1;
        // nd->sentinel_id = 2;

        // printf("addr nd: %p\n", nd);
        // printf("addr nd1: %p\n", nd1);
        // printf("&nd->head: %p\n", &(nd->head));
        // printf("nd->head: %p\n", nd->head);  
        // printf("nd->sentinel_id: %d\n", nd->sentinel_id); 
        // printf("nd->head->id: %d\n", nd->head->id);

        //pthread_mutex_unlock(&gm);
        //free(c); //to check how free works.
        //c = NULL;
        //printf("c: %d\n", *c);
        
        printf("finish writing to values\n");
        nvcheckpoint();

        pthread_join(tid1, NULL);
        printf("c: %d\n", *c);
        printf("internally abort!\n");
        abort(); 
    }

    printf("-------------main exits-------------------\n");
    return 0;
}
#endif


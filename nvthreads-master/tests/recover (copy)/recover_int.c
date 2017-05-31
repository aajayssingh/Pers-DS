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


 pthread_mutex_lock(&gm);
     int *c = (int *)nvmalloc(sizeof(int), (char *)"c");
        *c = 12345;
        printf("c: %d\n", *c);
        /*int *d = (int *)nvmalloc(sizeof(int), (char *)"d");
        *d = 56789;
        printf("d: %d\n", *d);
        int *e = (int *)nvmalloc(sizeof(int), (char *)"e");
        *e = 99999;
        printf("e: %d\n", *e); */

        struct list *nd = (struct list *)nvmalloc(sizeof(struct list), (char *)"z");
        nd->sentinel_id = 99;
        
 
 /*       nd->c[0] = 97;
        nd->c[1] = 98;
*/
        //preparing first node
        struct node *nd1 = (struct node *)nvmalloc(sizeof(struct node), (char *)"z");
        nd1->id = 999;
/*        nd1->c[0] = 99;
        nd1->c[1] = 100;*/
        nd1->next = NULL;

        nd->head = nd1;
        nd->sentinel_id = 2;

        printf("addr nd: %p\n", nd);
        printf("addr nd1: %p\n", nd1);
        printf("&nd->head: %p\n", &(nd->head));
        printf("nd->head: %p\n", nd->head);  
        printf("nd->sentinel_id: %d\n", nd->sentinel_id); 
        printf("nd->head->id: %d\n", nd->head->id);
 pthread_mutex_unlock(&gm);


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
        nvrecover(ptr, sizeof(int), (char *)"c");
        printf("Recovered c = %d\n", *(int*)ptr);
        free(ptr);
        /*ptr = malloc(sizeof(int));
        nvrecover(ptr, sizeof(int), (char*)"d");
        printf("Recovered d = %d\n", *(int*)ptr);
        free(ptr);
        ptr = malloc(sizeof(int));
        nvrecover(ptr, sizeof(int), (char*)"e");
        printf("Recovered e = %d\n", *(int*)ptr);
        free(ptr);
*/
        struct list *ptr_list_head = (struct list*)malloc(sizeof(struct list)/*, (char*)"z"*/);
       int node_count = 0;
        nvrecover(ptr_list_head, sizeof(struct list), (char*)"z", node_count);
        printf("Recovered ptraggr->sentinel_id = %d\n", ptr_list_head->sentinel_id);
        
        printf("ptr_list_head: %p\n", ptr_list_head);
        printf("ptr_list_head->head: %p\n", ptr_list_head->head);  

        //next node
        node_count = 1;
        struct node *node1 = (struct node *)malloc(sizeof(struct node));
        nvrecover(node1, sizeof(struct node), (char*)"z", node_count);

        node1 = (struct node *)ptr_list_head->head;

        if(ptr_list_head->head)
        {
           printf("GONNA ACCESS NEXT.\n");          
          // printf("Recovered ptr_list_head->head not null = %d\n", (struct node *)(ptr_list_head->head)->id);
        }
       // getchar();
        //free(ptr);
    }
    else{    
        printf("Program did not crash before, continue normal execution.\n");
        pthread_create(&tid1, NULL, t, NULL);

//         int *c = (int *)nvmalloc(sizeof(int), (char *)"c");
//         *c = 12345;
//         printf("c: %d\n", *c);
//         /*int *d = (int *)nvmalloc(sizeof(int), (char *)"d");
//         *d = 56789;
//         printf("d: %d\n", *d);
//         int *e = (int *)nvmalloc(sizeof(int), (char *)"e");
//         *e = 99999;
//         printf("e: %d\n", *e); */

//         struct list *nd = (struct list *)nvmalloc(sizeof(struct list), (char *)"z");
//         nd->sentinel_id = 99;
        
 
//  /*       nd->c[0] = 97;
//         nd->c[1] = 98;
// */
//         //preparing first node
//         struct node *nd1 = (struct node *)malloc(sizeof(struct node)/*, (char *)"z"*/);
//         nd1->id = 999;
// /*        nd1->c[0] = 99;
//         nd1->c[1] = 100;*/
//         nd1->next = NULL;

//         nd->head = nd1;

//         printf("addr nd: %p\n", nd);
//         printf("addr nd1: %p\n", nd1);
//         printf("&nd->head: %p\n", &(nd->head));
//         printf("nd->head: %p\n", nd->head);  
//         printf("nd->sentinel_id: %d\n", nd->sentinel_id); 
//         printf("nd->head->id: %d\n", nd->head->id);


        printf("finish writing to values\n");
        nvcheckpoint();

        pthread_join(tid1, NULL);
        printf("internally abort!\n");
        abort(); 
    }

    printf("-------------main exits-------------------\n");
    return 0;
}

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

// aggregate data structures

struct foo{
    int id;
    int arr[15];
    struct foo *head;
};


struct thread_data {
    int tid;
    struct foo *local_data;
};

void *t(void *args){
    struct thread_data *data = (struct thread_data*)args;
    int tid = data->tid;
    struct foo *tmp = data->local_data;

    pthread_mutex_lock(&gm);
    tmp->arr[7+tid] = 97+tid;    // 97 is 'a'
    printf("tid %d wrote %c at f->arr[%d]\n", tid, tmp->arr[7+tid], 7+tid);   
    tmp->arr[1] = '!'+tid;

    if (tid == 0) {
        tmp->arr[3] = '@';
        printf("tid %d wrote %c at f->arr[3]\n", tid, tmp->arr[3]);   
    }
  
    printf("tid %d wrote %c at f->arr[1]\n", tid, tmp->arr[1]);   
    pthread_mutex_unlock(&gm);
 
    free(args);
    pthread_exit(NULL);
}

int main(){
    pthread_mutex_init(&gm, NULL);
    pthread_t tid[3];    
    
    printf("Checking crash status\n");
    if ( isCrashed() ) {
        printf("I need to recover!\n");
        // Recover aggregate data structure
       struct foo *f = (struct foo*)nvmalloc(sizeof(struct foo), (char*)"f");
        nvrecover(f, sizeof(struct foo), (char *)"f");

        // struct bar *aj = (struct bar*)nvmalloc(sizeof(struct bar), (char*)"aj");
        // nvrecover(aj, sizeof(struct bar), (char *)"aj");

        // Verify results
        printf("f->arr[7] = %c\n", f->arr[7]);        
        printf("f->arr[8] = %c\n", f->arr[8]);        
        printf("f->arr[9] = %c\n", f->arr[9]);        
        printf("f->arr[1] = %c\n", f->arr[1]);        
        printf("f->arr[3] = %c\n", f->arr[3]);        
        printf("f->id = %d\n", f->id);
        
        //f->head = aj;
        if(f->head)
        {
            printf("head not null\n");
            printf("f->head = %d\n", f->head->id);
        }
        else
            printf("f->head = null\n");
        
        free(f);
    }
    else{    
        printf("Program did not crash before, continue normal execution.\n");

        // Assign magic numbers and character to the aggregate data structure
        struct foo *f = (struct foo*)nvmalloc(sizeof(struct foo), (char*)"f");
        //memset(f->b.c, 0, sizeof(struct foo));
        printf("finish writing to values\n");                
       
        int i;
        for (i = 0; i < 3; i++) {
            struct thread_data *tmp = (struct thread_data*)malloc(sizeof(struct thread_data));
            tmp->tid = i;
            tmp->local_data = f;
            pthread_create(&tid[i], NULL, t, (void*)tmp);
        }

        pthread_mutex_lock(&gm);
        f->id = 12345;

        f->arr[0] = 10;
        f->arr[1] = 20;
        struct foo *aj = (struct foo*)nvmalloc(sizeof(struct foo), (char*)"f");
        aj->id = 99;
        f->head = aj;
        printf("main wrote aj->id = 99 &f->head = aj thus f->head->id - %d\n", f->head->id);

        f->arr[1] = '$';
        printf("main wrote $ to f->arr[1]\n");
        pthread_mutex_unlock(&gm);

        pthread_mutex_lock(&gm);
        f->arr[1] = '$';
        printf("main wrote $ to f->arr[1] again\n");
        pthread_mutex_unlock(&gm);

        for (i = 0; i < 3; i++) {
            pthread_join(tid[i], NULL);
        }

        printf("f->arr[7] = %c\n", f->arr[7]);        
        printf("f->arr[8] = %c\n", f->arr[8]);        
        printf("f->arr[9] = %c\n", f->arr[9]);        
        printf("f->arr[1] = %c\n", f->arr[1]);        
        printf("f->arr[3] = %c\n", f->arr[3]);        
        printf("f->id = %d\n", f->id);

        // Crash the program
        printf("internally abort!\n");
        abort(); 
    }

    printf("-------------main exits-------------------\n");
    return 0;
}

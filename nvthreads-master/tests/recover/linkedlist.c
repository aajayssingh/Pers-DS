/* ---------------------------------------------------------------------------

 * ---------------------------------------------------------------------------
 */
#include "linkedlist.h"

// #define lock(mtx) pthread_mutex_lock(&(mtx))
// #define unlock(mtx) pthread_mutex_unlock(&(mtx))
#ifdef _FREE_
#define free_node(node) free(node)
#else
#define free_node(node) ;
#endif

static node_t *create_node(const key_t, const val_t);

/*
 * success : return pointer of this node
 * failure : return NULL
 */
static node_t *create_node(const key_t key, const val_t val)
{
  node_t *node;
  node = (node_t *)nvmalloc(sizeof(node_t), (char *)"z");
  
  if (node == NULL) {
    elog("create_node | nvmalloc error");
    return NULL;
  }
  node->key = key;
  node->val = val;
  node->marked = false;

  
  return node;
}

/*
 * success : return true
 * failure(key already exists) : return false
 */
bool add(list_t * list, const key_t key, const val_t val)
{
  node_t *pred, *curr;
  node_t *newNode;
  bool ret = true;
  
  printf("add | enter add [%d, %d]\n", key, val);

  if ((newNode = create_node(key, val)) == NULL)
    return false;
  
  pthread_mutex_lock(&(list->gmtx));
  
  pred = list->head;
  curr = pred->next;
  
  if (curr == list->tail)
  {
    list->head->next = newNode;
    newNode->next = list->tail;
    list->node_count++;
  }
  else
  {
    while (curr != list->tail && curr->key < key)
    {
      pred = curr;
      curr = curr->next;
    }
    
    if (curr != list->tail && key == curr->key)
    {
      free_node(newNode);
      printf("node already present with key = %d\n", key);
      ret = false;
    }
    else
    {
      newNode->next = curr;
      pred->next = newNode;

      list->node_count = list->node_count + 1;
    }
  }
  
  pthread_mutex_unlock(&(list->gmtx));

printf("add | exit add [%d, %d] and node_count- %d \n", key, val, list->node_count);
  return ret;
}

/*
 * success : return true
 * failure(not found) : return false
 */
bool remove(list_t * list, const key_t key, val_t *val)
{
  node_t *pred, *curr;
  bool ret = true;
  
  pthread_mutex_lock(&(list->gmtx));

  pred = list->head;
  curr = pred->next;
  
  if (curr == list->tail) /*list empty*/ 
  {
    ret = false;
  }
  else
  {
    while (curr->key < key && curr != list->tail) /*traverse correct location*/
    {
      pred = curr;
      curr = curr->next;
    }
    
    if (key == curr->key)
    {
      *val = curr->val;
      
      pred->next = curr->next;
      free_node(curr);
      list->node_count--;
    }
    else
      ret = false;
  }
  
  pthread_mutex_unlock(&(list->gmtx));
  return ret;
}

/*
 * success : return true
 * failure(not found) : return false
 */
bool find(list_t * list, const key_t key, val_t *val)
{
  node_t *pred, *curr;
  bool ret = true;
  
  pthread_mutex_lock(&(list->gmtx));
  
  pred = list->head;
  curr = pred->next;
  
  if (curr == list->tail) 
  {
    ret = false;
  }
  else
  {
    while (curr != list->tail && curr->key < key)
    {
      pred = curr;
      curr = curr->next;
    }
    if (curr != list->tail && key == curr->key)
    {
      *val = curr->val;
      ret = true;
    }
    else
      ret = false;
  }
  
  pthread_mutex_unlock(&(list->gmtx));
  return ret;
}

/*
 * success : return pointer to the initial list
 * failure : return NULL
 */
list_t *init_list(void)
{
  list_t *list;
 
  list = (list_t *)nvmalloc(sizeof(list_t), (char *)"z");

  if (list == NULL) {
    elog("nvmalloc error");
    return NULL;
  }
  
  pthread_mutex_init(&(list->gmtx), NULL);
  
  list->head = (node_t *)nvmalloc(sizeof(node_t), (char *)"z");  
  if (list->head == NULL) {
    elog("nvmalloc error");
    goto end;
  }
  list->head->key = 0;
  list->head->val = 0;


  list->tail = (node_t *)nvmalloc(sizeof(node_t), (char *)"z");
  if (list->tail == NULL) {
    elog("nvmalloc error");
    goto end;
  }
  list->tail->key = 99;
  list->tail->val = 99;

  list->head->next = list->tail;
  list->tail->next = NULL;
  list->node_count = 2;
//  list->mtx = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
  
  return list;
  
 end:
  free(list->head);
  free(list);
  return NULL;
}

/*
 * success : return pointer to the initial list
 * failure : return NULL
 */
list_t * recover_init_list (void)
{
  list_t *list;
  
  list = (list_t*)malloc(sizeof(list_t));
  int node_index = 0;
  nvrecover(list, sizeof(list_t), (char*)"z", 0);
  printf("Recovered list & node_count = %d\n", list->node_count);
  pthread_mutex_init(&(list->gmtx), NULL);

  printf("list->head: %p\n", list->head);  
  printf("list->tail: %p\n", list->tail);

  printf("recovering head & tail...\n");

  //next node
  //node_index = (list->node_count) - 2; //two sentinel nodes
  
  node_index++;
  node_t *head = (node_t *)malloc(sizeof(node_t));
  nvrecover(head, sizeof(node_t), (char*)"z", node_index);
  list->head = head;

  node_index++;
  node_t *tail = (node_t *)malloc(sizeof(node_t));
  nvrecover(tail, sizeof(node_t), (char*)"z", node_index);
  list->tail = tail;

  printf("recovering complete list...\n"); 

  node_t *pred, *curr;
  pred = list->head;
//  curr = pred->next;

  while ((++node_index) <= (list->node_count)) {

    node_t *curr = (node_t *)malloc(sizeof(node_t));
    nvrecover(curr, sizeof(node_t), (char*)"z", node_index);
    printf("recovered node_index %d\n", node_index);
    printf(" [%lu:%ld]", (unsigned long int) curr->key, (long int)curr->val);

    pred->next = curr;    
    pred = curr;

  } 
  pred->next = tail;

  printf("recover_init_list | full list recovered :) :)\n");

//  list->mtx = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
  
  return list;

}

/*
 * void free_list(list_t * list)
 */
void free_list(list_t * list)
{
  node_t *curr, *next;

  curr = list->head->next;

  while (curr != list->tail) {
    next = curr->next;
    free_node (curr);
    curr = next;
  }

  free_node(list->head);
  free_node(list->tail);
  //pthread_mutex_destroy(&list->mtx);
  free(list);
}

/*
 * void show_list(const list_t * l)
 */
void show_list(const list_t * l)
{
    node_t *pred, *curr;

    pred = l->head;
    curr = pred->next;

    printf ("list:\n\t");
    while (curr != l->tail) {
      printf(" ------>[%lu:%ld]", (unsigned long int) curr->key, (long int)curr->val);
      pred = curr;
      curr = curr->next;
    }
    printf("\n");
}


/*#include <string.h>

list_t *list;

int main (int argc, char **argv)
{
  key_t i;
  val_t g;

  list = init_list();

  show_list (list);

  for (i = 0; i < 10; i++) {
    add (list, (key_t)i, (val_t) i * 10);
  }  

  show_list (list);

  for (i = 0; i < 5; i++) {
    delete (list, (key_t)i, &g);
    printf ("ret = %ld\n", (long int)g);
  }

  show_list (list);

  free_list (list);

  return 0;
}*/

//#endif

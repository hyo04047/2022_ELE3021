#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_THREADS 4
#define MAX_NUM 40000000

int arr[MAX_NUM];

int check_sorted(int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (arr[i] != i)
            return 0;
    return 1;
}

// Implement your solution here
void merge(int low, int mid, int high){
    int* left = (int*)malloc(((mid - low +1) * sizeof(int)));
    int* right = (int*)malloc((high - mid) * sizeof(int));

    int n1 = mid - low + 1;
    int n2 = high - mid;
    int i, j;

    for(i = 0; i < n1; i++)
        left[i] = arr[i + low];
    
    for(i = 0; i < n2; i++)
        right[i] = arr[i + mid + 1];

    int k = low;
    i = j = 0;

    while(i < n1 && j < n2){
        if(left[i] <= right[j])
            arr[k++] = left[i++];
        else
            arr[k++] = right[j++];
    }

    while(i < n1)
        arr[k++] = left[i++];

    while(j < n2)
        arr[k++] = right[j++];

    free(left);
    free(right);
}

void merge_sort(int low, int high){
    int mid = low + (high - low) / 2;

    if(low < high){
        merge_sort(low, mid);
        merge_sort(mid + 1, high);
        merge(low, mid, high);
    }
}

void* merge_sort_(void *threadid){
    long taskid = (long) threadid;

    int low = taskid * (MAX_NUM / NUM_THREADS);
    int high = (taskid + 1) * (MAX_NUM / NUM_THREADS) - 1;
    int mid = low + (high - low) / 2;

    if(low < high){
        merge_sort(low, mid);
        merge_sort(mid + 1, high);
        merge(low, mid, high);
    }
}

///////////////////////////////

int main(void)
{
    srand((unsigned int)time(NULL));
    const int n = MAX_NUM;
    int i;

    for (i = 0; i < n; i++)
        arr[i] = i;
    for (i = n - 1; i >= 1; i--)
    {
        int j = rand() % (i + 1);
        int t = arr[i];
        arr[i] = arr[j];
        arr[j] = t;
    }

    printf("Sorting %d elements...\n", n);

    // Create threads and execute.
    pthread_t threads[NUM_THREADS];
    long taskids[NUM_THREADS];
    int rc;

    for(int i = 0; i < NUM_THREADS; i++){
        taskids[i] = i;
        rc = pthread_create(&threads[i], NULL, merge_sort_, (void*) taskids[i]);
        if (rc) {
        printf("ERROR; return code from pthread_create() is %d\n", rc);
        exit(-1);
        }
    }

    for(int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    merge(0, (MAX_NUM / 2 - 1) / 2, MAX_NUM / 2 - 1);
    merge(MAX_NUM / 2, MAX_NUM / 2 + (MAX_NUM - 1 - MAX_NUM / 2) / 2, MAX_NUM - 1);
    merge(0, (MAX_NUM - 1) / 2, MAX_NUM - 1);
    //////////////////////////////

    if (!check_sorted(n))
    {
        printf("Sort failed!\n");
        return 0;
    }

    printf("Ok %d elements sorted\n", n);
    return 0;
}

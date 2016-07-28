#include <stdio.h>
#include <string.h>
#include <omp.h>
#include <pthread.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

// Number of threads
#define NUM_THREADS 32
//OpenMP chunk size
#define CHUNK_SIZE 128

// Number of iterations
#define TIMES 1000

// Input Size
#define NSIZE 7
#define NMAX 262144
int Ns[NSIZE] = {4096, 8192, 16384, 32768, 65536, 131072, 262144};   

typedef struct __ThreadArg {
	int id;
	int nrT;
	int n;
	int *cur_array;
	int *compared_array;
	int *C;
	int compared_length;
} tThreadArg;

pthread_t callThd[NUM_THREADS];
pthread_mutex_t mutexpm;
pthread_barrier_t barr, internal_barr;
pthread_attr_t attr;

// Seed Input
int A[NMAX];
int B[NMAX];
// Store Result
int C[NMAX];

int vector_A_size, vector_B_size;

void read_file(char *file_name, int *data_array, int *data_array_length) {
	int i = 0;
	int num;
	FILE *file;
	file = fopen(file_name, "r");
	if (file == NULL) {
		printf("Error Reading File\n");
		exit(0);
	}
	while (fscanf(file, "%d", &num) > 0) {
		data_array[i] = num;
		i++;
	} 
	*data_array_length = i;
	fclose(file);
}

void print_array(int *array, int length) {
	int i;
	for (i=0; i<length; i++) {
		printf("%d ", array[i]);
	}
	printf("\n");
}

void init(int n) {
	/* Initialize the input for this iteration*/
	int i;
	for (i=0; i<n; i++) {
		C[i] = 0;
	}
}

// v: value, A: array, si: start_index, ei: end_index
int get_rank(int v, int *A, int si, int ei, int maxindex) {
	// binary search
	if (si == ei) {
		if (si == maxindex) {
			return maxindex;
		}
		if (A[si] <= v) {
			return si+1;
		} else {
			return si;
		}
	} else {
		int ci = (si + ei) / 2;
		if (A[ci] > v) {
			return get_rank(v, A, si, ci, maxindex);
		} else {
			return get_rank(v, A, ci+1, ei, maxindex);
		}
	}
}

void seq_merge(int *A, int *B, int *C, int A_length, int B_length) {
	int i, cur_rank;

	for (i=0; i<A_length; i++) {
		cur_rank = get_rank(A[i], B, 0, B_length, B_length);
		C[cur_rank + i] = A[i];
	}
	for (i=0; i<B_length; i++) {
		cur_rank = get_rank(B[i], A, 0, A_length, A_length);
		C[cur_rank + i] = B[i];
	}
}

void openmp_function(int *A, int *B, int *C, int A_length, int B_length, int num_threads) {
	/* The code for sequential algorithm */
	int i, chunk;
	chunk = CHUNK_SIZE;

#pragma omp parallel for shared(A, B, C, chunk, num_threads) \
	private(i) schedule(static, chunk) num_threads(num_threads) 
	for (i=1; i<A_length; i++) {
		int cur_rank = get_rank(A[i], B, 0, B_length, B_length);
		C[cur_rank + i] = A[i];
	}

#pragma omp parallel for shared(A, B, C, chunk, num_threads) \
	private(i) schedule(static, chunk) num_threads(num_threads) 
	for (i=0; i<B_length; i++) {
		int cur_rank = get_rank(B[i], A, 0, A_length, A_length);
		C[cur_rank + i] = B[i];
	}
}

void *calc_rank_and_update(void *para_arg) {
	tThreadArg *thread_arg;
	thread_arg = (tThreadArg *)para_arg;

	int *cur_array = thread_arg->cur_array;
	int *compared_array = thread_arg->compared_array;
	int *result_array = thread_arg->C;
	int maxindex = thread_arg->compared_length;

	int i;
	int j = thread_arg->id;
	int ele_per_td = (int)(thread_arg->n / thread_arg->nrT);
	int si = (j - 1) * ele_per_td;
	int ei = j * ele_per_td;
	printf("si: %d    ei: %d\n",si, ei );
	for (i=si; i<ei; i++) {
		int cur_rank = get_rank(cur_array[i], compared_array, 0, maxindex, maxindex);
		printf("%d\n", cur_rank);
		result_array[cur_rank + i] = cur_array[i];
	}
}

void par_function(int *A, int *B, int *C, int A_length, int B_length, int nt){
	/* The code for threaded computation */
	void *status;
	tThreadArg x[NUM_THREADS];

	int j;
	// parallelize the original for loop
	for (j=1; j<=nt; j++)
	{
		x[j].id = j; 
		x[j].nrT=nt; // number of threads in this round
		x[j].n=A_length;
		x[j].cur_array = A;
		x[j].compared_array = B;
		x[j].C = C;
		x[j].compared_length = B_length;
		pthread_create(&callThd[j-1], &attr, calc_rank_and_update, (void *)&x[j]);
	}

	/* Wait on the other threads */
	for(j=0; j<nt; j++)
	{
		pthread_join(callThd[j], &status);
	}

	// parallelize the original for loop
	for (j=1; j<=nt; j++)
	{
		x[j].id = j; 
		x[j].nrT=nt; // number of threads in this round
		x[j].n=B_length;
		x[j].cur_array = B;
		x[j].compared_array = A;
		x[j].C = C;
		x[j].compared_length = A_length;
		pthread_create(&callThd[j-1], &attr, calc_rank_and_update, (void *)&x[j]);
	}

	/* Wait on the other threads */
	for(j=0; j<nt; j++)
	{
		pthread_join(callThd[j], &status);
	}
}


int main (int argc, char *argv[])
{
	struct timeval startt, endt, result;
	int i, j, k, nt, t, n, c;

	result.tv_sec = 0;
	result.tv_usec= 0;

	/* Test Correctness */
	printf("Test for correctness:\n");

	read_file("test01_A.in", A, &vector_A_size);
	read_file("test01_B.in", B, &vector_B_size);
	printf("Array A:\n");
	print_array(A, vector_A_size);
	printf("Array B:\n");
	print_array(B, vector_B_size);

	printf("Result after merging for sequential algorithm:\n");
	init(vector_A_size + vector_B_size);
	seq_merge(A, B, C, vector_A_size, vector_B_size);
	print_array(C, vector_A_size + vector_B_size);

	printf("Result after merging for openmp algorithm:\n");
	init(vector_A_size + vector_B_size);
	openmp_function(A, B, C, vector_A_size, vector_B_size, 2);
	print_array(C, vector_A_size + vector_B_size);

	printf("Results for pthread algorithm:\n");
	init(vector_A_size + vector_B_size);
	par_function(A, B, C, vector_A_size, vector_B_size, 3);
	print_array(C, vector_A_size + vector_B_size);
/*
	// Generate a seed input 
	srand ( time(NULL) );
	for(k=0; k<NMAX; k++){
		A[k] = k;
		B[k] = 2*k;
	}

	printf("OpenMP:\n");
	printf("|NSize|Iterations| Seq | Th01 | Th02 | Th04 | Th08 | Par16|\n");

	// for each input size
	for(c=0; c<NSIZE; c++){
		n=Ns[c];
		printf("| %d | %d |",n,TIMES);

		// Run sequential algorithm 
		result.tv_usec=0;
		gettimeofday (&startt, NULL);
		for (t=0; t<TIMES; t++) {
			init(n);
			seq_merge(A, B, C, n/2, n/2);
		}
		gettimeofday (&endt, NULL);
		result.tv_usec = (endt.tv_sec*1000000+endt.tv_usec) - (startt.tv_sec*1000000+startt.tv_usec);
		printf(" %ld.%06ld | ", result.tv_usec/1000000, result.tv_usec%1000000);

		// Run OpenMP algorithm(s) 
		for(nt=1; nt<NUM_THREADS; nt=nt<<1){
			result.tv_sec=0; result.tv_usec=0;
			gettimeofday (&startt, NULL);
			for (t=0; t<TIMES; t++) 
			{
				init(n);
				openmp_function(A, B, C, n/2, n/2, nt);
			}
			gettimeofday (&endt, NULL);
			result.tv_usec += (endt.tv_sec*1000000+endt.tv_usec) - (startt.tv_sec*1000000+startt.tv_usec);
			printf(" %ld.%06ld | ", result.tv_usec/1000000, result.tv_usec%1000000);
		}
		printf("\n");
	}

	// Initialize and set thread detached attribute 
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	printf("Pthread:\n");
	printf("|NSize|Iterations| Seq | Th01 | Th02 | Th04 | Th08 | Par16|\n");

	// for each input size
	for(c=0; c<NSIZE; c++){
		n=Ns[c];
		printf("| %d | %d |",n,TIMES);

		// Run sequential algorithm 
		result.tv_usec=0;
		gettimeofday (&startt, NULL);
		for (t=0; t<TIMES; t++) {
			init(n);
			seq_merge(A, B, C, n/2, n/2);
		}
		gettimeofday (&endt, NULL);
		result.tv_usec = (endt.tv_sec*1000000+endt.tv_usec) - (startt.tv_sec*1000000+startt.tv_usec);
		printf(" %ld.%06ld | ", result.tv_usec/1000000, result.tv_usec%1000000);

		// Run pthread algorithm(s) 
		for(nt=1; nt<NUM_THREADS; nt=nt<<1){
			if(pthread_barrier_init(&barr, NULL, nt+1))
			{
				printf("Could not create a barrier\n");
				return -1;
			}
			if(pthread_barrier_init(&internal_barr, NULL, nt))
			{
				printf("Could not create a barrier\n");
				return -1;
			}

			result.tv_sec=0; result.tv_usec=0;

			gettimeofday (&startt, NULL);
			for (t=0; t<TIMES; t++) 
			{
				init(n);
				par_function(A, B, C, n/2, n/2, nt);
				// pthread_barrier_wait(&barr);
			}
			gettimeofday (&endt, NULL);

			if (pthread_barrier_destroy(&barr)) {
				printf("Could not destroy the barrier\n");
				return -1;
			}
			if (pthread_barrier_destroy(&internal_barr)) {
				printf("Could not destroy the barrier\n");
				return -1;
			}
			result.tv_usec += (endt.tv_sec*1000000+endt.tv_usec) - (startt.tv_sec*1000000+startt.tv_usec);
			printf(" %ld.%06ld | ", result.tv_usec/1000000, result.tv_usec%1000000);
		}
		printf("\n");
	}
	pthread_exit(NULL);
*/
}

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
	int choice;
	int start_index;
	int end_index;
	int* src;
	int* result;
} tThreadArg;

pthread_t callThd[NUM_THREADS];
pthread_mutex_t mutexpm;
pthread_barrier_t barr, internal_barr;
pthread_attr_t attr;

// Seed Input
int A[NMAX];
// Store result
int B[NMAX];
int C[NMAX];
// file size
int file_size;

void read_file(char *file_name) {
	int i = 0;
	int num;
	FILE *file;
	file = fopen(file_name, "r");
	if (file == NULL) {
		printf("Error Reading File\n");
		exit(0);
	}
	while (fscanf(file, "%d", &num) > 0) {
		A[i] = num;
		i++;
	} 
	file_size = i;
	fclose(file);
}

void print_array(int *array, int length) {
	int i;
	for (i=0; i<length; i++) {
		printf("%d ", array[i]);
	}
	printf("\n");
}

int min(int a, int b) {
	if (a < b) {
		return a;
	} else {
		return b;
	}
} 

void init(int n){
	/* Initialize the input for this iteration*/
	// B <- A, C <- A
	int i;
	for (i=0; i<n; i++) {
		B[i] = A[i];
		C[i] = A[i];
	}
}

void seq_minima(int *array, int n, int choice) {
	int i;
	int Z[n];

	// terminating case
	if (n == 1) {
		return;
	}

	for (i=0; i<n; i++) {
		if (i%2 == 0) {
			Z[i/2] = min(array[i], array[i+1]);
		}
	}

	// recursion
	seq_minima(Z, n/2, choice);

	if (choice == 1) {
		for (i=1; i<n; i++) {
			if (i%2 == 1) {
				array[i] = Z[i/2];
			} else {
				array[i] = min(array[i], Z[i/2 - 1]);
			}
		}
	} else {
		for (i=n-2; i>=0; i--) {
			if (i%2 == 0) {
				array[i] = Z[i/2];
			} else {
				array[i] = min(array[i], Z[i/2 + 1]);
			}
		}
	}
}

void seq_function(int m, int show_output){
	/* The code for sequential algorithm */
	// Perform operations on B and C
	seq_minima(B, m, 0);
	if (show_output) {
		printf("resulting suffix_minima array:\n");
		print_array(B, file_size);
	}
	seq_minima(C, m, 1);
	if (show_output) {
		printf("resulting prefix_minima array:\n");
		print_array(C, file_size);
	}
}		

void openmp_minima(int *array, int n, int choice, int num_threads) {
	int i, chunk;
	int Z[n];

	// terminating case
	if (n == 1) {
		return;
	}
	chunk = CHUNK_SIZE;

#pragma omp parallel for shared(Z, array, chunk, num_threads) \
	private(i) schedule(static, chunk) num_threads(num_threads) 
	for (i=0; i<n; i++) {
		if (i%2 == 0) {
			Z[i/2] = min(array[i], array[i+1]);
		}
	}

	// recursion
	openmp_minima(Z, n/2, choice);

	if (choice == 1) {
#pragma omp parallel for shared(Z, array, chunk, num_threads) \
		private(i) schedule(static, chunk) num_threads(num_threads) 
		for (i=1; i<n; i++) {
			if (i%2 == 1) {
				array[i] = Z[i/2];
			} else {
				array[i] = min(array[i], Z[i/2 - 1]);
			}
		}
	} else {
#pragma omp parallel for shared(Z, array, chunk, num_threads) \
		private(i) schedule(static, chunk) num_threads(num_threads) 
		for (i=n-2; i>=0; i--) {
			if (i%2 == 0) {
				array[i] = Z[i/2];
			} else {
				array[i] = min(array[i], Z[i/2 + 1]);
			}
		}
	}

}

void openmp_function(int m, int show_output, int nt){
	/* The code for sequential algorithm */
	// Perform operations on B and C
	openmp_minima(B, m, 0, nt);
	if (show_output) {
		printf("resulting suffix_minima array:\n");
		print_array(B, file_size);
	}
	openmp_minima(C, m, 1, nt);
	if (show_output) {
		printf("resulting prefix_minima array:\n");
		print_array(C, file_size);
	}
}

void *par_half_minima(void *par_arg) {
	tThreadArg *thread_arg;
	int i;

	thread_arg = (tThreadArg *)par_arg;
	for (i=thread_arg->start_index; i<thread_arg->end_index; i++) {
		if (i%2 == 0) {
			thread_arg->result[i/2] = min(thread_arg->src[i], thread_arg->src[i+1]);
		}
	}

	return NULL;
}

void *par_final_minima(void *par_arg) {
	tThreadArg *thread_arg;
	int i;
	thread_arg = (tThreadArg *)par_arg;

	if (thread_arg->choice == 0) {
		for (i=thread_arg->end_index-1; i>=thread_arg->start_index; i--) {
			if (i>thread_arg->n - 2) {
				continue;
			}
			if (i%2 == 0) {
				thread_arg->src[i] = thread_arg->result[i/2];
			} else {
				thread_arg->src[i] = min(thread_arg->src[i], thread_arg->result[i/2 + 1]);
			}
		}
	} else if (thread_arg->choice == 1) {
		for (i=thread_arg->start_index; i<thread_arg->end_index; i++) {
			if (i%2 == 1) {
				thread_arg->src[i] = thread_arg->result[i/2];
			} else if (i != 0) {
				thread_arg->src[i] = min(thread_arg->src[i], thread_arg->result[i/2 - 1]);
			}
		}
	}

	return NULL;
}

// same algo as the sequential one, just parallalize it
// choice: 0 for suffix, 1 for prefix
void par_minima(int *array, int n, int nt, int choice) {
	int i, j, ele_per_td;
	int Z[n];
	void *status;
	tThreadArg x[NUM_THREADS];

	// terminating case
	if (n == 1) {
		return;
	}

	// parallelize the original for loop
	for (i=1; i<=nt; i++) {
		ele_per_td = (int)(n / nt);
		x[i].n = n;
		x[i].start_index = (i - 1) * ele_per_td;
		x[i].end_index = i * ele_per_td;
		x[i].choice = choice;
		x[i].src = array;
		x[i].result = Z;
		pthread_create(&callThd[i-1], &attr, par_half_minima, (void *)&x[i]);
	}

	// Wait on the other threads 
	for(j=0; j<nt; j++) {
		pthread_join(callThd[j], &status);
	}

	// recursion
	par_minima(Z, n/2, nt, choice);

	// parallelize the original for loop
	for (i=1; i<=nt; i++) { 
		pthread_create(&callThd[i-1], &attr, par_final_minima, (void *)&x[i]);
	}

	// Wait on the other threads 
	for(j=0; j<nt; j++) {
		pthread_join(callThd[j], &status);
	}
}

void par_function(int num_ele, int num_threads) {
	/* The code for threaded computation */
	// Perform operations on B and C
	par_minima(B, num_ele, num_threads, 0);
	par_minima(C, num_ele, num_threads, 1);
}

int main (int argc, char *argv[])
{
	struct timeval startt, endt, result;
	int i, j, k, nt, t, n, c;

	result.tv_sec = 0;
	result.tv_usec= 0;

	/* Test Correctness */
	printf("Test for correctness:\n");

	read_file("test01.in");
	printf("Input array: \n");
	print_array(A, file_size);

	printf("Results for sequential algorithm:\n");
	init(file_size);
	seq_function(file_size, 1);

	printf("Results for openmp algorithm:\n");
	init(file_size);
	openmp_function(file_size, 1, 2);

	printf("Results for pthread algorithm:\n");
	init(file_size);
	par_function(file_size, 2);
	printf("suffix minima: \n");
	print_array(B, file_size);
	printf("preffix minima: \n");
	print_array(C, file_size);

	/* Generate a seed input */
	srand ( time(NULL) );
	for(k=0; k<NMAX; k++){
		A[k] = rand();
	}

	printf("OpenMP:\n");
	printf("|NSize|Iterations| Seq | Th01 | Th02 | Th04 | Th08 | Par16|\n");

	// for each input size
	for(c=0; c<NSIZE; c++){
		n=Ns[c];
		printf("| %d | %d |",n,TIMES);

		/* Run sequential algorithm */
		result.tv_usec=0;
		gettimeofday (&startt, NULL);
		for (t=0; t<TIMES; t++) {
			init(n);
			seq_function(n, 0);
		}
		gettimeofday (&endt, NULL);
		result.tv_usec = (endt.tv_sec*1000000+endt.tv_usec) - (startt.tv_sec*1000000+startt.tv_usec);
		printf(" %ld.%06ld | ", result.tv_usec/1000000, result.tv_usec%1000000);

		/* Run OpenMP algorithm */
		for(nt=1; nt<NUM_THREADS; nt=nt<<1){
			result.tv_sec=0; 
			result.tv_usec=0;
			gettimeofday (&startt, NULL);
			for (t=0; t<TIMES; t++) 
			{
				init(n);
				openmp_function(n, 0, nt);
			}
			gettimeofday (&endt, NULL);
			result.tv_usec += (endt.tv_sec*1000000+endt.tv_usec) - (startt.tv_sec*1000000+startt.tv_usec);
			printf(" %ld.%06ld | ", result.tv_usec/1000000, result.tv_usec%1000000);
		}
		printf("\n");
	}

	/* Initialize and set thread detached attribute */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	printf("Pthread:\n");
	printf("|NSize|Iterations| Seq | Th01 | Th02 | Th04 | Th08 | Par16|\n");

	// for each input size
	for(c=0; c<NSIZE; c++){
		n=Ns[c];
		printf("| %d | %d |",n,TIMES);

		/* Run sequential algorithm */
		result.tv_usec=0;
		gettimeofday (&startt, NULL);
		for (t=0; t<TIMES; t++) {
			init(n);
			seq_function(n, 0);
		}
		gettimeofday (&endt, NULL);
		result.tv_usec = (endt.tv_sec*1000000+endt.tv_usec) - (startt.tv_sec*1000000+startt.tv_usec);
		printf(" %ld.%06ld | ", result.tv_usec/1000000, result.tv_usec%1000000);

		/* Run pthread algorithm(s) */
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

			result.tv_sec=0; 
			result.tv_usec=0;

			gettimeofday (&startt, NULL);
			for (t=0; t<TIMES; t++) 
			{
				init(n);
				par_function(n, nt);
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
}

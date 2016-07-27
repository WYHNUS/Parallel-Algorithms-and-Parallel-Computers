#include <stdio.h>
#include <string.h>
#include <omp.h>
#include <pthread.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

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
	int *S;
	int *R;
} tThreadArg;


pthread_t callThd[NUM_THREADS];
pthread_mutex_t mutexpm;
pthread_barrier_t barr, internal_barr;
pthread_attr_t attr;

// Seed Input
int A[NMAX];
// S is successor array, R is range array
int S[NMAX];
int R[NMAX];

int data_length;

void read_file(char *file_name) {
	int i = 1;
	int index, num;
	FILE *file;
	file = fopen(file_name, "r");
	if (file == NULL) {
		printf("Error Reading File\n");
		exit(0);
	}
	while (fscanf(file, "%d %d", &index, &num) > 0) {
		A[index] = num;
		i++;
	} 
	data_length = i;
}

void print_array(int *array, int length) {
	int i;
	for (i=0; i<length; i++) {
		printf("%d ", array[i]);
	}
	printf("\n");
}

void init(int n){
	/* Initialize the input for this iteration*/
	int i;
	for (i=0; i<n; i++) {
		S[i] = A[i];
		R[i] = 0;
	}
}

void seq_function(int n) {
	/* The code for sequential algorithm */
	int i, j, not_finish;
	for (i=0; i<n; i++) {
		if (S[i] == 0) {
			R[i] = 0;	// root
		} else {
			R[i] = 1;
		}
	}

	for (i=0; i<log2(n); i++) {
		for (j=1; j<n; j++) {
			if (S[j] != 0) {	// not root
				R[j] += R[S[j]];
				S[j] = S[S[j]];
			}
		}
	}
	/*
	   not_finish = 1;
	   while (not_finish) {
	   not_finish = 0;
	   for (i=1; i<data_length; i++) {
	   if (S[i] != 0) {	// not root
	   R[i] += R[S[i]];
	   S[i] = S[S[i]];
	   not_finish = 1; 
	   }
	   }
	   }
	 */
}

void openmp_function(int n, int num_threads){
	/* The code for sequential algorithm */
	int i, j, chunk;
	chunk = CHUNK_SIZE;

#pragma omp parallel for shared(S, R) private(i) \
	schedule(static, chunk) num_threads(num_threads)
	for (i=0; i<n; i++) {
		if (S[i] == 0) {
			R[i] = 0;	// root
		} else {
			R[i] = 1;
		}
	}


	for (i=0; i<log2(n); i++) {
#pragma omp parallel for shared(S, R, i) private(j) \
		schedule(static, chunk) num_threads(num_threads)
		for (j=1; j<n; j++) {
			if (S[j] != 0) {	// not root
				R[j] += R[S[j]];
				S[j] = S[S[j]];
			}
		}

	}
}

void *init_dist(void *par_arg) {
	tThreadArg *thread_arg;
	int i;

	thread_arg = (tThreadArg *)par_arg;
	int size = (int)(thread_arg->n / thread_arg->nrT);
	int si = size * (thread_arg->id - 1) + 1;
	int ei = size * thread_arg->id + 1;

	for (i=si; i<ei; i++) {
		if (thread_arg->S[i] == 0) {
			thread_arg->R[i] = 0;
		} else {
			thread_arg->R[i] = 1;
		}
	}
}

void *search_root(void *par_arg) {
	tThreadArg *thread_arg;
	int j;

	thread_arg = (tThreadArg *)par_arg;
	int size = (int)(thread_arg->n / thread_arg->nrT);
	int si = size * (thread_arg->id - 1) + 1;
	int ei = size * thread_arg->id + 1;
	int *S = thread_arg->S;
	int *R = thread_arg->R;

	for (j=si; j<ei; j++) {	
		if (S[j] != 0) {	// not root
			R[j] += R[S[j]];
			S[j] = S[S[j]];
		}
	}
}

void par_function(int n, int nt){
	/* The code for threaded computation */
	int i, j;
	void *status;
	tThreadArg x[NUM_THREADS];

	// parallelize the original for loop
	for (j=1; j<=nt; j++)
	{
		x[j].id = j; 
		x[j].nrT=nt; // number of threads in this round
		x[j].n=n;  //input size
		x[j].R = R;
		x[j].S = S;
		pthread_create(&callThd[j-1], &attr, init_dist, (void *)&x[j]);
	}

	/* Wait on the other threads */
	for(j=0; j<nt; j++)
	{
		pthread_join(callThd[j], &status);
	}

	for (i=0; i<log2(n); i++) {
		for (j=1; j<=nt; j++)
		{
			pthread_create(&callThd[j-1], &attr, search_root, (void *)&x[j]);
		}
		// Wait on the other threads 
		for(j=0; j<nt; j++)
		{
			pthread_join(callThd[j], &status);
		}
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

	read_file("test01.in");
	printf("Input array: (ignore leading 0)\n");
	print_array(A, data_length);

	printf("Results for sequential algorithm: (ignore leading 0)\n");
	init(data_length);
	seq_function(data_length);
	print_array(R, data_length);

	printf("Results for openmp algorithm: (ignore leading 0)\n");
	init(data_length);
	openmp_function(data_length);
	print_array(R, data_length);

	printf("Results for pthread algorithm: (ignore leading 0)\n");
	init(data_length);
	par_function(data_length, 2);
	print_array(R, data_length);

	// Generate a seed input 
	srand ( time(NULL) );
	for(k=0; k<NMAX; k++){
		S[k] = rand();
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
			seq_function(n);
		}
		gettimeofday (&endt, NULL);
		result.tv_usec = (endt.tv_sec*1000000+endt.tv_usec) - (startt.tv_sec*1000000+startt.tv_usec);
		printf(" %ld.%06ld | ", result.tv_usec/1000000, result.tv_usec%1000000);

		// Run OpenMP algorithm(s) 
		for(nt=1; nt<NUM_THREADS; nt=nt<<1){
			result.tv_sec=0; 
			result.tv_usec=0;
			gettimeofday (&startt, NULL);
			for (t=0; t<TIMES; t++) 
			{
				init(n);
				openmp_function(n, nt);
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
			seq_function(n);
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

			result.tv_sec=0; 
			result.tv_usec=0;

			gettimeofday (&startt, NULL);
			for (t=0; t<TIMES; t++) 
			{
				init(n);
				par_function(n-1, nt);
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

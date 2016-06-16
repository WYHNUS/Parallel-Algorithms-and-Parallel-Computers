#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

// Number of threads
#define NUM_THREADS 32

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
} tThreadArg;

pthread_t callThd[NUM_THREADS];
pthread_mutex_t mutexpm;
pthread_barrier_t barr, internal_barr;

// Seed Input
int A[NMAX];
// Store result
int B[NMAX];
int C[NMAX];
// file size
int file_size;

void read_file(char* file_name) {
	int i = 0;
	int num;
	FILE* file;
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

void print_array(int* array, int length) {
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

void seq_suffix_minima(int* array, int n) {
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
	seq_suffix_minima(Z, n/2);

	for (i=n-2; i>=0; i--) {
		if (i%2 == 0) {
			array[i] = Z[i/2];
		} else {
			array[i] = min(array[i], Z[i/2 + 1]);
		}
	}
	// debugging
	// printf("result array:\n");
	// print_array(array, n);
}

void seq_prefix_minima(int* array, int n) {
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
	seq_prefix_minima(Z, n/2);

	for (i=1; i<n; i++) {
		if (i%2 == 1) {
			array[i] = Z[i/2];
		} else {
			array[i] = min(array[i], Z[i/2 - 1]);
		}
	}
	// debugging
	// printf("result array:\n");
	// print_array(array, n);
}

void seq_function(int m, int show_output){
	/* The code for sequential algorithm */
	// Perform operations on B
	if (show_output) {
		printf("initial array:\n");
		print_array(A, file_size);
	}
	seq_suffix_minima(B, m);
	if (show_output) {
		printf("resulting suffix_minima array:\n");
		print_array(B, file_size);
	}
	seq_prefix_minima(C, m);
	if (show_output) {
		printf("resulting prefix_minima array:\n");
		print_array(C, file_size);
	}
}

void* par_function(void* a){
	/* The code for threaded computation */
	// Perform operations on B
	return a;
}

int main (int argc, char *argv[])
{
  	struct timeval startt, endt, result;
	int i, j, k, nt, t, n, c;
	void *status;
   	pthread_attr_t attr;
  	tThreadArg x[NUM_THREADS];
	
  	result.tv_sec = 0;
  	result.tv_usec= 0;

  	/* Test Correctness */
	read_file("test01.in");
	init(file_size);
	seq_function(file_size, 1);

	/* Generate a seed input */
	srand ( time(NULL) );
	for(k=0; k<NMAX; k++){
		A[k] = rand();
	}

   	/* Initialize and set thread detached attribute */
   	pthread_attr_init(&attr);
   	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	printf("|NSize|Iterations|Seq|Th01|Th02|Th04|Th08|Par16|\n");

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

		/* Run threaded algorithm(s) */
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
			for (j=1; j<=/*NUMTHRDS*/nt; j++)
        		{
				x[j].id = j; 
				x[j].nrT=nt; // number of threads in this round
				x[j].n=n;  //input size
				pthread_create(&callThd[j-1], &attr, par_function, (void *)&x[j]);
			}

			gettimeofday (&startt, NULL);
			for (t=0; t<TIMES; t++) 
			{
				init(n);
				pthread_barrier_wait(&barr);
			}
			gettimeofday (&endt, NULL);

			/* Wait on the other threads */
			for(j=0; j</*NUMTHRDS*/nt; j++)
			{
				pthread_join(callThd[j], &status);
			}

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

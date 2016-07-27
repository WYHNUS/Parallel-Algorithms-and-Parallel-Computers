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
	int *A;
	int *B;
	int *C;
	int B_length;
	int *sa;
	int *sb;
	int part_size;
	int num_part;
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

// void seq_merge(int *A, int *B, int *C, int A_length, int B_length) {
// 	int i, cur_rank;

// 	for (i=0; i<A_length; i++) {
// 		cur_rank = get_rank(A[i], B, 0, B_length, B_length);
// 		C[cur_rank + i] = A[i];
// 	}
// 	for (i=0; i<B_length; i++) {
// 		cur_rank = get_rank(B[i], A, 0, A_length, A_length);
// 		C[cur_rank + i] = B[i];
// 	}
// }

// si: start_index; ei: end_index
void opt_seq_merge(int *A, int*B, int *C, int si_A, int ei_A, int si_B, int ei_B) {
	/* The code for optimal sequential merge */
	int i = si_A;
	int j = si_B;
	while (i<ei_A || j<ei_B) {
		if (i<ei_A && A[i] < B[j]) {
			C[i+j] = A[i];
			i++;
		} else {
			C[i+j] = B[j];
			j++;
		}
	}
}

void seq_merge(int *A, int *B, int *C, int A_length, int B_length) {
	int i;
	int num_part = log2(A_length);
	int part_size = A_length/num_part;
	int sa[num_part+1];
	int sb[num_part+1];
	sa[0] = 0; sb[0] = 0; 
	sa[num_part] = A_length; sb[num_part] = B_length;

	for (i=1; i<num_part; i++) {
		sa[i] = sa[i-1] + part_size;
		sb[i] = get_rank(A[i*part_size-1], B, 0, B_length);
	}

	for (i=0; i<num_part; i++) {
		opt_seq_merge(A, B, C, sa[i], sa[i+1], sb[i], sb[i+1]);
	}
}

void openmp_function(int *A, int *B, int *C, int A_length, int B_length, int num_threads) {
	/* The code for sequential algorithm */
	int i, chunk;
	chunk = CHUNK_SIZE;

	int num_part = log2(A_length);
	int part_size = A_length/num_part;
	int sa[num_part+1];
	int sb[num_part+1];
	sa[0] = 0; sb[0] = 0; 
	sa[num_part] = A_length; sb[num_part] = B_length;

#pragma omp parallel for shared(A, sa, sb, chunk, num_threads) \
	private(i) schedule(static, chunk) num_threads(num_threads) 
	for (i=1; i<num_part; i++) {
		sa[i] = sa[i-1] + part_size;
		sb[i] = get_rank(A[i*part_size-1], B, 0, B_length);
	}

#pragma omp parallel for shared(A, B, C, sa, sb, chunk, num_threads) \
	private(i) schedule(static, chunk) num_threads(num_threads) 
	for (i=0; i<num_part; i++) {
		opt_seq_merge(A, B, C, sa[i], sa[i+1], sb[i], sb[i+1]);
	}
}

void *calc_rank(void *par_arg) {
	tThreadArg *thread_arg;
	thread_arg = (tThreadArg *)par_arg;

	int i, j, num_part, part_size, ele_per_td, si, ei;
	int *sa = thread_arg->sa;
	int *sb = thread_arg->sb;

	j = thread_arg->id;
	num_part = thread_arg->num_part;
	part_size = thread_arg->part_size;

	ele_per_td = (int)(num_part / thread_arg->nrT);
	si = (j - 1) * ele_per_td;
	ei = j * ele_per_td;

	for (i=si; i<ei; i++) {
		sa[i] = i * part_size;
		sb[i] = get_rank(thread_arg->A[i*part_size-1], thread_arg->B, 0, thread_arg->B_length);
	}

	return NULL;
}

void *par_merge(void *par_arg) {
	tThreadArg *thread_arg;
	thread_arg = (tThreadArg *)par_arg;

	int i, j, num_part, ele_per_td, si, ei;
	int *sa = thread_arg->sa;
	int *sb = thread_arg->sb;

	j = thread_arg->id;
	num_part = thread_arg->num_part;

	ele_per_td = (int)(num_part / thread_arg->nrT);
	si = (j - 1) * ele_per_td;
	ei = j * ele_per_td;

	for (i=si; i<ei; i++) {
		opt_seq_merge(thread_arg->A, thread_arg->B, thread_arg->C, sa[i], sa[i+1], sb[i], sb[i+1]);
	}
}

void par_function(int *A, int *B, int *C, int A_length, int B_length, int nt){
	/* The code for threaded computation */
	void *status;
	tThreadArg x[NUM_THREADS];

	int j;
	int num_part = log2(A_length);
	int part_size = A_length/num_part;
	int sa[num_part+1];
	int sb[num_part+1];
	sa[0] = 0; sb[0] = 0; 
	sa[num_part] = A_length; sb[num_part] = B_length;

	// parallelize the original for loop
	for (j=1; j<=nt; j++)
	{
		x[j].id = j; 
		x[j].nrT=nt; // number of threads in this round
		x[j].A = A;
		x[j].B = B;
		x[j].C = C;
		x[j].B_length = B_length;
		x[j].sa = sa;
		x[j].sb = sb;
		x[j].part_size = part_size;
		x[j].num_part = num_part;
		pthread_create(&callThd[j-1], &attr, calc_rank, (void *)&x[j]);
	}

	/* Wait on the other threads */
	for(j=0; j<nt; j++)
	{
		pthread_join(callThd[j], &status);
	}

	// parallelize the original for loop
	for (j=1; j<=nt; j++)
	{
		pthread_create(&callThd[j-1], &attr, par_merge, (void *)&x[j]);
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
	openmp_function(A, B, C, vector_A_size, vector_B_size);
	print_array(C, vector_A_size + vector_B_size);

	printf("Results for pthread algorithm:\n");
	init(vector_A_size + vector_B_size);
	par_function(A, B, C, vector_A_size, vector_B_size, 3);
	print_array(C, vector_A_size + vector_B_size);

	/* Generate a seed input */
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

		/* Run sequential algorithm */
		result.tv_usec=0;
		gettimeofday (&startt, NULL);
		for (t=0; t<TIMES; t++) {
			init(n);
			opt_seq_merge(A, B, C, 0, vector_A_size, 0, vector_B_size);
		}
		gettimeofday (&endt, NULL);
		result.tv_usec = (endt.tv_sec*1000000+endt.tv_usec) - (startt.tv_sec*1000000+startt.tv_usec);
		printf(" %ld.%06ld | ", result.tv_usec/1000000, result.tv_usec%1000000);

		/* Run OpenMP algorithm(s) */
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
			opt_seq_merge(A, B, C, 0, vector_A_size, 0, vector_B_size);
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
}

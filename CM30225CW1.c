#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include <sys/time.h>

// Contains arguments to be passed to row_calc.
struct args {
	// Contains the matrices, one must be read from and the other written to. These roles swap after each iteration.
	double **arr;
	double **arr2;
	long unsigned int length;
	// Works as the end condition for iterations. Will finish once all values meet this difference.
	double minimal_difference;
	char stop;
	char final_array;
	// Used by threads to figure out the sections of the matrices they will work on. Once chosen they will set the value of the section index to 1.
	char *available;
	// Used by threads to communicate the largest difference between values they found.
	double *largest_difference;
	long unsigned int thread_count;
	// Tools to help threads not trip over each other.
	pthread_mutex_t lock;
	pthread_barrier_t barrier;
};

// Procedure to be used by threads. Applies relaxation technique to a section of each matrix.
void *row_calc(void *arg){
	struct args *matrices = (struct args *)arg;
	// Take copies of arguments so threads don't have to wait on each other to read.
	long unsigned int length = matrices->length;
	long unsigned int thread_count = matrices->thread_count;
	// Used to contain the index a thread will work on and act as a rough id.
	long unsigned int thread_num;

	// Prevents data race mentioned in document. Threads could end up choosing same section so they choose their section one at a time.
	pthread_mutex_lock(&matrices->lock);

	long unsigned int start_row, last_row;
	
	// Loop through all of the available indexes to look for the first free option.
	for(long unsigned int i = 0; i < thread_count; ++i) {
		// Is the section free?
		if(matrices->available[i] == 0) {
			// Yes, show that it's taken.
			matrices->available[i] = 1;
			// Let other threads continue.
			pthread_mutex_unlock(&matrices->lock);
			
			thread_num = i;
			// Figure out where to start from.

			// Is the length of the submatrix a multiple of the number of threads?
			if((length - 2) % thread_count == 0) {
				// Yes. Section length is uniform.
				start_row = 1 + (i * ((length - 2) / thread_count));
				last_row = start_row + ((length - 2) / thread_count);
			// Submatrix length is not a multiple of thread count, therefore section sizes are not equal. Distribute additional length fairly across earlier sections.
			// Is thread writing to an earlier section?
			} else if (((length - 2) % thread_count) - thread_num > 0) {
				// Yes. Adjust their start and end row.
				start_row = 1 + (i * ((length - 2) / thread_count)) + i;
				last_row = start_row + ((length - 2) / thread_count) + 1;
			} else {
				// Section comes after those that have had their size adjusted. Account for the other section adjustments.
				start_row = 1 + (i * ((length - 2) / thread_count)) + ((length - 2) % thread_count);
				last_row = start_row + ((length - 2) / thread_count);
			}
			break;
		}
	}
	
	// Used to flip between matrices.
	char i = 0;
	// Is the largest difference between values smaller than the minimal difference?
	while(matrices->stop != 1){
		// No. Apply relaxation.
		// Synchronise threads ready for an iteration.
		pthread_barrier_wait(&matrices->barrier);
		// Working on first matrix?
		if(i == 0) {
			// Yes. Iterate through section.
			for(long unsigned int x = start_row; x < last_row; ++x) {
				for(long unsigned int y = 1; y < length-1; ++y) {
					// Apply average of four neighbours and set new value.
					matrices->arr[x][y] = (matrices->arr2[x + 1][y] + matrices->arr2[x - 1][y] + matrices->arr2[x][y + 1] + matrices->arr2[x][y - 1]) / 4;
					// Is the difference between the old and value larger than the current largest difference for this thread?
					if(fabs(matrices->arr[x][y] - matrices->arr2[x][y]) > matrices->largest_difference[thread_num]) {
						// Yes. Update the value.
						matrices->largest_difference[thread_num] = fabs(matrices->arr[x][y] - matrices->arr2[x][y]);
					}
				}
			}
		} else {
			// No. Work on second matrix.
			for(long unsigned int x = start_row; x < last_row; ++x) {
				for(long unsigned int y = 1; y < length-1; ++y) {
					// Apply average of four neighbours and set new value.
					matrices->arr2[x][y] = (matrices->arr[x + 1][y] + matrices->arr[x - 1][y] + matrices->arr[x][y + 1] + matrices->arr[x][y - 1]) / 4;
					// Is the difference between the old and value larger than the current largest difference for this thread?
					if(fabs(matrices->arr[x][y] - matrices->arr2[x][y]) > matrices->largest_difference[thread_num]) {
						// Yes. Update the value.
						matrices->largest_difference[thread_num] = fabs(matrices->arr[x][y] - matrices->arr2[x][y]);
					}
				}
			}
		}
		
		// Wait for all other threads to be done.
		pthread_barrier_wait(&matrices->barrier);
		// Is thread working on first section?
		if(start_row == 1) {
			// Yes, iterate through largest differences and see if the end condition is met.
			matrices->stop = 1;
			matrices->final_array = i;
			for(long unsigned int j = 0; j < matrices->thread_count; ++j){
				// Is the difference bigger than the minimal difference?
				if(matrices->largest_difference[j] > matrices->minimal_difference) {
					// Yes. Continue iterations.
					matrices->stop = 0;
				}
				matrices->largest_difference[j] = 0;
			}
		}
		// Swap matrices.
		if(i==0) i = 1;
		else i = 0;
		// Make threads wait for result of check.
		pthread_barrier_wait(&matrices->barrier);
	}
	return NULL;
}

// Prints values of square matrices.
void print_array(double **matrix, long unsigned int length) {

	long unsigned int x, y;

	printf("\n%ld x %ld Matrix\n", length, length);
	for(x = 0; x < length; ++x){
		for(y = 0; y < length; ++y) {
			printf("%f ", matrix[x][y]);
		}
		printf("\n");
	}
}


int main() {
	// Used to calculate time taken by program.
	struct timeval begin, end;
	gettimeofday(&begin, 0);

	// ARGUMENTS TO BE PASSED TO THREADS. PLAY AROUND WITH THESE AT YOUR PERIL.
	struct args matrices;
	matrices.minimal_difference = 0.0001;
	matrices.length = 100;
	// DO YOU WANT TO CHOOSE THE NUMBER OF THREADS? 1 FOR YES.
	char manual_threading_on = 1;
	// Do you want to use the Matrix B (non complex) from the document. 1 for yes, else Matrix A will be used.
	char default_values = 1;

	// Is manual threading on?
	if(manual_threading_on == 1) {
		// SET AMOUNT OF THREADS YOU WANT HERE.
		matrices.thread_count = 2;
	} else {
		// No, calculate a reasonable amount of threads for to use.
		if(matrices.length - 2 > 4300) {
			// Max threads is based on available amount of cores on Azure. 1 thread is used for main, so 43 are available.
			// Can reasonably change this to 44 for most matrices.
			matrices.thread_count = 43;
		}
		else {
			// Calculates threads by a minimum section size of 100 indexes.
			matrices.thread_count = ceil((matrices.length - 2) / (double)100);
		}
	}
	
	// Set up the lock. Check if successful.
	if(pthread_mutex_init(&matrices.lock, NULL) != 0) {
		printf("Mutex initialisation failed.\n");
		return 1;
	}

	// Some error checks.
	// Is the number of threads greater than the indexes available?
	if(matrices.thread_count > matrices.length - 2) {
		// Yes, end program and inform user.
		printf("Total threads greater than effective row count (total rows - 2)\n");
		return 1;
	}

	// Is the matrix an invalid size?
	if(matrices.length < 3) {
		// Yes, end program and inform user.
		printf("Invalid array length of %ld\n", matrices.length);
		return 1;
	}

	// Matrix initialisation
	matrices.arr = malloc(matrices.length * sizeof(*matrices.arr));
	matrices.arr2 = malloc(matrices.length * sizeof(*matrices.arr2));

	// Was Matrix B chosen?
	if(default_values == 1) {
		// Yes, set up matrix following that pattern.
		for(long unsigned int r = 0; r<matrices.length; r++) {
			matrices.arr[r] = malloc(matrices.length * sizeof(*matrices.arr[r]));
			matrices.arr2[r] = malloc(matrices.length * sizeof(*matrices.arr2[r]));
			for(long unsigned int c=0; c<matrices.length; ++c) {
				if(r==0 || c==0) {
					matrices.arr[r][c] = 1;
				}
				else {
					matrices.arr[r][c] = 0;
				}
				matrices.arr2[r][c] = matrices.arr[r][c];
			}
		}
	} else {
		// No, set up matrix following A's pattern
		for(long unsigned int r = 0; r<matrices.length; r++) {
			matrices.arr[r] = malloc(matrices.length * sizeof(*matrices.arr[r]));
			matrices.arr2[r] = malloc(matrices.length * sizeof(*matrices.arr2[r]));
			for(long unsigned int c=0; c<matrices.length; ++c) {
				if(r%2==1) {
					if(c%2==0) {
						matrices.arr[r][c] = 0;
					}
					else {
						matrices.arr[r][c] = 1;
					}
				} else {
					if(c%2==0) {
						matrices.arr[r][c] = 1;
					}
					else {
						matrices.arr[r][c] = 0;
					}
				}
				matrices.arr2[r][c] = matrices.arr[r][c];
			}
		}
	}

	// Set up array to hold largest difference found in each section for each iteration.
	matrices.largest_difference = malloc(matrices.thread_count * sizeof(double));
	// Set up array for threads to choose sections from.
	matrices.available = malloc(matrices.thread_count * sizeof(char));
	// Assign values to both arrays.
	for(long unsigned int i = 0; i < matrices.thread_count; ++i) {
		matrices.available[i] = 0;
		matrices.largest_difference[i] = 0;
	}


	// Try creating a barrier. Was it successful?
	if(pthread_barrier_init(&matrices.barrier, NULL, matrices.thread_count) != 0) {
		// No, send error message and close program/
		printf("Barrier initialization failed.\n");
		return 1;
	}

	// Print starting matrix.
	print_array(matrices.arr, matrices.length);

	// Begin creating threads to match thread count.
	pthread_t *tid = malloc(matrices.thread_count * sizeof(pthread_t));
	
	for(long unsigned int i = 0; i < matrices.thread_count; ++i){
		// Try createing threads.
		if(pthread_create(&tid[i], NULL, row_calc, (void *)&matrices) != 0) {
			// Thread creation unsuccesful, print error and end program.
			printf("Thread creation error\n");
			return 1;
		}
	}
	
	// Make sure main won't exit until threads are finished.
	for (long unsigned int i = 0; i < matrices.thread_count; ++i){
		pthread_join(tid[i], NULL);
	}

	// Dispose of parallel tools.
	pthread_mutex_destroy(&matrices.lock);
	pthread_barrier_destroy(&matrices.barrier);
	
	// Calculate time taken to process matrices.
	gettimeofday(&end, 0);
	long seconds = end.tv_sec - begin.tv_sec;
	long microseconds = end.tv_usec - begin.tv_usec;
	double t = seconds + microseconds*1e-6;

	// Print final matrix values. Comment this out if you don't want to see values.
	printf("Final matrix...\n");
	if(matrices.final_array == 0) {
		print_array(matrices.arr, matrices.length);
	}
	else {
		print_array(matrices.arr2, matrices.length);
	}

	
	// Print time taken.
	printf("Result took... %f seconds\n", t);
	printf("Total threads: %ld\n", matrices.thread_count);
	return 0;
}
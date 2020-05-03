/* Gaussian elimination code.
 * 
 * Author: Naga Kandasamy
 * Date of last update: April 22, 2020
 *
 * Student name(s): Nicholas Sica and Cameron Calv
 * Date: 5/1/2020
 *
 * Compile as follows: 
 * gcc -o gauss_eliminate gauss_eliminate.c compute_gold.c -O3 -Wall -lpthread -lm
 */

#include "gauss_eliminate.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s matrix-size\n", argv[0]);
        fprintf(stderr, "matrix-size: width and height of the square matrix\n");
        exit(EXIT_FAILURE);
    }
    
    int matrix_size = atoi(argv[1]);

    Matrix A;			                                    /* Input matrix */
    Matrix U_reference;		                                    /* Upper triangular matrix computed by reference code */
    Matrix U_mt;			                            /* Upper triangular matrix computed by pthreads */

    fprintf(stderr, "Generating input matrices\n");
    srand(time (NULL));                                             /* Seed random number generator */
    A = allocate_matrix(matrix_size, matrix_size, 1);               /* Allocate and populate random square matrix */
    U_reference = allocate_matrix (matrix_size, matrix_size, 0);    /* Allocate space for reference result */
    U_mt = allocate_matrix (matrix_size, matrix_size, 0);           /* Allocate space for multi-threaded result */

    /* Copy contents A matrix into U matrices */
    int i, j;
    for (i = 0; i < A.num_rows; i++) {
        for (j = 0; j < A.num_rows; j++) {
            U_reference.elements[A.num_rows * i + j] = A.elements[A.num_rows * i + j];
            U_mt.elements[A.num_rows * i + j] = A.elements[A.num_rows * i + j];
        }
    }

    fprintf(stderr, "\nPerforming gaussian elimination using reference code\n");
    struct timeval start, stop;
    gettimeofday(&start, NULL);
    
    int status = compute_gold(U_reference.elements, A.num_rows);
  
    gettimeofday(&stop, NULL);
    fprintf(stderr, "CPU run time = %0.2f s\n", (float)(stop.tv_sec - start.tv_sec\
                + (stop.tv_usec - start.tv_usec) / (float)1000000));

    if (status < 0) {
        fprintf(stderr, "Failed to convert given matrix to upper triangular. Try again.\n");
        exit(EXIT_FAILURE);
    }
  
    status = perform_simple_check(U_reference);	/* Check that principal diagonal elements are 1 */ 
    if (status < 0) {
        fprintf(stderr, "Upper triangular matrix is incorrect. Exiting.\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Single-threaded Gaussian elimination was successful.\n");
  
    /* FIXME: Perform Gaussian elimination using pthreads. 
     * The resulting upper triangular matrix should be returned in U_mt */
    fprintf(stderr, "\nPerforming gaussian elimination using pthreads\n");
    gauss_eliminate_using_pthreads(U_mt);

    /* Check if pthread result matches reference solution within specified tolerance */
    fprintf(stderr, "\nChecking results\n");
    int size = matrix_size * matrix_size;
    int res = check_results(U_reference.elements, U_mt.elements, size, 1e-6);
    fprintf(stderr, "TEST %s\n", (0 == res) ? "PASSED" : "FAILED");

    /* Free memory allocated for matrices */
    free(A.elements);
    free(U_reference.elements);
    free(U_mt.elements);

    exit(EXIT_SUCCESS);
}


/* Perform gaussian elimination using pthreads */
void gauss_eliminate_using_pthreads(Matrix U)
{
    int size = U.num_rows * U.num_columns;
    pthread_barrierattr_t barrier_attr;
    pthread_barrierattr_init(&barrier_attr);	
    for(int i = 0; i < U.num_rows; i++)
    {
	pthread_barrier_t div_barrier;
	pthread_barrier_init(&div_barrier, &barrier_attr, NUM_THREADS + 1);
    
	int start_row = i * U.num_columns + i;
	int end_row = (i + 1) * U.num_columns;
	float piv_element = U.elements[start_row - 1];
	U.elements[start_row] = 1;
	start_row++;

        // Divide row i with the pivot element
        pthread_t *tid = malloc(NUM_THREADS * sizeof(pthread_t));
	pthread_attr_t thread_attr;
	pthread_attr_init(&thread_attr);

	divide_data_t *div_data = malloc(NUM_THREADS * sizeof(divide_data_t));
	for(int j = 0; j < NUM_THREADS; j++)
	{
	    div_data[j].start = start_row + j;
	    div_data[j].end = end_row;
	    div_data[j].piv_element = piv_element;
	    div_data[j].elements = U.elements;
	    div_data[j].barrier = &div_barrier;
	}
	    
	for(int j = 0; j < NUM_THREADS; j++)
	    pthread_create(&tid[j], &thread_attr, divide, (void *)&div_data[j]);

	pthread_barrier_wait(&div_barrier);
	
	// Don't do elimination if we are at the last row
	if(i != U.num_rows - 1)
	{
	    // Eliminate rows (i + 1) to (n - 1)
	    pthread_barrier_t elim_barrier;
	    pthread_barrier_init(&elim_barrier, &barrier_attr, NUM_THREADS + 1);
	    
	    int num_elements = size - (i + 1) * U.num_columns;
	    int chunk_size = (int)floor((float)num_elements / (float)NUM_THREADS);
	    elim_data_t *elim_data = malloc(NUM_THREADS * sizeof(elim_data_t));
	    for(int j = 0; j < NUM_THREADS; j++)
	    {
		elim_data[j].tid = j;
		elim_data[j].chunk_size = chunk_size;
		elim_data[j].start = end_row + (chunk_size * i);
		elim_data[j].num_iter = i;
		elim_data[j].piv_element = piv_element;
		elim_data[j].matrix = &U;
		elim_data[j].barrier = &elim_barrier;
	    }

            for(int j = 0; j < NUM_THREADS; j++)
		pthread_create(&tid[j], &thread_attr, eliminate, (void *)&elim_data[j]);
	    
	    pthread_barrier_wait(&elim_barrier);
	}
    }
}

void *divide(void *args)
{
    divide_data_t *thread_data = (divide_data_t *)args;
    for(int i = thread_data->start; i < thread_data->end; i += NUM_THREADS)
	thread_data->elements[i] = thread_data->elements[i] / thread_data->piv_element;

    pthread_barrier_wait(thread_data->barrier);
}

void *eliminate(void *args)
{
    elim_data_t *thread_data = (elim_data_t *)args;
    float elim_val = 0;
    int elim_row = -1;
    Matrix *matrix = thread_data->matrix;
    if(thread_data->tid < NUM_THREADS - 1)
	for(int i = thread_data->start; i < (thread_data->start + thread_data->chunk_size); i++)
	{
	    int new_elim_row = floor(i / matrix->num_columns);
	    if(new_elim_row != elim_row)
	    {
		elim_row = new_elim_row;
		elim_val = thread_data->piv_element / matrix->elements[elim_row * matrix->num_columns + thread_data->num_iter];
	    }

	    matrix->elements[i] = matrix->elements[i] - elim_val * matrix->elements[i - matrix->num_columns];
	}
    else
	for(int i = thread_data->start; i < matrix->num_columns * matrix->num_rows; i++)
	{
	    int new_elim_row = floor(i / matrix->num_columns);
	    if(new_elim_row != elim_row)
	    {
		elim_row = new_elim_row;
		elim_val = thread_data->piv_element / matrix->elements[new_elim_row * matrix->num_columns + thread_data->num_iter];
	    }

	    matrix->elements[i] = matrix->elements[i] - elim_val * matrix->elements[i - matrix->num_columns];
	}
    
    pthread_barrier_wait(thread_data->barrier);
}

/* Check if results generated by single threaded and multi threaded versions match within tolerance */
int check_results(float *A, float *B, int size, float tolerance)
{
    int i;
    for (i = 0; i < size; i++)
        if(fabsf(A[i] - B[i]) > tolerance)
            return -1;
    return 0;
}


/* Allocate a matrix of dimensions height*width
 * If init == 0, initialize to all zeroes.  
 * If init == 1, perform random initialization. 
*/
Matrix allocate_matrix(int num_rows, int num_columns, int init)
{
    int i;
    Matrix M;
    M.num_columns = num_columns;
    M.num_rows = num_rows;
    int size = M.num_rows * M.num_columns;
    M.elements = (float *)malloc(size * sizeof(float));
  
    for (i = 0; i < size; i++) {
        if (init == 0)
            M.elements[i] = 0;
        else
            M.elements[i] = get_random_number(MIN_NUMBER, MAX_NUMBER);
    }
  
    return M;
}

/* Return a random floating-point number between [min, max] */ 
float get_random_number(int min, int max)
{
    return (float)floor((double)(min + (max - min + 1) * ((float)rand() / (float)RAND_MAX)));
}

/* Perform simple check on upper triangular matrix if the principal diagonal elements are 1 */
int perform_simple_check(const Matrix M)
{
    int i;
    for (i = 0; i < M.num_rows; i++)
        if ((fabs(M.elements[M.num_rows * i + i] - 1.0)) > 1e-6)
            return -1;
  
    return 0;
}
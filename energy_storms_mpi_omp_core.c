#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "energy_storms.h"
#include "mpi.h"
#include "omp.h"

#define MIN(x,y) (((x) < (y)) ? (x) : (y))
#define ZERO_OR_ONE(x,y) (((x) < (y)) ? (1) : (0))

/* THIS FUNCTION CAN BE MODIFIED */
/* Function to update a single position of the layer
 * */
static float updateControlPoint( float *local_layer, int local_size, int k, int pos, float energy ) {
    /* 1. Compute the absolute value of the distance between the
        impact position and the k-th position of the layer */
    int distance = pos - k;
    if ( distance < 0 ) distance = - distance;

    /* 2. Impact cell has a distance value of 1 */
    distance = distance + 1;

    /* 3. Square root of the distance */
    /* NOTE: Real world atenuation typically depends on the square of the distance.
       We use here a tailored equation that affects a much wider range of cells */
    float atenuacion = sqrtf( (float)distance );

    /* 4. Compute attenuated energy */
    float energy_k = energy / local_size / atenuacion;

    /* 5. Do not add if its absolute value is lower than the threshold */
    if ( energy_k >= THRESHOLD / local_size || energy_k <= -THRESHOLD / local_size )
        return energy_k;
    return 0.0f
}


void core(int layer_size, int num_storms, Storm *storms, float *maximum, int *positions) {
    /* Let's define alse here the global comunicator and the rank variables */
    int rank, comm_sz;
    int i, j, k;
    int sub_domain;

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);

    /* To devide the array size proportionaly with the number of process.
     * Evaluate alse the correct number of index fot that proces */
    int sub_domain = layer_size / comm_sz;
    int rest = layer_size % comm_sz;
    int local_start = rank * sub_domain + MIN(rank, rest);
    int local_size = sub_domain + ZERO_OR_ONE(rank, rest);
    int local_end = local_size + local_start

    /* 3. Allocate memory for the layer and initialize to zero
     *  for this allocation in mamory we are gonna teke into account 2 hidden position border
     * */

    float *local_layer = (float *)calloc( sizeof(float) * (local_size + 2 ));
    float *local_layer_copy = (float *)calloc( sizeof(float) * (local_size + 2 ));
    if ( local_layer == NULL || local_layer_copy == NULL ) {
        fprintf(stderr,"Error: Allocating the local layer memory\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit( EXIT_FAILURE );
    }

        #pragma omp parallel for schedule(static) default(none) private(k) shared(layer_size, local_layer, local_layer_copy)
        for (k=0; k<local_size; k++){
            local_layer[k] = 0.0f;
            local_layer_copy[k] = 0.0f;
        }

        /* 4. Storms simulation */
        for( i=0; i<num_storms; i++) {

            /* 4.1. Add impacts energies to layer cells */
            /* For each particle */
            /* Here we've to changhe the update of control points, we want to avoid to use the critical section   */
            #pragma omp parallel for schedule(dynamic) default(none) private(i,j) shared(storms, local_start, local_end)
            for( j=0; j<storms[i].size; j++ ) {
                /* Get impact energy (expressed in thousandths) */
                float energy = (float)storms[i].posval[j*2+1] * 1000;
                /* Get impact position */
                int position = storms[i].posval[j*2];

                /*  For each cell in the layer
                 *  Keep the first cell empty for the ghost value
                 * */
                for( k=local_start; k<local_end; k++ ) {                    /* Update the energy value for the cell
                     * Here we change the update control point, instead of update the energy point, in the fuction
                     * we returned a "delta" and update here the value, just for use the atomic directive for performace improving
                     * */
                    float energy_update =  updateControlPoint( local_layer, local_size, k, position, energy );
                    if (energy_update != 0.0f)
                        #pragma omp atomic
                        local_layer[k] += energy_update;
                }
            }
            /*  We decide to use Sendrecv collective, to leverage it's self handling of send/recv
             *  Here we trat the halo exchange problem. Exchange the halo data between process
             *  MPI_PROC_NULL is used to ignore the send or recv of sender, to keep invariant the respected buffers,
             *  Without the MPI_PROC_NULL we should add a loot of conditional if, to handle potential errors
             *
             * */
            int left_neighbor = (rank == 0 ) ? MPI_PROC_NULL : rank - 1;
            int right_neighbor = (rank == comm_sz - 1) ? MPI_PROC_NULL : rank + 1;
            MPI_Status status;

            /* Send the first right border element to the gosth cell  */
            MPI_Sendrecv(&local_layer[1], 1, MPI_FLOAT, left_neighbor, 0, &local_layer[local_size + 1], 1, MPI_FLOAT, right_neighbor, 0, MPI_COMM_WORLD, &status);
            MPI_Sendrecv(&local_layer[local_size], 1, MPI_FLOAT, right_neighbor, 0, &local_size[0], 1, MPI_FLOAT, left_neighbor, 0, MPI_COMM_WORLD, &status);

            /* 4.2. Energy relaxation between storms */
            /* 4.2.1. Copy values to the ancillary array */

            float local_max = 0.0f;
            int local_max_pos = 0;

            #pragma omp parallel default(none) private (k, i) shared(local_size, local_layer, local_layer_copy, maximum, positions, local_max, local_max_pos)
            {
                #pragma omp for schedule(static)
                for( k=0; k<local_size; k++ )
                    local_layer_copy[k] = local_layer[k];

                /* 4.2.2. Update layer using the ancillary values.
                          Skip updating the first and last positions
                */
                #pragma omp for schedule(static)
                for( k=1; k<local_size-1; k++ ){
                    int local_k = local_start + k -1;
                    local_layer[local_k] = ( local_layer_copy[k-1] + local_layer_copy[k] + local_layer_copy[k+1] ) / 3.0f;
                }
            /* 4.3. Locate the maximum value in the layer, and its position */
                #pragma omp for schedule(static)
                {
                    float thread_max = 0.0f;
                    int thread_max_pos = 0;
                    for( k=1; k<local_size-1; k++ ) {
                        /* Check it only if it is a local maximum */
                        int local_k = local_size + k -1;
                        if ( local_layer[k] > local_layer[k-1] && local_layer[k] > local_layer[k+1] ) {
                            thread_max = local_layer[k];
                            thread_max_pos = local_k
                            #pragma omp critical
                            if ( thread_max > local_max ) {
                                local_max = thread_max;
                                local_max_pos = thread_max_pos;
                            }
                        }
                    }
                }
            }
    }
}

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mpi.h>
#include <time.h>
#include <unistd.h>
#include <semaphore.h>

#define MSG_EXIT 1
#define MSG_PRINT_ORDERED 2
#define MSG_PRINT_UNORDERED 3
#define MSG_REQUEST 4
#define MSG_RESPONSE 5
#define MSG_REPORT 6
#define MSG_TERMINATION 7
#define MSG_REPORT_RESPONSE 8

#define AVAILABILITY_UPDATE_INTERVAL 10
#define NUM_CHARGING_PORT 3 // Number of free charging port of each EV node
#define AVAILABILITY_CONSIDERED_FULL 0
#define NUM_ITERATIONS 5

typedef struct {
    int rank;
    int shared_array[NUM_CHARGING_PORT]; 
    int totalAvailable;
    int coord[2];
    int recv_neighbours[4];
    time_t last_update_time;
    int left;
    int right;
    int top;
    int bottom; 
    time_t alert_time;
    pthread_mutex_t lock;
    pthread_mutex_t lock2;
    MPI_Datatype ToBaseType;
    int ncols;
    int nrows;
    int termination;
} EVNode;

typedef struct
{
    EVNode *node;
    int thread_id;
} ThreadData;

sem_t semA;
sem_t semB;
int master_io(MPI_Comm world_comm, MPI_Comm comm);
int slave_io(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols);

int main(int argc, char **argv)
{   

    int rank, size; 
    MPI_Comm new_comm;
    int provided;
    int nrows = atoi(argv[1]);
    int ncols = atoi(argv[2]);

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_split(MPI_COMM_WORLD,rank == size-1, 0, &new_comm);

    // color will either be 0 or 1
    if (rank == size-1)
        master_io( MPI_COMM_WORLD, new_comm);
    else
        slave_io( MPI_COMM_WORLD, new_comm, nrows, ncols);
    
    MPI_Finalize();
    return 0;
}

void getNeighbour(int coord_y, int coord_x, int nrows, int ncols, int nearby_neighbours_rank[], int *size)
{
    int dx[] = {-1, 0, 1, 0}; 
    int dy[] = {0, 1, 0, -1}; 

    int index = 0;

    int size_exists = nrows * ncols;
    int neighbour_rank_exists[size_exists];
    for (int i = 0; i < size_exists; i++)
    {
        neighbour_rank_exists[i] = 0;
    }

    // loop over direct neighbours
    for (int i = 0; i < 4; i++)
    {
        int neighbor_x = coord_x + dx[i];
        int neighbor_y = coord_y + dy[i];

        // Skip if the neighbor is outside the grid
        if (neighbor_x < 0 || neighbor_x >= ncols || neighbor_y < 0 || neighbor_y >= nrows)
        {
            continue;
        }

        // Loop over neighbours of direct neighbours
        for (int j = 0; j < 4; j++)
        {
            int nearby_x = neighbor_x + dx[j];
            int nearby_y = neighbor_y + dy[j];

            // Skip if the nearby node is outside the grid or is the original (coord_x, coord_y) point
            if (nearby_x < 0 || nearby_x >= ncols || nearby_y < 0 || nearby_y >= nrows || (nearby_x == coord_x && nearby_y == coord_y))
            {
                continue;
            }

            // Convert the coordinates to rank and store in nearby_neighbours_rank
            int rank = nearby_y * ncols + nearby_x;

            if (neighbour_rank_exists[rank] == 0)
            {
                neighbour_rank_exists[rank] = 1;
                nearby_neighbours_rank[index] = rank;
                index++;
            }

        }
    }
    *size = index;
}

//  base station listening to other processes
void* ListenIncomingReportsFunc(void *pArg) // Common function prototype
{
    int nearby_neighbours_rank[12];
    int *size_nearby_neighbours_rank = malloc(sizeof(int));

    int i = 0, size, nslaves, firstmsg;
    char buf[256], buf2[256];
    MPI_Status status;
    MPI_Comm_size(MPI_COMM_WORLD, &size );
    EVNode received_data;
    int reported_nodes[received_data.ncols*received_data.nrows];
    for (int i = 0; i < received_data.ncols*received_data.nrows; i++)
    {
        reported_nodes[i] = 0;
    }

    FILE *logFile = fopen("log.txt", "w");
    if (logFile == NULL) {
        perror("Failed to open log file");
    }

    for (int iteration = 0; iteration < NUM_ITERATIONS; iteration++){
        
        MPI_Recv(&received_data, sizeof(EVNode), MPI_BYTE, MPI_ANY_SOURCE, MSG_REPORT, MPI_COMM_WORLD, &status);

        if (status.MPI_ERROR != MPI_SUCCESS) {
            printf("MPI_Recv failed with error code %d\n", status.MPI_ERROR);
        }

        int source = status.MPI_SOURCE;
        int availability_array[received_data.ncols*received_data.nrows];
        reported_nodes[source] = 1;

        double start_time = MPI_Wtime();
        time_t current_time;
        time(&current_time);

        int neighbours_arr[4] = {received_data.left, received_data.right, received_data.top, received_data.bottom};

        int num_of_neighbour = 0;

        if (received_data.left >= 0){
            num_of_neighbour += 1;
        }
        if (received_data.right >= 0){
            num_of_neighbour += 1;
        }
        if (received_data.top >= 0){
            num_of_neighbour += 1;
        }
        if (received_data.bottom >= 0){
            num_of_neighbour += 1;
        }
 
        // Get neighbours of neightbours
        getNeighbour(received_data.coord[0], received_data.coord[1], received_data.nrows, received_data.ncols, nearby_neighbours_rank, size_nearby_neighbours_rank);

        fprintf(logFile, "------------------------------------------------------------------------------------------------------------\n");
        fflush(logFile);
        fprintf(logFile, "Iteration : %-3d\n", iteration + 1);
        fprintf(logFile, "Logged time : %-30s", asctime(localtime(&current_time)));
        fprintf(logFile, "\n");
        fprintf(logFile, "%-16s%-8s%-16s%-16s\n", "Reporting Node", "Coord", "Port Value", "Available Port");                                     // Header
        fprintf(logFile, "%-16d(%d,%d)    %-16d%-16d\n", source, received_data.coord[0], received_data.coord[1], 5, AVAILABILITY_CONSIDERED_FULL); // Example data, make dynamic
        fprintf(logFile, "\n");
        fprintf(logFile, "Number of adjacent node: %-d\n", num_of_neighbour);              
        fprintf(logFile, "Availability to be considered full: %-3d\n\n", AVAILABILITY_CONSIDERED_FULL); 
 
        fprintf(logFile, "%-16s%-8s%-16s\n", "Adjacent Nodes", "Coord", "Available Port");
        
        if(received_data.left >= 0){
            fprintf(logFile, "%-16d(%d,%d)    %-16d\n", received_data.left, received_data.left / received_data.ncols, received_data.left % received_data.nrows, 0);
        }

        if(received_data.right >= 0){
            fprintf(logFile, "%-16d(%d,%d)    %-16d\n", received_data.right, received_data.right / received_data.ncols, received_data.right % received_data.ncols, 0);
        }

        if(received_data.bottom >= 0){
            fprintf(logFile, "%-16d(%d,%d)    %-16d\n", received_data.bottom, received_data.bottom / received_data.ncols, received_data.bottom % received_data.ncols, 0);
        }

        if(received_data.top >= 0){
            fprintf(logFile, "%-16d(%d,%d)    %-16d\n", received_data.top, received_data.top / received_data.ncols, received_data.top % received_data.ncols, 0);
        }

        fprintf(logFile, "%-16s%-8s\n", "Nearby Nodes", "Coord");
        for (int i = 0; i < *size_nearby_neighbours_rank; i++)
        {
            int rank = nearby_neighbours_rank[i];
            int x = rank / received_data.ncols;
            int y = rank % received_data.ncols;
            fprintf(logFile, "%-16d(%d,%d)\n", rank, x, y);
        }

        fprintf(logFile, "Available station nearby (no report received in last 3 iteration): ");
        for (int i = 0; i < *size_nearby_neighbours_rank; i++)
        {
            int nearby_node = nearby_neighbours_rank[i];
            if (reported_nodes[nearby_node] == 0)
            {   
                if(i !=0){
                fprintf(logFile, ", ");
                }
                fprintf(logFile, "%d ", nearby_node);
            }
        }

        int num_nearby_nodes_available = 0;
        int nearby_nodes_available[13]; // Assuming a max of 12 nearby nodes + 1 for the count

        for (int i = 0; i < *size_nearby_neighbours_rank; i++)
        {
            int nearby_node = nearby_neighbours_rank[i];
            if (reported_nodes[nearby_node] == 0)
            {                                                              // If the nearby node has not reported, assume it's available
                nearby_nodes_available[num_nearby_nodes_available + 1] = nearby_node; // +1 to leave space for the count at the beginning
                num_nearby_nodes_available++;
            }
        }
        nearby_nodes_available[0] = num_nearby_nodes_available;

        // Send the array with the count as the first element
        MPI_Send(nearby_nodes_available, num_nearby_nodes_available + 1, MPI_INT, source, MSG_REPORT_RESPONSE, MPI_COMM_WORLD);

        fprintf(logFile, "\n");
        double end_time = MPI_Wtime();
        double total_comm_time = end_time - start_time;
        fprintf(logFile, "Communication Time (seconds) : %f\n", total_comm_time);
        fprintf(logFile, "Total Messages send between reporting node and base station: %d\n", 2);
        fprintf(logFile, "\n");
        // fprintf(logFile, "------------------------------------------------------------------------------------------------------------\n");
    }

    // Send termination signal
    int termination = 0; 
    for (int i = 0; i < size - 1; i++)
    {                                                          
        MPI_Send(&termination, 1, MPI_INT, i, MSG_TERMINATION, MPI_COMM_WORLD); 
    }

    fclose(logFile);
    return 0;
}

/* This is the master */
int master_io(MPI_Comm world_comm, MPI_Comm comm)
{
    int i, size, nslaves, firstmsg;
    char buf[256], buf2[256];
    MPI_Status status;
    MPI_Comm_size(world_comm, &size );
    nslaves = size - 1;

    pthread_t tid;
    if (pthread_create(&tid, NULL, ListenIncomingReportsFunc, &nslaves) != 0) {
        perror("Failed to create thread");
        return 1;  // or other error handling
    }

    pthread_join(tid, NULL); // Wait for the thread to complete.
    return 0;
}

/* This is the function that simulate charging port */
void* slave_thread_func(void *pArg) {
    ThreadData *data = (ThreadData *) pArg;
    EVNode* node = data->node; // Get the node pointer from the data structure

    int my_rank = node->rank;
    int thread_id = data->thread_id;

    char buf[256];
    int worldSize;
    
    // Seed the random number generator with a combination of current time, rank, and thread id
    srand(time(NULL) + my_rank * 100 + thread_id);

    MPI_Comm_size(MPI_COMM_WORLD, &worldSize);

    while(node->termination == 0) {
        // Sleep for a certain interval
        sleep(AVAILABILITY_UPDATE_INTERVAL);

        // Wait for permission from thread D to start the next iteration
        sem_wait(&semB);

        int randomNumber = rand() % 10; // This gives a number between 0 and 9 inclusive
        int isAvailable = (randomNumber < 0.01) ? 1 : 0; // For testing purpose, 0.1% chance of being 1, 99.9% chance of being 0

        // Update the shared array
        pthread_mutex_lock(&node->lock); // Lock
        node->shared_array[thread_id] = isAvailable;
        pthread_mutex_unlock(&node->lock); // Unlock

        // Signal that this thread has completed the iteration
        sem_post(&semA);

        // Toggle the availability
        isAvailable = rand() % 2;
        
    }
    return NULL;
}

/* This is the function that each listening_thread in the slave will run */
void* listening_request(void *pArg) {
    ThreadData *data = (ThreadData *) pArg;
    EVNode* node = data->node; // Get the node pointer from the data structure

    int my_rank = node->rank;
    int *shared_array = node->shared_array;
    int left = node->left;
    int right = node->right;
    int top = node->top;
    int bottom = node->bottom;
    int totalAvailable = node->totalAvailable;
    int neighbours[4] = {left, right, top, bottom};
    char buf[256], buf2[256];
    MPI_Status status;

    // This thread keep listening request from other threads
    while (node->termination == 0) {
        MPI_Recv(buf, 256, MPI_CHAR, MPI_ANY_SOURCE, MSG_REQUEST, MPI_COMM_WORLD, &status );
        // Send the response back to the source thread
        MPI_Send(&node->totalAvailable, 1, MPI_INT, status.MPI_SOURCE, MSG_RESPONSE, MPI_COMM_WORLD);
    }
    
    return NULL;
}

/* This is the function that each availability_thread in the slave will run */
void* send_total_availability(void *pArg) {
    ThreadData *data = (ThreadData *) pArg;
    EVNode* node = data->node; // Get the node pointer from the data structure

    int my_rank = node->rank;
    int left = node->left;
    int right = node->right;
    int top = node->top;
    int bottom = node->bottom;
    int neighbours[4] = {left, right, top, bottom};
    int worldSize;
    MPI_Comm_size(MPI_COMM_WORLD, &worldSize);

    // Initialize availability of neighbours as 0 
    for (int i = 0; i < 4; ++i) {
        node->recv_neighbours[i] = 0;
    }

    while(node->termination == 0) {

        // Wait until all 3 threads have completed an iteration
        for (int i = 0; i < NUM_CHARGING_PORT; i++) {
            sem_wait(&semA);
        }

        node->totalAvailable = 0;

        int flag, flag2;
        MPI_Status status;
        char recvMsg[256];
        
        // Calculate totalAvailable
        for(int i = 0; i < NUM_CHARGING_PORT; i++) {
            node->totalAvailable += node->shared_array[i];
        }

        // Print total availability of the node
        printf("Total availability of slave %d is %d\n", my_rank, node->totalAvailable);

        int neighbor_availability;

        // If availability = 0, prompt for neighbour node
        if(node->totalAvailable == 0 ){
            sendRequests(neighbours, my_rank);
            
            // Receive replies from neighbours
            for(int i = 0; i < 4; i++) {
                if (neighbours[i] != MPI_PROC_NULL) {
                    MPI_Recv(&node->recv_neighbours[i], 1, MPI_INT, neighbours[i], MSG_RESPONSE, MPI_COMM_WORLD, &status);
                }
            }

            int flag_send_report = 1;
            for (int i = 0; i < 4; i++)
            {
                if (node->recv_neighbours[i] >= 1)
                {
                    flag_send_report = 0;
                }
            }

            if (flag_send_report == 1){
                MPI_Send(node, sizeof(EVNode), MPI_BYTE, worldSize - 1, MSG_REPORT, MPI_COMM_WORLD);
                int nearby_nodes_available[13];
                MPI_Status status;
                MPI_Recv(nearby_nodes_available, 13, MPI_INT, worldSize - 1, MSG_REPORT_RESPONSE, MPI_COMM_WORLD, &status); // Assuming master's rank is worldSize - 1
            }
        }

        // Signal threads A, B, and C to start their next iteration
        for (int i = 0; i < NUM_CHARGING_PORT; i++) {
            sem_post(&semB);
        }
    }
    return NULL;
}

void sendRequests(int neighbours[4], int my_rank) {
    char requestMsg[] = "Requesting data";
    for(int i = 0; i < 4; i++) {
        if (neighbours[i] != MPI_PROC_NULL){
        MPI_Send(requestMsg, strlen(requestMsg)+1, MPI_CHAR, neighbours[i], MSG_REQUEST, MPI_COMM_WORLD);
        }
    }
}

/* This is the slave */
int slave_io(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols)
{   
    int ndims=2, size, my_rank, reorder, my_cart_rank, ierr, worldSize;
    MPI_Comm comm2D;
    int dims[ndims], coord[ndims];
    int wrap_around[ndims];
    MPI_Comm_size(world_comm, &worldSize); 
    MPI_Comm_size(comm, &size); 
    MPI_Comm_rank(comm, &my_rank); 
    dims[0] = nrows;
    dims[1] = ncols;
    MPI_Dims_create(size, ndims, dims);

    EVNode node;
    pthread_mutex_init(&(node.lock), NULL);
    pthread_mutex_init(&(node.lock2), NULL);

    if(my_rank==0)
        printf("Slave Rank: %d. Comm Size: %d: Grid Dimension = [%d x %d]\n",my_rank,size,dims[0],dims[1]);

    /* create cartesian mapping */
    wrap_around[0] = 0;
    wrap_around[1] = 0; /* periodic shift is .false. */
    reorder = 0;
    ierr = 0;
    ierr = MPI_Cart_create(comm, ndims, dims, wrap_around, reorder, &comm2D);
    if(ierr != 0) printf("ERROR[%d] creating CART\n",ierr);

    /* find my coordinates in the cartesian communicator group */
    MPI_Cart_coords(comm2D, my_rank, ndims, coord); //coordinated is returned into the coord array

    /* use my cartesian coordinates to find my rank in cartesian group*/
    MPI_Cart_rank(comm2D, coord, &my_cart_rank);
    
    int left, right, top, bottom;

    MPI_Cart_shift(comm2D, 0, 1, &top, &bottom); // Vertical shift
    MPI_Cart_shift(comm2D, 1, 1, &left, &right); // Horizontal shift
    // int neighbours[4] = {left, right, top, bottom};

    // Create 5 threads
    pthread_t tid[5];
    ThreadData data[5];

    // Initialize the semaphores
    sem_init(&semA, 0, 0);
    sem_init(&semB, 0, NUM_CHARGING_PORT);  // Initially, allow threads A, B, and C to start
    
    node.rank = my_rank;
    node.left = left;
    node.right = right;
    node.top = top;
    node.bottom = bottom;
    node.coord[0] = coord[0];
    node.coord[1] = coord[1];
    node.ncols = ncols;
    node.nrows = nrows;
    node.termination = 0;

    MPI_Status status;

    // Initialize availability of neighbours as 0 
    for (int i = 0; i < 4; ++i) {
        node.recv_neighbours[i] = 0;
    }

    for (int j = 0; j < NUM_CHARGING_PORT; j++) {
        node.shared_array[j] = 0;
    }

    for(int i = 0; i < 5; i++) {
        data[i].node = &node;
    }

    for(int i = 0; i < NUM_CHARGING_PORT; i++) {
        data[i].thread_id = i;
        pthread_create(&tid[i], NULL, slave_thread_func, &data[i]);
    }

    // Create availability thread
    pthread_create(&tid[3], NULL, send_total_availability, &data[3]);

    // Create listening thread
    pthread_create(&tid[4], NULL, listening_request, &data[4]);

    MPI_Recv(&node.termination, 1, MPI_INT, worldSize - 1, MSG_TERMINATION, MPI_COMM_WORLD, &status);
    printf("%d received termination signal from master\n", my_rank);
    fflush(stdout);
    node.termination = 1;

    // Wait for all 5 threads to complete
    for(int i = 0; i < 5; i++) {
        pthread_join(tid[i], NULL);
    }
    
    printf("Successfully Terminated");
    
    // Cleanup
    sem_destroy(&semA);
    sem_destroy(&semB);

    return 0;
}
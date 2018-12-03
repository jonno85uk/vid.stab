__kernel void vector_add(__global const int *a,  
                        __global const int *b,                  
                        __global const int *c)                  
{                                                               
    // Get the index of the current element to be processed
    int i = get_global_id(0); 
    // Do the operation
    C[i] = A[i] + B[i];                                                      
}

__kernel void mult(__global const uint8_t *vals, __global uint8_t *result) {
    int i = get_global_id(0);
    result[i] = vals[i] * 0.25;           
}
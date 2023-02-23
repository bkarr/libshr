#ifndef SHARED_SM_H_
#define SHARED_SM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <shared.h>

typedef struct shr_map shr_map_s;

typedef struct  sm_item
{
    sh_status_e status;         // returned status
    long token;                 // state token
    sh_type_e type;             // data type
    size_t vlength;             // length of data being returned
    void *value;                // pointer to data value being returned
    void *buffer;               // pointer to data buffer
    size_t buf_size;            // size of buffer
    int vcount;                 // vector count
    sh_vec_s *vector;           // array of vectors
} sm_item_s;

/*==============================================================================

    public function interface

==============================================================================*/


extern sh_status_e shr_map_create(
    shr_map_s **map,            // address of map struct pointer -- not NULL
    char const * const name,	// name of map as a null terminated string -- not NULL
    size_t max_size		        // max memory size limit of map allowed which will force LRU eviction
);


extern sh_status_e shr_map_open(
    shr_map_s **map,            // address of map struct pointer -- not NULL
    char const * const name		// name of map as a null terminated string -- not NULL
);


extern sh_status_e shr_map_close(
    shr_map_s **map				// address of map struct pointer -- not NULL
);


extern sh_status_e shr_map_destroy(
    shr_map_s **map				// address of map struct pointer -- not NULL
);


extern sm_item_s shr_map_add(
    shr_map_s *map,             // pointer to map struct -- not NULL
    uint8_t *key,               // pointer to key -- not NULL
    size_t klength,             // length of key -- greater than 0
    void *value,                // pointer to value -- not NULL
    size_t vlength,             // length of value -- greater than 0
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size           // pointer to size of buffer -- not NULL
);


extern sm_item_s shr_map_addv(
    shr_map_s *map,             // pointer to map struct -- not NULL
    uint8_t *key,               // pointer to key -- not NULL
    size_t klength,             // length of key -- greater than 0
    sh_vec_s *vector,           // pointer to vector of items -- not NULL
    int vcnt,                   // count of vector array -- must be >= 1
    sh_type_e repr,             // type represented by vector
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size           // pointer to size of buffer -- not NULL
);


extern sm_item_s shr_map_get(
    shr_map_s *map,             // pointer to map struct -- not NULL
    uint8_t *key,               // pointer to key -- not NULL
    size_t klength,             // length of key -- greater than 0
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size           // pointer to size of buffer -- not NULL
);


extern sm_item_s shr_map_get_partial(
    shr_map_s *map,             // pointer to map struct -- not NULL
    uint8_t *key,               // pointer to key -- not NULL
    size_t klength,             // length of key -- greater than 0
    int index,                  // index of field to read
    size_t offset,              // offset into field to read
    size_t length,              // length of max read length
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size           // pointer to size of buffer -- not NULL
);


extern sm_item_s shr_map_get_attr(
    shr_map_s *map,             // pointer to map struct -- not NULL
    uint8_t *key,               // pointer to key -- not NULL
    size_t klength,             // length of key -- greater than 0
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size           // pointer to size of buffer -- not NULL
);


extern sm_item_s shr_map_put(
    shr_map_s *map,			    // pointer to map struct -- not NULL
    uint8_t *key,               // pointer to key -- not NULL
    size_t klength,             // length of key -- greater than 0
    void *value,                // pointer to value -- not NULL
    size_t vlength,             // length of value -- greater than 0
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size           // pointer to size of buffer -- not NULL
);


extern sh_status_e shr_map_putv(
    shr_map_s *map,             // pointer to map struct -- not NULL
    uint8_t *key,               // pointer to key -- not NULL
    size_t klength,             // length of key -- greater than 0
    sh_vec_s *vector,           // pointer to vector of items -- not NULL
    int vcnt,                   // count of vector array -- must be >= 1
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size           // pointer to size of buffer -- not NULL
);


extern sm_item_s shr_map_remove(
    shr_map_s *map,			    // pointer to map structure -- not NULL
    uint8_t *key,               // pointer to key -- not NULL
    size_t klength,             // length of key -- greater than 0
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size           // pointer to size of buffer -- not NULL
);


extern long shr_map_count(
    shr_map_s *map              // pointer to map struct -- not NULL
);


extern bool shr_map_is_valid(
    char const * const name     // name of map as a null terminated string -- not NULL
);


#ifdef __cplusplus
}

#endif


#endif // SHARED_SM_H_

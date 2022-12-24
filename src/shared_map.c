/*
The MIT License (MIT)

Copyright (c) 2022-2023 Bryan Karr

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/


#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <shared_map.h>
#include "shared_int.h"


#define SHMP "shmp"
#define IDX_BLOCK 0xffffffff


// define useful integer constants (mostly sizes and offsets)
enum shr_map_constants
{

    INDEX_ITEM = 4,             // number of slots for index item
    NODE_SIZE = 4,              // internal node slot count
    BUCKET_COUNT = 15,          // number of items indexable by hash bucket
    BUCKET_SIZE = (BUCKET_COUNT * INDEX_ITEM ) + INDEX_ITEM,  // bucket size in slots 
    MPVERSION = 1,              // map memory layout version 
    SLOT_OFFSET = 2,            // slot offset in defer list node 
    SIZE_OFFSET = 3,            // slot size offset in defer list node 

};


// define data header layout offsets
enum shr_map_data
{

    TYPE_VEC,               // offset of type indicator/vector count shifted together
    DATA_LENGTH,            // value length
    KEY_LENGTH,             // key length
    DATA_HDR,               // data header length/start of key & data slots

};


// define bucket header
enum shr_map_bkt_hdr
{

    BITMAP,                 // lower half bitmap of in use index items/upper half bucket 0 null value used to block inserts
    BTMP_CNTR,              // version counter of bitmap updates
    FILTER,                 // mini filter of memory sizes indexed in bucket
    REHASH_BKT = FILTER,    // bucket index for rehashing to larger index structure
    BKT_ACCESSORS,          // count of current processes accessing bucket

};


// define bucket index item offsets
enum shr_map_idx 
{

    HASH,                   // hash value
    ITEM_LENGTH,            // length of the item being indexed in slot count
    DATA_SLOT,              // slot where data is to be found
    DATA_CNTR,              // data slot version counter 

};


// define map header slot offsets
enum shr_map_disp
{
    
    DEFER_HEAD = BASE,      // head of deferred release blocks list
    DEFER_HD_CNT,           // defer head version counter
    CURRENT_IDX,            // active hash index
    CRNT_BKT_CNT,           // number of buckets at CURRENT_IDX value
    PREV_IDX,               // expanded hash index
    PREV_BKT_CNT,           // number of buckets at PREV_IDX value
    SEED,                   // seed value for hash function
    ALIGN,                  // spare for alignment
    DEFER_TAIL,             // tail of deferred release blocks list
    DEFER_TL_CNT,           // defer tail version counter
    EVICT_BKT,              // current bucket to start eviction search
    ACCESSORS,              // count of current processes accessing map
    AVAIL,                  // next avail free slot
    HDR_END = AVAIL,        // end of map header

};


/*
    map structure
*/
struct shr_map
{

    BASEFIELDS;
    uint32_t seed;

};



//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.

//-----------------------------------------------------------------------------
// Platform-specific functions and macros


#define	FORCE_INLINE inline __attribute__((always_inline))

//inline uint64_t rotl64 ( uint64_t x, int8_t r )
uint64_t rotl64 ( uint64_t x, int8_t r )
{
  return (x << r) | (x >> (64 - r));
}

#define ROTL64(x,y)	rotl64(x,y)

#define BIG_CONSTANT(x) (x##LLU)

//-----------------------------------------------------------------------------
// Block read - if your platform needs to do endian-swapping or can only
// handle aligned reads, do the conversion here

FORCE_INLINE uint64_t getblock64 ( const uint64_t * p, int i )
{
  return p[i];
}

//-----------------------------------------------------------------------------
// Finalization mix - force all bits of a hash block to avalanche


FORCE_INLINE uint64_t fmix64 ( uint64_t k )
{
  k ^= k >> 33;
  k *= BIG_CONSTANT(0xff51afd7ed558ccd);
  k ^= k >> 33;
  k *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
  k ^= k >> 33;

  return k;
}

void MurmurHash3_x64_128 ( const void * key, const int len,
                           const uint32_t seed, void * out )
{
  const uint8_t * data = (const uint8_t*)key;
  const int nblocks = len / 16;

  uint64_t h1 = seed;
  uint64_t h2 = seed;

  const uint64_t c1 = BIG_CONSTANT(0x87c37b91114253d5);
  const uint64_t c2 = BIG_CONSTANT(0x4cf5ad432745937f);

  //----------
  // body

  const uint64_t * blocks = (const uint64_t *)(data);

  for(int i = 0; i < nblocks; i++)
  {
    uint64_t k1 = getblock64(blocks,i*2+0);
    uint64_t k2 = getblock64(blocks,i*2+1);

    k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;

    h1 = ROTL64(h1,27); h1 += h2; h1 = h1*5+0x52dce729;

    k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;

    h2 = ROTL64(h2,31); h2 += h1; h2 = h2*5+0x38495ab5;
  }

  //----------
  // tail

  const uint8_t * tail = (const uint8_t*)(data + nblocks*16);

  uint64_t k1 = 0;
  uint64_t k2 = 0;

  switch(len & 15)
  {
  case 15: k2 ^= ((uint64_t)tail[14]) << 48;
  case 14: k2 ^= ((uint64_t)tail[13]) << 40;
  case 13: k2 ^= ((uint64_t)tail[12]) << 32;
  case 12: k2 ^= ((uint64_t)tail[11]) << 24;
  case 11: k2 ^= ((uint64_t)tail[10]) << 16;
  case 10: k2 ^= ((uint64_t)tail[ 9]) << 8;
  case  9: k2 ^= ((uint64_t)tail[ 8]) << 0;
           k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;

  case  8: k1 ^= ((uint64_t)tail[ 7]) << 56;
  case  7: k1 ^= ((uint64_t)tail[ 6]) << 48;
  case  6: k1 ^= ((uint64_t)tail[ 5]) << 40;
  case  5: k1 ^= ((uint64_t)tail[ 4]) << 32;
  case  4: k1 ^= ((uint64_t)tail[ 3]) << 24;
  case  3: k1 ^= ((uint64_t)tail[ 2]) << 16;
  case  2: k1 ^= ((uint64_t)tail[ 1]) << 8;
  case  1: k1 ^= ((uint64_t)tail[ 0]) << 0;
           k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;
  };

  //----------
  // finalization

  h1 ^= len; h2 ^= len;

  h1 += h2;
  h2 += h1;

  h1 = fmix64(h1);
  h2 = fmix64(h2);

  h1 += h2;
  h2 += h1;

  ((uint64_t*)out)[0] = h1;
  ((uint64_t*)out)[1] = h2;
}


/*
================================================================================

    private functions

================================================================================
*/


static sh_status_e format_as_map(

    shr_map_s *map,         // pointer to map struct -- not NULL
    size_t max_size         // max memory limit which will force eviction

)   {

    init_data_allocator( (shr_base_s*)map, HDR_END );
    long *array = map->current->array;

    array[ MAX_SIZE ] = max_size; 
    view_s view = alloc_new_data( (shr_base_s*)map, BUCKET_SIZE );
    array[ view.slot ] = 0;
    array[ CURRENT_IDX ] = view.slot;
    array[ PREV_IDX ] = view.slot;
    array[ PREV_BKT_CNT ] = 1;
    array[ CRNT_BKT_CNT ] = 1;
    srand( time(NULL) );
    array[ SEED ] = rand();

    prime_list( (shr_base_s*)map, NODE_SIZE, DEFER_HEAD, DEFER_HD_CNT, DEFER_TAIL, DEFER_TL_CNT );
    return SH_OK;
}


static sh_status_e initialize_map_struct(

    shr_map_s **map            // address of map struct pointer -- not NULL

) {

    *map = calloc( 1, sizeof(shr_map_s) );
    if ( *map == NULL ) {

        return SH_ERR_NOMEM;
    }

    (*map)->current = calloc( 1, sizeof(extent_s) );
    if ( (*map)->current == NULL ) {

        free( *map );
        *map = NULL;
        return SH_ERR_NOMEM;
    }

    (*map)->prev = (*map)->current;

    return SH_OK;
}


static bool is_valid_map(

    shr_map_s *map          // pointer to map

)   {

    if ( memcmp( &map->current->array[ TAG ], SHMP, sizeof(SHMP) - 1 ) != 0 ) {

        return false;
    }

    if ( map->current->array[ VERSION ] != MPVERSION ) {

        return false;
    }

    return true;
}


static bool is_at_limit(

    shr_map_s *map          // pointer to map

)   {

    return ( map->current->array[ SIZE ] >= map->current->array[ MAX_SIZE ] );
}


static bool is_expanded(

    shr_map_s *map          // pointer to map

)   {

    return ( map->current->array[ CURRENT_IDX ] != map->current->array[ PREV_IDX ] );
}


static void guard_map_memory(

    shr_map_s *map      // pointer to map

)   {

    (void) AFA( &map->accessors, 1 );   // protect extents
    (void) AFA( &map->current->array[ ACCESSORS ], 1 );
    //TODO clean up defer list
}


static void unguard_map_memory(

    shr_map_s *map      // pointer to map

)   {

    release_prev_extents( (shr_base_s*) map );
    //TODO clean up defer list
    (void) AFS( &map->current->array[ ACCESSORS ], 1 );
    (void) AFS( &map->accessors, 1 );
}


static void guard_bucket(
        
        long *array,         
        long bucket
        
)   {
    (void) AFA( &array[ bucket + BKT_ACCESSORS ], 1 );
}


static void unguard_bucket(
        
        long *array,            
        long bucket
        
)   {
    (void) AFS( &array[ bucket + BKT_ACCESSORS ], 1 );
}


static long calc_data_slots(

    long length

)   {

    // calculate number of slots needed for data
    long space = length >> SZ_SHIFT;

    // account for remainder
    if ( length & REM ) {

        space += 1;
    }

    return space;
}


long compute_hash(

    long capacity, 
    uint8_t *key, 
    long klength,
    uint32_t seed    

)   {

    long hash[ 2 ];
    MurmurHash3_x64_128( key, klength, seed, &hash[ 0 ] );
    return hash[ 1 ];
}


static uint32_t pair_vec_count( 

    long *array,        // pointer to map array
    long data_slot      // data item index

)   {

    return (uint32_t) ( ( array[ data_slot + TYPE_VEC ] << 32 ) >> 32 );
}


static sh_type_e pair_type( 

    long *array,        // pointer to map array
    long data_slot      // data item index

)   {

    return (sh_type_e) ( array[ data_slot + TYPE_VEC ] >> 32 );
}


static sh_status_e pair_compare_keys( 

    long *array,        // pointer to map array
    long data_slot,     // data item index
    uint8_t *key,       // pointer to key -- not NULL
    size_t klength      // length of key -- greater than 0

)   {

    if ( klength != array[ data_slot + KEY_LENGTH ]) {
        
        return SH_ERR_NO_MATCH;
    }

    return ( memcmp( key, &array[ data_slot + DATA_HDR ], klength) == 0 ) ? SH_OK : SH_ERR_NO_MATCH;
}


static long copy_kv_pair(

    shr_map_s *map,             // pointer to map struct
    uint8_t *key,               // pointer to key -- not NULL
    size_t klength,             // length of key -- greater than 0
    void *value,                // pointer to value -- not NULL
    size_t vlength,             // length of value -- greater than 0 
    sh_type_e type,             // data type
    long *size                  // total size of data in slots

)   {

    long kslots = calc_data_slots( klength );
    long vslots = calc_data_slots( vlength );
    long space = DATA_HDR + kslots + vslots;
    update_buffer_size( map->current->array, vslots, sizeof(sh_vec_s) );
    view_s view = alloc_data_slots( (shr_base_s*)map, space );
    long current = view.slot;

    //TODO if no allocation returned, invoke cache eviction

    if ( current >= HDR_END ) {

        long *array = view.extent->array;
        *size = array[ current ];
        array[ current + TYPE_VEC ] = ( (long)type << 32) | 1;
        array[ current + DATA_LENGTH ] = vlength;
        array[ current + KEY_LENGTH ] = klength;
        memcpy( &array[ current + DATA_HDR ], key, klength );
        memcpy( &array[ current + DATA_HDR + kslots ], value, vlength );
    }

    return current;
}


static sh_status_e resize_buffer(

    long *array,        // pointer to map array
    long data_slot,     // data item index
    void **buffer,      // address of buffer pointer, or NULL
    size_t *buff_size,  // pointer to length of buffer if buffer present
    long size           // required size of data

)   {

    long total = size + ( pair_vec_count( array, data_slot ) * sizeof(sh_vec_s) );

    if ( *buffer && *buff_size < total ) {

        free( *buffer );
        *buffer = NULL;
        *buff_size = 0;
    }

    if ( *buffer == NULL ) {

        *buffer = malloc( total );
        *buff_size = total;

        if ( *buffer == NULL ) {

            *buff_size = 0;
            return SH_ERR_NOMEM;
        }
    }

    return SH_OK;
}


static void copy_to_buffer(

    long *array,        // pointer to map array -- not NULL
    long data_slot,     // data item index
    sm_item_s *item,    // pointer to item -- not NULL
    void **buffer,      // address of buffer pointer, or NULL
    size_t *buff_size   // pointer to length of buffer if buffer present

)   {

    long size = array[ data_slot + DATA_LENGTH ];
    sh_status_e status = resize_buffer( array, data_slot, buffer, buff_size, size );
    if ( status != SH_OK ) {

        item->status = SH_ERR_NOMEM;
        return;
    }

    long offset = DATA_HDR + calc_data_slots( array[ data_slot + KEY_LENGTH ] );
    memcpy( *buffer, &array[ data_slot + offset ], size );
    item->buffer = *buffer;
    item->buf_size = size;
    item->type = pair_type( array, data_slot );
    item->vlength = array[ data_slot + DATA_LENGTH ];
    item->value = (uint8_t*) *buffer;
    item->vcount = pair_vec_count( array, data_slot );
    item->vector = (sh_vec_s*) ( (uint8_t*) *buffer + size );

    if ( item->vcount == 1 ) {

        item->vector[ 0 ].type = item->type;
        item->vector[ 0 ].len = item->vlength;
        item->vector[ 0 ].base = item->value;

    } else {

        //TODO initialize_item_vector( item );
    }
}



static void reindex_pair(

    shr_map_s *map,     // pointer to map
    long pair           // k/v pair slots to be reindexed

)   {

    long *array = map->current->array;
    long hash = array[ pair + HASH ];
    long length = array[ pair + ITEM_LENGTH ];
    long slot = array[ pair + DATA_SLOT ];
    long counter = array[ pair + DATA_CNTR ];
    long bucket_count = array[ CRNT_BKT_CNT ];
    long bucket = ( ( hash & ( bucket_count - 1 ) ) * BUCKET_SIZE ) + array[ CURRENT_IDX ];

    view_s view = insure_in_range( (shr_base_s*)map, bucket + BUCKET_SIZE );
    array = view.extent->array;
    
    long mask = 1;
    for ( int i = 1; i <= BUCKET_COUNT; i++ ) {

        mask <<= 1;
        long bit_map = array[ bucket + BITMAP ];
        if ( bit_map & mask ) {
            
            continue;            
        }

        DWORD before = { .high = bit_map, .low = array[ bucket + BTMP_CNTR ] };
        DWORD after = { .high = bit_map | mask, .low = before.low + 1 };
        if ( !DWCAS( (DWORD*)&array[ bucket ], &before, after ) ) {
        
            continue;
        }

        long item = bucket + ( i * INDEX_ITEM );
        array[ item + HASH ] = hash;
        array[ item + ITEM_LENGTH ] = length;
        array[ item + DATA_SLOT ] = slot;
        array[ item + DATA_CNTR ] = counter;

        long filter = array[ bucket + FILTER ];
        while ( !(filter & length) && !CAS( &array[ bucket + FILTER ], &filter, filter | length ) ) {

            filter = array[ bucket + FILTER ];
        }

        break;
    }
}


static void reindex_bucket(

    shr_map_s *map,     // pointer to map
    long bucket         // bucket slot to be reindexed

)   {

    long *array = map->current->array;
    long mask = 1;
    for ( int i = 1; i <= BUCKET_COUNT; i++ ) {

        mask <<= 1;
        long bit_map = array[ bucket + BITMAP ];
        if ( !( bit_map & mask ) ) {
            
            continue;            
        }

        // clear bit for item to know it is safe to reindex
        DWORD before = { .high = bit_map, .low = array[ bucket + BTMP_CNTR ] };
        DWORD after = { .high = bit_map & ~mask, .low = before.low + 1 };
        if ( !DWCAS( (DWORD*)&array[ bucket ], &before, after ) ) {
        
            continue;
        }

        reindex_pair( map, bucket + ( i * INDEX_ITEM ) );
    }

    // mark bucket as fully reindexed
    DWORD before = { .high = 0, .low = array[ bucket + BTMP_CNTR ] };
    DWORD after = { .high = 1, .low = before.low + 1 };
    (void) DWCAS( (DWORD*)&array[ bucket ], &before, after );
}

 
static sh_status_e reindex_indices(

    shr_map_s *map      // pointer to map

)   {

    long *array = map->current->array;
    long buckets = array[ PREV_BKT_CNT ];
    long prev = array[ PREV_IDX ];
    long rehash  = array[ prev + REHASH_BKT ];
    
    if ( prev == array[ CURRENT_IDX ] ) {
    
        return SH_OK;
    }

    // insure at least one thread works each bucket
    for (long i = rehash; i < buckets; i++ ) {
        
        if ( rehash != i || !CAS( &array[ REHASH_BKT ], &rehash, i + 1 ) ) {

            rehash = array[ prev + REHASH_BKT ];
            continue;
        }

        long bucket = prev + ( i * BUCKET_SIZE );
        view_s view = insure_in_range( (shr_base_s*)map, bucket + BUCKET_SIZE );
        array = view.extent->array;

        if ( array[ bucket ] == 1 ) {
            
            rehash = array[ prev + REHASH_BKT ];
            continue;
        }

        reindex_bucket( map, bucket );
        rehash = array[ prev + REHASH_BKT ];
    }

    // help other threads work unfinished buckets
    for (long i = 0; i < buckets; i++ ) {
        
        long bucket = prev + ( i * BUCKET_SIZE );
        view_s view = insure_in_range( (shr_base_s*)map, bucket + BUCKET_SIZE );
        array = view.extent->array;

        if ( array[ bucket ] == 1 ) {
            
            continue;
        }

        reindex_bucket( map, bucket );
    }

    return SH_OK;
}


static bool is_bucket_item_empty( 

    shr_map_s *map,     // pointer to map
    long slot           // slot for bucket item

)   {
    
    long *array = map->current->array;
    view_s view = insure_in_range( (shr_base_s*)map, slot + INDEX_ITEM );
    array = view.extent->array;
    long null = array[ CURRENT_IDX ] >> 32;

    return ( array[ slot + HASH ] == null && 
             array[ slot + ITEM_LENGTH ] == null && 
             array[ slot + DATA_SLOT ] == null && 
             array[ slot + DATA_CNTR ] == null );
}


static sh_status_e allocate_new_index(

    shr_map_s *map,     // pointer to map
    long current_idx

)   {

    long *array = map->current->array;

    if ( current_idx != array[ PREV_IDX]) {
        
        return SH_OK;
    }

    // block new adds
    long prev = array[ current_idx ];
    long block = ( (uint64_t)IDX_BLOCK << 32 ) | prev;
    (void) CAS( &array[ current_idx ], &prev, block );

    // allocate new index structure
    DWORD before = { .high = array[ current_idx ], .low = array[ PREV_BKT_CNT ] };
    long new_bkt_cnt = before.low << 1; 
    view_s view = alloc_data_slots( (shr_base_s*)map, new_bkt_cnt );
    if ( view.slot == 0 ) {

        return SH_ERR_NOMEM;
    }

    array = view.extent->array;
    new_bkt_cnt = array[ view.slot ];
    array[ view.slot ] = 0;

    // replace current index with expanded
    DWORD after = { .high = view.slot, .low = new_bkt_cnt };
    if ( DWCAS( (DWORD*)&array[ CURRENT_IDX ], &before, after ) ) {

        long rehash = array[ current_idx + REHASH_BKT ];
        (void) CAS( &array[ current_idx + REHASH_BKT ], &rehash, 0 );

    } else {

        array[ view.slot ] = new_bkt_cnt;       
        (void) free_data_slots( (shr_base_s*)map, view.slot ); 
    }

    return SH_OK;
}


static sh_status_e release_prev_index(

    shr_map_s *map      // pointer to map

)   {

    long *array = map->current->array;
    long prev = array [ PREV_IDX ];

    if ( prev == array[ CURRENT_IDX ] ) {
       
        return SH_OK;
    }

    // remove previous index by making same as current
    DWORD before = { .high = prev, .low = array[ PREV_BKT_CNT ] };
    DWORD after = { .high = array[ CURRENT_IDX ], .low = array[ CRNT_BKT_CNT] };
    if ( !DWCAS( (DWORD*)&array[ PREV_IDX ], &before, after ) ) {
    
        return SH_OK;
    }


    // allocate list node
    view_s view = alloc_idx_slots( (shr_base_s*)map );
    if ( view.slot == 0 ) {

        return SH_ERR_NOMEM;
    }

    // update list node with memory info
    array = view.extent->array;
    array[ view.slot + SLOT_OFFSET ] = array[ PREV_IDX ];
    array[ view.slot + SIZE_OFFSET ] = array[ PREV_BKT_CNT * BUCKET_SIZE ];

    // append node to end of defer list
    add_end( (shr_base_s*)map, view.slot, DEFER_TAIL );

    return SH_OK;
}


static sh_status_e expand_hash_index(

    shr_map_s *map      // pointer to map

)   {
    long current = map->current->array[ CURRENT_IDX ];

    if ( is_expanded( map ) ) {

        return SH_OK;
    }

    if ( is_at_limit( map ) ) {

        return SH_ERR_NOMEM;
    }
    
    sh_status_e status = allocate_new_index( map, current );
    if ( status != SH_OK ) {
        
        return status;
    }
    
    // reindex values
    status = reindex_indices( map );
    if ( status != SH_OK ) {
        
        return status;
    }
 
    // update prev index to match new
    status = release_prev_index( map );
    return status;
}


static sm_item_s scan_for_match(

    shr_map_s *map,             // pointer to map struct -- not NULL
    long hash,                  // computed hash value
    uint8_t *key,               // pointer to key -- not NULL
    size_t klength,             // length of key -- greater than 0
    long bucket,                // bucket slot
    long bitmap,                // bucket bitmap 
    long *empty,                // empty slot pointer
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size           // pointer to size of buffer -- not NULL

)   {

    sm_item_s result = { .status = SH_ERR_NO_MATCH };

    long *array = map->current->array;

    view_s view = insure_in_range( (shr_base_s*)map, bucket + BUCKET_SIZE );
    array = view.extent->array;

    long mask = 1;
    *empty = 0;
    for ( int i = 1; i <= BUCKET_COUNT; i++ ) {

        mask <<= 1;
        long item = bucket + ( i * INDEX_ITEM );
        if ( !( bitmap & mask ) ) {
            
            if ( *empty == 0 && is_bucket_item_empty( map, item ) ) {
            
                *empty = i;
            }        
            continue;            
        }
        
        if ( hash != array[ item + HASH ]) {
        
            continue;
        }

        view = insure_in_range( (shr_base_s*)map, array[ item + DATA_SLOT ] + array[ item + ITEM_LENGTH ] );
        array = view.extent->array;
        if ( pair_compare_keys( array, array[ item + DATA_SLOT ], key, klength ) == SH_ERR_NO_MATCH ) {
        
            continue;
        }
        
        copy_to_buffer( array, array[ item + DATA_SLOT ], &result, buffer, buff_size );
        result.token = array[ item + DATA_CNTR ];
        result.status = SH_OK;
        break; 
    }

    return result;
}


static sh_status_e add_to_bucket( 
    
    long *array,        // pointer to map array
    long hash,          // computed hash value
    long pair_slot,     // slot index for k/v pair
    long pair_size,     // allocated size of k/v pair
    long bucket,        // slot index for bucket
    long empty,         // empty slot in bucket
    long bitmap,        // previous captured bucket bitmap 
    long counter        // previous captured bucket bitmap version counter
        
)   {

    DWORD before = { 0 };
    DWORD after = { .low = pair_slot, .high = AFA( &array[ ID_CNTR ], 1 ) };
    long slot = bucket + ( empty * INDEX_ITEM );
    if ( !DWCAS( (DWORD*)&array[ slot + DATA_SLOT ], &before, after ) ) {

        return SH_ERR_CONFLICT;
    }

    array[ slot + HASH ] = hash;
    array[ slot + ITEM_LENGTH ] = pair_size;
    before.high = bitmap & ( (long)IDX_BLOCK << 32 );
    before.low = counter;
    after.low = bitmap | ( 1 << empty );
    after.high = before.low + 1;
    if ( DWCAS( (DWORD*)&array[ bucket ], &before, after ) ) {

        return SH_OK;
    }

    array[ slot + HASH ] = 0;
    array[ slot + ITEM_LENGTH ] = 0;
    array[ slot + DATA_SLOT ] = 0;
    array[ slot + DATA_CNTR ] = 0;
    return SH_ERR_NO_MATCH;
}

static sm_item_s hash_add(

    shr_map_s *map,             // pointer to map struct -- not NULL
    uint8_t *key,               // pointer to key -- not NULL
    size_t klength,             // length of key -- greater than 0
    long pair_slot,             // data slot of k/v pair    
    long pair_size,             // data allocation for k/v pair    
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size           // pointer to size of buffer -- not NULL

)   {

    sm_item_s result = { .status = SH_ERR_NO_MATCH };
    long *array = map->current->array;
    long hash = compute_hash( array[ BUCKET_COUNT ], key, klength, map->seed );
    long bucket = ( ( hash & ( array[ CRNT_BKT_CNT ] - 1 ) ) * BUCKET_SIZE ) + array[ CURRENT_IDX ];
    view_s view = insure_in_range( (shr_base_s*)map, bucket + BUCKET_SIZE );
    array = view.extent->array;
    guard_bucket( array, bucket );

    while ( result.status == SH_ERR_NO_MATCH ) {

        if ( is_expanded( map ) ) {
        
            reindex_indices( map ); 
        }
     
        long empty = 0;
        long bitmap = array[ bucket + BITMAP ];
        long counter = array[ bucket + BTMP_CNTR ]; 

        result = scan_for_match( map, hash, key, klength, bucket, bitmap, &empty, buffer, buff_size );
        if ( result.status == SH_OK ) { 

            result.status = SH_ERR_CONFLICT; 
            break;
        }

        if ( empty <= 0 ) {

            result.status = expand_hash_index( map );
            if (result.status == SH_OK) {

                continue;
            }

            //TODO evict_bucket( map, hash, &empty, &bitmap, &counter );
        }

        result.status = add_to_bucket( array, hash, pair_slot, pair_size, bucket, empty, bitmap, counter );
        if ( result.status == SH_OK ) {
            
            break;
        }
    }
      
    unguard_bucket( array, bucket );
    return result;
}


static sm_item_s add_value_uniquely(

    shr_map_s *map,             // pointer to map struct -- not NULL
    uint8_t *key,               // pointer to key -- not NULL
    size_t klength,             // length of key -- greater than 0
    void *value,                // pointer to value -- not NULL
    size_t vlength,             // length of value -- greater than 0
    sh_type_e type,             // data type
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size           // pointer to size of buffer -- not NULL

)   {

    sm_item_s result = {0};

    // allocate space and copy key/value pair
    long size = 0;
    long data_slot = copy_kv_pair( map, key, klength, value, vlength, type, &size );

    if ( data_slot == 0 ) {
        
        return (sm_item_s) { .status = SH_ERR_NOMEM };
    }

    if ( data_slot < HDR_END ) {

        return (sm_item_s) { .status = SH_ERR_STATE };
    }

    result = hash_add( map, key, klength, data_slot, size, buffer, buff_size );
    if ( result.status != SH_OK ) {

        map->current->array[ data_slot ] = size;
        free_data_slots( (shr_base_s*)map, data_slot );
    }

    return result;
}


/*
================================================================================

    public function interface

================================================================================
*/


/*
    shr_map_create -- create shared memory map using name

    Creates shared map using name to mmap POSIX shared memory object.

    The max_size argument specifies the maximum amount of memory allowed to be 
    used before the least recently updated (LRUP) eviction is activated.  At that
    point, any adds to the map will cause an eviction of an item occupying at 
    least as much memory required by the new item.  Obviously, once the limit is 
    reached, eviction processing will impact the performance of adding items.  
    The value needs to be a multiple of 4096.
    
    If the value is 0, then no limit is enforced.

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if pointer to map struct or map name is NULL, or max_size is 
                    not 0 or a multiple of 4096
    SH_ERR_ACCESS   on permissions error for map name
    SH_ERR_EXIST    if map already exists
    SH_ERR_NOMEM    if not enough memory to allocate
    SH_ERR_PATH     if error in map name
    SH_ERR_SYS      if system call returns an error
*/
extern sh_status_e shr_map_create(

    shr_map_s **map,            // address of map struct pointer -- not NULL
    char const * const name,	// name of map as a null terminated string -- not NULL
    size_t max_size		        // max memory size limit of map allowed before evicting 

)   {

    if ( map == NULL || 
         name == NULL || 
         ( max_size != 0 && ( max_size & ( PAGE_SIZE - 1 ) ) ) ) {

        return SH_ERR_ARG;
    }

    sh_status_e status = perform_name_validations( name, NULL );
    if ( status == SH_ERR_STATE ) {

        return SH_ERR_EXIST;
    }

    if ( status != SH_ERR_EXIST ) {

        return status;
    }

    status = create_base_object( (shr_base_s**) map, sizeof(shr_map_s), name, SHMP,
                                 sizeof(SHMP) - 1, MPVERSION );
    if ( status ) {

        return status;
    }

    status = format_as_map( *map, max_size );
    if ( status ) {

        free( *map );
        *map = NULL;
    }
    else {

        (*map)->seed = (*map)->current->array[ SEED ];
    }

    return status;
}


/*
    shr_map_destroy -- unlink and release shared memory map

    Unlink shared map and releases associated memory and resources.  The
    pointer to the map instance will be NULL on return. Shared map will not
    be available or usable to other processes.

    returns sh_status_e:

    SH_OK       on success
    SH_ERR_ARG  if pointer to map struct is NULL
    SH_ERR_SYS  if an error occurs releasing associated resources
*/
extern sh_status_e shr_map_destroy(

    shr_map_s **map         // address of map struct pointer -- not NULL

)   {

    if ( map == NULL || *map == NULL ) {

        return SH_ERR_ARG;
    }

    release_prev_extents( (shr_base_s*) *map );

    sh_status_e status = release_mapped_memory( (shr_base_s**) map );

    free( *map );
    *map = NULL;

    return status;
}


/*
    shr_map_open -- open shared memory map for modification using name

    Opens shared map using name to mmap shared memory.  The map must already
    exist before it is opened.  

    returns sh_status_e:

    SH_OK           on success
    SH_ERR_ARG      if pointer to map struct or name is not NULL
	SH_ERR_NOMEM	failed memory allocation
    SH_ERR_ACCESS   on permissions error for map name
    SH_ERR_EXIST    if map does not already exist
    SH_ERR_PATH     if error in map name
    SH_ERR_STATE    if incompatible implementation
    SH_ERR_SYS      if system call returns an error
*/
extern sh_status_e shr_map_open(

    shr_map_s **map,        // address of map struct pointer -- not NULL
    char const * const name	// name of map as a null terminated string -- not NULL

)   {

    if ( map == NULL || name == NULL ) {

        return SH_ERR_ARG;
    }


    size_t size = 0;
    sh_status_e status = perform_name_validations( name, &size );
    if ( status ) {

        return status;
    }

    status = initialize_map_struct( map );
    if ( status ) {

        return status;
    }

    status = map_shared_memory( (shr_base_s**) map, name, size );
    if ( status ) {

        return status;
    }

    if ( !is_valid_map( *map ) ) {

        shr_map_close( map );
        return SH_ERR_STATE;
    }
    
    (*map)->seed = (*map)->current->array[ SEED ];

    //TODO clean defer list
    return SH_OK;
}


/*
    shr_map_close -- close shared memory map

    Closes shared map and releases associated memory.  The pointer to the
    map instance will be NULL on return.

    returns sh_status_e:

    SH_OK       on success
    SH_ERR_ARG  if pointer to map struct is NULL
*/
extern sh_status_e shr_map_close(

    shr_map_s **map         // address of map struct pointer -- not NULL

)   {

    if ( map == NULL || *map == NULL ) {

        return SH_ERR_ARG;
    }

    close_base( (shr_base_s*) *map );

    free( *map );
    *map = NULL;

    return SH_OK;
}


/*
    shr_map_add -- add a key/value pair to the map uniquely

    The shr_map_add function will attempt to add an item to the map and will 
    succeed only if an item associated with the key is not already in the map.

    returns sh_status_e:

    SH_OK       on success
    SH_ERR_ARG  if pointer to map struct, key, or value is NULL or if key length
                or value length equals 0
*/
extern sm_item_s shr_map_add(

    shr_map_s *map,             // pointer to map struct -- not NULL
    uint8_t *key,               // pointer to key -- not NULL
    size_t klength,             // length of key -- greater than 0
    void *value,                // pointer to value -- not NULL
    size_t vlength,             // length of value -- greater than 0
    void **buffer,              // address of buffer pointer -- not NULL
    size_t *buff_size           // pointer to size of buffer -- not NULL

)   {

    if ( map == NULL || 
         key == NULL || 
         klength == 0 || 
         value == NULL || 
         vlength == 0 || 
         buffer == NULL ||
         buff_size == NULL || 
         ( *buffer != NULL && *buff_size == 0 ) ) {

        return (sm_item_s) { .status = SH_ERR_ARG };
    }

    guard_map_memory( map );

    sm_item_s result = add_value_uniquely( map, key, klength, value, vlength, SH_STRM_T, buffer, buff_size );
    if (result.status == SH_OK) {
        
        (void) AFA( &map->current->array[ COUNT ], 1 );
    }

    unguard_map_memory( map );
    return result;
}


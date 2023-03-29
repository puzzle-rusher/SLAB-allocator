#include <iostream>
#include <set>

/**
 * This two functions you should use to allocate
 * and free memory in this task. Consider internally
 * they use buddy-allocator with page size of 4096 bytes
 **/

/**
 * Allocates 4096 * 2^order byte chunk of memory,
 * aligned on a 4096 * 2^order byte boundary. `order`
 * must be in the interval [0; 10] (both borders
 * inclusive), i.e. you can't allocate more than
 * 4Mb at a time.
 **/
void *alloc_slab(int order) {
    return aligned_alloc(4096 * (1 << order), 4096 * (1 << order));
}
/**
 * Free memory chunk previously allocated
 * with the alloc_slab function
 **/
void free_slab(void *slab) {
    free(slab);
}



struct slabStruct {
    slabStruct *previous;
    slabStruct *next;

    uint32_t refcnt;
};

/**
 * This structure presents an allocator,
 * you can change it as you like.
 * The fields and comments in it just give you
 * a general idea that you might need
 * to store in this structure.
 **/
struct cache {
    slabStruct *complete_slab; /* list of free slabs to support chache_shrink */
    slabStruct *partially_slab; /* list of partially occupied SLABs */
    slabStruct *empty_slab; /* list of fully occupied SLABs */

    size_t object_size; /* size of allocating object */
    int slab_order; /* using size of SLAB */
    size_t slab_objects; /* count of objects in one SLAB */
};



int smallest_power_of_two(size_t n) {
    int pow = 0;
    while (n > 0) {
        n >>= 1;
        pow += 1;
    }

    return pow;
}

slabStruct* calculate_slab_start(struct cache *cache, void *allocation) {
    auto mask = ~(((uintptr_t)1 << (cache->slab_order + 12)) - 1);
    return (slabStruct*)((uintptr_t)allocation & mask);
}

void free_list(slabStruct *list) {
    slabStruct* current = list;

    while (current) {
        slabStruct* next = current->next;
        free_slab(current);
        current = next;
    }
}

void remove_from_empty_list(struct cache *cache, slabStruct *slab) {
    if(auto previous = slab->previous) {
        previous->next = slab->next;
    } else {
        if (cache->empty_slab == slab) {
            cache->empty_slab = slab->next;
        } else if (cache->partially_slab == slab) {
            cache->partially_slab = slab->next;
        }
    }

    if (auto next = slab->next) {
        next->previous = slab->previous;
    }

    slab->previous = nullptr;
    slab->next = nullptr;
}

void insert_in_complete_list(struct cache *cache, slabStruct *slab) {
    slab->next = cache->complete_slab;
    slab->previous = nullptr;
    if (cache->complete_slab) {
        cache->complete_slab->previous = slab;
    }
    cache->complete_slab = slab;
}

void insert_in_partially_list(struct cache *cache, slabStruct *slab) {
    slab->next = cache->partially_slab;
    slab->previous = nullptr;
    if (cache->partially_slab) {
        cache->partially_slab->previous = slab;
    }
    cache->partially_slab = slab;
}

void insert_in_empty_list(struct cache *cache, slabStruct *slab) {
    slab->next = cache->empty_slab;
    slab->previous = nullptr;
    if (cache->empty_slab) {
        cache->empty_slab->previous = slab;
    }
    cache->empty_slab = slab;
}

/**
 * Function of initialization will be called before
 * using this caching allocator for allocation.
 * Parameters:
 *  - cache - structure you need to initialize
 *  - object_size - the size of the objects
 *  that this caching allocator should allocate
 **/
void cache_setup(struct cache *cache, size_t object_size)
{
    cache->complete_slab = nullptr;
    cache->partially_slab = nullptr;
    cache->empty_slab = nullptr;

    cache->object_size = object_size;
    cache->slab_order = smallest_power_of_two((sizeof(slabStruct) + object_size) / 4096);

    if (cache->slab_order > 0) {
        cache->slab_objects = 1;
    } else {
        cache->slab_objects = (4096 - sizeof(slabStruct)) / object_size;
    }
}

/**
 * The function of release will be called when job
 * with this allocator will be finished. It must free
 * all the memory occupied by this allocator.
 * The checking system will consider
 * it an error if not all memory is freed
 **/
void cache_release(struct cache *cache)
{
    free_list(cache->complete_slab);
    free_list(cache->partially_slab);
    free_list(cache->empty_slab);

    cache->complete_slab = nullptr;
    cache->partially_slab = nullptr;
    cache->empty_slab = nullptr;
}


/**
 * The function of memory allocation from caching allocator.
 * It should return pointer at the memory chunk with minimum size
 * of object_size bytes (see cache_setup).
 * It is guaranteed that the cache points
 * to the correct initialized allocator.
 **/
void *cache_alloc(struct cache *cache)
{
    if (cache->partially_slab) {
        slabStruct* current_slab = cache->partially_slab;
        void* result = (uint8_t*)current_slab + sizeof(slabStruct) + cache->object_size * cache->partially_slab->refcnt;
        current_slab->refcnt += 1;

        if (current_slab->refcnt == cache->slab_objects) {
            // remove from partially slabs list
            cache->partially_slab = current_slab->next;
            if (cache->partially_slab) {
                cache->partially_slab->previous = nullptr;
            }
            // add to empty slabs list
            insert_in_empty_list(cache, current_slab);
        }

        return result;
    } else if (cache->complete_slab) {
        slabStruct* current_slab = cache->complete_slab;
        void* result = (uint8_t*)current_slab + sizeof(slabStruct);
        current_slab->refcnt += 1;
        // remove from complete slabs list
        cache->complete_slab = current_slab->next;
        if (cache->complete_slab) {
            cache->complete_slab->previous = nullptr;
        }

        if (current_slab->refcnt == cache->slab_objects) {
            // add to empty slabs list
            insert_in_empty_list(cache, current_slab);
        } else {
            // add to partially slabs list
            insert_in_partially_list(cache, current_slab);
        }

        return result;
    } else {
        // new slab logic
        auto current_slab = (slabStruct*)alloc_slab(cache->slab_order);
        void* result = (uint8_t*)current_slab + sizeof(slabStruct);
        current_slab->refcnt = 1;

        if (current_slab->refcnt == cache->slab_objects) {
            // add to empty slabs list
            insert_in_empty_list(cache, current_slab);
        } else {
            // add to partially slabs list
            insert_in_partially_list(cache, current_slab);
        }

        return result;
    }
}


/**
 * The function of freeing memory back to the caching allocator.
 * It is guaranteed that ptr - is a pointer was previously returned from cache_alloc.
 **/
void cache_free(struct cache *cache, void *ptr)
{
    auto slab = calculate_slab_start(cache, ptr);
    slab->refcnt -= 1;

    if (slab->refcnt == 0) {
        remove_from_empty_list(cache, slab);
        insert_in_complete_list(cache, slab);
    }
}


/**
 * The function must release all SLABs, which
 * don't contain retained objects.
 * If SLAB wasn't used for object allocation
 * (for instance, if you allocated memory using
 * alloc_slab for the internal needs of your algorithm),
 * then it is not necessarily to release it
 **/
void cache_shrink(struct cache *cache)
{
    free_list(cache->complete_slab);
    cache->complete_slab = nullptr;
}

int main() {
    auto cache_obj = cache{};
    cache_setup(&cache_obj, 41);

    std::set<void*> refs;
    for (int i = 0; i < 100000; ++i) {
        if (rand() % 2) {
            printf("alloc\n");
            if (auto pointer = cache_alloc(&cache_obj)) {
                refs.insert(pointer);
            }
        } else if (!refs.empty()) {
            printf("free\n");
            int randomIndex = rand() % refs.size();
            auto itr = refs.begin();
            advance(itr, randomIndex);
            cache_free(&cache_obj, *itr);
            refs.erase(itr);
        }
        printf("%p\n", cache_obj.complete_slab);
        printf("%p\n", cache_obj.partially_slab);
        printf("%p\n", cache_obj.empty_slab);
        printf("\n");
    }

    while (!refs.empty()) {
        auto element = *refs.begin();
        cache_free(&cache_obj, element);
        refs.erase(refs.begin());
    }

    cache_shrink(&cache_obj);

    printf("\n\n");
    printf("%p\n", cache_obj.complete_slab);
    printf("%p\n", cache_obj.partially_slab);
    printf("%p\n", cache_obj.empty_slab);

    return 0;
}

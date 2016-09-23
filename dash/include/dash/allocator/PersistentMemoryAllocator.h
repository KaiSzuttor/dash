#ifndef DASH__ALLOCATOR__PERSISTENT_MEMORY_ALLOCATOR_H_INCLUDED
#define DASH__ALLOCATOR__PERSISTENT_MEMORY_ALLOCATOR_H__INCLUDED

#include <sys/stat.h>
#include <unistd.h>

#include <dash/dart/if/dart.h>

#include <dash/Types.h>
#include <dash/Team.h>

#include <dash/internal/Logging.h>
#include <dash/util/String.h>

#include <vector>
#include <algorithm>
#include <utility>

namespace dash {
namespace allocator {

/**
 * Encapsulates a memory allocation and deallocation strategy of global
 * memory regions distributed across local memory of units in a specified
 * team.
 *
 * Satisfied STL concepts:
 *
 * - Allocator
 * - CopyAssignable
 *
 * \concept{DashAllocatorConcept}
 */
template<typename ElementType>
class PersistentMemoryAllocator {
  template <class T, class U>
  friend bool operator==(
    const PersistentMemoryAllocator<T> & lhs,
    const PersistentMemoryAllocator<U> & rhs);

  template <class T, class U>
  friend bool operator!=(
    const PersistentMemoryAllocator<T> & lhs,
    const PersistentMemoryAllocator<U> & rhs);


  /// Type definitions required for std::allocator concept:
public:
  using value_type                             = ElementType;
  using size_type                              = dash::default_size_t;
  //using propagate_on_container_move_assignment = std::true_type;

  /// Type definitions required for dash::allocator concept:
public:
  typedef dash::gptrdiff_t        difference_type;
  typedef dart_gptr_t                     pointer;
  typedef dart_gptr_t                void_pointer;
  typedef dart_gptr_t               const_pointer;
  typedef dart_gptr_t          const_void_pointer;
  typedef value_type        *       local_pointer;
  typedef const value_type  * const_local_pointer;

public:
  /// Convert PersistentMemoryAllocator<T> to PersistentMemoryAllocator<U>.
  template<typename U>
  struct rebind {
    typedef PersistentMemoryAllocator<U> other;
  };

private:
  using self_t = PersistentMemoryAllocator<ElementType>;

  struct pmem_bucket_info {
    //dart_pmem_oid_t pmem_addr;
    size_t          nbytes = 0;
    dart_gptr_t gptr = DART_GPTR_NULL;
  };

  using pmem_bucket_info_t = struct pmem_bucket_info;
  using pmem_bucket_item_t = std::pair<local_pointer, pmem_bucket_info_t>;

public:
  /**
   * Constructor.
   * Creates a new instance of \c dash::PersistentMemoryAllocator for a given team.
   */
  explicit PersistentMemoryAllocator(
    Team & team = dash::Team::All()):
    _team(&team),
    _team_id(team.dart_id()),
    _nunits(team.size())
  {
    DASH_LOG_TRACE("PersistentMemoryAllocator.PersistentMemoryAllocator(team)",
                   _nunits);
    //Ensure that this file is exclusively created
    //If the path already exists it raises an error
    int flags = DART_PMEM_FILE_CREATE | DART_PMEM_FILE_EXCL;

    _pmem_pool = dart__pmem__open(_team_id, _poolId.c_str(), flags, S_IRWXU);

    DASH_ASSERT_MSG(_pmem_pool != nullptr, "_pmem_pool must not be NULL");

    DASH_LOG_TRACE("PersistentMemoryAllocator.PersistentMemoryAllocator(team) >",
                   _nunits);
  }

  PersistentMemoryAllocator(
    Team & team,
    std::string const & poolId) noexcept:
    _team(&team),
    _team_id(team.dart_id()),
    _nunits(team.size()),
    _poolId(poolId)
    //TODO: save objectid
  {
    DASH_LOG_TRACE("PersistentMemoryAllocator.PersistentMemoryAllocator(team, poolId)",
                   _poolId);

    _pmem_pool = dart__pmem__open(_team_id, _poolId.c_str(), DART_PMEM_FILE_CREATE,
                                  S_IRWXU);

    DASH_ASSERT_MSG(_pmem_pool != nullptr, "_pmem_pool must not be NULL");

    relocate_pmem_buckets();

    DASH_LOG_TRACE("PersistentMemoryAllocator.PersistentMemoryAllocator(team, poolId) >");
  }

  /**
   * Move-constructor.
   * Takes ownership of the moved instance's allocation.
   */
  PersistentMemoryAllocator(self_t && other) noexcept:
    _poolId("")
  {
    std::swap(_team, other._team);
    std::swap(_team_id, other._team_id);
    std::swap(_nunits, other._nunits);
    std::swap(_poolId, other._poolId);
    std::swap(_pmem_pool, other._pmem_pool);
    std::swap(_allocated, other._allocated);

    DASH_LOG_TRACE("PersistentMemoryAllocator.PersistentMemoryAllocator(&&) >");
  }

  /**
   * Default constructor, deleted.
   */
  PersistentMemoryAllocator() noexcept
    = delete;

  /**
   * Copy constructor.
   *
   * \see DashAllocatorConcept
   */
  PersistentMemoryAllocator(const self_t & other) noexcept:
    _team(other._team),
    _team_id(other._team_id),
    _nunits(other._nunits),
    _poolId(other._poolId)
  {

    /*
    _pmem_pool = dart__pmem__open(_team_id, _poolId.c_str(), DART_PMEM_FILE_CREATE,
                                  S_IRWXU);
    */

    DASH_THROW(
      dash::exception::NotImplemented,
      "PersistentMemoryAllocator.PersistentMemoryAllocator(self_t const &) is not implemented!");
  }

  /**
   * Copy-constructor.
   * Does not take ownership of the copied instance's allocation.
   */
  template<class U>
  PersistentMemoryAllocator(
    const PersistentMemoryAllocator<U> & other) noexcept:
    _team(other._team),
    _team_id(other._team_id),
    _nunits(other._nunits)
  {
    DASH_LOG_TRACE("PersistentMemoryAllocator.PersistentMemoryAllocator(U other)");
  }

  /**
   * Destructor.
   * Frees all global memory regions allocated by this allocator instance.
   */
  ~PersistentMemoryAllocator() noexcept
  {
    clear();

    //closing the pool and free the pool handle
    if (_pmem_pool != nullptr) {
      DASH_ASSERT_RETURNS(dart__pmem__close(&_pmem_pool), DART_OK);
    }
    DASH_LOG_TRACE("PersistentMemoryAllocator.~PersistentMemoryAllocator >");
  }

  /**
   * Assignment operator.
   *
   * \see DashAllocatorConcept
   */
  self_t & operator=(const self_t & other) noexcept
  {
    // noop
    return *this;
  }

  /**
   * Move-assignment operator.
   */
  self_t & operator=(const self_t && other) noexcept
  {
    DASH_LOG_DEBUG("PersistentMemoryAllocator.=(&&)()");
    // Take ownership of other instance's allocation vector:
    clear();
    _allocated = other._allocated;
    other._allocated.clear();
    DASH_LOG_DEBUG("PersistentMemoryAllocator.=(&&) >");
    return *this;
  }

  /**
   * Whether storage allocated by this allocator can be deallocated
   * through the given allocator instance.
   * Establishes reflexive, symmetric, and transitive relationship.
   * Does not throw exceptions.
   *
   * \returns  true if the storage allocated by this allocator can be
   *           deallocated through the given allocator instance.
   *
   * \see DashAllocatorConcept
   */
  bool operator==(const self_t & rhs) const noexcept
  {
    return (_team_id == rhs._team_id);
  }

  /**
   * Whether storage allocated by this allocator cannot be deallocated
   * through the given allocator instance.
   * Establishes reflexive, symmetric, and transitive relationship.
   * Does not throw exceptions.
   * Same as \c !(operator==(rhs)).
   *
   * \returns  true if the storage allocated by this allocator cannot be
   *           deallocated through the given allocator instance.
   *
   * \see DashAllocatorConcept
   */
  bool operator!=(const self_t & rhs) const noexcept
  {
    return !(*this == rhs);
  }

  /**
   * Team containing units associated with the allocator's memory space.
   */
  inline dash::Team & team() const noexcept
  {
    if (_team == nullptr) {
      return dash::Team::Null();
    }
    return *_team;
  }

  /**
   * Register pre-allocated local memory segment of \c num_local_elem
   * elements in global memory space.
   *
   * Collective operation.
   * The number of allocated elements may differ between units.
   *
   * \see DashDynamicAllocatorConcept
   */
  pointer attach(local_pointer lptr, size_type num_local_elem)
  {
    size_type num_local_bytes = sizeof(ElementType) * num_local_elem;

    auto bucket_it =
      std::find_if(_allocated.begin(), _allocated.end(),
    [lptr] (pmem_bucket_item_t const & i) {
      return i.first == lptr;
    });

    if (bucket_it == _allocated.end()) {
      DASH_LOG_ERROR("local_pointer ", lptr,
                     " has never been allocated in persistent memory");
      return DART_GPTR_NULL;
    }

    if (dart_team_memregister(
          _team_id, num_local_bytes, lptr, &bucket_it->second.gptr) == DART_OK) {
      DASH_LOG_TRACE("PersistentMemoryAllocator.attach ", num_local_bytes,
                     " bytes >");
      return bucket_it->second.gptr;
    }
    return DART_GPTR_NULL;
  }

  /**
   * Unregister local memory segment from global memory space.
   * Does not deallocate local memory.
   *
   * Collective operation.
   *
   * \see DashDynamicAllocatorConcept
   */
  void detach(pointer gptr)
  {
    DASH_LOG_DEBUG("PersistentMemoryAllocator.detach()", "gptr:", gptr);
    if (!dash::is_initialized()) {
      // If a DASH container is deleted after dash::finalize(), global
      // memory has already been freed by dart_exit() and must not be
      // deallocated again.
      DASH_LOG_DEBUG("PersistentMemoryAllocator.detach >",
                     "DASH not initialized, abort");
      return;
    }

    detach_bucket_by_gptr(gptr, false);

    DASH_LOG_DEBUG("PersistentMemoryAllocator.detach >");

  }

  std::vector<std::pair<local_pointer, size_t>> get_local_pointers() const noexcept
  {
    std::vector<std::pair<local_pointer, size_t>> pointers;

    auto first_element = [](pmem_bucket_item_t const & item) {
      auto nel = item.second.nbytes / sizeof(value_type);
      return std::make_pair(item.first, nel);
    };

    std::transform(_allocated.begin(), _allocated.end(),
                   std::back_inserter(pointers), first_element);

    return pointers;
  }

  /**
   * Allocates \c num_local_elem local elements in the active unit's local
   * memory.
   *
   * Local operation.
   *
   * \see DashDynamicAllocatorConcept
   */
  local_pointer allocate_local(size_type num_local_elem)
  {
    local_pointer lptr = nullptr;
    size_t nbytes =  sizeof(value_type) * num_local_elem;

    //allocate persistent memory
    dart_pmem_oid_t oid = dart__pmem__alloc(_pmem_pool, nbytes);

    //convert it to a native address
    dart_ret_t success = dart__pmem__getaddr(oid,
                         reinterpret_cast<void **>(&lptr));

    if (success == DART_OK) {
      pmem_bucket_info_t bucket;
      //bucket.pmem_addr = oid;
      bucket.nbytes = nbytes;
      _allocated.push_back(std::make_pair(lptr, bucket));
    }


    DASH_LOG_DEBUG("PersistentMemoryAllocator.allocate_local: ",
                   sizeof(value_type) * num_local_elem, " bytes");

    return lptr;
  }

  /**
   * Deallocates memory segment in the active unit's local memory.
   *
   * Local operation.
   *
   * \see DashDynamicAllocatorConcept
   */
  void deallocate_local(local_pointer lptr)
  {
    DASH_THROW(
      dash::exception::NotImplemented,
      "PersistentMemoryAllocator.deallocate_local is not implemented!");
  }

  /**
   * Allocates \c num_local_elem local elements at active unit and attaches
   * the local memory segment in global memory space.
   *
   * Collective operation.
   * The number of allocated elements may differ between units.
   *
   * \see DashAllocatorConcept
   */
  pointer allocate(size_type num_local_elem)
  {
    local_pointer lmem = allocate_local(num_local_elem);
    pointer       gmem = attach(lmem, num_local_elem);
    if (DART_GPTR_EQUAL(gmem, DART_GPTR_NULL)) {
      // Attach failed, free requested local memory:
      deallocate_local(lmem);
    }
    return gmem;
  }

  /**
   * Detaches memory segment from global memory space and deallocates the
   * associated local memory region.
   *
   * Collective operation.
   *
   * \see DashAllocatorConcept
   */
  void deallocate(pointer gptr)
  {
    DASH_LOG_DEBUG("PersistentMemoryAllocator.deallocate()", "gptr:", gptr);
    if (!dash::is_initialized()) {
      // If a DASH container is deleted after dash::finalize(), global
      // memory has already been freed by dart_exit() and must not be
      // deallocated again.
      DASH_LOG_DEBUG("PersistentMemoryAllocator.deallocate >",
                     "DASH not initialized, abort");
      return;
    }
    // Free local memory:
    DASH_LOG_DEBUG("PersistentMemoryAllocator.deallocate",
                   "deallocate local memory");

    detach_bucket_by_gptr(gptr, true);

    DASH_LOG_DEBUG("PersistentMemoryAllocator.deallocate >");
  }

  inline bool isPersistent() const noexcept
  {
    return true;
  }

private:
  /**
   * Frees and detaches all global memory regions allocated by this allocator
   * instance.
   */

  void detach_bucket_by_gptr(dart_gptr_t const & gptr, bool deallocate = false)
  {
    DASH_LOG_DEBUG("PersistentMemoryAllocator.detach_bucket_by_gptr",
                   "deallocate: ", deallocate);
    auto bucket_it =
      std::find_if(_allocated.begin(), _allocated.end(),
    [&gptr] (pmem_bucket_item_t const & i) {
      return i.second.gptr == gptr;
    });

    if (bucket_it == _allocated.end()) {
      DASH_LOG_ERROR("PersistentMemoryAllocator.detach: cannot detach gptr");
      return;
    }

    if (dart_team_memderegister(_team_id, gptr) == DART_OK) {
      //persist all changes in persistent memory
      DASH_ASSERT_RETURNS(
        dart__pmem__persist(_pmem_pool, bucket_it->first, bucket_it->second.nbytes),
        DART_OK);

      if (deallocate) {
        bucket_it->first = nullptr;
        bucket_it->second.gptr = DART_GPTR_NULL;
        bucket_it->second.nbytes = 0;

        //TODO: free persistent memory region
      }

      _allocated.erase(bucket_it);
    }
    DASH_LOG_DEBUG("PersistentMemoryAllocator.detach_bucket_by_gptr >");
  }

  void clear() noexcept
  {
    DASH_LOG_DEBUG("PersistentMemoryAllocator.clear()");
    for (auto e : _allocated) {

      if (!DART_GPTR_EQUAL(e.second.gptr, DART_GPTR_NULL)) {
        DASH_LOG_DEBUG("PersistentMemoryAllocator.clear",
                       "detach local persistent memory:",
                       e.second.gptr);

        detach_bucket_by_gptr(e.second.gptr, false);
      }
      // Null-buckets have lptr set to nullptr
      /*
      if (e.first != nullptr) {
        DASH_LOG_DEBUG("PersistentMemoryAllocator.clear", "deallocate local memory:",
                       e.first);
        deallocate_local(e.first);
      }
      */
    }
    _allocated.clear();
    DASH_LOG_DEBUG("PersistentMemoryAllocator.clear >");
  }

  void relocate_pmem_buckets()
  {
    struct dart_pmem_pool_stat stats;

    dart__pmem__pool_stat(_pmem_pool, &stats);

    if (stats.num_buckets > 0) {
      std::vector<dart_pmem_oid_t> bucket_ptrs{stats.num_buckets, DART_PMEM_OID_NULL};

      dart__pmem__fetch_all(_pmem_pool, bucket_ptrs.data());

      local_pointer lptr = nullptr;

      for (auto poid : bucket_ptrs) {
        //convert it to a native address
        dart_ret_t success = dart__pmem__getaddr(poid,
                             reinterpret_cast<void **>(&lptr));

        if (success == DART_OK) {
          pmem_bucket_info_t bucket;
          //bucket.pmem_addr = oid;
          DASH_ASSERT_RETURNS(dart__pmem__oid_size(_pmem_pool, poid, &bucket.nbytes),
                              DART_OK);
          DASH_LOG_DEBUG(__func__, "allocated relocated bucket, nbytes", bucket.nbytes);
          _allocated.push_back(std::make_pair(lptr, bucket));
        }
      }
    }
  }

private:
  dash::Team                   *                  _team      = nullptr;
  dart_team_t                                     _team_id   = DART_TEAM_NULL;
  size_t                                          _nunits    = 0;
  std::vector<pmem_bucket_item_t>                 _allocated;
  std::string                                     _poolId = "pool.pmem";
  dart_pmem_pool_t                *               _pmem_pool = nullptr;

}; // class PersistentMemoryAllocator

template <class T, class U>
bool operator==(
  const PersistentMemoryAllocator<T> & lhs,
  const PersistentMemoryAllocator<U> & rhs)
{
  return (sizeof(T)    == sizeof(U) &&
          lhs._team_id == rhs._team_id &&
          lhs._poolId == rhs._poolId &&
          lhs._nunits  == rhs._nunits );
}

template <class T, class U>
bool operator!=(
  const PersistentMemoryAllocator<T> & lhs,
  const PersistentMemoryAllocator<U> & rhs)
{
  return !(lhs == rhs);
}

} // namespace allocator
} // namespace dash

#endif // DASH__ALLOCATOR__PERSISTENT_MEMORY_ALLOCATOR_H__INCLUDED
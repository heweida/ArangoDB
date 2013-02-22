////////////////////////////////////////////////////////////////////////////////
/// @brief vocbase
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2011 triagens GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Copyright 2011, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_DURHAM_VOC_BASE_VOCBASE_H
#define TRIAGENS_DURHAM_VOC_BASE_VOCBASE_H 1

#include "BasicsC/common.h"

#include "BasicsC/associative.h"
#include "BasicsC/locks.h"
#include "BasicsC/threads.h"
#include "BasicsC/vector.h"
#include "BasicsC/voc-errors.h"

#include "VocBase/server-id.h"
#include "VocBase/sequence.h"
#include "VocBase/transaction.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// --SECTION--                                              forward declarations
// -----------------------------------------------------------------------------

struct TRI_primary_collection_s;
struct TRI_col_info_s;
struct TRI_shadow_store_s;

// -----------------------------------------------------------------------------
// --SECTION--                                                     public macros
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief read locks the collections structure
////////////////////////////////////////////////////////////////////////////////

#define TRI_READ_LOCK_COLLECTIONS_VOCBASE(a) \
  TRI_ReadLockReadWriteLock(&(a)->_lock)

////////////////////////////////////////////////////////////////////////////////
/// @brief read unlocks the collections structure
////////////////////////////////////////////////////////////////////////////////

#define TRI_READ_UNLOCK_COLLECTIONS_VOCBASE(a) \
  TRI_ReadUnlockReadWriteLock(&(a)->_lock)

////////////////////////////////////////////////////////////////////////////////
/// @brief write locks the collections structure
////////////////////////////////////////////////////////////////////////////////

#define TRI_WRITE_LOCK_COLLECTIONS_VOCBASE(a) \
  TRI_WriteLockReadWriteLock(&(a)->_lock)

////////////////////////////////////////////////////////////////////////////////
/// @brief write unlocks the collections structure
////////////////////////////////////////////////////////////////////////////////

#define TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(a) \
  TRI_WriteUnlockReadWriteLock(&(a)->_lock)

////////////////////////////////////////////////////////////////////////////////
/// @brief tries to read lock the vocbase collection status
////////////////////////////////////////////////////////////////////////////////

#define TRI_TRY_READ_LOCK_STATUS_VOCBASE_COL(a) \
  TRI_TryReadLockReadWriteLock(&(a)->_lock)

////////////////////////////////////////////////////////////////////////////////
/// @brief read locks the vocbase collection status
////////////////////////////////////////////////////////////////////////////////

#define TRI_READ_LOCK_STATUS_VOCBASE_COL(a) \
  TRI_ReadLockReadWriteLock(&(a)->_lock)

////////////////////////////////////////////////////////////////////////////////
/// @brief read unlocks the vocbase collection status
////////////////////////////////////////////////////////////////////////////////

#define TRI_READ_UNLOCK_STATUS_VOCBASE_COL(a) \
  TRI_ReadUnlockReadWriteLock(&(a)->_lock)

////////////////////////////////////////////////////////////////////////////////
/// @brief write locks the vocbase collection status
////////////////////////////////////////////////////////////////////////////////

#define TRI_WRITE_LOCK_STATUS_VOCBASE_COL(a) \
  TRI_WriteLockReadWriteLock(&(a)->_lock)

////////////////////////////////////////////////////////////////////////////////
/// @brief write unlocks the vocbase collection status
////////////////////////////////////////////////////////////////////////////////

#define TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(a) \
  TRI_WriteUnlockReadWriteLock(&(a)->_lock)

////////////////////////////////////////////////////////////////////////////////
/// @brief locks the synchroniser waiters
////////////////////////////////////////////////////////////////////////////////

#define TRI_LOCK_SYNCHRONISER_WAITER_VOC_BASE(a) \
  TRI_LockCondition(&(a)->_syncWaitersCondition)

////////////////////////////////////////////////////////////////////////////////
/// @brief unlocks the synchroniser waiters
////////////////////////////////////////////////////////////////////////////////

#define TRI_UNLOCK_SYNCHRONISER_WAITER_VOC_BASE(a) \
  TRI_UnlockCondition(&(a)->_syncWaitersCondition)

////////////////////////////////////////////////////////////////////////////////
/// @brief waits for synchroniser waiters
////////////////////////////////////////////////////////////////////////////////

#define TRI_WAIT_SYNCHRONISER_WAITER_VOC_BASE(a, b) \
  TRI_TimedWaitCondition(&(a)->_syncWaitersCondition, (b))

////////////////////////////////////////////////////////////////////////////////
/// @brief signals the synchroniser waiters
////////////////////////////////////////////////////////////////////////////////

#define TRI_BROADCAST_SYNCHRONISER_WAITER_VOC_BASE(a) \
  TRI_BroadcastCondition(&(a)->_syncWaitersCondition)

////////////////////////////////////////////////////////////////////////////////
/// @brief reduces the number of sync waiters
////////////////////////////////////////////////////////////////////////////////

#define TRI_DEC_SYNCHRONISER_WAITER_VOC_BASE(a)         \
  TRI_LockCondition(&(a)->_syncWaitersCondition);       \
  --((a)->_syncWaiters);                                \
  TRI_UnlockCondition(&(a)->_syncWaitersCondition)

////////////////////////////////////////////////////////////////////////////////
/// @brief reduces the number of sync waiters
////////////////////////////////////////////////////////////////////////////////

#define TRI_INC_SYNCHRONISER_WAITER_VOC_BASE(a)         \
  TRI_LockCondition(&(a)->_syncWaitersCondition);       \
  ++((a)->_syncWaiters);                                \
  TRI_BroadcastCondition(&(a)->_syncWaitersCondition);  \
  TRI_UnlockCondition(&(a)->_syncWaitersCondition)

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public constants
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief page size
////////////////////////////////////////////////////////////////////////////////

extern size_t PageSize;

////////////////////////////////////////////////////////////////////////////////
/// @brief id regex
////////////////////////////////////////////////////////////////////////////////

#define TRI_VOC_ID_REGEX        "[0-9][0-9]*"

////////////////////////////////////////////////////////////////////////////////
/// @brief maximal path length
////////////////////////////////////////////////////////////////////////////////

#define TRI_COL_PATH_LENGTH     (4096)

////////////////////////////////////////////////////////////////////////////////
/// @brief default maximal collection journal size
////////////////////////////////////////////////////////////////////////////////

#define TRI_JOURNAL_DEFAULT_MAXIMAL_SIZE (1024 * 1024 * 32)

////////////////////////////////////////////////////////////////////////////////
/// @brief minimal collection journal size
////////////////////////////////////////////////////////////////////////////////

#define TRI_JOURNAL_MINIMAL_SIZE (1024 * 1024)

////////////////////////////////////////////////////////////////////////////////
/// @brief journal overhead
////////////////////////////////////////////////////////////////////////////////

#define TRI_JOURNAL_OVERHEAD (sizeof(TRI_df_header_marker_t) + sizeof(TRI_df_footer_marker_t))

////////////////////////////////////////////////////////////////////////////////
/// @brief document handle separator as character
////////////////////////////////////////////////////////////////////////////////

#define TRI_DOCUMENT_HANDLE_SEPARATOR_CHR '/'

////////////////////////////////////////////////////////////////////////////////
/// @brief document handle separator as string
////////////////////////////////////////////////////////////////////////////////

#define TRI_DOCUMENT_HANDLE_SEPARATOR_STR "/"

////////////////////////////////////////////////////////////////////////////////
/// @brief index handle separator as character
////////////////////////////////////////////////////////////////////////////////

#define TRI_INDEX_HANDLE_SEPARATOR_CHR '/'

////////////////////////////////////////////////////////////////////////////////
/// @brief index handle separator as string
////////////////////////////////////////////////////////////////////////////////

#define TRI_INDEX_HANDLE_SEPARATOR_STR "/"

////////////////////////////////////////////////////////////////////////////////
/// @brief no limit
////////////////////////////////////////////////////////////////////////////////

#define TRI_QRY_NO_LIMIT ((TRI_voc_size_t) (4294967295U))

////////////////////////////////////////////////////////////////////////////////
/// @brief no skip
////////////////////////////////////////////////////////////////////////////////

#define TRI_QRY_NO_SKIP ((TRI_voc_ssize_t) 0)

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                      public types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief collection identifier type
////////////////////////////////////////////////////////////////////////////////

typedef TRI_sequence_value_t TRI_voc_cid_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief datafile identifier type
////////////////////////////////////////////////////////////////////////////////

typedef TRI_sequence_value_t TRI_voc_fid_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief document key identifier type
////////////////////////////////////////////////////////////////////////////////

typedef char* TRI_voc_key_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief revision identifier type
////////////////////////////////////////////////////////////////////////////////

typedef uint64_t TRI_voc_rid_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief step identifier type
////////////////////////////////////////////////////////////////////////////////

typedef uint64_t TRI_voc_eid_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief transaction identifier type
////////////////////////////////////////////////////////////////////////////////

typedef uint64_t TRI_voc_tid_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief size type
////////////////////////////////////////////////////////////////////////////////

typedef uint32_t TRI_voc_size_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief signed size type
////////////////////////////////////////////////////////////////////////////////

typedef int32_t TRI_voc_ssize_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief boolean flag type
////////////////////////////////////////////////////////////////////////////////

typedef uint32_t TRI_voc_flag_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief milli-seconds
////////////////////////////////////////////////////////////////////////////////

typedef uint32_t TRI_voc_ms_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief crc type
////////////////////////////////////////////////////////////////////////////////

typedef uint32_t TRI_voc_crc_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief collection storage type
////////////////////////////////////////////////////////////////////////////////

typedef uint32_t TRI_col_type_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief database
///
/// There are the following locks:
///
/// @LIT{TRI_vocbase_t._lock}: This lock protects the access to _collections,
/// _collectionsByName, and _collectionsById.
///
/// @LIT{TRI_vocbase_col_t._lock}: This lock protects the status (loaded,
/// unloaded) of the collection. If you want to use a collection, you must call
/// @ref TRI_UseCollectionVocBase, this will either load or manifest the
/// collection and a read-lock is held when the functions returns.  You must
/// call @ref TRI_ReleaseCollectionVocBase, when you finished using the
/// collection. The functions that modify the status of collection (load,
/// unload, manifest) will grab a write-lock.
////////////////////////////////////////////////////////////////////////////////

typedef struct TRI_vocbase_s {
  TRI_server_id_t _serverId;   // server uuid
  
  TRI_read_write_lock_t _lock;

  TRI_vector_pointer_t _collections;
  TRI_vector_pointer_t _deadCollections; // pointers to collections dropped that can be removed later

  TRI_associative_pointer_t _collectionsByName;
  TRI_associative_pointer_t _collectionsById;
  
  TRI_transaction_context_t* _transactionContext;
  
  char* _path;
  char* _lockFile;

  bool _authInfoLoaded;     // flag indicating whether the authentication info was loaded successfully
  bool _removeOnDrop;       // wipe collection from disk after dropping
  bool _removeOnCompacted;  // wipe datafile from disk after compaction
  bool _defaultWaitForSync; // default waitForSync value when creating new collections
  bool _forceSyncShapes;    // force synching of shape data to disk

  TRI_voc_size_t _defaultMaximalSize;

  TRI_associative_pointer_t _authInfo;
  TRI_read_write_lock_t     _authInfoLock;
  
  struct TRI_shadow_store_s* _cursors;
  TRI_associative_pointer_t* _functions; 

  // state of the database
  // 0 = inactive
  // 1 = normal operation/running
  // 2 = shutdown in progress/waiting for compactor/synchroniser thread to finish
  // 3 = shutdown in progress/waiting for cleanup thread to finish
  sig_atomic_t _state; 

  TRI_thread_t _synchroniser;
  TRI_thread_t _compactor;
  TRI_thread_t _cleanup;

  TRI_condition_t _cleanupCondition;
  TRI_condition_t _syncWaitersCondition;
  int64_t _syncWaiters;  
}
TRI_vocbase_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief status of a collection
////////////////////////////////////////////////////////////////////////////////

typedef enum {
  TRI_VOC_COL_STATUS_CORRUPTED = 0,
  TRI_VOC_COL_STATUS_NEW_BORN  = 1,
  TRI_VOC_COL_STATUS_UNLOADED  = 2,
  TRI_VOC_COL_STATUS_LOADED    = 3,
  TRI_VOC_COL_STATUS_UNLOADING = 4,
  TRI_VOC_COL_STATUS_DELETED   = 5
}
TRI_vocbase_col_status_e;

////////////////////////////////////////////////////////////////////////////////
/// @brief collection container
////////////////////////////////////////////////////////////////////////////////

typedef struct TRI_vocbase_col_s {
  TRI_vocbase_t* _vocbase;

  TRI_col_type_t _type;              // collection type
  TRI_voc_cid_t _cid;                // collecttion identifier
  char _name[TRI_COL_PATH_LENGTH];   // name of the collection

  char const* _path;                 // path to the collection files

  TRI_read_write_lock_t _lock;               // lock protecting the status
  TRI_vocbase_col_status_e _status;          // status of the collection
  struct TRI_primary_collection_s* _collection;  // NULL or pointer to loaded collection
}
TRI_vocbase_col_t;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief checks if a collection is allowed
////////////////////////////////////////////////////////////////////////////////

bool TRI_IsAllowedCollectionName (bool, char const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief msyncs a memory block between begin (incl) and end (excl)
////////////////////////////////////////////////////////////////////////////////

bool TRI_msync (int fd, void* mmHandle, char const* begin, char const* end);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief opens an exiting database, loads all collections
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_t* TRI_OpenVocBase (char const* path);

////////////////////////////////////////////////////////////////////////////////
/// @brief closes a database and all collections
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyVocBase (TRI_vocbase_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief load authentication information
////////////////////////////////////////////////////////////////////////////////

void TRI_LoadAuthInfoVocBase (TRI_vocbase_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief returns all known collections
////////////////////////////////////////////////////////////////////////////////

TRI_vector_pointer_t TRI_CollectionsVocBase (TRI_vocbase_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief get a collection name by a collection id
///
/// the name is fetched under a lock to make this thread-safe. returns NULL if
/// the collection does not exist
/// it is the caller's responsibility to free the name returned
////////////////////////////////////////////////////////////////////////////////

char* TRI_GetCollectionNameByIdVocBase (TRI_vocbase_t*, const TRI_voc_cid_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a (document) collection by name
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* TRI_LookupCollectionByNameVocBase (TRI_vocbase_t*, char const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a (document) collection by identifier
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* TRI_LookupCollectionByIdVocBase (TRI_vocbase_t*, TRI_voc_cid_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief finds a collection by name or creates it
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* TRI_FindCollectionByNameOrBearVocBase (TRI_vocbase_t*, 
                                                          char const*, 
                                                          const TRI_col_type_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new (document) collection from parameter set
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* TRI_CreateCollectionVocBase (TRI_vocbase_t*, 
                                                struct TRI_col_info_s*,
                                                TRI_voc_cid_t cid);

////////////////////////////////////////////////////////////////////////////////
/// @brief unloads a (document) collection
////////////////////////////////////////////////////////////////////////////////

int TRI_UnloadCollectionVocBase (TRI_vocbase_t*, TRI_vocbase_col_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief drops a (document) collection
////////////////////////////////////////////////////////////////////////////////

int TRI_DropCollectionVocBase (TRI_vocbase_t*, TRI_vocbase_col_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief renames a (document) collection
////////////////////////////////////////////////////////////////////////////////

int TRI_RenameCollectionVocBase (TRI_vocbase_t*, TRI_vocbase_col_t*, char const* name);

////////////////////////////////////////////////////////////////////////////////
/// @brief locks a (document) collection for usage, loading or manifesting it
///
/// Note that this will READ lock the collection you have to release the
/// collection lock by yourself.
////////////////////////////////////////////////////////////////////////////////

int TRI_UseCollectionVocBase (TRI_vocbase_t*, TRI_vocbase_col_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief locks a (document) collection for usage by id
///
/// Note that this will READ lock the collection you have to release the
/// collection lock by yourself and call @ref TRI_ReleaseCollectionVocBase
/// when you are done with the collection.
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* TRI_UseCollectionByIdVocBase (TRI_vocbase_t*, const TRI_voc_cid_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief locks a (document) collection for usage by name
///
/// Note that this will READ lock the collection you have to release the
/// collection lock by yourself and call @ref TRI_ReleaseCollectionVocBase
/// when you are done with the collection.
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* TRI_UseCollectionByNameVocBase (TRI_vocbase_t*, char const* name);

////////////////////////////////////////////////////////////////////////////////
/// @brief releases a (document) collection from usage
////////////////////////////////////////////////////////////////////////////////

void TRI_ReleaseCollectionVocBase (TRI_vocbase_t*, TRI_vocbase_col_t*);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                            MODULE
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises the voc database components
////////////////////////////////////////////////////////////////////////////////

void TRI_InitialiseVocBase (void);

////////////////////////////////////////////////////////////////////////////////
/// @brief shut downs the voc database components
////////////////////////////////////////////////////////////////////////////////

void TRI_ShutdownVocBase (void);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:

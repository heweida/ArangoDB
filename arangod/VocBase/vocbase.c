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

#ifdef _WIN32
#include "BasicsC/win-utils.h"
#endif

#include "vocbase.h"

#include <regex.h>

#include "BasicsC/files.h"
#include "BasicsC/hashes.h"
#include "BasicsC/locks.h"
#include "BasicsC/logging.h"
#include "BasicsC/memory-map.h"
#include "BasicsC/random.h"
#include "BasicsC/strings.h"
#include "BasicsC/threads.h"
#include "VocBase/auth.h"
#include "VocBase/barrier.h"
#include "VocBase/cleanup.h"
#include "VocBase/compactor.h"
#include "VocBase/primary-collection.h"
#include "VocBase/shadow-data.h"
#include "VocBase/document-collection.h"
#include "VocBase/synchroniser.h"
#include "VocBase/general-cursor.h"

#include "Ahuacatl/ahuacatl-functions.h"
#include "Ahuacatl/ahuacatl-statementlist.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief random server identifier (16 bit)
////////////////////////////////////////////////////////////////////////////////

static uint16_t ServerIdentifier = 0;

////////////////////////////////////////////////////////////////////////////////
/// @brief current tick identifier (48 bit)
////////////////////////////////////////////////////////////////////////////////

static uint64_t CurrentTick = 0;

////////////////////////////////////////////////////////////////////////////////
/// @brief tick lock
////////////////////////////////////////////////////////////////////////////////

static TRI_spin_t TickLock;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                             COLLECTION DICTIONARY
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief hashs the auth info
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashKeyAuthInfo (TRI_associative_pointer_t* array, void const* key) {
  char const* k = (char const*) key;

  return TRI_FnvHashString(k);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashs the auth info
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementAuthInfo (TRI_associative_pointer_t* array, void const* element) {
  TRI_vocbase_auth_t const* e = element;

  return TRI_FnvHashString(e->_username);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares a auth info and a username
////////////////////////////////////////////////////////////////////////////////

static bool EqualKeyAuthInfo (TRI_associative_pointer_t* array, void const* key, void const* element) {
  char const* k = (char const*) key;
  TRI_vocbase_auth_t const* e = element;

  return TRI_EqualString(k, e->_username);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashs the collection id
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashKeyCid (TRI_associative_pointer_t* array, void const* key) {
  TRI_voc_cid_t const* k = key;

  return *k;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashs the collection id
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementCid (TRI_associative_pointer_t* array, void const* element) {
  TRI_vocbase_col_t const* e = element;

  return e->_cid;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares a collection id and a collection
////////////////////////////////////////////////////////////////////////////////

static bool EqualKeyCid (TRI_associative_pointer_t* array, void const* key, void const* element) {
  TRI_voc_cid_t const* k = key;
  TRI_vocbase_col_t const* e = element;

  return *k == e->_cid;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashs the collection name
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashKeyCollectionName (TRI_associative_pointer_t* array, void const* key) {
  char const* k = (char const*) key;

  return TRI_FnvHashString(k);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashs the collection name
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementCollectionName (TRI_associative_pointer_t* array, void const* element) {
  TRI_vocbase_col_t const* e = element;
  char const* name = e->_name;

  return TRI_FnvHashString(name);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares a collection name and a collection
////////////////////////////////////////////////////////////////////////////////

static bool EqualKeyCollectionName (TRI_associative_pointer_t* array, void const* key, void const* element) {
  char const* k = (char const*) key;
  TRI_vocbase_col_t const* e = element;

  return TRI_EqualString(k, e->_name);
}
    
////////////////////////////////////////////////////////////////////////////////
/// @brief remove a collection name from the global list of collections
///
/// this is called when a collection is dropped. it is necessary that the 
/// caller also holds a write-lock using TRI_WRITE_LOCK_STATUS_VOCBASE_COL().
////////////////////////////////////////////////////////////////////////////////

static bool UnregisterCollection (TRI_vocbase_t* vocbase, TRI_vocbase_col_t* collection) {
  TRI_WRITE_LOCK_COLLECTIONS_VOCBASE(vocbase);

  TRI_RemoveKeyAssociativePointer(&vocbase->_collectionsByName, collection->_name);
  TRI_RemoveKeyAssociativePointer(&vocbase->_collectionsById, &collection->_cid);

  TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief unloads a collection
////////////////////////////////////////////////////////////////////////////////

static bool UnloadCollectionCallback (TRI_collection_t* col, void* data) {
  TRI_vocbase_col_t* collection;
  TRI_document_collection_t* sim;
  int res;

  collection = data;

  TRI_WRITE_LOCK_STATUS_VOCBASE_COL(collection);

  if (collection->_status != TRI_VOC_COL_STATUS_UNLOADING) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return false;
  }

  if (collection->_collection == NULL) {
    collection->_status = TRI_VOC_COL_STATUS_CORRUPTED;

    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return true;
  }

  if (! TRI_IS_DOCUMENT_COLLECTION(collection->_collection->base._type)) {
    LOG_ERROR("cannot unload collection '%s' of type '%d'",
              collection->_name,
              (int) collection->_collection->base._type);

    collection->_status = TRI_VOC_COL_STATUS_LOADED;

    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return false;
  }

  sim = (TRI_document_collection_t*) collection->_collection;

  res = TRI_CloseDocumentCollection(sim);

  if (res != TRI_ERROR_NO_ERROR) {
    LOG_ERROR("failed to close collection '%s': %s",
              collection->_name,
              TRI_last_error());

    collection->_status = TRI_VOC_COL_STATUS_CORRUPTED;

    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return true;
  }

  TRI_FreeDocumentCollection(sim);

  collection->_status = TRI_VOC_COL_STATUS_UNLOADED;
  collection->_collection = NULL;

  TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief drops a collection
////////////////////////////////////////////////////////////////////////////////

static bool DropCollectionCallback (TRI_collection_t* col, void* data) {
  TRI_document_collection_t* sim;
  TRI_vocbase_col_t* collection;
  TRI_vocbase_t* vocbase;
  regmatch_t matches[3];
  regex_t re;
  char* tmp1;
  char* tmp2;
  char* tmp3;
  char* newFilename;
  int res;
  size_t i;
  
  collection = data;

  TRI_WRITE_LOCK_STATUS_VOCBASE_COL(collection);

  if (collection->_status != TRI_VOC_COL_STATUS_DELETED) {
    LOG_ERROR("someone resurrected the collection '%s'", collection->_name);
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return false;
  }

  // .............................................................................
  // unload collection
  // .............................................................................

  if (collection->_collection != NULL) {
    if (! TRI_IS_DOCUMENT_COLLECTION(collection->_collection->base._type)) {
      LOG_ERROR("cannot drop collection '%s' of type '%d'",
                collection->_name,
                (int) collection->_collection->base._type);

      TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
      return false;
    }

    sim = (TRI_document_collection_t*) collection->_collection;

    res = TRI_CloseDocumentCollection(sim);

    if (res != TRI_ERROR_NO_ERROR) {
      LOG_ERROR("failed to close collection '%s': %s",
                collection->_name,
                TRI_last_error());

      TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
      return true;
    }

    TRI_FreeDocumentCollection(sim);

    collection->_collection = NULL;
  }

  TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

  // .............................................................................
  // remove from list of collections
  // .............................................................................

  vocbase = collection->_vocbase;

  TRI_WRITE_LOCK_COLLECTIONS_VOCBASE(vocbase);

  for (i = 0;  i < vocbase->_collections._length;  ++i) {
    if (vocbase->_collections._buffer[i] == collection) {
      TRI_RemoveVectorPointer(&vocbase->_collections, i);
      break;
    }
  }
 
  // we need to clean up the pointers later so we insert it into this vector
  TRI_PushBackVectorPointer(&vocbase->_deadCollections, collection);
  
  TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);


  // .............................................................................
  // rename collection directory
  // .............................................................................

  if (collection->_path != NULL) {
    int regExpResult;

    #ifdef _WIN32
      // .........................................................................
      // Just thank your lucky stars that there are only 4 backslashes
      // .........................................................................
      regcomp(&re, "^(.*)\\\\collection-([0-9][0-9]*)$", REG_ICASE | REG_EXTENDED);
    #else
      regcomp(&re, "^(.*)/collection-([0-9][0-9]*)$", REG_ICASE | REG_EXTENDED);
    #endif

    
    regExpResult = regexec(&re, collection->_path, sizeof(matches) / sizeof(matches[0]), matches, 0); 

    if (regExpResult == 0) {

      char const* first = collection->_path + matches[1].rm_so;
      size_t firstLen = matches[1].rm_eo - matches[1].rm_so;
      
      char const* second = collection->_path + matches[2].rm_so;
      size_t secondLen = matches[2].rm_eo - matches[2].rm_so;
      
      tmp1 = TRI_DuplicateString2(first, firstLen);
      tmp2 = TRI_DuplicateString2(second, secondLen);
      tmp3 = TRI_Concatenate2String("deleted-", tmp2);

      TRI_FreeString(TRI_CORE_MEM_ZONE, tmp2);
      
      newFilename = TRI_Concatenate2File(tmp1, tmp3);

      TRI_FreeString(TRI_CORE_MEM_ZONE, tmp1);
      TRI_FreeString(TRI_CORE_MEM_ZONE, tmp3);
      
      res = TRI_RenameFile(collection->_path, newFilename);
      
      if (res != TRI_ERROR_NO_ERROR) {
        LOG_ERROR("cannot rename dropped collection '%s' from '%s' to '%s'",
                  collection->_name,
                  collection->_path,
                  newFilename);
      }
      else {
        if (collection->_vocbase->_removeOnDrop) {
          LOG_DEBUG("wiping dropped collection '%s' from disk",
                    collection->_name);

          res = TRI_RemoveDirectory(newFilename);

          if (res != TRI_ERROR_NO_ERROR) {
            LOG_ERROR("cannot wipe dropped collecton '%s' from disk: %s",
                      collection->_name,
                      TRI_last_error());
          }
        }
        else {
          LOG_DEBUG("renamed dropped collection '%s' from '%s' to '%s'",
                    collection->_name,
                    collection->_path,
                    newFilename);
        }
      }

      TRI_FreeString(TRI_CORE_MEM_ZONE, newFilename);
    }
    else {
      LOG_ERROR("cannot rename dropped collection '%s': unknown path '%s'",
                collection->_name,
                collection->_path);
    }

    regfree(&re);
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                           VOCBASE
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief free the path buffer allocated for a collection
////////////////////////////////////////////////////////////////////////////////

static inline void FreeCollectionPath (TRI_vocbase_col_t* const collection) {
  if (collection->_path) {
    TRI_Free(TRI_CORE_MEM_ZONE, (char*) collection->_path);
  }
  collection->_path = NULL;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free the memory associated with a collection
////////////////////////////////////////////////////////////////////////////////

static void FreeCollection (TRI_vocbase_t* vocbase, TRI_vocbase_col_t* collection) {
  FreeCollectionPath(collection);

  TRI_DestroyReadWriteLock(&collection->_lock);

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, collection);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief adds a new collection
////////////////////////////////////////////////////////////////////////////////

static TRI_vocbase_col_t* AddCollection (TRI_vocbase_t* vocbase,
                                         TRI_col_type_e type,
                                         char const* name,
                                         TRI_voc_cid_t cid,
                                         char const* path) {
  void const* found;
  TRI_vocbase_col_t* collection;

  // create a new proxy
  collection = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_vocbase_col_t), false);
  if (collection == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return NULL;
  }

  collection->_vocbase = vocbase;
  collection->_type = (TRI_col_type_t) type;
  TRI_CopyString(collection->_name, name, sizeof(collection->_name));
  if (path == NULL) {
    collection->_path = NULL;
  }
  else {
    collection->_path = TRI_DuplicateString(path);
    if (!collection->_path) {
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, collection);
      TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

      return NULL;
    }
  }

  collection->_collection = NULL;
  collection->_status = TRI_VOC_COL_STATUS_CORRUPTED;
  collection->_cid = cid;

  // check name
  found = TRI_InsertKeyAssociativePointer(&vocbase->_collectionsByName, name, collection, false);

  if (found != NULL) {
    FreeCollectionPath(collection);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, collection);

    LOG_ERROR("duplicate entry for collection name '%s'", name);
    TRI_set_errno(TRI_ERROR_ARANGO_DUPLICATE_NAME);

    return NULL;
  }

  // check collection identifier (unknown for new born collections)
  found = TRI_InsertKeyAssociativePointer(&vocbase->_collectionsById, &cid, collection, false);

  if (found != NULL) {
    TRI_RemoveKeyAssociativePointer(&vocbase->_collectionsByName, name);
    FreeCollectionPath(collection);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, collection);

    LOG_ERROR("duplicate collection identifier '%lu' for name '%s'", (unsigned long) cid, name);
    TRI_set_errno(TRI_ERROR_ARANGO_DUPLICATE_IDENTIFIER);

    return NULL;
  }

  TRI_InitReadWriteLock(&collection->_lock);

  TRI_PushBackVectorPointer(&vocbase->_collections, collection);
  return collection;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief scans a directory and loads all collections
////////////////////////////////////////////////////////////////////////////////

static int ScanPath (TRI_vocbase_t* vocbase, char const* path) {
  TRI_vector_string_t files;
  regmatch_t matches[2];
  regex_t re;
  int res;
  size_t n;
  size_t i;

  files = TRI_FilesDirectory(path);
  n = files._length;

  regcomp(&re, "^collection-([0-9][0-9]*)$", REG_ICASE | REG_EXTENDED);

  for (i = 0;  i < n;  ++i) {
    char* name;
    char* file;

    name = files._buffer[i];
    assert(name);

    if (regexec(&re, name, sizeof(matches) / sizeof(matches[0]), matches, 0) != 0) {
      // do not issue a notice about the "lock" file
      if (! TRI_EqualString(name, "lock")) {
        LOG_DEBUG("ignoring file/directory '%s'", name);
      }
      continue;
    }

    file = TRI_Concatenate2File(path, name);

    if (!file) {
      LOG_FATAL("out of memory");
      regfree(&re);
      return TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
    }

    if (TRI_IsDirectory(file)) {
      TRI_col_info_t info;

      // no need to lock as we are scanning
      res = TRI_LoadParameterInfoCollection(file, &info);

      if (res == TRI_ERROR_NO_ERROR) {
        TRI_UpdateTickVocBase(info._cid);
      }

      if (res != TRI_ERROR_NO_ERROR) {
        LOG_DEBUG("ignoring directory '%s' without valid parameter file '%s'", file, TRI_COL_PARAMETER_FILE);
      }
      else if (info._deleted) {
        if (vocbase->_removeOnDrop) {
          LOG_WARNING("collection '%s' was deleted, wiping it", name);

          res = TRI_RemoveDirectory(file);

          if (res != TRI_ERROR_NO_ERROR) {
            LOG_WARNING("cannot wipe deleted collection: %s", TRI_last_error());
          }
        }
        else {
          char* newFile;
          char* tmp1;
          char* tmp2;

          char const* first = name + matches[1].rm_so;
          size_t firstLen = matches[1].rm_eo - matches[1].rm_so;

          tmp1 = TRI_DuplicateString2(first, firstLen);
          tmp2 = TRI_Concatenate2String("deleted-", tmp1);

          TRI_FreeString(TRI_CORE_MEM_ZONE, tmp1);

          newFile = TRI_Concatenate2File(path, tmp2);

          TRI_FreeString(TRI_CORE_MEM_ZONE, tmp2);

          LOG_WARNING("collection '%s' was deleted, renaming it to '%s'", name, newFile);

          res = TRI_RenameFile(file, newFile);

          if (res != TRI_ERROR_NO_ERROR) {
            LOG_WARNING("cannot rename deleted collection: %s", TRI_last_error());
          }

          TRI_FreeString(TRI_CORE_MEM_ZONE, newFile);
        }
      }
      else {
        TRI_col_type_e type = (TRI_col_type_e) info._type;

        if (TRI_IS_DOCUMENT_COLLECTION(type)) {
          TRI_vocbase_col_t* c;

          c = AddCollection(vocbase, type, info._name, info._cid, file);

          if (c == NULL) {
            LOG_FATAL("failed to add simple collection from '%s'", file);

            TRI_FreeString(TRI_CORE_MEM_ZONE, file);
            regfree(&re);
            TRI_DestroyVectorString(&files);

            return TRI_set_errno(TRI_ERROR_ARANGO_CORRUPTED_COLLECTION);
          }

          c->_status = TRI_VOC_COL_STATUS_UNLOADED;

          LOG_DEBUG("added simple collection from '%s'", file);
        }
        else {
          LOG_DEBUG("skipping collection of unknown type %d", (int) type);
        }
      }
    }
    else {
      LOG_DEBUG("ignoring non-directory '%s'", file);
    }

    TRI_FreeString(TRI_CORE_MEM_ZONE, file);
  }

  regfree(&re);

  TRI_DestroyVectorString(&files);
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief bears a new collection or returns an existing one by name
////////////////////////////////////////////////////////////////////////////////

static TRI_vocbase_col_t* BearCollectionVocBase (TRI_vocbase_t* vocbase, 
                                                 char const* name,
                                                 TRI_col_type_e type) {
  union { void const* v; TRI_vocbase_col_t* c; } found;
  TRI_vocbase_col_t* collection;
  TRI_col_parameter_t parameter;
  
  // check that the name does not contain any strange characters
  parameter._isSystem = false;
  if (! TRI_IsAllowedCollectionName(&parameter, name)) {
    TRI_set_errno(TRI_ERROR_ARANGO_ILLEGAL_NAME);

    return NULL;
  }


  TRI_WRITE_LOCK_COLLECTIONS_VOCBASE(vocbase);

  // .............................................................................
  // check if we have an existing name
  // .............................................................................

  found.v = TRI_LookupByKeyAssociativePointer(&vocbase->_collectionsByName, name);

  if (found.v != NULL) {
    TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
    return found.c;
  }

  // .............................................................................
  // create a new one
  // .............................................................................

  // create a new collection
  collection = AddCollection(vocbase, type, name, TRI_NewTickVocBase(), NULL);

  if (collection == NULL) {
    TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
    return NULL;
  }

  collection->_status = TRI_VOC_COL_STATUS_NEW_BORN;

  TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
  return collection;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief manifests a new born (document) collection
////////////////////////////////////////////////////////////////////////////////

static int ManifestCollectionVocBase (TRI_vocbase_t* vocbase, TRI_vocbase_col_t* collection) {
  TRI_col_type_e type;

  TRI_WRITE_LOCK_STATUS_VOCBASE_COL(collection);

  // cannot manifest a corrupted collection
  if (collection->_status == TRI_VOC_COL_STATUS_CORRUPTED) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_set_errno(TRI_ERROR_ARANGO_CORRUPTED_COLLECTION);
  }

  // cannot manifest a deleted collection
  if (collection->_status == TRI_VOC_COL_STATUS_DELETED) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_set_errno(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
  }

  // loaded, unloaded, or unloading are manifested
  if (collection->_status == TRI_VOC_COL_STATUS_UNLOADED) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_ERROR_NO_ERROR;
  }

  if (collection->_status == TRI_VOC_COL_STATUS_LOADED) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_ERROR_NO_ERROR;
  }

  if (collection->_status == TRI_VOC_COL_STATUS_UNLOADING) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_ERROR_NO_ERROR;
  }

  if (collection->_status != TRI_VOC_COL_STATUS_NEW_BORN) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }

  // .............................................................................
  // manifest the collection
  // .............................................................................

  type = (TRI_col_type_e) collection->_type;

  if (TRI_IS_DOCUMENT_COLLECTION(type)) {
    TRI_document_collection_t* sim;
    TRI_col_parameter_t parameter;

    TRI_InitParameterCollection(vocbase, &parameter, collection->_name, type, vocbase->_defaultMaximalSize);

    parameter._type = (TRI_col_type_t) type;

    sim = TRI_CreateDocumentCollection(vocbase, vocbase->_path, &parameter, collection->_cid);

    if (sim == NULL) {
      collection->_status = TRI_VOC_COL_STATUS_CORRUPTED;

      TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
      return TRI_errno();
    }

    collection->_status = TRI_VOC_COL_STATUS_LOADED;
    collection->_collection = &sim->base;
    FreeCollectionPath(collection);
    collection->_path = TRI_DuplicateString(sim->base.base._directory);

    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_ERROR_NO_ERROR;
  }
  else {
    collection->_status = TRI_VOC_COL_STATUS_CORRUPTED;

    LOG_ERROR("unknown collection type '%d' in collection '%s'", (int) type, collection->_name);

    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_set_errno(TRI_ERROR_ARANGO_UNKNOWN_COLLECTION_TYPE);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief finds a collection by name or creates it
////////////////////////////////////////////////////////////////////////////////

static TRI_vocbase_col_t* FindCollectionByNameVocBase (TRI_vocbase_t* vocbase, 
                                                       char const* name, 
                                                       bool bear, 
                                                       TRI_col_type_e type) {
  union { void const* v; TRI_vocbase_col_t* c; } found;

  TRI_READ_LOCK_COLLECTIONS_VOCBASE(vocbase);
  found.v = TRI_LookupByKeyAssociativePointer(&vocbase->_collectionsByName, name);
  TRI_READ_UNLOCK_COLLECTIONS_VOCBASE(vocbase);

  if (found.v != NULL) {
    return found.c;
  }

  if (! bear) {
    TRI_set_errno(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
    return NULL;
  }

  return BearCollectionVocBase(vocbase, name, type);
}


////////////////////////////////////////////////////////////////////////////////
/// @brief loads an existing (document) collection
///
/// Note that this will READ lock the collection you have to release the
/// collection lock by yourself.
////////////////////////////////////////////////////////////////////////////////

static int LoadCollectionVocBase (TRI_vocbase_t* vocbase, TRI_vocbase_col_t* collection) {
  TRI_col_type_e type;
  int res;

  // .............................................................................
  // read lock
  // .............................................................................

  // check if the collection is already loaded
  TRI_READ_LOCK_STATUS_VOCBASE_COL(collection);

  if (collection->_status == TRI_VOC_COL_STATUS_LOADED) {

    // DO NOT release the lock
    return TRI_ERROR_NO_ERROR;
  }

  if (collection->_status == TRI_VOC_COL_STATUS_DELETED) {
    TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_set_errno(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
  }

  if (collection->_status == TRI_VOC_COL_STATUS_CORRUPTED) {
    TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_set_errno(TRI_ERROR_ARANGO_CORRUPTED_COLLECTION);
  }

  // release the read lock and acquire a write lock, we have to do some work
  TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);

  // .............................................................................
  // write lock
  // .............................................................................

  TRI_WRITE_LOCK_STATUS_VOCBASE_COL(collection);

  // someone else loaded the collection, release the WRITE lock and try again
  if (collection->_status == TRI_VOC_COL_STATUS_LOADED) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

    return LoadCollectionVocBase(vocbase, collection);
  }

  // someone is trying to unload the collection, cancel this,
  // release the WRITE lock and try again
  if (collection->_status == TRI_VOC_COL_STATUS_UNLOADING) {
    // check if there is a deferred drop action going on for this collection
    if (TRI_ContainsBarrierList(&collection->_collection->_barrierList, TRI_BARRIER_COLLECTION_DROP_CALLBACK)) {
      // drop call going on, we must abort
      TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

      return TRI_set_errno(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
    }

    // no drop action found, go on
    collection->_status = TRI_VOC_COL_STATUS_LOADED;

    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    
    return LoadCollectionVocBase(vocbase, collection);
  }

  // deleted, give up
  if (collection->_status == TRI_VOC_COL_STATUS_DELETED) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_set_errno(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
  }

  // corrupted, give up
  if (collection->_status == TRI_VOC_COL_STATUS_CORRUPTED) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_set_errno(TRI_ERROR_ARANGO_CORRUPTED_COLLECTION);
  }

  // new born, manifest collection, release the WRITE lock and try again
  if (collection->_status == TRI_VOC_COL_STATUS_NEW_BORN) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

    res = ManifestCollectionVocBase(vocbase, collection);

    if (res != TRI_ERROR_NO_ERROR) {
      return res;
    }

    return LoadCollectionVocBase(vocbase, collection);
  }

  // unloaded, load collection
  if (collection->_status == TRI_VOC_COL_STATUS_UNLOADED) {
    type = (TRI_col_type_e) collection->_type;

    if (TRI_IS_DOCUMENT_COLLECTION(type)) {
      TRI_document_collection_t* sim;

      sim = TRI_OpenDocumentCollection(vocbase, collection->_path);

      if (sim == NULL) {
        collection->_status = TRI_VOC_COL_STATUS_CORRUPTED;

        TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
        return TRI_set_errno(TRI_ERROR_ARANGO_CORRUPTED_COLLECTION);
      }

      collection->_collection = &sim->base;
      collection->_status = TRI_VOC_COL_STATUS_LOADED;
      FreeCollectionPath(collection);
      collection->_path = TRI_DuplicateString(sim->base.base._directory);

      // release the WRITE lock and try again
      TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    
      return LoadCollectionVocBase(vocbase, collection);
    }
    else {
      LOG_ERROR("unknown collection type %d for '%s'", (int) type, collection->_name);

      TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
      return TRI_set_errno(TRI_ERROR_ARANGO_UNKNOWN_COLLECTION_TYPE);
    }
  }

  LOG_ERROR("unknown collection status %d for '%s'", (int) collection->_status, collection->_name);

  TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
  return TRI_set_errno(TRI_ERROR_INTERNAL);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief page size
////////////////////////////////////////////////////////////////////////////////

size_t PageSize;

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
/// @brief checks if a collection name is allowed
///
/// Returns true if the name is allowed and false otherwise
////////////////////////////////////////////////////////////////////////////////

bool TRI_IsAllowedCollectionName (TRI_col_parameter_t* paramater, char const* name) {
  bool ok;
  char const* ptr;
  size_t length = 0;

  for (ptr = name;  *ptr;  ++ptr) {
    if (name < ptr || paramater->_isSystem) {
      ok = (*ptr == '_') || (*ptr == '-') || ('0' <= *ptr && *ptr <= '9') || ('a' <= *ptr && *ptr <= 'z') || ('A' <= *ptr && *ptr <= 'Z');
    }
    else {
      ok = ('a' <= *ptr && *ptr <= 'z') || ('A' <= *ptr && *ptr <= 'Z');
    }

    if (! ok) {
      return false;
    }

    ++length;
  }

  if (length == 0 || length > 64) {
    // invalid name length
    return false;
  } 

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create a new tick
////////////////////////////////////////////////////////////////////////////////

TRI_voc_tick_t TRI_NewTickVocBase () {
  uint64_t tick = ServerIdentifier;

  TRI_LockSpin(&TickLock);

  tick |= (++CurrentTick) << 16;

  TRI_UnlockSpin(&TickLock);

  return tick;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief updates the tick counter
////////////////////////////////////////////////////////////////////////////////

void TRI_UpdateTickVocBase (TRI_voc_tick_t tick) {
  TRI_voc_tick_t s = tick >> 16;

  TRI_LockSpin(&TickLock);

  if (CurrentTick < s) {
    CurrentTick = s;
  }

  TRI_UnlockSpin(&TickLock);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief msyncs a memory block between begin (incl) and end (excl)
////////////////////////////////////////////////////////////////////////////////

bool TRI_msync (int fd, void* mmHandle, char const* begin, char const* end) {
  uintptr_t p = (intptr_t) begin;
  uintptr_t q = (intptr_t) end;
  uintptr_t g = (intptr_t) PageSize;

  char* b = (char*)( (p / g) * g );
  char* e = (char*)( ((q + g - 1) / g) * g ); 
  int result;
  
  result = TRI_FlushMMFile(fd, &mmHandle, b, e - b, MS_SYNC);

  if (result != TRI_ERROR_NO_ERROR) {
    TRI_set_errno(result);
    return false;
  }
  return true;  
}

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
/// @brief opens an exiting database, scans all collections
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_t* TRI_OpenVocBase (char const* path) {
  TRI_vocbase_t* vocbase;
  char* lockFile;
  int res;

  if (! TRI_IsDirectory(path)) {
    LOG_ERROR("database path '%s' is not a directory", path);

    TRI_set_errno(TRI_ERROR_ARANGO_WRONG_VOCBASE_PATH);
    return NULL;
  }

  // .............................................................................
  // check that the database is not locked and lock it
  // .............................................................................

  lockFile = TRI_Concatenate2File(path, "lock");
  res = TRI_VerifyLockFile(lockFile);

  if (res == TRI_ERROR_NO_ERROR) {
    LOG_FATAL("database is locked, please check the lock file '%s'", lockFile);

    TRI_FreeString(TRI_CORE_MEM_ZONE, lockFile);

    TRI_set_errno(TRI_ERROR_ARANGO_DATABASE_LOCKED);
    return NULL;
  }

  if (TRI_ExistsFile(lockFile)) {
    TRI_UnlinkFile(lockFile);
  }

  res = TRI_CreateLockFile(lockFile);

  if (res != TRI_ERROR_NO_ERROR) {
    LOG_FATAL("cannot lock the database, please check the lock file '%s': %s", lockFile, TRI_last_error());

    TRI_FreeString(TRI_CORE_MEM_ZONE, lockFile);
    return NULL;
  }

  // .............................................................................
  // setup vocbase structure
  // .............................................................................

  vocbase = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_vocbase_t), false);

  vocbase->_cursors = TRI_CreateShadowsGeneralCursor();

  // init AQL functions
  vocbase->_functions = TRI_InitialiseFunctionsAql();
  vocbase->_lockFile = lockFile;
  vocbase->_path = TRI_DuplicateString(path);

  TRI_InitVectorPointer(&vocbase->_collections, TRI_UNKNOWN_MEM_ZONE);
  TRI_InitVectorPointer(&vocbase->_deadCollections, TRI_UNKNOWN_MEM_ZONE);

  TRI_InitAssociativePointer(&vocbase->_collectionsById,
                             TRI_UNKNOWN_MEM_ZONE, 
                             HashKeyCid,
                             HashElementCid,
                             EqualKeyCid,
                             NULL);

  TRI_InitAssociativePointer(&vocbase->_collectionsByName,
                             TRI_UNKNOWN_MEM_ZONE, 
                             HashKeyCollectionName,
                             HashElementCollectionName,
                             EqualKeyCollectionName,
                             NULL);

  TRI_InitAssociativePointer(&vocbase->_authInfo,
                             TRI_CORE_MEM_ZONE, 
                             HashKeyAuthInfo,
                             HashElementAuthInfo,
                             EqualKeyAuthInfo,
                             NULL);

  TRI_InitReadWriteLock(&vocbase->_authInfoLock);
  TRI_InitReadWriteLock(&vocbase->_lock);

  vocbase->_syncWaiters = 0;
  TRI_InitCondition(&vocbase->_syncWaitersCondition);
  
  TRI_InitCondition(&vocbase->_cleanupCondition);

  // .............................................................................
  // scan directory for collections
  // .............................................................................

  // defaults
  vocbase->_removeOnDrop = true;
  vocbase->_removeOnCompacted = true;
  vocbase->_defaultWaitForSync = false;    // default sync behavior for new collections
  vocbase->_defaultMaximalSize = TRI_JOURNAL_DEFAULT_MAXIMAL_SIZE;
  vocbase->_forceSyncShapes = true; // force sync of shape data to disk

  // scan the database path for collections
  res = ScanPath(vocbase, vocbase->_path);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_DestroyAssociativePointer(&vocbase->_collectionsByName);
    TRI_DestroyAssociativePointer(&vocbase->_collectionsById);
    TRI_DestroyVectorPointer(&vocbase->_collections);
    TRI_DestroyVectorPointer(&vocbase->_deadCollections);
    TRI_DestroyLockFile(vocbase->_lockFile);
    TRI_FreeString(TRI_CORE_MEM_ZONE, vocbase->_lockFile);
    TRI_FreeShadowStore(vocbase->_cursors);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, vocbase);
    TRI_DestroyReadWriteLock(&vocbase->_authInfoLock);
    TRI_DestroyReadWriteLock(&vocbase->_lock);

    return NULL;
  }
  

  // .............................................................................
  // vocbase is now active
  // .............................................................................

  vocbase->_state = 1;

  // .............................................................................
  // start helper threads
  // .............................................................................

  // start synchroniser thread
  TRI_InitThread(&vocbase->_synchroniser);
  TRI_StartThread(&vocbase->_synchroniser, "[synchroniser]", TRI_SynchroniserVocBase, vocbase);

  // start compactor thread
  TRI_InitThread(&vocbase->_compactor);
  TRI_StartThread(&vocbase->_compactor, "[compactor]", TRI_CompactorVocBase, vocbase);
  
  // start cleanup thread
  TRI_InitThread(&vocbase->_cleanup);
  TRI_StartThread(&vocbase->_cleanup, "[cleanup]", TRI_CleanupVocBase, vocbase);

  // .............................................................................
  // load auth information
  // .............................................................................

  TRI_LoadAuthInfo(vocbase);
  TRI_DefaultAuthInfo(vocbase);

  // we are done
  return vocbase;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief closes a database and all collections
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyVocBase (TRI_vocbase_t* vocbase) {
  size_t i;
  
  // starts unloading of collections
  for (i = 0;  i < vocbase->_collections._length;  ++i) {
    TRI_vocbase_col_t* collection;

    collection = (TRI_vocbase_col_t*) vocbase->_collections._buffer[i];
    TRI_UnloadCollectionVocBase(vocbase, collection);
  }
  
  // this will signal the synchroniser and the compactor threads to do one last iteration
  vocbase->_state = 2;

  // wait until synchroniser and compactor are finished
  TRI_JoinThread(&vocbase->_synchroniser);
  TRI_JoinThread(&vocbase->_compactor);
  
  // this will signal the cleanup thread to do one last iteration
  vocbase->_state = 3;
  TRI_JoinThread(&vocbase->_cleanup);

  // free dead collections (already dropped but pointers still around)
  for (i = 0;  i < vocbase->_deadCollections._length;  ++i) {
    TRI_vocbase_col_t* collection;

    collection = (TRI_vocbase_col_t*) vocbase->_deadCollections._buffer[i];
    FreeCollection(vocbase, collection);
  }
  
  // free collections
  for (i = 0;  i < vocbase->_collections._length;  ++i) {
    TRI_vocbase_col_t* collection;

    collection = (TRI_vocbase_col_t*) vocbase->_collections._buffer[i];
    FreeCollection(vocbase, collection);
  }
  
  // free the auth info
  TRI_DestroyAuthInfo(vocbase);

  // clear the hashes and vectors
  TRI_DestroyAssociativePointer(&vocbase->_collectionsByName);
  TRI_DestroyAssociativePointer(&vocbase->_collectionsById);
  TRI_DestroyAssociativePointer(&vocbase->_authInfo);

  TRI_DestroyVectorPointer(&vocbase->_collections);
  TRI_DestroyVectorPointer(&vocbase->_deadCollections);

  // free AQL functions
  TRI_FreeFunctionsAql(vocbase->_functions);
  
  // free the cursors
  TRI_FreeShadowStore(vocbase->_cursors);

  // release lock on database
  TRI_DestroyLockFile(vocbase->_lockFile);
  TRI_FreeString(TRI_CORE_MEM_ZONE, vocbase->_lockFile);

  // destroy locks
  TRI_DestroyReadWriteLock(&vocbase->_authInfoLock);
  TRI_DestroyReadWriteLock(&vocbase->_lock);
  TRI_DestroyCondition(&vocbase->_syncWaitersCondition);
  TRI_DestroyCondition(&vocbase->_cleanupCondition);

  // free the filename path
  TRI_Free(TRI_CORE_MEM_ZONE, vocbase->_path);

}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns all known (document) collections
////////////////////////////////////////////////////////////////////////////////

TRI_vector_pointer_t TRI_CollectionsVocBase (TRI_vocbase_t* vocbase) {
  TRI_vector_pointer_t result;
  TRI_vocbase_col_t* found;
  size_t i;

  TRI_InitVectorPointer(&result, TRI_UNKNOWN_MEM_ZONE);

  TRI_READ_LOCK_COLLECTIONS_VOCBASE(vocbase);

  for (i = 0;  i < vocbase->_collectionsById._nrAlloc;  ++i) {
    found = vocbase->_collectionsById._table[i];

    if (found != NULL) {
      TRI_PushBackVectorPointer(&result, found);
    }
  }

  TRI_READ_UNLOCK_COLLECTIONS_VOCBASE(vocbase);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a (document) collection by name
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* TRI_LookupCollectionByNameVocBase (TRI_vocbase_t* vocbase, char const* name) {
  union { void const* v; TRI_vocbase_col_t* c; } found;

  TRI_READ_LOCK_COLLECTIONS_VOCBASE(vocbase);
  found.v = TRI_LookupByKeyAssociativePointer(&vocbase->_collectionsByName, name);
  TRI_READ_UNLOCK_COLLECTIONS_VOCBASE(vocbase);

  return found.c;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a (document) collection by identifier
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* TRI_LookupCollectionByIdVocBase (TRI_vocbase_t* vocbase, TRI_voc_cid_t id) {
  union { void const* v; TRI_vocbase_col_t* c; } found;

  TRI_READ_LOCK_COLLECTIONS_VOCBASE(vocbase);
  found.v = TRI_LookupByKeyAssociativePointer(&vocbase->_collectionsById, &id);
  TRI_READ_UNLOCK_COLLECTIONS_VOCBASE(vocbase);

  return found.c;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief finds a collection by name
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* TRI_FindCollectionByNameVocBase (TRI_vocbase_t* vocbase, char const* name, bool bear) {
  return TRI_FindDocumentCollectionByNameVocBase(vocbase, name, bear);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief finds a primary collection by name
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* TRI_FindDocumentCollectionByNameVocBase (TRI_vocbase_t* vocbase, char const* name, bool bear) {
  return FindCollectionByNameVocBase(vocbase, name, bear, TRI_COL_TYPE_DOCUMENT);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief finds an edge collection by name
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* TRI_FindEdgeCollectionByNameVocBase (TRI_vocbase_t* vocbase, char const* name, bool bear) {
  return FindCollectionByNameVocBase(vocbase, name, bear, TRI_COL_TYPE_EDGE);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new (document) collection from parameter set
///
/// collection id (cid) is normally passed with a value of 0
/// this means that the system will assign a new collection id automatically
/// using a cid of > 0 is supported to import dumps from other servers etc.
/// but the functionality is not advertised
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* TRI_CreateCollectionVocBase (TRI_vocbase_t* vocbase, 
                                                TRI_col_parameter_t* parameter,
                                                TRI_voc_cid_t cid) {
  TRI_primary_collection_t* primary = NULL;
  TRI_vocbase_col_t* collection;
  TRI_document_collection_t* sim;
  TRI_col_type_e type;
  char const* name;
  void const* found;
  
  assert(parameter);
  name = parameter->_name;


  // check that the name does not contain any strange characters
  if (! TRI_IsAllowedCollectionName(parameter, name)) {
    TRI_set_errno(TRI_ERROR_ARANGO_ILLEGAL_NAME);

    return NULL;
  }
  
  type = (TRI_col_type_e) parameter->_type;

  if (! TRI_IS_DOCUMENT_COLLECTION(type)) {
    LOG_ERROR("unknown collection type: %d", (int) parameter->_type);

    TRI_set_errno(TRI_ERROR_ARANGO_UNKNOWN_COLLECTION_TYPE);
    return NULL;
  }

  TRI_WRITE_LOCK_COLLECTIONS_VOCBASE(vocbase);

  // .............................................................................
  // check that we have a new name
  // .............................................................................

  found = TRI_LookupByKeyAssociativePointer(&vocbase->_collectionsByName, name);

  if (found != NULL) {
    TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);

    LOG_DEBUG("collection named '%s' already exists", name);

    TRI_set_errno(TRI_ERROR_ARANGO_DUPLICATE_NAME);
    return NULL;
  }

  // .............................................................................
  // ok, construct the collection
  // .............................................................................

  sim = TRI_CreateDocumentCollection(vocbase, vocbase->_path, parameter, cid);

  if (sim == NULL) {
    TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
    return NULL;
  }

  primary = &sim->base;


  // add collection container -- however note that the compactor is added later which could fail!
  collection = AddCollection(vocbase,
                             primary->base._type,
                             primary->base._name,
                             primary->base._cid,
                             primary->base._directory);


  if (collection == NULL) {
    if (TRI_IS_DOCUMENT_COLLECTION(type)) {
      TRI_CloseDocumentCollection((TRI_document_collection_t*) primary);
      TRI_FreeDocumentCollection((TRI_document_collection_t*) primary);
    }

    TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
    return NULL;
  }


  collection->_status = TRI_VOC_COL_STATUS_LOADED;
  collection->_collection = primary;
  FreeCollectionPath(collection);
  collection->_path = TRI_DuplicateString(primary->base._directory);


  TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
  return collection;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief unloads a (document) collection
////////////////////////////////////////////////////////////////////////////////

int TRI_UnloadCollectionVocBase (TRI_vocbase_t* vocbase, TRI_vocbase_col_t* collection) {
  TRI_WRITE_LOCK_STATUS_VOCBASE_COL(collection);

  // cannot unload a corrupted collection
  if (collection->_status == TRI_VOC_COL_STATUS_CORRUPTED) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_set_errno(TRI_ERROR_ARANGO_CORRUPTED_COLLECTION);
  }

  // a unloaded collection is unloaded
  if (collection->_status == TRI_VOC_COL_STATUS_UNLOADED) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_ERROR_NO_ERROR;
  }

  // a unloading collection is treated as unloaded
  if (collection->_status == TRI_VOC_COL_STATUS_UNLOADING) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_ERROR_NO_ERROR;
  }

  // a new born collection is treated as unloaded
  if (collection->_status == TRI_VOC_COL_STATUS_NEW_BORN) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_ERROR_NO_ERROR;
  }

  // a deleted collection is treated as unloaded
  if (collection->_status == TRI_VOC_COL_STATUS_DELETED) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_ERROR_NO_ERROR;
  }

  // must be loaded
  if (collection->_status != TRI_VOC_COL_STATUS_LOADED) {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }

  // mark collection as unloading
  collection->_status = TRI_VOC_COL_STATUS_UNLOADING;

  // added callback for unload
  TRI_CreateBarrierUnloadCollection(&collection->_collection->_barrierList,
                                    &collection->_collection->base,
                                    UnloadCollectionCallback,
                                    collection);

  // release locks
  TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
  
  // wake up the cleanup thread
  TRI_LockCondition(&vocbase->_cleanupCondition);
  TRI_SignalCondition(&vocbase->_cleanupCondition);
  TRI_UnlockCondition(&vocbase->_cleanupCondition);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief drops a (document) collection
////////////////////////////////////////////////////////////////////////////////

int TRI_DropCollectionVocBase (TRI_vocbase_t* vocbase, TRI_vocbase_col_t* collection) {
  int res;

  // mark collection as deleted
  TRI_WRITE_LOCK_STATUS_VOCBASE_COL(collection);

  // .............................................................................
  // collection already deleted
  // .............................................................................

  if (collection->_status == TRI_VOC_COL_STATUS_DELETED) {
    UnregisterCollection(vocbase, collection);

    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_ERROR_NO_ERROR;
  }

  // .............................................................................
  // new born collection, no datafile/parameter file exists
  // .............................................................................

  else if (collection->_status == TRI_VOC_COL_STATUS_NEW_BORN) {
    collection->_status = TRI_VOC_COL_STATUS_DELETED;

    UnregisterCollection(vocbase, collection);

    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    return TRI_ERROR_NO_ERROR;
  }

  // .............................................................................
  // collection is unloaded
  // .............................................................................

  else if (collection->_status == TRI_VOC_COL_STATUS_UNLOADED) {
    TRI_col_info_t info;
    char* tmpFile;

    res = TRI_LoadParameterInfoCollection(collection->_path, &info);

    if (res != TRI_ERROR_NO_ERROR) {
      TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
      return TRI_set_errno(res);
    }

    // remove dangling .json.tmp file if it exists
    tmpFile = TRI_Concatenate4String(collection->_path, "/", TRI_COL_PARAMETER_FILE, ".tmp");
    if (tmpFile != NULL) {
      if (TRI_ExistsFile(tmpFile)) {
        TRI_UnlinkFile(tmpFile);
        LOG_WARNING("removing dangling temporary file '%s'", tmpFile); 
      }
      TRI_FreeString(TRI_CORE_MEM_ZONE, tmpFile);
    }

    if (! info._deleted) {
      info._deleted = true;

      res = TRI_SaveParameterInfoCollection(collection->_path, &info);

      if (res != TRI_ERROR_NO_ERROR) {
        TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
        return TRI_set_errno(res);
      }
    }

    collection->_status = TRI_VOC_COL_STATUS_DELETED;
    
    UnregisterCollection(vocbase, collection);

    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

    DropCollectionCallback(0, collection);

    return TRI_ERROR_NO_ERROR;
  }

  // .............................................................................
  // collection is loaded
  // .............................................................................

  else if (collection->_status == TRI_VOC_COL_STATUS_LOADED || collection->_status == TRI_VOC_COL_STATUS_UNLOADING) {
    collection->_collection->base._deleted = true;

    res = TRI_UpdateParameterInfoCollection(vocbase, &collection->_collection->base, 0);

    if (res != TRI_ERROR_NO_ERROR) {
      TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
      return res;
    }

    collection->_status = TRI_VOC_COL_STATUS_DELETED;
    UnregisterCollection(vocbase, collection);

    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

    // added callback for dropping
    TRI_CreateBarrierDropCollection(&collection->_collection->_barrierList,
                                    &collection->_collection->base,
                                    DropCollectionCallback,
                                    collection);

    // wake up the cleanup thread
    TRI_LockCondition(&vocbase->_cleanupCondition);
    TRI_SignalCondition(&vocbase->_cleanupCondition);
    TRI_UnlockCondition(&vocbase->_cleanupCondition);

    return TRI_ERROR_NO_ERROR;
  }

  // .............................................................................
  // upps, unknown status
  // .............................................................................

  else {
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);
    LOG_WARNING("internal error in TRI_DropCollectionVocBase");

    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief renames a (document) collection
////////////////////////////////////////////////////////////////////////////////

int TRI_RenameCollectionVocBase (TRI_vocbase_t* vocbase, TRI_vocbase_col_t* collection, char const* newName) {
  union { TRI_vocbase_col_t* v; TRI_vocbase_col_t const* c; } cnv;
  TRI_col_info_t info;
  TRI_col_parameter_t parameter;
  void const* found;
  char const* oldName;
  int res;

  // old name should be different
  oldName = collection->_name;

  if (TRI_EqualString(oldName, newName)) {
    return TRI_ERROR_NO_ERROR;
  }

  parameter._isSystem = (*oldName == '_');
  if (! TRI_IsAllowedCollectionName(&parameter, newName)) {
    return TRI_set_errno(TRI_ERROR_ARANGO_ILLEGAL_NAME);
  }

  // lock collection because we are going to change the name
  TRI_WRITE_LOCK_STATUS_VOCBASE_COL(collection);

  // the must be done after the collection lock
  TRI_WRITE_LOCK_COLLECTIONS_VOCBASE(vocbase);

  // cannot rename a corrupted collection
  if (collection->_status == TRI_VOC_COL_STATUS_CORRUPTED) {
    TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

    return TRI_set_errno(TRI_ERROR_ARANGO_CORRUPTED_COLLECTION);
  }

  // cannot rename a deleted collection
  if (collection->_status == TRI_VOC_COL_STATUS_DELETED) {
    TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

    return TRI_set_errno(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
  }

  // check if the new name is unused
  found = (void*) TRI_LookupByKeyAssociativePointer(&vocbase->_collectionsByName, newName);

  if (found != NULL) {
    TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

    return TRI_set_errno(TRI_ERROR_ARANGO_DUPLICATE_NAME);
  }

  // .............................................................................
  // new born collection, no datafile/parameter file exists
  // .............................................................................

  if (collection->_status == TRI_VOC_COL_STATUS_NEW_BORN) {
    // do nothing
  }

  // .............................................................................
  // collection is unloaded
  // .............................................................................

  else if (collection->_status == TRI_VOC_COL_STATUS_UNLOADED) {
    res = TRI_LoadParameterInfoCollection(collection->_path, &info);

    if (res != TRI_ERROR_NO_ERROR) {
      TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
      TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

      return TRI_set_errno(res);
    }

    TRI_CopyString(info._name, newName, sizeof(info._name));

    res = TRI_SaveParameterInfoCollection(collection->_path, &info);

    if (res != TRI_ERROR_NO_ERROR) {
      TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
      TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

      return TRI_set_errno(res);
    }
  }

  // .............................................................................
  // collection is loaded
  // .............................................................................

  else if (collection->_status == TRI_VOC_COL_STATUS_LOADED || collection->_status == TRI_VOC_COL_STATUS_UNLOADING) {
    res = TRI_RenameCollection(&collection->_collection->base, newName);

    if (res != TRI_ERROR_NO_ERROR) {
      TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
      TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

      return TRI_set_errno(res);
    }
  }

  // .............................................................................
  // upps, unknown status
  // .............................................................................

  else {
    TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
    TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }
  
  // .............................................................................
  // rename and release locks
  // .............................................................................

  TRI_RemoveKeyAssociativePointer(&vocbase->_collectionsByName, oldName);
  TRI_CopyString(collection->_name, newName, sizeof(collection->_name));

  cnv.c = collection;
  TRI_InsertKeyAssociativePointer(&vocbase->_collectionsByName, newName, cnv.v, false);

  TRI_WRITE_UNLOCK_COLLECTIONS_VOCBASE(vocbase);
  TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief locks a (document) collection for usage, loading or manifesting it
////////////////////////////////////////////////////////////////////////////////

int TRI_UseCollectionVocBase (TRI_vocbase_t* vocbase, TRI_vocbase_col_t* collection) {
  return LoadCollectionVocBase(vocbase, collection);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief locks a (document) collection for usage by name
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* TRI_UseCollectionByNameVocBase (TRI_vocbase_t* vocbase, char const* name) {
  union { TRI_vocbase_col_t const* c; TRI_vocbase_col_t* v; } cnv;
  TRI_vocbase_col_t const* collection;
  int res;

  // .............................................................................
  // check that we have an existing name
  // .............................................................................

  TRI_READ_LOCK_COLLECTIONS_VOCBASE(vocbase);
  collection = TRI_LookupByKeyAssociativePointer(&vocbase->_collectionsByName, name);
  TRI_READ_UNLOCK_COLLECTIONS_VOCBASE(vocbase);

  if (collection == NULL) {
    LOG_DEBUG("unknown collection '%s'", name);

    TRI_set_errno(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
    return NULL;
  }

  // .............................................................................
  // try to load the collection
  // .............................................................................

  cnv.c = collection;
  res = LoadCollectionVocBase(vocbase, cnv.v);

  return res == TRI_ERROR_NO_ERROR ? cnv.v : NULL;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief releases a (document) collection from usage
////////////////////////////////////////////////////////////////////////////////

void TRI_ReleaseCollectionVocBase (TRI_vocbase_t* vocbase, TRI_vocbase_col_t* collection) {
  TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);
}

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

void TRI_InitialiseVocBase () {
  TRI_InitialiseHashes();
  TRI_InitialiseRandom();
  TRI_GlobalInitStatementListAql();

  ServerIdentifier = TRI_UInt16Random();
  PageSize = getpagesize();

  TRI_InitSpin(&TickLock);

#ifdef TRI_READLINE_VERSION
  LOG_TRACE("%s", "$Revision: READLINE " TRI_READLINE_VERSION " $");
#endif

#ifdef TRI_V8_VERSION
  LOG_TRACE("%s", "$Revision: V8 " TRI_V8_VERSION " $");
#endif
  
#ifdef TRI_ICU_VERSION
  LOG_TRACE("%s", "$Revision: ICU " TRI_ICU_VERSION " $");
#endif

  LOG_TRACE("sizeof df_header:        %d", (int) sizeof(TRI_df_marker_t));
  LOG_TRACE("sizeof df_header_marker: %d", (int) sizeof(TRI_df_header_marker_t));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief shut downs the voc database components
////////////////////////////////////////////////////////////////////////////////

void TRI_ShutdownVocBase () {
  TRI_DestroySpin(&TickLock);

  TRI_ShutdownRandom();
  TRI_ShutdownHashes();
  TRI_GlobalFreeStatementListAql();
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:

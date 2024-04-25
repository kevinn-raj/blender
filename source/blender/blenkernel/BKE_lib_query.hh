/* SPDX-FileCopyrightText: 2014 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

/** \file
 * \ingroup bke
 *
 * API to perform operations over all ID pointers used by a given data-block.
 *
 * \note `BKE_lib_` files are for operations over data-blocks themselves, although they might
 * alter Main as well (when creating/renaming/deleting an ID e.g.).
 *
 * \section Function Names
 *
 * \warning Descriptions below is ideal goal, current status of naming does not yet fully follow it
 * (this is WIP).
 *
 * - `BKE_lib_query_` should be used for functions in that file.
 */

#include "DNA_ID.h"

#include "BLI_function_ref.hh"
#include "BLI_sys_types.h"

#include <array>

struct IDTypeInfo;
struct LibraryForeachIDData;
struct Main;

/* Tips for the callback for cases it's gonna to modify the pointer. */
enum {
  IDWALK_CB_NOP = 0,
  IDWALK_CB_NEVER_NULL = (1 << 0),
  IDWALK_CB_NEVER_SELF = (1 << 1),

  /**
   * Indicates whether this is direct (i.e. by local data) or indirect (i.e. by linked data) usage.
   */
  IDWALK_CB_INDIRECT_USAGE = (1 << 2),
  /**
   * Indicates that this is a direct weak link usage, i.e. if the user is a local ID, and is using
   * (pointing to) a linked ID, that usage does not make the linked ID directly linked.
   *
   * E.g. usages of linked collections or objects by ViewLayerCollections or Bases in scenes.
   *
   * See also #LIB_INDIRECT_WEAK_LINK in DNA_ID.h
   */
  IDWALK_CB_DIRECT_WEAK_LINK = (1 << 3),

  /**
   * That ID is used as mere sub-data by its owner (only case currently: those root node-trees in
   * materials etc., and the Scene's master collections).
   * This means callback shall not *do* anything, only use this as informative data if it needs it.
   */
  IDWALK_CB_EMBEDDED = (1 << 4),
  /**
   * That ID pointer points to an embedded ID, but does not own it.
   *
   * E.g the `collection` pointer of the first ViewLayerCollection of a ViewLayer should always
   * point to the scene's master collection, which is an embedded ID 'owned' by
   * `Scene.master_collection`.
   */
  IDWALK_CB_EMBEDDED_NOT_OWNING = (1 << 5),

  /**
   * That ID is not really used by its owner, it's just an internal hint/helper.
   * This marks the 'from' pointers issue, like Key->from.
   * How to handle that kind of cases totally depends on what caller code is doing... */
  IDWALK_CB_LOOPBACK = (1 << 6),

  /**
   * Indicates that this is an internal runtime ID pointer, like e.g. `ID.newid` or `ID.original`.
   * \note Those should be ignored in most cases, and won't be processed/generated anyway unless
   * `IDWALK_DO_INTERNAL_RUNTIME_POINTERS` option is enabled.
   */
  IDWALK_CB_INTERNAL = (1 << 9),

  /**
   * This ID usage should not be processed during readfile (neither during lib-linking nor
   * expanding).
   *
   * Note that all embedded IDs pointers (#IDWALK_CB_EMBEDDED and #IDWALK_CB_EMBEDDED_NOT_OWNING)
   * cases are also ignored during readfile.
   *
   * Mainly used for some 'loopback' pointers like the 'owner_id' of the embedded IDs.
   */
  IDWALK_CB_READFILE_IGNORE = (1 << 10),

  /**
   * This ID usage is fully refcounted.
   * Callback is responsible to deal accordingly with #ID.us if needed.
   */
  IDWALK_CB_USER = (1 << 11),
  /**
   * This ID usage is not refcounted, but at least one user should be generated by it (to avoid
   * e.g. losing the used ID on save/reload).
   * Callback is responsible to deal accordingly with #ID.us if needed.
   */
  IDWALK_CB_USER_ONE = (1 << 12),

  /** This ID is used as library override's reference by its owner. */
  IDWALK_CB_OVERRIDE_LIBRARY_REFERENCE = (1 << 16),

  /** This ID pointer is not overridable. */
  IDWALK_CB_OVERRIDE_LIBRARY_NOT_OVERRIDABLE = (1 << 17),

  /** This ID pointer is expected to be overridden by default, in liboverride hierarchy context. */
  IDWALK_CB_OVERRIDE_LIBRARY_HIERARCHY_DEFAULT = (1 << 18),

};

enum {
  IDWALK_RET_NOP = 0,
  /** Completely stop iteration. */
  IDWALK_RET_STOP_ITER = 1 << 0,
  /** Stop recursion, that is, do not loop over ID used by current one. */
  IDWALK_RET_STOP_RECURSION = 1 << 1,
};

struct LibraryIDLinkCallbackData {
  void *user_data;
  /** Main database used to call `BKE_library_foreach_ID_link()`. */
  Main *bmain;
  /**
   * 'Real' ID, the one that might be in bmain, only differs from self_id when the later is an
   * embedded one.
   */
  ID *owner_id;
  /**
   * ID from which the current ID pointer is being processed. It may be an embedded ID like master
   * collection or root node tree.
   */
  ID *self_id;
  ID **id_pointer;
  int cb_flag;
};

/**
 * Call a callback for each ID link which the given ID uses.
 *
 * \return a set of flags to control further iteration (0 to keep going).
 */
using LibraryIDLinkCallback = int(LibraryIDLinkCallbackData *cb_data);

/* Flags for the foreach function itself. */
enum {
  IDWALK_NOP = 0,
  /**
   * The callback will never modify the ID pointers it processes.
   * WARNING: It is very important to pass this flag when valid, as it can lead to important
   * optimizations and debug/assert code.
   */
  IDWALK_READONLY = (1 << 0),
  /**
   * Recurse into 'descendant' IDs.
   * Each ID is only processed once. Order of ID processing is not guaranteed.
   *
   * Also implies IDWALK_READONLY, and excludes IDWALK_DO_INTERNAL_RUNTIME_POINTERS.
   *
   * NOTE: When enabled, embedded IDs are processed separately from their owner, as if they were
   * regular IDs. Owner ID is not available then in the #LibraryForeachIDData callback data.
   */
  IDWALK_RECURSE = (1 << 1),
  /** Include UI pointers (from WM and screens editors). */
  IDWALK_INCLUDE_UI = (1 << 2),
  /** Do not process ID pointers inside embedded IDs. Needed by depsgraph processing e.g. */
  IDWALK_IGNORE_EMBEDDED_ID = (1 << 3),

  /**
   * Do not access original processed pointer's data, only process its address value.
   *
   * This is required in cases where to current address may not be valid anymore (e.g. during
   * readfile process). A few ID pointers (like e.g. the `LayerCollection.collection` one) are by
   * default accessed to check things (e.g. whether they are pointing to an embedded ID or a
   * regular one).
   *
   * \note Access to owning embedded ID pointers (e.g. `Scene.master_collection`) is not affected
   * here, these are presumed always valid.
   *
   * \note This flag is mutually exclusive with `IDWALK_RECURSE`, since by definition accessing the
   * current ID pointer is required for recursion.
   *
   * \note After remapping, code may access the newly set ID pointer, which is always presumed
   * valid.
   *
   * \warning Use only with great caution, this flag will modify the handling of some ID pointers
   * (especially when it comes to detecting `IDWALK_CB_EMBEDDED_NOT_OWNING` usages).
   */
  IDWALK_NO_ORIG_POINTERS_ACCESS = (1 << 5),

  /**
   * Also process internal ID pointers like `ID.newid` or `ID.orig_id`.
   * WARNING: Dangerous, use with caution.
   */
  IDWALK_DO_INTERNAL_RUNTIME_POINTERS = (1 << 9),
  /**
   * Also process the ID.lib pointer. It is an option because this pointer can usually be fully
   * ignored.
   */
  IDWALK_DO_LIBRARY_POINTER = (1 << 10),
  /** Also process the DNA-deprecated pointers. Should only be used in readfile related code (for
   * proper lib_linking and expanding of older files). */
  IDWALK_DO_DEPRECATED_POINTERS = (1 << 11),
};

/**
 * Check whether current iteration over ID usages should be stopped or not.
 * \return true if the iteration should be stopped, false otherwise.
 */
bool BKE_lib_query_foreachid_iter_stop(const LibraryForeachIDData *data);
void BKE_lib_query_foreachid_process(LibraryForeachIDData *data, ID **id_pp, int cb_flag);
int BKE_lib_query_foreachid_process_flags_get(const LibraryForeachIDData *data);
int BKE_lib_query_foreachid_process_callback_flag_override(LibraryForeachIDData *data,
                                                           int cb_flag,
                                                           bool do_replace);

/** Should typically only be used when processing deprecated ID types (like IPO ones). */
#define BKE_LIB_FOREACHID_PROCESS_ID_NOCHECK(data_, id_, cb_flag_) \
  { \
    BKE_lib_query_foreachid_process((data_), (ID **)&(id_), (cb_flag_)); \
    if (BKE_lib_query_foreachid_iter_stop((data_))) { \
      return; \
    } \
  } \
  ((void)0)

#define BKE_LIB_FOREACHID_PROCESS_ID(data_, id_, cb_flag_) \
  { \
    CHECK_TYPE_ANY((id_), ID *, void *); \
    BKE_LIB_FOREACHID_PROCESS_ID_NOCHECK(data_, id_, cb_flag_); \
  } \
  ((void)0)

#define BKE_LIB_FOREACHID_PROCESS_IDSUPER_P(data_, id_super_p_, cb_flag_) \
  { \
    CHECK_TYPE(&((*(id_super_p_))->id), ID *); \
    BKE_lib_query_foreachid_process((data_), (ID **)(id_super_p_), (cb_flag_)); \
    if (BKE_lib_query_foreachid_iter_stop((data_))) { \
      return; \
    } \
  } \
  ((void)0)

#define BKE_LIB_FOREACHID_PROCESS_IDSUPER(data_, id_super_, cb_flag_) \
  { \
    BKE_LIB_FOREACHID_PROCESS_IDSUPER_P(data_, &(id_super_), cb_flag_); \
  } \
  ((void)0)

#define BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(data_, func_call_) \
  { \
    func_call_; \
    if (BKE_lib_query_foreachid_iter_stop((data_))) { \
      return; \
    } \
  } \
  ((void)0)

/**
 * Process embedded ID pointers (root node-trees, master collections, ...).
 *
 * Those require specific care, since they are technically sub-data of their owner, yet in some
 * cases they still behave as regular IDs.
 */
void BKE_library_foreach_ID_embedded(LibraryForeachIDData *data, ID **id_pp);
void BKE_lib_query_idpropertiesForeachIDLink_callback(IDProperty *id_prop, void *user_data);

/**
 * Loop over all of the ID's this data-block links to.
 */
void BKE_library_foreach_ID_link(Main *bmain,
                                 ID *id,
                                 blender::FunctionRef<LibraryIDLinkCallback> callback,
                                 void *user_data,
                                 int flag);
/**
 * Re-usable function, use when replacing ID's.
 */
void BKE_library_update_ID_link_user(ID *id_dst, ID *id_src, int cb_flag);

/**
 * Return the number of times given \a id_user uses/references \a id_used.
 *
 * \note This only checks for pointer references of an ID, shallow usages
 * (like e.g. by RNA paths, as done for FCurves) are not detected at all.
 *
 * \param id_user: the ID which is supposed to use (reference) \a id_used.
 * \param id_used: the ID which is supposed to be used (referenced) by \a id_user.
 * \return the number of direct usages/references of \a id_used by \a id_user.
 */
int BKE_library_ID_use_ID(ID *id_user, ID *id_used);

/**
 * Say whether given \a owner_id may use (in any way) a data-block of \a id_type_used.
 *
 * This is a 'simplified' abstract version of #BKE_library_foreach_ID_link() above,
 * quite useful to reduce useless iterations in some cases.
 */
bool BKE_library_id_can_use_idtype(ID *owner_id, short id_type_used);

/**
 * Given the owner_id return the type of id_types it can use as a filter_id.
 */
uint64_t BKE_library_id_can_use_filter_id(const ID *owner_id,
                                          const bool include_ui,
                                          const IDTypeInfo *owner_id_type = nullptr);

/**
 * Check whether given ID is used locally (i.e. by another non-linked ID).
 */
bool BKE_library_ID_is_locally_used(Main *bmain, void *idv);
/**
 * Check whether given ID is used indirectly (i.e. by another linked ID).
 */
bool BKE_library_ID_is_indirectly_used(Main *bmain, void *idv);
/**
 * Combine #BKE_library_ID_is_locally_used() and #BKE_library_ID_is_indirectly_used()
 * in a single call.
 */
void BKE_library_ID_test_usages(Main *bmain,
                                void *idv,
                                bool *r_is_used_local,
                                bool *r_is_used_linked);

/** Parameters and result data structure for the 'unused IDs' functions below. */
struct LibQueryUnusedIDsData {
  /** Process local data-blocks. */
  bool do_local_ids = false;
  /** Process linked data-blocks. */
  bool do_linked_ids = false;
  /**
   * Process all actually unused data-blocks, including these that are currently only used by
   * other unused data-blocks, and 'dependency islands' of several data-blocks using each-other,
   * without any external valid user.
   */
  bool do_recursive = false;

  /**
   * Callback filter, if defined and it returns `true`, the given `id` may be considered as unused,
   * otherwise it will always be considered as used.
   *
   * Allows for more complex handling of which IDs should be deleted, on top of the basic
   * local/linked choices.
   */
  blender::FunctionRef<bool(ID *id)> filter_fn = nullptr;

  /**
   * Amount of detected as unused data-blocks, per type and total as the last value of the array
   * (#INDEX_ID_NULL).
   *
   * \note Return value, set by the executed function.
   */
  std::array<int, INDEX_ID_MAX> num_total;
  /**
   * Amount of detected as unused local data-blocks, per type and total as the last value of the
   * array (#INDEX_ID_NULL).
   *
   * \note Return value, set by the executed function.
   */
  std::array<int, INDEX_ID_MAX> num_local;
  /**
   * Amount of detected as unused linked data-blocks, per type and total as the last value of the
   * array (#INDEX_ID_NULL).
   *
   * \note Return value, set by the executed function.
   */
  std::array<int, INDEX_ID_MAX> num_linked;
};

/**
 * Compute amount of unused IDs (a.k.a 'orphaned').
 *
 * By default only consider IDs with `0` user count.
 * If `do_recursive` is set, it will check dependencies to detect all IDs that are not actually
 * used in current file, including 'archipelagos` (i.e. set of IDs referencing each other in
 * loops, but without any 'external' valid usages.
 *
 * Valid usages here are defined as ref-counting usages, which are not towards embedded or
 * loop-back data.
 *
 * \param r_num_total: A zero-initialized array of #INDEX_ID_MAX integers. Number of IDs detected
 * as unused from given parameters, per ID type in the matching index, and as total in
 * #INDEX_ID_NULL item.
 * \param r_num_local: A zero-initialized array of #INDEX_ID_MAX integers. Number of local IDs
 * detected as unused from given parameters (but assuming \a do_local_ids is true), per ID type in
 * the matching index, and as total in #INDEX_ID_NULL item.
 * \param r_num_linked: A zero-initialized array of #INDEX_ID_MAX integers. Number of linked IDs
 * detected as unused from given parameters (but assuming \a do_linked_ids is true), per ID type in
 * the matching index, and as total in #INDEX_ID_NULL item.
 */
void BKE_lib_query_unused_ids_amounts(Main *bmain, LibQueryUnusedIDsData &parameters);
/**
 * Tag all unused IDs (a.k.a 'orphaned').
 *
 * By default only tag IDs with `0` user count.
 * If `do_recursive` is set, it will check dependencies to detect all IDs that are not actually
 * used in current file, including 'archipelagos` (i.e. set of IDs referencing each other in
 * loops, but without any 'external' valid usages.
 *
 * Valid usages here are defined as ref-counting usages, which are not towards embedded or
 * loop-back data.
 *
 * \param tag: the ID tag to use to mark the ID as unused. Should never be `0`.
 * \param r_num_tagged_total: A zero-initialized array of #INDEX_ID_MAX integers. Number of IDs
 * tagged as unused from given parameters, per ID type in the matching index, and as total in
 * #INDEX_ID_NULL item.
 */
void BKE_lib_query_unused_ids_tag(Main *bmain, int tag, LibQueryUnusedIDsData &parameters);

/**
 * Detect orphaned linked data blocks (i.e. linked data not used (directly or indirectly)
 * in any way by any local data), including complex cases like 'linked archipelagoes', i.e.
 * linked data-blocks that use each other in loops,
 * which prevents their deletion by 'basic' usage checks.
 *
 * \param do_init_tag: if \a true, all linked data are checked, if \a false,
 * only linked data-blocks already tagged with #LIB_TAG_DOIT are checked.
 */
void BKE_library_unused_linked_data_set_tag(Main *bmain, bool do_init_tag);
/**
 * Untag linked data blocks used by other untagged linked data-blocks.
 * Used to detect data-blocks that we can forcefully make local
 * (instead of copying them to later get rid of original):
 * All data-blocks we want to make local are tagged by caller,
 * after this function has ran caller knows data-blocks still tagged can directly be made local,
 * since they are only used by other data-blocks that will also be made fully local.
 */
void BKE_library_indirectly_used_data_tag_clear(Main *bmain);

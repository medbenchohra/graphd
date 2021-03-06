/*
Copyright 2015 Google Inc. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "libaddb/addbp.h"
#include "libaddb/addb-smap.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * @brief Remove a smap database from a file tree.
 *
 *  The call fails if any of the files to be removed don't look right
 *  - don't have the right magic number, or extension, or if there are
 *  additional files in the directory that shouldn't be there.
 *
 *  This call is intended to surgically remove only a database
 *  that we know is there.
 *
 * @param addb	opaque database handle
 * @param path	pathname of the SMAP directory.
 * @return 0 on success, a nonzero error code on error.
 */

int addb_smap_remove(addb_handle* addb, char const* path) {
  unsigned int partition = 0;
  char* partition_path;
  char* partition_base;
  size_t partition_base_n = 80;
  size_t const path_n = strlen(path);
  int err;

  partition_path = cm_malloc(addb->addb_cm, path_n + partition_base_n);
  if (!partition_path) {
    err = errno;
    cl_log_errno(addb->addb_cl, CL_LEVEL_ERROR, "cm_malloc", errno,
                 "addb: failed to allocate %lu bytes for partition file name",
                 (unsigned long)path_n + partition_base_n);
    return err;
  }

  memcpy(partition_path, path, path_n);
  partition_base = partition_path + path_n;
  if (partition_base > partition_path && partition_base[-1] != '/') {
    *partition_base++ = '/';
    partition_base_n--;
  }

  for (; partition < ADDB_GMAP_PARTITIONS_MAX; partition++) {
    addb_smap_partition_basename(addb, partition, partition_base,
                                 partition_base_n);
    if (unlink(partition_path)) {
      err = errno;
      if (ENOENT == err)
        err = 0;
      else
        cl_log_errno(addb->addb_cl, CL_LEVEL_ERROR, "unlink", err,
                     "addb: can't remove smap "
                     "partition \"%s\"",
                     partition_path);
    }
  }

  cm_free(addb->addb_cm, partition_path);

  err = addb_largefile_remove(path, addb->addb_cl, addb->addb_cm);
  if (err)
    cl_log_errno(addb->addb_cl, CL_LEVEL_ERROR, "addb_largefile_remove", err,
                 "unable to remove largefiles for \"%s\"", path);

  if (rmdir(path)) {
    if (!err) err = errno;
    cl_log_errno(addb->addb_cl, CL_LEVEL_ERROR, "rmdir", errno,
                 "unable to remove \"%s\"", path);
  }

  return err;
}

int addb_smap_truncate(addb_smap* sm, char const* path) {
  if (sm) {
    addb_handle* const addb = sm->sm_addb;
    addb_smap_partition* part = sm->sm_partition;
    addb_smap_partition* const part_end =
        sm->sm_partition + sm->sm_partition_n - 1;
    int err = 0;
    int e;

    for (; part < part_end; part++)
      if (part->part_td) {
        e = addb_tiled_backup(part->part_td, false);
        if (e) {
          if (!err) err = e;
          cl_log_errno(addb->addb_cl, CL_LEVEL_ERROR, "addb_tiled_backup", e,
                       "unable to turn off "
                       "backup on \"%s\"",
                       part->part_path);
        }
      }

    e = addb_smap_close(sm);
    if (e) {
      if (!err) err = e;
      cl_log_errno(addb->addb_cl, CL_LEVEL_ERROR, "addb_smap_close", e,
                   "unable to close \"%s\"", path);
    }

    e = addb_smap_remove(addb, path);
    if (e) {
      if (!err) err = e;
      cl_log_errno(addb->addb_cl, CL_LEVEL_ERROR, "addb_smap_remove", e,
                   "unable to remove \"%s\"", path);
    }

    return err;
  }

  return 0;
}

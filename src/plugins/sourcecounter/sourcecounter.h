/*
 * Copyright (c) 2015 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __included_sourcecounter_h__
#define __included_sourcecounter_h__

#include <vnet/vnet.h>
#include <vnet/ip/ip.h>
#include <vnet/ethernet/ethernet.h>

#include <vppinfra/hash.h>
#include <vppinfra/error.h>
#include <vppinfra/elog.h>

#include <vppinfra/bihash_16_8.h>

typedef struct {
  /**
   * Each CPU has its own sticky flow hash table.
   * One single table is used for all VIPs.
   */
  clib_bihash_16_8_t hash_table;
} fc_per_cpu_t;

typedef struct {
    /* API message ID base */
    u16 msg_id_base;

    /* convenience */
    vnet_main_t * vnet_main;

    /* Some global data is per-cpu */
    fc_per_cpu_t *per_cpu;

    clib_spinlock_t writer_lock;

} sourcecounter_main_t;

extern sourcecounter_main_t sourcecounter_main;

extern vlib_node_registration_t sourcecounter_node;

#define SOURCECOUNTER_PLUGIN_BUILD_VER "1.0"

#endif /* __included_sourcecounter_h__ */

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
/**
 * @file
 * @brief Flowcounter Plugin, plugin API / trace / CLI handling.
 */

#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>
#include <flowcounter/flowcounter.h>

#include <vlibapi/api.h>
#include <vlibmemory/api.h>

#include <flowcounter/flowcounter.api_enum.h>
#include <flowcounter/flowcounter.api_types.h>

#define REPLY_MSG_ID_BASE sm->msg_id_base
#include <vlibapi/api_helper_macros.h>


VLIB_PLUGIN_REGISTER () = {
    .version = FLOWCOUNTER_PLUGIN_BUILD_VER,
    .description = "FlowCounter plugin",
};

flowcounter_main_t flowcounter_main;

/**
 * @brief Enable/disable the macswap plugin. 
 *
 * Action function shared between message handler and debug CLI.
 */

int flowcounter_macswap_enable_disable (flowcounter_main_t * sm, u32 sw_if_index,
                                   int enable_disable)
{
  vnet_sw_interface_t * sw;
  int rv = 0;

  /* Utterly wrong? */
  if (pool_is_free_index (sm->vnet_main->interface_main.sw_interfaces, 
                          sw_if_index))
    return VNET_API_ERROR_INVALID_SW_IF_INDEX;

  /* Not a physical port? */
  sw = vnet_get_sw_interface (sm->vnet_main, sw_if_index);
  if (sw->type != VNET_SW_INTERFACE_TYPE_HARDWARE)
    return VNET_API_ERROR_INVALID_SW_IF_INDEX;
  
  vnet_feature_enable_disable ("device-input", "flowcounter",
                               sw_if_index, enable_disable, 0, 0);
  return rv;
}

static clib_error_t *
macswap_enable_disable_command_fn (vlib_main_t * vm,
                                   unformat_input_t * input,
                                   vlib_cli_command_t * cmd)
{
  flowcounter_main_t * sm = &flowcounter_main;
  u32 sw_if_index = ~0;
  int enable_disable = 1;
    
  int rv;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT) {
    if (unformat (input, "disable"))
      enable_disable = 0;
    else if (unformat (input, "%U", unformat_vnet_sw_interface,
                       sm->vnet_main, &sw_if_index))
      ;
    else
      break;
  }

  if (sw_if_index == ~0)
    return clib_error_return (0, "Please specify an interface...");
    
  rv = flowcounter_macswap_enable_disable (sm, sw_if_index, enable_disable);

  switch(rv) {
  case 0:
    break;

  case VNET_API_ERROR_INVALID_SW_IF_INDEX:
    return clib_error_return 
      (0, "Invalid interface, only works on physical ports");
    break;

  case VNET_API_ERROR_UNIMPLEMENTED:
    return clib_error_return (0, "Device driver doesn't support redirection");
    break;

  default:
    return clib_error_return (0, "flowcounter_macswap_enable_disable returned %d",
                              rv);
  }
  return 0;
}

/**
 * @brief CLI command to enable/disable the flowcounter plugin.
 */
VLIB_CLI_COMMAND (sr_content_command, static) = {
    .path = "flowcounter macswap",
    .short_help = 
    "flowcounter macswap <interface-name> [disable]",
    .function = macswap_enable_disable_command_fn,
};

/**
 * @brief Plugin API message handler.
 */
static void vl_api_flowcounter_macswap_enable_disable_t_handler
(vl_api_flowcounter_macswap_enable_disable_t * mp)
{
  vl_api_flowcounter_macswap_enable_disable_reply_t * rmp;
  flowcounter_main_t * sm = &flowcounter_main;
  int rv;

  rv = flowcounter_macswap_enable_disable (sm, ntohl(mp->sw_if_index), 
                                      (int) (mp->enable_disable));
  
  REPLY_MACRO(VL_API_FLOWCOUNTER_MACSWAP_ENABLE_DISABLE_REPLY);
}

/* API definitions */
#include <flowcounter/flowcounter.api.c>

/*
static uint32_t ipv4_hash_crc(const void *data,  uint32_t data_len, uint32_t init_val){
  return (uint32_t) data;
}
*/

/**
 * @brief Initialize the flowcounter plugin.
 */
static clib_error_t * flowcounter_init (vlib_main_t * vm)
{
  flowcounter_main_t * sm = &flowcounter_main;

  sm->vnet_main =  vnet_get_main ();

  /* Add our API messages to the global name_crc hash table */
  sm->msg_id_base = setup_message_id_table ();

  /* Create per CPU hash tables! */
  sm->per_cpu = 0;
  vlib_thread_main_t *tm = vlib_get_thread_main ();
  vec_validate(sm->per_cpu, tm->n_vlib_mains - 1);

/*
  struct rte_hash_parameters hash_params = {0};
  hash_params.key_len = sizeof(u32);
  hash_params.hash_func_init_val = 0;
  hash_params.extra_flag = 0;
  hash_params.entries = 1024;
  hash_params.hash_func = ipv4_hash_crc;
*/
  
  for (int i=0; i < tm->n_vlib_mains; i++) {
    char* name = (char *) format(0, "flowcounter_%d", i);
    clib_bihash_init_16_8(&sm->per_cpu[i].hash_table, 
                            name, 2097152, 1 << 30);
//    hash_params.name = (char *) format(0, "flowcounter_%d", i);
//    sm->per_cpu[i].sticky_ht = rte_hash_create(&hash_params); 
  }

  return 0;
}

VLIB_INIT_FUNCTION (flowcounter_init);

/**
 * @brief Hook the flowcounter plugin into the VPP graph hierarchy.
 */
VNET_FEATURE_INIT (flowcounter, static) = 
{
  .arc_name = "device-input",
  .node_name = "flowcounter",
  .runs_before = VNET_FEATURES ("sample"),
};

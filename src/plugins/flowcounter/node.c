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
#include <vlib/vlib.h>
#include <vnet/vnet.h>
#include <vnet/pg/pg.h>
#include <vnet/ethernet/ethernet.h>
#include <vppinfra/error.h>
#include <flowcounter/flowcounter.h>

typedef struct
{
  u32 next_index;
  u32 sw_if_index;
  u8 new_src_mac[6];
  u8 new_dst_mac[6];
  ip4_address_t src_ip;
  ip4_address_t dst_ip;
  u16 src_port;
  u16 dst_port;
} flowcounter_trace_t;


/* packet trace format function */
static u8 *
format_flowcounter_trace (u8 * s, va_list * args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  flowcounter_trace_t *t = va_arg (*args, flowcounter_trace_t *);

  s = format (s, "FLOWCOUNTER: sw_if_index %d, next index %d\n",
	      t->sw_if_index, t->next_index);
  s = format (s, "  new src %U -> new dst %U \n",
	      format_mac_address, t->new_src_mac,
	      format_mac_address, t->new_dst_mac);
  s = format (s, "  src ip %U -> dst ip %U \n",
	      format_ip4_address, &t->src_ip,
	      format_ip4_address, &t->dst_ip);
  s = format (s, "  src port %d -> dst port %d", t->src_port, t->dst_port);

  return s;
}

extern vlib_node_registration_t flowcounter_node;

#define foreach_flowcounter_error \
_(SWAPPED, "Mac swap packets processed") \
_(INSERTS, "Packets inserted")

typedef enum
{
#define _(sym,str) FLOWCOUNTER_ERROR_##sym,
  foreach_flowcounter_error
#undef _
    FLOWCOUNTER_N_ERROR,
} flowcounter_error_t;

static char *flowcounter_error_strings[] = {
#define _(sym,string) string,
  foreach_flowcounter_error
#undef _
};

typedef enum
{
  FLOWCOUNTER_NEXT_INTERFACE_OUTPUT,
  FLOWCOUNTER_N_NEXT,
} flowcounter_next_t;


static_always_inline clib_bihash_kv_16_8_t
get_hash_key(ip4_header_t *ip4){
	clib_bihash_kv_16_8_t key;
	udp_header_t *udp = (udp_header_t *)(ip4 + 1);

	key.key[0] = ( ((u64) ip4->src_address.as_u32) << 32) | (ip4->dst_address.as_u32);
	key.key[1] = ( ((u32) udp->src_port) << 16) | udp->dst_port;

	return key;
}


/*
 * Simple dual/single loop version, default version which will compile
 * everywhere.
 *
 * Node costs 30 clocks/pkt at a vector size of 51
 */

#define VERSION_1 1
#ifdef VERSION_1
#define foreach_mac_address_offset              \
_(0)                                            \
_(1)                                            \
_(2)                                            \
_(3)                                            \
_(4)                                            \
_(5)

VLIB_NODE_FN (flowcounter_node) (vlib_main_t * vm, vlib_node_runtime_t * node,
			    vlib_frame_t * frame)
{
  u32 n_left_from, *from, *to_next;
  flowcounter_next_t next_index;
  u32 pkts_swapped = 0;
  u32 pkts_inserted = 0;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
	  {
		u32 bi0;
		vlib_buffer_t *b0;
		u32 next0 = FLOWCOUNTER_NEXT_INTERFACE_OUTPUT;
		u32 sw_if_index0;
		ethernet_header_t *en0;
		u16 src_port;
		u16 dst_port;

		/* speculatively enqueue b0 to the current next frame */
		bi0 = from[0];
		to_next[0] = bi0;
		from += 1;
		to_next += 1;
		n_left_from -= 1;
		n_left_to_next -= 1;

		b0 = vlib_get_buffer (vm, bi0);
	  	/*
	   	* Direct from the driver, we should be at offset 0
	   	* aka at &b0->data[0]
	   	*/
	  	ASSERT (b0->current_data == 0);

		en0 = vlib_buffer_get_current (b0);
		en0->dst_address[0] = en0->dst_address[1];

		ip4_header_t* ip40 = (ip4_header_t *) (en0 + 1);
		ip4_address_t src_ip;
		ip4_address_t dst_ip;
		src_ip = ip40->src_address;
		dst_ip = ip40->dst_address;
		udp_header_t *udp0 = (udp_header_t *)(ip40 + 1);
		src_port = udp0->src_port;
		dst_port = udp0->dst_port;


		clib_bihash_kv_16_8_t key = get_hash_key(ip40);
		clib_bihash_kv_16_8_t value;

		flowcounter_main_t * fcm = &flowcounter_main;
		if (clib_bihash_search_16_8 (&fcm->per_cpu->hash_table, &key, &value) < 0) {
			key.value = 1;
			pkts_inserted += 1;
		}
		else {
			key.value = value.value + 1;
		}
		
		clib_bihash_add_del_16_8(&fcm->per_cpu->hash_table, &key, 1);

    	sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];

	    /* Send pkt back out the RX interface */
	  	vnet_buffer (b0)->sw_if_index[VLIB_TX] = sw_if_index0;

	  	if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE)
			     && (b0->flags & VLIB_BUFFER_IS_TRACED)))
	    {
	      flowcounter_trace_t *t = vlib_add_trace (vm, node, b0, sizeof (*t));
	      t->sw_if_index = sw_if_index0;
	      t->next_index = next0;
	      clib_memcpy_fast (t->new_src_mac, en0->src_address,
				sizeof (t->new_src_mac));
	      clib_memcpy_fast (t->new_dst_mac, en0->dst_address,
				sizeof (t->new_dst_mac));
		  t->src_ip = src_ip;
	      t->dst_ip = dst_ip;
	      t->src_port = src_port;
		  t->dst_port = dst_port;
	    }

	  	pkts_swapped += 1;

	  	/* verify speculative enqueue, maybe switch current next frame */
	  	vlib_validate_buffer_enqueue_x1 (vm, node, next_index,
					   to_next, n_left_to_next,
					   bi0, next0);
	  }

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }
  
  vlib_node_increment_counter (vm, flowcounter_node.index,
			       FLOWCOUNTER_ERROR_SWAPPED, pkts_swapped);
  vlib_node_increment_counter (vm, flowcounter_node.index,
			       FLOWCOUNTER_ERROR_INSERTS, pkts_inserted);
  return frame->n_vectors;
}
#endif


/* *INDENT-OFF* */
VLIB_REGISTER_NODE (flowcounter_node) =
{
  .name = "flowcounter",
  .vector_size = sizeof (u32),
  .format_trace = format_flowcounter_trace,
  .type = VLIB_NODE_TYPE_INTERNAL,

  .n_errors = ARRAY_LEN(flowcounter_error_strings),
  .error_strings = flowcounter_error_strings,

  .n_next_nodes = FLOWCOUNTER_N_NEXT,

  /* edit / add dispositions here */
  .next_nodes = {
    [FLOWCOUNTER_NEXT_INTERFACE_OUTPUT] = "interface-output",
  },
};
/* *INDENT-ON* */

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */

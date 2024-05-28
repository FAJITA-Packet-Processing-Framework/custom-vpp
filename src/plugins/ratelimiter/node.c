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
#include <ratelimiter/ratelimiter.h>

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
} ratelimiter_trace_t;


/* packet trace format function */
static u8 *
format_ratelimiter_trace (u8 * s, va_list * args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  ratelimiter_trace_t *t = va_arg (*args, ratelimiter_trace_t *);

  s = format (s, "RATELIMITER: sw_if_index %d, next index %d\n",
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

extern vlib_node_registration_t ratelimiter_node;

#define foreach_ratelimiter_error \
_(SWAPPED, "Mac swap packets processed") \
_(INSERTS, "Packets inserted")

typedef enum
{
#define _(sym,str) RATELIMITER_ERROR_##sym,
  foreach_ratelimiter_error
#undef _
    RATELIMITER_N_ERROR,
} ratelimiter_error_t;

static char *ratelimiter_error_strings[] = {
#define _(sym,string) string,
  foreach_ratelimiter_error
#undef _
};

typedef enum
{
  RATELIMITER_NEXT_INTERFACE_OUTPUT,
  RATELIMITER_N_NEXT,
} ratelimiter_next_t;


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

VLIB_NODE_FN (ratelimiter_node) (vlib_main_t * vm, vlib_node_runtime_t * node,
			    vlib_frame_t * frame)
{
  u32 n_left_from, *from, *to_next;
  ratelimiter_next_t next_index;
  u32 thread_index = vm->thread_index;
  clib_bihash_16_8_t hash_table = ratelimiter_main.per_cpu[thread_index].hash_table;

  u32 pkts_swapped = 0;
  u32 pkts_inserted = 0;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

	  clib_bihash_kv_16_8_t key0, key1, key2, key3, key4, key5;
	  u64 hash0, hash1, hash2, hash3, hash4, hash5;


	  if (n_left_from >= 8 && n_left_to_next >= 2) {
		vlib_buffer_t *p0, *p1, *p2, *p3;
		ethernet_header_t *en0, *en1, *en2, *en3;

		p0 = vlib_get_buffer (vm, from[0]);
		p1 = vlib_get_buffer (vm, from[1]);
		p2 = vlib_get_buffer (vm, from[2]);
		p3 = vlib_get_buffer (vm, from[3]);

		en0 = vlib_buffer_get_current (p0);
		en1 = vlib_buffer_get_current (p1);
		en2 = vlib_buffer_get_current (p2);
		en3 = vlib_buffer_get_current (p3);

		key0 = get_hash_key((ip4_header_t *) (en0 + 1));
		key1 = get_hash_key((ip4_header_t *) (en1 + 1));
		key2 = get_hash_key((ip4_header_t *) (en2 + 1));
		key3 = get_hash_key((ip4_header_t *) (en3 + 1));

		hash0 = clib_bihash_hash_16_8(&key0);
		hash1 = clib_bihash_hash_16_8(&key1);
		hash2 = clib_bihash_hash_16_8(&key2);
		hash3 = clib_bihash_hash_16_8(&key3);

	  }
	  
	  while (n_left_from >= 8 && n_left_to_next >= 2){
		u32 next0 = RATELIMITER_NEXT_INTERFACE_OUTPUT;
	  	u32 next1 = RATELIMITER_NEXT_INTERFACE_OUTPUT;
	  	u32 sw_if_index0, sw_if_index1;

	  	u32 bi0, bi1;
	  	vlib_buffer_t *b0, *b1;
		

		/* Prefetch 6th and 7th vlib buffers. */
	  	{
	    	vlib_buffer_t *p6, *p7;

		    p6 = vlib_get_buffer (vm, from[6]);
		    p7 = vlib_get_buffer (vm, from[7]);

	    	vlib_prefetch_buffer_header (p6, LOAD);
	    	vlib_prefetch_buffer_header (p7, LOAD);

		    clib_prefetch_store (p6->data);
		    clib_prefetch_store (p7->data);
	  	}

		/* record keys and hashes, plus prefetch buckets. */
		{
			vlib_buffer_t *p4, *p5;
			ethernet_header_t *en4, *en5;

		    p4 = vlib_get_buffer (vm, from[4]);
		    p5 = vlib_get_buffer (vm, from[5]);

			en4 = vlib_buffer_get_current (p4);
			en5 = vlib_buffer_get_current (p5);

			key4 = get_hash_key((ip4_header_t *) (en4 + 1));
			key5 = get_hash_key((ip4_header_t *) (en5 + 1));

			hash4 = clib_bihash_hash_16_8(&key4);
			hash5 = clib_bihash_hash_16_8(&key5);

			clib_bihash_prefetch_bucket_16_8(&hash_table, hash4);
			clib_bihash_prefetch_bucket_16_8(&hash_table, hash5);

		}

		/* prefetch data. */
		{
			clib_bihash_prefetch_data_16_8(&hash_table, hash2);
			clib_bihash_prefetch_data_16_8(&hash_table, hash3);
		}

		to_next[0] = bi0 = from[0];
		to_next[1] = bi1 = from[1];
	  	from += 2;
	  	to_next += 2;
	  	n_left_from -= 2;
	  	n_left_to_next -= 2;
	  	
		b0 = vlib_get_buffer (vm, bi0);
	  	b1 = vlib_get_buffer (vm, bi1);

		clib_bihash_kv_16_8_t value0, value1;

		if (clib_bihash_search_inline_with_hash_16_8 (&ratelimiter_main.per_cpu[thread_index].hash_table, hash0, &value0) < 0) {
			key0.value = 1;
			pkts_inserted += 1;
		}
		else
			key0.value = value0.value + 1;
		
		clib_bihash_add_del_with_hash_16_8(&ratelimiter_main.per_cpu[thread_index].hash_table, &key0, hash0, 1);

		if (clib_bihash_search_inline_with_hash_16_8 (&ratelimiter_main.per_cpu[thread_index].hash_table, hash1, &value1) < 0) {
			key1.value = 1;
			pkts_inserted += 1;
		}
		else
			key1.value = value1.value + 1;
		
		clib_bihash_add_del_with_hash_16_8(&ratelimiter_main.per_cpu[thread_index].hash_table, &key1, hash1, 1);

		/* shift stored keys and hashes by 2 on every iteration */
		key0 = key2;
		key1 = key3;
		key2 = key4;
		key3 = key5;
		hash0 = hash2;
		hash1 = hash3;
		hash2 = hash4;
		hash3 = hash5;


    	sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];
    	sw_if_index1 = vnet_buffer (b1)->sw_if_index[VLIB_RX];

	    /* Send pkt back out the RX interface */
	  	vnet_buffer (b0)->sw_if_index[VLIB_TX] = sw_if_index0;
	  	vnet_buffer (b1)->sw_if_index[VLIB_TX] = sw_if_index1;

		vlib_validate_buffer_enqueue_x2 (vm, node, next_index,
					   to_next, n_left_to_next,
					   bi0, bi1, next0, next1);
	  }

      while (n_left_from > 0 && n_left_to_next > 0)
	  {
		u32 bi0;
		vlib_buffer_t *b0;
		u32 next0 = RATELIMITER_NEXT_INTERFACE_OUTPUT;
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

		ratelimiter_main_t * fcm = &ratelimiter_main;
		if (clib_bihash_search_16_8 (&fcm->per_cpu[thread_index].hash_table, &key, &value) < 0) {
			key.value = 1;
			pkts_inserted += 1;
		}
		else {
			key.value = value.value + 1;
		}
		
		clib_bihash_add_del_16_8(&fcm->per_cpu[thread_index].hash_table, &key, 1);

    	sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];

	    /* Send pkt back out the RX interface */
	  	vnet_buffer (b0)->sw_if_index[VLIB_TX] = sw_if_index0;

	  	if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE)
			     && (b0->flags & VLIB_BUFFER_IS_TRACED)))
	    {
	      ratelimiter_trace_t *t = vlib_add_trace (vm, node, b0, sizeof (*t));
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
  
  vlib_node_increment_counter (vm, ratelimiter_node.index,
			       RATELIMITER_ERROR_SWAPPED, pkts_swapped);
  vlib_node_increment_counter (vm, ratelimiter_node.index,
			       RATELIMITER_ERROR_INSERTS, pkts_inserted);
  return frame->n_vectors;
}
#endif


/* *INDENT-OFF* */
VLIB_REGISTER_NODE (ratelimiter_node) =
{
  .name = "ratelimiter",
  .vector_size = sizeof (u32),
  .format_trace = format_ratelimiter_trace,
  .type = VLIB_NODE_TYPE_INTERNAL,

  .n_errors = ARRAY_LEN(ratelimiter_error_strings),
  .error_strings = ratelimiter_error_strings,

  .n_next_nodes = RATELIMITER_N_NEXT,

  /* edit / add dispositions here */
  .next_nodes = {
    [RATELIMITER_NEXT_INTERFACE_OUTPUT] = "flowcounter",
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

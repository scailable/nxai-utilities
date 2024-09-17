#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "mpack.h"
#include "yyjson.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

mpack_tree_t *copy_yyjson_to_mpack( yyjson_val *input_object );

mpack_tree_t *copy_mpack_node( mpack_node_t input_node );

void copy_mpack_object_recursive( mpack_node_t node, mpack_writer_t *writer );

#ifdef __cplusplus
}
#endif

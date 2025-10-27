#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(EXPORT_MACRO)
#define EXPORT_MACRO 
#endif

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "mpack.h"
#include "yyjson.h"

EXPORT_MACRO mpack_tree_t* copy_yyjson_to_mpack(yyjson_val* input_object);

EXPORT_MACRO mpack_tree_t* copy_mpack_node(mpack_node_t input_node);

EXPORT_MACRO void print_mpack_object(mpack_node_t node);

EXPORT_MACRO void copy_mpack_object_recursive(mpack_node_t node, mpack_writer_t* writer);

#ifdef __cplusplus
}
#endif

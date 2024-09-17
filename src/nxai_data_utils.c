#include "nxai_data_utils.h"
#include "nxai_process_utils.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mpack.h"
#include "yyjson.h"

#ifdef NXAI_DEBUG
#include "memory_leak_detector.h"
#endif

// Static function declarations
static void _copy_yyjson_to_mpack_recursive( yyjson_val *input_object, mpack_writer_t *writer );

static void _copy_yyjson_to_mpack_recursive( yyjson_val *input_object, mpack_writer_t *writer ) {
    yyjson_type object_type = yyjson_get_type( input_object );
    switch ( object_type ) {
        case YYJSON_TYPE_OBJ: {
            size_t idx;
            size_t map_size = yyjson_obj_size( input_object );
            yyjson_val *key, *val;
            // Start map
            mpack_start_map( writer, map_size );
            for ( ( idx ) = 0, ( key ) = ( input_object ) ? unsafe_yyjson_get_first( input_object ) : ( (void *) 0 ), ( val ) = ( key ) + 1; ( idx ) < ( map_size ); ( idx )++, ( key ) = unsafe_yyjson_get_next( val ), ( val ) = ( key ) + 1 ) {
                // Write obj key
                mpack_write_cstr( writer, yyjson_get_str( key ) );
                // Recursive function will write value
                _copy_yyjson_to_mpack_recursive( val, writer );
            }
            mpack_finish_map( writer );
            break;
        }
        case YYJSON_TYPE_ARR: {
            size_t arr_length = yyjson_arr_size( input_object );
            // Start array
            mpack_start_array( writer, arr_length );
            size_t idx;
            yyjson_val *val;
            for ( ( idx ) = 0, ( val ) = yyjson_arr_get_first( input_object ); ( idx ) < ( arr_length ); ( idx )++, ( val ) = unsafe_yyjson_get_next( val ) ) {
                // Each value of array will be written by recursive function
                _copy_yyjson_to_mpack_recursive( val, writer );
            }
            mpack_finish_array( writer );
            break;
        }
        case YYJSON_TYPE_BOOL: {
            mpack_write_bool( writer, yyjson_get_bool( input_object ) );
            break;
        }
        case YYJSON_TYPE_NUM: {
            yyjson_subtype subtype = yyjson_get_subtype( input_object );
            switch ( subtype ) {
                case YYJSON_SUBTYPE_UINT: {
                    mpack_write_uint( writer, yyjson_get_uint( input_object ) );
                    break;
                }
                case YYJSON_SUBTYPE_SINT: {
                    mpack_write_int( writer, yyjson_get_sint( input_object ) );
                    break;
                }
                case YYJSON_SUBTYPE_REAL: {
                    mpack_write_double( writer, yyjson_get_real( input_object ) );
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case YYJSON_TYPE_STR: {
            const char *string = yyjson_get_str( input_object );
            size_t string_length = yyjson_get_len( input_object );
            mpack_write_str( writer, string, (uint32_t) string_length );
            break;
        }
        default:
            nxai_vlog( "WARNING! Unknown YYJSON_TYPE %d\n", object_type );
            break;
    }
}

mpack_tree_t *copy_yyjson_to_mpack( yyjson_val *input_object ) {
    // Initialize writer
    mpack_writer_t writer;
    char *new_buffer;
    size_t buffer_length;
    mpack_writer_init_growable( &writer, &new_buffer, &buffer_length );
    // Recursively write all values from input object to writer
    _copy_yyjson_to_mpack_recursive( input_object, &writer );
    mpack_finish_map( &writer );
    if ( mpack_writer_destroy( &writer ) != mpack_ok ) {
        nxai_vlog( "Problem writing data: %s\n", mpack_error_to_string( mpack_writer_error( &writer ) ) );
    }

    mpack_tree_t *tree = malloc( sizeof( mpack_tree_t ) );
    mpack_tree_init_data( tree, new_buffer, buffer_length );
    mpack_tree_parse( tree );

    return tree;
}

mpack_tree_t *copy_mpack_node( mpack_node_t input_node ) {
    // Initialize writer
    mpack_writer_t writer;
    char *new_buffer;
    size_t buffer_length;
    mpack_writer_init_growable( &writer, &new_buffer, &buffer_length );
    // Recursively write all values from input object to writer
    copy_mpack_object_recursive( input_node, &writer );
    mpack_finish_map( &writer );
    if ( mpack_writer_destroy( &writer ) != mpack_ok ) {
        nxai_vlog( "Problem writing data: %s\n", mpack_error_to_string( mpack_writer_error( &writer ) ) );
    }

    mpack_tree_t *tree = malloc( sizeof( mpack_tree_t ) );
    mpack_tree_init_data( tree, new_buffer, buffer_length );
    mpack_tree_parse( tree );

    return tree;
}

void copy_mpack_object_recursive( mpack_node_t node, mpack_writer_t *writer ) {
    mpack_type_t node_type = mpack_node_type( node );
    switch ( node_type ) {
        case mpack_type_bool: {
            mpack_write_bool( writer, mpack_node_bool( node ) );
            break;
        }
        case mpack_type_uint:
            mpack_write_u64( writer, mpack_node_u64( node ) );
            break;
        case mpack_type_int:
            mpack_write_i64( writer, mpack_node_i64( node ) );
            break;
        case mpack_type_float:
            mpack_write_float( writer, mpack_node_float( node ) );
            break;
        case mpack_type_double:
            mpack_write_double( writer, mpack_node_double( node ) );
            break;
        case mpack_type_str: {
            const char *string = mpack_node_str( node );
            size_t string_length = mpack_node_strlen( node );
            mpack_write_str( writer, string, (uint32_t) string_length );
            break;
        }
        case mpack_type_bin: {
            mpack_write_bin( writer, mpack_node_bin_data( node ), mpack_node_bin_size( node ) );
            break;
        }
        case mpack_type_array: {
            size_t arr_length = mpack_node_array_length( node );
            mpack_start_array( writer, arr_length );
            for ( size_t arr_index = 0; arr_index < arr_length; arr_index++ ) {
                copy_mpack_object_recursive( mpack_node_array_at( node, arr_index ), writer );
            }
            mpack_finish_array( writer );
            break;
        }
        case mpack_type_map: {
            size_t map_length = mpack_node_map_count( node );
            mpack_build_map( writer );// Start map
            for ( size_t map_index = 0; map_index < map_length; map_index++ ) {
                mpack_node_t key_node = mpack_node_map_key_at( node, map_index );
                // Write map key
                copy_mpack_object_recursive( key_node, writer );
                // Write map value
                mpack_node_t value_node = mpack_node_map_value_at( node, map_index );
                copy_mpack_object_recursive( value_node, writer );
            }
            mpack_complete_map( writer );// Finish map
            break;
        }
        default:
            nxai_vlog( "Warning! Unknown mpack type: %d\n", node_type );
            break;
    }
}
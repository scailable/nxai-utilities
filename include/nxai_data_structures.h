#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct score_object_t {
    char *class_name;
    float score;
} score_object_t;

typedef struct count_object_t {
    char *class_name;
    size_t count;
} count_object_t;

typedef struct bbox_object_t {
    char *class_name;
    char *format;
    size_t coords_length;
    float *coordinates;
    float *scores;
} bbox_object_t;

typedef struct tensor_object_t {
    char *name;
    size_t rank;
    size_t *shape;
    size_t type;
    char *data;
    size_t size;
} tensor_object_t;

enum nxai_data_type {
    DATA_TYPE_FLOAT = 1,
    DATA_TYPE_UINT8 = 2,
    DATA_TYPE_INT8 = 3,
    DATA_TYPE_UINT16 = 4,
    DATA_TYPE_INT16 = 5,
    DATA_TYPE_INT32 = 6,
    DATA_TYPE_INT64 = 7,
    DATA_TYPE_STRING = 8,
    DATA_TYPE_BOOL = 9,
    DATA_TYPE_DOUBLE = 11,
    DATA_TYPE_UINT32 = 12,
    DATA_TYPE_UINT64 = 13
};

typedef struct nxai_output_object_t {
    size_t num_outputs;
    tensor_object_t *outputs;
    bbox_object_t *bboxes;
    size_t num_classes;
    count_object_t *counts;
    size_t num_counts;
    score_object_t *scores;
    size_t num_scores;
} nxai_output_object_t;

#ifdef __cplusplus
}
#endif
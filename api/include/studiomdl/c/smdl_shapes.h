/* studiomdl C API: shapes (vertex animation) of a geometry.
 *
 * A shape is a named set of vertex changes of a geometry: sparse vertex indices (into the geometry's positions) with a
 * position delta each, optionally a normal delta and a wrinkle value each. Flexes (smdl_flex.h) refer to shapes by
 * their index (the order of creation, 0, 1, ...); `SMDL_NO_ID` stands for "the base shape" (no changes) where a call
 * accepts it.
 *
 * What the compile builds from them is what studiomdl builds from a VTA file (one frame per shape, frame 0 = the
 * base pose) or, for DMX eyelids, from the delta states of a DMX file:
 *   - the vertices of the vertex animation are the distinct positions of the vertices the triangles/quads use (in the
 *     order of their first corner), with the normal of the first corner at that position; vertices with the same
 *     position share their deltas. A shape that gives different position deltas to two vertices at one position is a
 *     compile error (differences below 1e-4 are tolerated, the first vertex wins);
 *   - positions and deltas are in the geometry's units: the compile applies the geometry's scale like it does to the
 *     positions (a VTA file's data is not scaled);
 *   - wrinkle values only reach the MDL for the flexes of smdl_mesh_add_flex* when a value is not zero (the flex
 *     becomes a wrinkle flex).
 * Shapes of a mesh geometry cannot be combined with appended geometries (the compile reports it).
 * Vertices of a shape that no triangle uses have no effect.
 */
#ifndef STUDIOMDL_C_SMDL_SHAPES_H
#define STUDIOMDL_C_SMDL_SHAPES_H

#include "smdl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Adds a shape: `count` vertex indices (uint32_t, strided) and one position delta (xyz floats) per index. The
 * geometry's positions must be set first (smdl_geometry_set_positions): SMDL_ERROR_INVALID_ARGUMENT otherwise and for a
 * duplicate index inside the shape or a non-finite delta, SMDL_ERROR_OUT_OF_RANGE for an index >= the vertex count,
 * SMDL_ERROR_DUPLICATE_NAME for a name the geometry uses already, SMDL_ERROR_LIMIT beyond 4999 shapes. The compile
 * checks the indices again against the final positions. out_shape receives the shape index (may be NULL). */
SMDL_API smdl_status smdl_geometry_add_shape(smdl_geometry *geometry, const char *name, const uint32_t *vertices,
                                             size_t count, size_t vertices_stride_bytes,
                                             const float *position_deltas, size_t position_deltas_stride_bytes,
                                             uint32_t *out_shape);

/* Normal deltas (xyz per vertex entry of the shape, `count` must equal the shape's entry count). Without them the
 * shape leaves normals unchanged. */
SMDL_API smdl_status smdl_geometry_set_shape_normal_deltas(smdl_geometry *geometry, uint32_t shape, const float *xyz,
                                                           size_t count, size_t stride_bytes);

/* Wrinkle values (one float per vertex entry of the shape, `count` must equal the shape's entry count). */
SMDL_API smdl_status smdl_geometry_set_shape_wrinkle(smdl_geometry *geometry, uint32_t shape, const float *values,
                                                     size_t count, size_t stride_bytes);

SMDL_API uint32_t smdl_geometry_shape_count(const smdl_geometry *geometry);

/* Shape index by name (exact match); SMDL_ERROR_NOT_FOUND when there is none. */
SMDL_API smdl_status smdl_geometry_find_shape(const smdl_geometry *geometry, const char *name, uint32_t *out_shape);

#ifdef __cplusplus
}
#endif

#endif

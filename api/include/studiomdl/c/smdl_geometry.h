/* studiomdl C API: geometry, the source data of one mesh (what an SMD file holds), built in memory.
 *
 * A geometry is created with its mesh (smdl_mesh_get_geometry) and owned by the model. Its data is turned into the
 * same internal source an SMD loader produces, so equal data gives byte-identical output.
 *
 * Vertex data comes in two forms that can be mixed:
 *  - indexed vertices: positions (+ normals, UVs, tangents, bone weights), one entry per vertex, referenced by the
 *    triangle indices;
 *  - per-corner overrides: normals and UVs per triangle corner (3 per triangle, in triangle order), which replace the
 *    indexed normal/UV of that corner. Studiomdl merges equal corners exactly as it does for an SMD file.
 * Every setter replaces what an earlier call of the same setter stored, validates what can be checked against the
 * data set so far (an array whose length contradicts the vertex or triangle count, an index out of range, an unknown
 * bone or material id), and leaves the geometry unchanged when it fails. Because of that cross-checking, set the
 * positions first, then the per-vertex arrays, then the triangles, then the per-corner arrays.
 *
 * Conventions (those of SMD files):
 *  - Positions are Source units, Z up. Triangles are given counter-clockwise like SMD corners: studiomdl emits the
 *    corners in the order (0, 2, 1) as an SMD loader does; smdl_geometry_set_flip_triangles reverses that (the
 *    "reverse" option of $body/studio).
 *  - UVs: u to the right, v up from the bottom-left corner. UV set 0: studiomdl stores 1 - v, as it does for SMD
 *    files. Sets 1-7 are stored exactly as given (the SMD loader doesn't flip the extra UV sets of a version 3 SMD
 *    either). A missing UV set 0 is (0, 0) for every corner.
 *  - Normals are required: either smdl_geometry_set_normals or smdl_geometry_set_corner_normals (or both).
 *  - Weights: studiomdl keeps the 3 largest influences per vertex and renormalizes (influences of the same bone are
 *    summed, zero weights are dropped). Without smdl_geometry_set_weights every vertex is rigidly bound to bone 0.
 *  - Tangents (optional): without them studiomdl computes the tangent space from positions, normals and UV set 0
 *    as for any source. With them the given tangent (xyz + handedness w = +1/-1) replaces the computed one,
 *    indexed like the positions.
 */
#ifndef STUDIOMDL_C_SMDL_GEOMETRY_H
#define STUDIOMDL_C_SMDL_GEOMETRY_H

#include "smdl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Vertex positions, 3 floats per vertex. stride 0 = packed (12 bytes). Defines the vertex count of the geometry. */
SMDL_API smdl_status smdl_geometry_set_positions(smdl_geometry *geometry, const float *xyz, size_t count, size_t stride);

/* Vertex normals, 3 floats per vertex; count must equal the vertex count. */
SMDL_API smdl_status smdl_geometry_set_normals(smdl_geometry *geometry, const float *xyz, size_t count, size_t stride);

/* Optional vertex tangents, 4 floats per vertex (x, y, z, w = handedness +1/-1); count = vertex count. */
SMDL_API smdl_status smdl_geometry_set_tangents(smdl_geometry *geometry, const float *xyzw, size_t count,
                                                size_t stride);

/* UV set `set` (0..7), 2 floats per vertex; count = vertex count. Sets must be filled from 0 upwards without gaps
 * (checked by the compile). */
SMDL_API smdl_status smdl_geometry_set_uvs(smdl_geometry *geometry, uint32_t set, const float *uv, size_t count,
                                           size_t stride);

/* Bone weights: `influences_per_vertex` (1..SMDL_MAX_INFLUENCES, i.e. 1..16) bone ids and weights per vertex; more is
 * SMDL_ERROR_INVALID_ARGUMENT (trim to the 16 largest first). bones/weights are arrays of
 * vertex_count rows of influences_per_vertex uint32/float each; bones_stride / weights_stride is the distance in
 * bytes between two rows (0 = packed). A vertex with fewer influences pads its row with weight 0 (any valid bone).
 * vertex_count must equal the vertex count, bone ids must exist (add the bones first), weights must be >= 0. */
SMDL_API smdl_status smdl_geometry_set_weights(smdl_geometry *geometry, uint32_t influences_per_vertex,
                                               const smdl_bone_id *bones, size_t bones_stride, const float *weights,
                                               size_t weights_stride, size_t vertex_count);

/* Adds a material to this geometry and returns its id (ids are 0, 1, 2... in call order). The name is a material
 * path relative to the $cdmaterials paths, with or without extension, like the texture name of an SMD triangle.
 * Names are unique in a geometry (ignoring case): SMDL_ERROR_DUPLICATE_NAME. Names that studiomdl skips in SMD
 * files (null.bmp, null.tga, debug/debugempty) and the -t overrides behave as they do for SMDs. */
SMDL_API smdl_status smdl_geometry_add_material(smdl_geometry *geometry, const char *name, smdl_material_id *out_id);

/* Triangles: `indices` holds triangle_count rows of 3 vertex indices (stride = bytes between rows, 0 = packed);
 * `materials` one material id per triangle (stride in bytes, 0 = packed; NULL = every triangle uses material 0).
 * Material ids must exist (add the materials first). Degenerate triangles (two equal vertices after the corners
 * were merged) are dropped as the SMD loader drops them. */
SMDL_API smdl_status smdl_geometry_set_triangles(smdl_geometry *geometry, const uint32_t *indices,
                                                 size_t triangle_count, size_t stride,
                                                 const smdl_material_id *materials, size_t materials_stride);

/* Quads for $subd models (smdl_model_set_subd): quad_count rows of 4 vertex indices; the corners are emitted in
 * the order (0, 3, 2, 1), the order the DMX loader uses for quads. Material ids as for triangles. A model with
 * quads must set the subd flag. Quad corners take their normals/UVs from the indexed arrays only. */
SMDL_API smdl_status smdl_geometry_set_quads(smdl_geometry *geometry, const uint32_t *indices, size_t quad_count,
                                             size_t stride, const smdl_material_id *materials,
                                             size_t materials_stride);

/* Per-corner normals, 3 floats per corner, corner_count = 3 * triangle count (corner 3k+c = corner c of triangle k).
 * They replace the indexed normals of those corners. */
SMDL_API smdl_status smdl_geometry_set_corner_normals(smdl_geometry *geometry, const float *xyz, size_t corner_count,
                                                      size_t stride);

/* Per-corner UVs of set `set`, 2 floats per corner, corner_count = 3 * triangle count. They replace the indexed UVs
 * of that set for those corners. */
SMDL_API smdl_status smdl_geometry_set_corner_uvs(smdl_geometry *geometry, uint32_t set, const float *uv,
                                                  size_t corner_count, size_t stride);

/* Rest pose of one bone as this geometry sees it (position + rotation local to the parent), replacing the model
 * skeleton's rest pose for this geometry only: the per-source skeleton of an SMD file. Calling it again for the
 * same bone replaces the override. The euler form takes SMD angles in radians. */
SMDL_API smdl_status smdl_geometry_set_rest_pose(smdl_geometry *geometry, smdl_bone_id bone, const float position[3],
                                                 const float rotation[4]);
SMDL_API smdl_status smdl_geometry_set_rest_pose_euler(smdl_geometry *geometry, smdl_bone_id bone,
                                                       const float position[3], const float euler[3]);

/* Reverses the winding of every triangle (the `reverse` option of $body / studio). Default 0. */
SMDL_API smdl_status smdl_geometry_set_flip_triangles(smdl_geometry *geometry, int flip);

/* The material names of this geometry are paths relative to the materials directory, as the materials of a DMX file
 * (and of a version 2 SMD) are: the compile then also searches an empty $cdmaterials path, after the model's own. Needed
 * for an MDL byte-identical to the compile of a DMX file. Default 0. */
SMDL_API smdl_status smdl_geometry_set_relative_materials(smdl_geometry *geometry, int relative);

/* Scale applied to positions and rest poses of this geometry (the `scale` option of $body / studio).
 * 0 (the default) uses the model scale (smdl_model_set_scale, $scale). Not allowed on geometries created by
 * smdl_mesh_append_geometry (use smdl_geometry_set_append_offset). */
SMDL_API smdl_status smdl_geometry_set_scale(smdl_geometry *geometry, float scale);

/* Offset of a geometry created by smdl_mesh_append_geometry ($appendsource ... offset pos[] angle[] scale[]):
 * position, angles in degrees (pitch, yaw, roll as QAngle) and a uniform scale. Without it the appended vertices
 * are only scaled by the model scale, as $appendsource without offset does. SMDL_ERROR_INVALID_ARGUMENT on a
 * geometry that was not created by smdl_mesh_append_geometry. */
SMDL_API smdl_status smdl_geometry_set_append_offset(smdl_geometry *geometry, const float position[3],
                                                     const float angles[3], float scale);

/* Vertex count (the length of the positions array); 0 for a null handle. */
SMDL_API uint32_t smdl_geometry_vertex_count(const smdl_geometry *geometry);

/* Triangle count (quads are counted separately by smdl_geometry_quad_count). */
SMDL_API uint32_t smdl_geometry_triangle_count(const smdl_geometry *geometry);

SMDL_API uint32_t smdl_geometry_quad_count(const smdl_geometry *geometry);

/* Number of materials added with smdl_geometry_add_material. */
SMDL_API uint32_t smdl_geometry_material_count(const smdl_geometry *geometry);

#ifdef __cplusplus
}
#endif

#endif

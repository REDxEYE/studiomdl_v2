/* C test host for the studiomdl shared library: pure C11, only <studiomdl/studiomdl_api.h>.
 *
 * Builds a static prop (a unit cube, one bone, one material) through the C API, compiles it in memory, checks the
 * status and the files, saves them with smdl_result_save_to and reads them back. Also checks that a failing compile
 * still returns a result with the error details and that the library stays usable afterwards.
 *
 * usage: api_c_host <output directory>
 */
#include <studiomdl/studiomdl_api.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define CHECK(cond)                                                                                                  \
    do {                                                                                                             \
        if (!(cond)) {                                                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);                                 \
            ++g_failures;                                                                                            \
        }                                                                                                            \
    } while (0)

/* A call that must succeed; stops the test on the first failure (the later calls depend on it) */
#define REQUIRE_OK(call)                                                                                             \
    do {                                                                                                             \
        const smdl_status status_ = (call);                                                                          \
        if (status_ != SMDL_OK) {                                                                                    \
            fprintf(stderr, "%s:%d: %s returned %s\n", __FILE__, __LINE__, #call, smdl_status_string(status_));      \
            exit(1);                                                                                                 \
        }                                                                                                            \
    } while (0)

static const float kPositions[8][3] = {
    {-8, -8, 0}, {8, -8, 0}, {8, 8, 0}, {-8, 8, 0}, {-8, -8, 16}, {8, -8, 16}, {8, 8, 16}, {-8, 8, 16},
};
static const float kNormals[8][3] = {
    {-0.57735f, -0.57735f, -0.57735f}, {0.57735f, -0.57735f, -0.57735f}, {0.57735f, 0.57735f, -0.57735f},
    {-0.57735f, 0.57735f, -0.57735f},  {-0.57735f, -0.57735f, 0.57735f},  {0.57735f, -0.57735f, 0.57735f},
    {0.57735f, 0.57735f, 0.57735f},    {-0.57735f, 0.57735f, 0.57735f},
};
static const float kUvs[8][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}, {1, 1}, {0, 1}};
static const uint32_t kTriangles[12][3] = {
    {0, 3, 2}, {0, 2, 1}, /* bottom */ {4, 5, 6}, {4, 6, 7}, /* top */ {0, 1, 5}, {0, 5, 4}, /* -y */
    {1, 2, 6}, {1, 6, 5},                                      /* +x */ {2, 3, 7}, {2, 7, 6}, /* +y */
    {3, 0, 4}, {3, 4, 7},                                      /* -x */
};

static int g_logMessages;

static void CountMessages(void *user, smdl_severity severity, const char *text) {
    (void) severity;
    (void) text;
    ++*(int *) user;
}

static void BuildCube(smdl_model *model) {
    smdl_bone_id root = 0;
    smdl_mesh *mesh = NULL;
    smdl_geometry *geometry = NULL;
    smdl_material_id material = 0;
    const float origin[3] = {0, 0, 0};
    const float identity[4] = {0, 0, 0, 1};
    uint32_t materials[12] = {0};

    REQUIRE_OK(smdl_model_add_material_path(model, "models/api_c"));
    REQUIRE_OK(smdl_model_set_surfaceprop(model, "wood"));
    REQUIRE_OK(smdl_model_set_static_prop(model, 1));
    REQUIRE_OK(smdl_model_add_bone(model, "root", SMDL_NO_BONE, origin, identity, &root));
    REQUIRE_OK(smdl_model_create_mesh(model, "crate", &mesh));
    REQUIRE_OK(smdl_mesh_get_geometry(mesh, &geometry));
    REQUIRE_OK(smdl_geometry_set_positions(geometry, &kPositions[0][0], 8, 0));
    REQUIRE_OK(smdl_geometry_set_normals(geometry, &kNormals[0][0], 8, 0));
    REQUIRE_OK(smdl_geometry_set_uvs(geometry, 0, &kUvs[0][0], 8, 0));
    REQUIRE_OK(smdl_geometry_add_material(geometry, "crate_wood", &material));
    REQUIRE_OK(smdl_geometry_set_triangles(geometry, &kTriangles[0][0], 12, 0, materials, 0));
    CHECK(smdl_geometry_vertex_count(geometry) == 8);
    CHECK(smdl_geometry_triangle_count(geometry) == 12);
    CHECK(smdl_model_bone_count(model) == 1);
}

static char *ReadFile(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    char *data = NULL;
    long length = 0;
    *size = 0;
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) >= 0 && fseek(file, 0, SEEK_SET) == 0) {
        data = (char *) malloc(length ? (size_t) length : 1);
        if (data && fread(data, 1, (size_t) length, file) == (size_t) length)
            *size = (size_t) length;
        else {
            free(data);
            data = NULL;
        }
    }
    fclose(file);
    return data;
}

int main(int argc, char **argv) {
    const char *outDir = argc > 1 ? argv[1] : "api_c_out";
    smdl_model *model;
    smdl_result *result = NULL;
    size_t fileCount, i;
    int sawMdl = 0, sawVvd = 0, sawVtx = 0;

    CHECK(smdl_api_version() == SMDL_API_VERSION);
    CHECK(strcmp(smdl_status_string(SMDL_OK), "") != 0);

    CHECK(smdl_model_create(NULL) == NULL);
    CHECK(smdl_model_create("") == NULL);

    /* 1. A model with nothing in it fails the compile, with details in the result */
    model = smdl_model_create("api_c/empty");
    CHECK(model != NULL);
    CHECK(smdl_model_compile(model, NULL, NULL, NULL, &result) == SMDL_ERROR_COMPILE);
    CHECK(result != NULL);
    if (result) {
        CHECK(smdl_result_status(result) == SMDL_ERROR_COMPILE);
        CHECK(smdl_result_file_count(result) == 0);
        CHECK(smdl_result_error_message(result) != NULL && smdl_result_error_message(result)[0] != '\0');
        smdl_result_destroy(result);
        result = NULL;
    }
    smdl_model_destroy(model);

    /* 2. The static prop */
    model = smdl_model_create("api_c/crate");
    CHECK(model != NULL);
    if (!model)
        return 1;
    CHECK(strcmp(smdl_model_get_name(model), "api_c/crate") == 0);
    BuildCube(model);

    REQUIRE_OK(smdl_model_compile(model, NULL, CountMessages, &g_logMessages, &result));
    CHECK(result != NULL);
    if (!result)
        return 1;
    if (smdl_result_status(result) != SMDL_OK) {
        fprintf(stderr, "compile failed: %s (%s)\n", smdl_result_error_message(result),
                smdl_result_error_object(result));
        return 1;
    }
    CHECK(g_logMessages > 0);
    CHECK((size_t) g_logMessages == smdl_result_message_count(result));

    fileCount = smdl_result_file_count(result);
    CHECK(fileCount >= 3); /* .mdl, .vvd, .dx90.vtx (+ .phy when there is collision) */
    for (i = 0; i < fileCount; ++i) {
        const char *relativePath = NULL;
        const void *data = NULL;
        size_t size = 0;
        size_t length;
        REQUIRE_OK(smdl_result_file(result, i, &relativePath, &data, &size));
        length = strlen(relativePath);
        CHECK(strncmp(relativePath, "models/api_c/crate.", 19) == 0);
        CHECK(size > 0 && data != NULL);
        if (length >= 4 && strcmp(relativePath + length - 4, ".mdl") == 0) {
            sawMdl = 1;
            CHECK(size >= 4 && memcmp(data, "IDST", 4) == 0);
        } else if (length >= 4 && strcmp(relativePath + length - 4, ".vvd") == 0) {
            sawVvd = 1;
            CHECK(size >= 4 && memcmp(data, "IDSV", 4) == 0);
        } else if (length >= 9 && strcmp(relativePath + length - 9, ".dx90.vtx") == 0) {
            sawVtx = 1;
        }
    }
    CHECK(sawMdl && sawVvd && sawVtx);

    /* 3. save_to writes <dir>/models/api_c/crate.*; read every file back and compare the bytes */
    REQUIRE_OK(smdl_result_save_to(result, outDir));
    for (i = 0; i < fileCount; ++i) {
        const char *relativePath = NULL;
        const void *data = NULL;
        size_t size = 0, fileSize = 0;
        char path[1024];
        char *onDisk;
        REQUIRE_OK(smdl_result_file(result, i, &relativePath, &data, &size));
        snprintf(path, sizeof path, "%s/%s", outDir, relativePath);
        onDisk = ReadFile(path, &fileSize);
        if (!onDisk) {
            fprintf(stderr, "cannot read back %s\n", path);
            ++g_failures;
            continue;
        }
        CHECK(fileSize == size && memcmp(onDisk, data, size) == 0);
        free(onDisk);
    }
    smdl_result_destroy(result);
    result = NULL;

    /* 4. The model compiles again (results are independent of the model) */
    REQUIRE_OK(smdl_model_compile(model, NULL, NULL, NULL, &result));
    CHECK(smdl_result_status(result) == SMDL_OK && smdl_result_file_count(result) == fileCount);
    smdl_result_destroy(result);
    smdl_model_destroy(model);

    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("api_c_host: ok (%zu files, %d log messages)\n", fileCount, g_logMessages);
    return 0;
}

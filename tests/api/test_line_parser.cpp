#include "harness.hpp"
#include "../../studiomdl/line_parser.h"

#include <fstream>
#include <string_view>

SMDL_TEST(line_parser_rejects_invalid_numbers) {
    int integer = 0;
    float decimal = 0;
    LineParser valid(std::string_view{"\t+17 -2.5e+1 extra"}.substr(0, 12));
    Require(valid.Read(integer, decimal) && integer == 17 && decimal == -25 && valid.Empty(),
            "numbers inside a bounded non-NUL-terminated view must parse");

    for (const auto bad : {"12junk", "+-12", "99999999999999999999"})
        Require(!LineParser(bad).Number(integer), "invalid integer must not be partially accepted");
    for (const auto bad : {"1e+", "1e999", "nan", "inf"})
        Require(!LineParser(bad).Number(decimal), "malformed and non-finite floats must be rejected");
}

SMDL_TEST(line_parser_keeps_record_delimiters) {
    LineParser line("  \"bone name\" -1 scale[+2.5] ");
    int parent = 0;
    float scale = 0;
    Require(line.Quoted() == "bone name" && line.Number(parent) && parent == -1 &&
            line.Consume("scale[") && line.Number(scale, ']') && scale == 2.5f &&
            line.Consume("]") && line.Empty(), "quoted names and attached brackets must parse as separate fields");
    Require(LineParser("\"unterminated").Quoted().empty(), "a missing closing quote must not yield a name");
}

SMDL_TEST(line_parser_obj_face_and_index_validation) {
    const TempDir folder("line_obj");
    const auto qc = folder.path() / "mesh.qc";
    const auto obj = folder.path() / "mesh.obj";
    std::ofstream(qc) << "$modelname \"line_obj.mdl\"\n$staticprop\n"
                         "$body \"mesh\" \"mesh.obj\"\n$sequence \"BindPose\" \"mesh.obj\"\n";

    const char *vertices = "v 0 0 0\nv +1 0 0\nv 0 1 0\nvn 0 0 1\n";
    std::ofstream(obj) << vertices << "f 1//1 2//1 3//1 # triangle\n";
    Require(RunReference(qc, {}).exitCode == 0, "an OBJ with normals and no texcoords must compile");

    std::ofstream(obj) << vertices << "f 1//1 2//1 4//1\n";
    const auto result = RunReference(qc, {});
    Require(result.exitCode != 0 && result.log.find("face index out of range") != std::string::npos,
            "an out-of-range OBJ index must be rejected by the loader");
}

SMDL_TEST(line_parser_smd_node_rejects_overflow) {
    const TempDir folder("line_smd");
    const auto qc = folder.path() / "mesh.qc";
    std::ofstream(qc) << "$modelname \"line_smd.mdl\"\n$body \"mesh\" \"mesh.smd\"\n";
    std::ofstream(folder.path() / "mesh.smd") << "version 1\nnodes\n999999999999999999 \"root\" -1\nend\n";
    const auto result = RunReference(qc, {});
    Require(result.exitCode != 0 && result.log.find("Malformed bone record") != std::string::npos,
            "an overflowing SMD bone index must not end the nodes section silently");
}

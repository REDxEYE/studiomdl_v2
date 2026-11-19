// vstd::paths / vstd string helpers: separators, extension boundaries, UTF-8 bytes, in-place edits of std::string
// and vstd::FixedString, UTF-8 <-> std::filesystem::path.

#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"

#include <cstdio>
#include <string>

static int s_failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++s_failures; \
        } \
    } while (0)

int main() {
    using namespace vstd::paths;

    // a dot in a directory name is not an extension
    CHECK(extension("dir.v2/name") == "");
    CHECK(without_extension("dir.v2/name") == "dir.v2/name");
    CHECK(without_extension("dir.v2\\name.smd") == "dir.v2\\name");
    CHECK(extension("a/b.tar.gz") == "gz");

    // both separators, Cyrillic bytes untouched
    CHECK(file_name("исходники\\анимации/движение.smd") == "движение.smd");
    CHECK(file_base("исходники/тело.smd") == "тело");
    CHECK(directory("исходники/тело.smd") == "исходники/");
    CHECK(parent("исходники/тело.smd") == "исходники");
    CHECK(directory("тело.smd").empty());
    CHECK(parent("тело.smd").empty());

    CHECK(is_absolute("/tmp/x"));
    CHECK(is_absolute("\\x"));
    CHECK(is_absolute("Z:\\x"));
    CHECK(!is_absolute("x/y"));
    CHECK(!is_absolute(""));

    CHECK(join("a", "b") == "a/b");
    CHECK(join("a/", "b") == "a/b");
    CHECK(join("a\\", "b") == "a\\b");
    CHECK(join("", "b") == "b");

    // in place: std::string
    std::string path = "Модели/персонаж.qc";
    strip_extension(path);
    CHECK(path == "Модели/персонаж");
    default_extension(path, ".qc");
    CHECK(path == "Модели/персонаж.qc");
    default_extension(path, "smd");
    CHECK(path == "Модели/персонаж.qc");
    set_extension(path, "smd");
    CHECK(path == "Модели/персонаж.smd");
    strip_filename(path);
    CHECK(path == "Модели");
    append_slash(path, '/');
    append_slash(path, '/');
    CHECK(path == "Модели/");
    path = "a\\b/c";
    fix_slashes(path, '/');
    CHECK(path == "a/b/c");

    // in place: FixedString (ASCII)
    vstd::FixedString<32> name("models\\golden\\x.mdl");
    strip_extension(name);
    CHECK(name == "models\\golden\\x");
    fix_slashes(name, '/');
    CHECK(name == "models/golden/x");
    set_extension(name, ".vvd");
    CHECK(name == "models/golden/x.vvd");
    strip_filename(name);
    CHECK(name == "models/golden");

    // UTF-8 round trip through std::filesystem::path, '\' normalized to '/'
    const std::filesystem::path fsPath = to_fs("Модели\\персонаж.qc");
    CHECK(fsPath.filename().u8string() == u8"персонаж.qc");
    CHECK(to_utf8(fsPath.parent_path()) == "Модели");

    // ASCII-only case folding: UTF-8 bytes compare exactly
    CHECK(vstd::iequals("Models/X.MDL", "models/x.mdl"));
    CHECK(!vstd::iequals("Модели", "модели"));
    CHECK(vstd::istarts_with("C:\\Content\\x", "c:\\content\\"));
    CHECK(vstd::ifind("/home/a/MODELS/x", "models") == 8);
    std::string upper = "ABC/Модели";
    vstd::make_lower_ascii(upper);
    CHECK(upper == "abc/Модели");

    if (s_failures == 0) {
        std::printf("path_utils: all checks passed\n");
    }
    return s_failures == 0 ? 0 : 1;
}

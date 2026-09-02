// Exercise the material-library seed state machine end to end.
//
// material_lib_path.cpp asks exeDir() where the shipped library is, and
// userDataDir() where the user's copy goes. exeDir() lives in
// script_callbacks.cpp, which drags in the whole document layer, so this test
// supplies its own -- the point here is the state machine, not path discovery.
// userDataDir() reads APPDATA/HOME, so the test steers it by setting them.
#include "material_lib_path.h"
#include "utf8_io.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static std::string g_exeDir;
std::string exeDir() { return g_exeDir; }   // stands in for script_callbacks.cpp

static int failures = 0;
static void check(bool ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static void writeFile(const std::string &p, const char *text)
{
    FILE *f = fopen(p.c_str(), "wb");
    fputs(text, f);
    fclose(f);
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "femm_matlib_test";
    fs::remove_all(root);
    const fs::path shipped = root / "program";
    const fs::path home = root / "home";
    fs::create_directories(shipped);
    fs::create_directories(home);

    g_exeDir = shipped.string() + "/";
#ifdef _WIN32
    _putenv_s("APPDATA", home.string().c_str());
#else
    setenv("HOME", home.string().c_str(), 1);
    setenv("XDG_DATA_HOME", (home.string() + "/.local/share").c_str(), 1);
#endif
    writeFile(g_exeDir + "matlib.dat", "SHIPPED v1\n");

    printf("no user copy yet:\n");
    check(materialLibPath("matlib.dat") == g_exeDir + "matlib.dat",
          "reads resolve to the shipped library");
    check(!materialLibUpdateAvailable("matlib.dat"),
          "no update offered when there is no user copy");

    printf("user saves for the first time:\n");
    const std::string w = materialLibWritePath("matlib.dat");
    check(!w.empty() && w != g_exeDir + "matlib.dat",
          "write path is the user's copy, not the shipped one");
    writeFile(w, "USER EDITED\n");
    check(materialLibPath("matlib.dat") == w,
          "reads now resolve to the user's copy");
    check(fs::exists(fs::path(w + ".seed")), "a seed was recorded on the fork");
    check(!materialLibUpdateAvailable("matlib.dat"),
          "no update offered while the shipped library is unchanged");

    printf("a new release ships a different library:\n");
    writeFile(g_exeDir + "matlib.dat", "SHIPPED v2 with more materials\n");
    check(materialLibUpdateAvailable("matlib.dat"), "update IS offered");
    check(materialLibUpdateAvailable("matlib.dat"),
          "and keeps being offered until answered");

    printf("user answers (declines):\n");
    materialLibMarkSeen("matlib.dat");
    check(!materialLibUpdateAvailable("matlib.dat"),
          "not asked again for this version");
    check(materialLibPath("matlib.dat") == w, "their copy is untouched");

    printf("a later release changes it again:\n");
    writeFile(g_exeDir + "matlib.dat", "SHIPPED v3\n");
    check(materialLibUpdateAvailable("matlib.dat"), "offered again");

    printf("same content, rewritten (an install that did not change it):\n");
    materialLibMarkSeen("matlib.dat");
    writeFile(g_exeDir + "matlib.dat", "SHIPPED v3\n");
    check(!materialLibUpdateAvailable("matlib.dat"),
          "NOT offered -- content compared, not mtime");

    // Deleting your copy is the documented way back to the shipped materials.
    // It leaves the .seed behind, so re-forking must re-mark against the
    // CURRENT shipped library rather than the stale one.
    printf("user deletes their copy to get the stock materials back:\n");
    fs::remove(fs::path(w));
    check(materialLibPath("matlib.dat") == g_exeDir + "matlib.dat",
          "reads go back to the shipped library");
    const std::string w2 = materialLibWritePath("matlib.dat");
    writeFile(w2, "USER EDITED AGAIN\n");
    check(!materialLibUpdateAvailable("matlib.dat"),
          "the new fork is marked against the CURRENT shipped library");

    fs::remove_all(root);
    printf("%s\n", failures ? "FAILURES" : "material library seed logic: all ok");
    return failures ? 1 : 0;
}

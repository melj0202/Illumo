#include <Illumo/Content/VirtualPath.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>

static std::string
normalized(const char* path)
{
  std::string output = "<unchanged>";
  if (!VirtualPath::normalize(path, output)) {
    return "<rejected>";
  }
  return output;
}

static int
testVirtualPathNormalize()
{
  TestCounters counters;
  testEqStr(counters, normalized("/"), "/", "root stays root");
  testEqStr(counters, normalized("///"), "/", "repeated root separators");
  testEqStr(counters,
            normalized("/app/meshes/tree.obj"),
            "/app/meshes/tree.obj",
            "plain path is unchanged");
  testEqStr(counters,
            normalized("//app//meshes///tree.obj/"),
            "/app/meshes/tree.obj",
            "repeated and trailing separators collapse");
  testEqStr(counters,
            normalized("/Packages/Forest"),
            "/Packages/Forest",
            "case is preserved, never folded");
  testEqStr(counters,
            normalized("/app/caf\xc3\xa9.png"),
            "/app/caf\xc3\xa9.png",
            "well-formed UTF-8 is accepted");

  std::string joined;
  testTrue(counters,
           VirtualPath::join("/packages/forest/scenes", "../x", joined) ==
             false,
           "join never resolves parent references");
  testTrue(counters,
           VirtualPath::join("/packages/forest", "meshes/tree.obj", joined) &&
             joined == "/packages/forest/meshes/tree.obj",
           "relative reference joins the base directory");
  testTrue(
    counters,
    VirtualPath::join("/packages/forest", "/engine/Skybox/sky.png", joined) &&
      joined == "/engine/Skybox/sky.png",
    "absolute reference ignores the base");
  testTrue(counters,
           VirtualPath::join("/", "app", joined) && joined == "/app",
           "join from the root");
  testTrue(counters,
           !VirtualPath::join("/app", "", joined),
           "empty reference is rejected");

  testEqStr(counters,
            VirtualPath::parent("/app/meshes/tree.obj"),
            "/app/meshes",
            "parent of a nested file");
  testEqStr(counters, VirtualPath::parent("/app"), "/", "parent of a mount");
  testEqStr(counters, VirtualPath::parent("/"), "/", "parent of the root");
  testEqStr(counters,
            std::string(VirtualPath::fileName("/app/meshes/tree.obj")),
            "tree.obj",
            "file name");
  testEqStr(counters,
            std::string(VirtualPath::fileName("/")),
            "",
            "the root has no file name");
  testEqStr(counters,
            std::string(VirtualPath::mountName("/packages/forest")),
            "packages",
            "mount name of a nested path");
  testEqStr(counters,
            std::string(VirtualPath::mountName("/app")),
            "app",
            "mount name of a mount");
  testTrue(counters,
           VirtualPath::isWithin("/app/a", "/app") &&
             VirtualPath::isWithin("/app", "/app") &&
             !VirtualPath::isWithin("/apple", "/app") &&
             VirtualPath::isWithin("/apple", "/"),
           "isWithin respects component boundaries");
  testEqStr(counters,
            std::string(VirtualPath::relativeTo("/app/meshes/a.obj", "/app")),
            "meshes/a.obj",
            "relative part below a prefix");
  testEqStr(counters,
            std::string(VirtualPath::relativeTo("/app", "/app")),
            "",
            "relative part of the prefix itself");
  testEqStr(counters,
            std::string(VirtualPath::relativeTo("/app/a", "/")),
            "app/a",
            "relative part below the root");
  return counters.failures;
}

static int
testVirtualPathRejects()
{
  TestCounters counters;
  const char* rejectedAbsolute[] = { "",
                                     "app",
                                     "app/a",
                                     "/app/./a",
                                     "/app/../a",
                                     "/app/a.",
                                     "/app/a ",
                                     "/app/a\\b",
                                     "/app/a:b",
                                     "/app/a*",
                                     "/app/a?",
                                     "/app/a\"",
                                     "/app/<a>",
                                     "/app/a|b",
                                     "/app/a\tb",
                                     "/app/CON",
                                     "/app/con.txt",
                                     "/app/Nul",
                                     "/app/COM1",
                                     "/app/lpt9.x",
                                     "/app/CONIN$",
                                     "/app/.illumo-stage",
                                     "/app/.ILLUMO-x",
                                     "/app/\xc0\xaf",         // overlong '/'
                                     "/app/\xed\xa0\x80",     // surrogate
                                     "/app/\xf4\x90\x80\x80", // above U+10FFFF
                                     "/app/\xe2\x82", // truncated sequence
                                     "/app/a\x7f" };
  for (const char* path : rejectedAbsolute) {
    std::string output = "<unchanged>";
    const bool accepted = VirtualPath::normalize(path, output);
    if (accepted || output != "<unchanged>") {
      std::printf("FAIL: accepted or modified output for \"%s\"\n", path);
      ++counters.failures;
    }
  }
  testTrue(counters, true, "every unsafe absolute path is rejected");

  const std::string longComponent(256, 'a');
  testTrue(counters,
           !VirtualPath::validComponent(longComponent),
           "component over 255 bytes is rejected");
  testTrue(counters,
           VirtualPath::validComponent(std::string(255, 'a')),
           "component of exactly 255 bytes is accepted");
  std::string longPath;
  while (longPath.size() <= VirtualPath::kMaximumPathBytes) {
    longPath += "/abcdefghij";
  }
  std::string output;
  testTrue(counters,
           !VirtualPath::normalize(longPath, output),
           "path over 1024 bytes is rejected");

  // The relative rule is the one host file services apply to package and
  // storage names; it must keep rejecting everything it always did.
  const char* rejectedRelative[] = {
    "",   "/a",  "a/",      "a//b", "./a", "a/..",     "a\\b",
    "C:", "CON", "prn.log", "a.",   "a ",  ".illumo-x"
  };
  for (const char* path : rejectedRelative) {
    if (VirtualPath::validRelative(path)) {
      std::printf("FAIL: relative name \"%s\" was accepted\n", path);
      ++counters.failures;
    }
  }
  testTrue(counters,
           VirtualPath::validRelative("Assets/IllEd/editor-ui-atlas.jpg") &&
             VirtualPath::validRelative("rulesets.user.json") &&
             VirtualPath::validRelative("CONSOLE.txt") &&
             VirtualPath::validRelative("com10"),
           "ordinary relative names stay accepted");
  return counters.failures;
}

void
registerVirtualPathTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Content.VirtualPathNormalize",
               []() { return testVirtualPathNormalize(); });
  registry.add("Illumo.Content.VirtualPathRejects",
               []() { return testVirtualPathRejects(); });
}

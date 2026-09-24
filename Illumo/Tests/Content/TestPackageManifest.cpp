#include <Illumo/Content/PackageManifest.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <initializer_list>
#include <vector>

static bool
decode(const std::string& json, PackageManifest& manifest, std::string& error)
{
  return decodePackageManifest(json, PackageCeilings{}, manifest, error);
}

static int
testPackageManifestDecode()
{
  TestCounters counters;
  PackageManifest manifest;
  std::string error;
  testTrue(counters,
           decode(R"({"format":"ilpk","format_version":1,"id":"illed",
                      "version":"26.09","title":"IllEd","kind":"app",
                      "app":{"module":"IllEd.wasm","launchAccess":"edit",
                             "metering":"epoch","memoryMiB":512,
                             "deadlineMilliseconds":10000}})",
                  manifest,
                  error),
           "application manifest decodes");
  testEqStr(counters, error, "", "no error on success");
  testEqStr(counters, manifest.id, "illed", "id");
  testEqStr(counters, manifest.title, "IllEd", "title");
  testTrue(counters, manifest.kind == PackageKind::App, "kind app");
  testEqStr(counters, manifest.app.module, "IllEd.wasm", "module");
  testTrue(counters, manifest.app.launchEditable, "edit launch access");
  testTrue(counters, !manifest.app.meterFuel, "epoch metering");
  testTrue(counters, manifest.app.memoryMiB == 512u, "memory request");
  testTrue(counters,
           manifest.targets.size() == 1 && manifest.targets[0] == "*",
           "targets default to every application");
  testTrue(counters,
           manifest.targetsApplication("anything"),
           "wildcard target applies everywhere");

  PackageManifest content;
  testTrue(counters,
           decode(R"({"format":"ilpk","format_version":1,"id":"forest-pack",
                      "kind":"content","targets":["illed","meshviewer"],
                      "dependencies":[{"id":"base.materials","version":"1"}],
                      "overlays":[{"target":"/app","priority":10}]})",
                  content,
                  error),
           "content manifest decodes");
  testTrue(counters, content.kind == PackageKind::Content, "kind content");
  testTrue(counters,
           content.targetsApplication("illed") &&
             !content.targetsApplication("game"),
           "explicit targets filter applications");
  testTrue(counters,
           content.dependencies.size() == 1 &&
             content.dependencies[0].id == "base.materials" &&
             content.dependencies[0].version == "1",
           "dependency recorded");
  testTrue(counters,
           content.overlays.size() == 1 &&
             content.overlays[0].target == "/app" &&
             content.overlays[0].priority == 10,
           "overlay recorded");

  PackageManifest mod;
  testTrue(counters,
           decode(R"({"format":"ilpk","format_version":1,"id":"palette",
                      "kind":"mod","targets":["game"],
                      "mod":{"module":"PaletteMod.wasm","extensionApi":"csim.1"}})",
                  mod,
                  error),
           "mod manifest decodes");
  testTrue(counters,
           mod.kind == PackageKind::Mod &&
             mod.mod.module == "PaletteMod.wasm" &&
             mod.mod.extensionApi == "csim.1",
           "mod section recorded");
  return counters.failures;
}

static int
testPackageManifestClampsBudgets()
{
  TestCounters counters;
  PackageCeilings ceilings;
  ceilings.memoryMiB = 256;
  ceilings.workers = 4;
  ceilings.deadlineMilliseconds = 5000;
  PackageManifest manifest;
  std::string error;
  testTrue(counters,
           decodePackageManifest(
             R"({"format":"ilpk","format_version":1,"id":"game","kind":"app",
                 "app":{"module":"IllumoGame.wasm","worker":"Worker.wasm",
                        "memoryMiB":4000,"workers":64,"workerMemoryMiB":9000,
                        "deadlineMilliseconds":999999}})",
             ceilings,
             manifest,
             error),
           "oversized requests decode");
  testTrue(counters, manifest.app.memoryMiB == 256u, "memory clamped");
  testTrue(counters, manifest.app.workers == 4u, "workers clamped");
  testTrue(counters,
           manifest.app.workerMemoryMiB == 256u,
           "worker memory clamped to the memory ceiling");
  testTrue(
    counters, manifest.app.deadlineMilliseconds == 5000u, "deadline clamped");

  const char* rejected[] = {
    R"({"format":"ilpk","format_version":1,"id":"g","kind":"app",
        "app":{"module":"G.wasm","memoryMiB":0}})",
    R"({"format":"ilpk","format_version":1,"id":"g","kind":"app",
        "app":{"module":"G.wasm","memoryMiB":-4}})",
    R"({"format":"ilpk","format_version":1,"id":"g","kind":"app",
        "app":{"module":"G.wasm","workers":2}})",
    R"({"format":"ilpk","format_version":1,"id":"g","kind":"app",
        "app":{"module":"G.wasm","metering":"epoch","fuelPerCall":5}})",
    R"({"format":"ilpk","format_version":1,"id":"g","kind":"app",
        "app":{"module":"G.wasm","launchAccess":"write"}})",
    R"({"format":"ilpk","format_version":1,"id":"g","kind":"app",
        "app":{"module":"../G.wasm"}})",
    R"({"format":"ilpk","format_version":1,"id":"g","kind":"app",
        "app":{"module":"bin/G.wasm"}})",
    R"({"format":"ilpk","format_version":1,"id":"g","kind":"app",
        "app":{"module":"G.wasm","title":"moved to the top level"}})"
  };
  for (const char* json : rejected) {
    PackageManifest untouched;
    untouched.id = "sentinel";
    if (decodePackageManifest(json, ceilings, untouched, error) ||
        untouched.id != "sentinel" || error.empty()) {
      std::printf("FAIL: accepted or modified output for %s\n", json);
      ++counters.failures;
    }
  }
  testTrue(counters, true, "invalid budget requests reject the manifest");
  return counters.failures;
}

static int
testPackageManifestKindMismatch()
{
  TestCounters counters;
  const char* rejected[] = {
    "not json",
    "[]",
    R"({"format_version":1,"id":"a","kind":"content"})",
    R"({"format":"zip","format_version":1,"id":"a","kind":"content"})",
    R"({"format":"ilpk","format_version":2,"id":"a","kind":"content"})",
    R"({"format":"ilpk","format_version":"1","id":"a","kind":"content"})",
    R"({"format":"ilpk","format_version":1,"kind":"content"})",
    R"({"format":"ilpk","format_version":1,"id":"Bad","kind":"content"})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"plugin"})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"app"})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"content",
        "app":{"module":"A.wasm"}})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"mod"})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"mod",
        "mod":{"module":"A.wasm"}})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"app",
        "app":{"module":"A.wasm"},"mod":{"module":"B.wasm","extensionApi":"x"}})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"content",
        "mount":"/engine"})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"content",
        "overlays":[{"target":"/engine"}]})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"content",
        "overlays":[{"target":"/app"},{"target":"/app"}]})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"content",
        "overlays":[{"target":"/app","priority":1.5}]})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"content",
        "dependencies":[{"id":"a"}]})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"content",
        "dependencies":[{"id":"b"},{"id":"b"}]})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"content",
        "dependencies":[{"id":"b","optional":true}]})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"content",
        "targets":["game","game"]})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"content",
        "targets":"game"})",
    R"({"format":"ilpk","format_version":1,"id":"a","kind":"content",
        "title":"line\nbreak"})"
  };
  for (const char* json : rejected) {
    PackageManifest untouched;
    untouched.id = "sentinel";
    std::string error;
    if (decodePackageManifest(json, PackageCeilings{}, untouched, error) ||
        untouched.id != "sentinel" || error.empty()) {
      std::printf("FAIL: accepted or modified output for %s\n", json);
      ++counters.failures;
    }
  }
  testTrue(counters, true, "malformed and mismatched manifests are rejected");
  return counters.failures;
}

static PackageManifest
package(const char* id, std::initializer_list<const char*> dependencies)
{
  PackageManifest manifest;
  manifest.id = id;
  for (const char* dependency : dependencies) {
    manifest.dependencies.push_back({ dependency, "" });
  }
  return manifest;
}

static std::string
ids(const std::vector<PackageManifest>& packages,
    const std::vector<std::size_t>& indices)
{
  std::string text;
  for (const std::size_t index : indices) {
    text += (text.empty() ? "" : ",") + packages[index].id;
  }
  return text;
}

static int
testDependencyOrderAndCycles()
{
  TestCounters counters;
  const std::vector<PackageManifest> acyclic = { package("zeta", { "alpha" }),
                                                 package("alpha", {}),
                                                 package("mid",
                                                         { "zeta", "alpha" }),
                                                 package("beta", {}) };
  const PackageOrderResult ordered = orderPackages(acyclic);
  testEqStr(counters,
            ids(acyclic, ordered.order),
            "alpha,beta,zeta,mid",
            "dependencies precede dependents, ties sorted by id");
  testTrue(counters, ordered.rejected.empty(), "nothing rejected");

  const std::vector<PackageManifest> broken = {
    package("a", { "b" }), package("b", { "a" }),
    package("c", { "a" }), package("d", { "missing" }),
    package("e", { "d" }), package("f", {}),
    package("f", {})
  };
  const PackageOrderResult result = orderPackages(broken);
  testEqStr(counters, ids(broken, result.order), "f", "only f survives");
  testEqSize(counters,
             result.rejected.size(),
             6,
             "duplicate, missing, dependent-of-missing and cycle members "
             "rejected");
  testEqSize(counters,
             result.reasons.size(),
             result.rejected.size(),
             "every rejection has a reason");
  return counters.failures;
}

void
registerPackageManifestTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Content.PackageManifestDecode",
               []() { return testPackageManifestDecode(); });
  registry.add("Illumo.Content.PackageManifestClampsBudgets",
               []() { return testPackageManifestClampsBudgets(); });
  registry.add("Illumo.Content.PackageManifestKindMismatch",
               []() { return testPackageManifestKindMismatch(); });
  registry.add("Illumo.Content.DependencyOrderAndCycles",
               []() { return testDependencyOrderAndCycles(); });
}

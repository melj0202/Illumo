#include <Illumo/Gui/GuiFileTree.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>

static std::vector<std::string>
paths(const std::vector<GuiFileTreeRow>& rows)
{
  std::vector<std::string> result;
  for (const GuiFileTreeRow& row : rows) {
    result.push_back(row.path);
  }
  return result;
}

static int
testFileTreeRows()
{
  TestCounters counters;
  GuiFileTree tree("/");
  testTrue(counters,
           tree.takePendingListings() == std::vector<std::string>{ "/" },
           "the unlisted root is requested first");
  testTrue(counters,
           tree.takePendingListings().empty(),
           "a requested listing is not requested twice");
  tree.setChildren(
    "/",
    { { "zeta.txt", false, 5 }, { "packages", true, 0 }, { "app", true, 0 } });
  testTrue(counters,
           paths(tree.rows()) ==
             std::vector<std::string>{ "/app", "/packages", "/zeta.txt" },
           "directories sort before files, each by name");
  tree.toggle("/packages");
  const std::vector<GuiFileTreeRow> loading = tree.rows();
  testTrue(counters,
           loading[1].expanded && loading[1].loading,
           "an expanded, unlisted directory shows as loading");
  testTrue(counters,
           tree.takePendingListings() ==
             std::vector<std::string>{ "/packages" },
           "expanding requests the directory's listing");
  tree.setChildren("/packages", { { "forest", true, 0 } });
  tree.setExpanded("/packages/forest", true);
  tree.setChildren("/packages/forest", { { "tree.obj", false, 42 } });
  const std::vector<GuiFileTreeRow> rows = tree.rows();
  testTrue(counters,
           paths(rows) == std::vector<std::string>{ "/app",
                                                    "/packages",
                                                    "/packages/forest",
                                                    "/packages/forest/tree.obj",
                                                    "/zeta.txt" },
           "expanded directories nest depth first");
  testTrue(counters,
           rows[3].depth == 2 && rows[3].size == 42 && !rows[3].directory,
           "rows carry depth, size and kind");
  tree.toggle("/packages");
  testTrue(counters,
           tree.rows().size() == 3 && tree.expanded("/packages/forest"),
           "collapsing hides descendants but keeps their expansion");
  tree.invalidate("/packages");
  tree.toggle("/packages");
  testTrue(counters,
           tree.takePendingListings() ==
             std::vector<std::string>{ "/packages" },
           "an invalidated directory is listed again");
  tree.setRoot("/project");
  testTrue(counters,
           tree.rows().empty() && !tree.expanded("/packages") &&
             tree.takePendingListings() ==
               std::vector<std::string>{ "/project" },
           "a new root forgets listings and expansion");
  testEqStr(counters,
            GuiFileTree::childPath("/", "app"),
            "/app",
            "root children have one slash");
  return counters.failures;
}

void
registerGuiFileTreeTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Gui.FileTreeRows", []() { return testFileTreeRows(); });
}

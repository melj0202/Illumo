#include <Illumo/Content/BehaviourSchema.h>
#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/SceneBehaviours.h>
#include <Illumo/Content/SceneInstance.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

static const char* kSchemaText = R"({
  "format": "illumo-behaviours",
  "format_version": 1,
  "behaviours": [
    { "type": "test.spinner", "title": "Spinner",
      "fields": [
        { "name": "speed", "kind": "number", "default": 1, "min": -10, "max": 10 },
        { "name": "steps", "kind": "integer", "default": 3 },
        { "name": "active", "kind": "bool", "default": true },
        { "name": "label", "kind": "text", "title": "Label" },
        { "name": "tint", "kind": "color", "default": [255, 128, 0, 255] },
        { "name": "axis", "kind": "vector3", "default": [0, 1, 0] },
        { "name": "mode", "kind": "choice", "options": ["loop", "once"] },
        { "name": "mesh", "kind": "asset", "asset_types": ["mesh"] },
        { "name": "target", "kind": "node" } ] },
    { "type": "test.logger", "fields": [] }
  ]
})";

static BehaviourSchema
testSchema()
{
  BehaviourSchema schema;
  std::string error;
  schema.parse(kSchemaText, error);
  return schema;
}

static int
testBehaviourSchemaParse()
{
  TestCounters counters;
  BehaviourSchema schema;
  std::string error;
  testTrue(counters, schema.parse(kSchemaText, error), "a valid file parses");
  testEqSize(counters, schema.types().size(), 2u, "two behaviour types");
  const BehaviourType* spinner = schema.find("test.spinner");
  testTrue(counters,
           spinner != nullptr && spinner->title == "Spinner" &&
             spinner->fields.size() == 9u,
           "the spinner and its fields");
  const BehaviourField* speed =
    spinner != nullptr ? spinner->field("speed") : nullptr;
  testTrue(counters,
           speed != nullptr && speed->hasMinimum && speed->maximum == 10.0 &&
             speed->defaultValue.number == 1.0 && speed->title == "speed",
           "a number field keeps its range and default; title falls back");
  const BehaviourField* mode =
    spinner != nullptr ? spinner->field("mode") : nullptr;
  testTrue(counters,
           mode != nullptr && mode->defaultValue.text == "loop",
           "a choice defaults to its first option");
  testTrue(counters,
           schema.find("test.logger") != nullptr &&
             schema.find("test.logger")->title == "test.logger",
           "a type's title falls back to its type");

  const char* refused[] = {
    "{",
    R"({"format":"other","format_version":1,"behaviours":[]})",
    R"({"format":"illumo-behaviours","format_version":2,"behaviours":[]})",
    R"({"format":"illumo-behaviours","format_version":1,"behaviours":[],"x":1})",
    R"({"format":"illumo-behaviours","format_version":1,"behaviours":[{"type":"nodot"}]})",
    R"({"format":"illumo-behaviours","format_version":1,"behaviours":[{"type":"a.b"},{"type":"a.b"}]})",
    R"({"format":"illumo-behaviours","format_version":1,"behaviours":[{"type":"a.b","fields":[{"name":"x","kind":"choice"}]}]})",
    R"({"format":"illumo-behaviours","format_version":1,"behaviours":[{"type":"a.b","fields":[{"name":"x","kind":"bool","default":2}]}]})",
    R"({"format":"illumo-behaviours","format_version":1,"behaviours":[{"type":"a.b","fields":[{"name":"x","kind":"number","default":5,"max":2}]}]})",
    R"({"format":"illumo-behaviours","format_version":1,"behaviours":[{"type":"a.b","fields":[{"name":"Bad","kind":"number"}]}]})",
    R"({"format":"illumo-behaviours","format_version":1,"behaviours":[{"type":"a.b","fields":[{"name":"type","kind":"number"}]}]})",
    R"({"format":"illumo-behaviours","format_version":1,"behaviours":[{"type":"a.b","fields":[{"name":"x","kind":"number"},{"name":"x","kind":"bool"}]}]})",
    R"({"format":"illumo-behaviours","format_version":1,"behaviours":[{"type":"a.b","fields":[{"name":"x","kind":"text","min":1}]}]})",
    R"({"format":"illumo-behaviours","format_version":1,"behaviours":[{"type":"a.b","fields":[{"name":"x","kind":"asset","asset_types":["sound"]}]}]})",
  };
  int index = 0;
  for (const char* text : refused) {
    BehaviourSchema probe = schema;
    std::string message;
    const bool parsed = probe.parse(text, message);
    testTrue(
      counters,
      !parsed && !message.empty() && probe.types().size() == 2u,
      ("refused file " + std::to_string(index) + " leaves the schema unchanged")
        .c_str());
    ++index;
  }

  BehaviourSchema other;
  other.parse(R"({"format":"illumo-behaviours","format_version":1,
    "behaviours":[{"type":"test.spinner"},{"type":"other.thing"}]})",
              error);
  std::vector<std::string> conflicts;
  schema.merge(other, &conflicts);
  testTrue(counters,
           schema.types().size() == 3u && conflicts.size() == 1u &&
             schema.find("test.spinner")->fields.size() == 9u,
           "merging keeps the first description of a type");
  return counters.failures;
}

static int
testBehaviourValues()
{
  TestCounters counters;
  const BehaviourSchema schema = testSchema();
  const BehaviourType& spinner = *schema.find("test.spinner");
  std::vector<std::string> warnings;
  BehaviourValues values = BehaviourSchema::decode(spinner, "{}", &warnings);
  testTrue(counters,
           warnings.empty() && values.number("speed") == 1.0 &&
             values.integer("steps") == 3 && values.flag("active") &&
             values.text("mode") == "loop" &&
             values.vector("axis") == Vector3(0.0f, 1.0f, 0.0f) &&
             values.color("tint").g == 128,
           "missing members take their defaults");

  values = BehaviourSchema::decode(
    spinner,
    R"({"speed":50,"steps":2.5,"active":false,"mode":"once","label":"hi",)"
    R"("axis":[1,0,0],"tint":[1,2,3,4],"unknown":7})",
    &warnings);
  testTrue(counters,
           values.number("speed") == 10.0 && values.integer("steps") == 3 &&
             !values.flag("active") && values.text("mode") == "once" &&
             values.text("label") == "hi" &&
             values.vector("axis") == Vector3(1.0f, 0.0f, 0.0f) &&
             values.color("tint").a == 4,
           "valid members apply, numbers clamp and bad ones keep defaults");
  testEqSize(counters,
             warnings.size(),
             2u,
             "one warning for the clamp and one for the non-integer");

  warnings.clear();
  BehaviourSchema::decode(spinner, "[1,2]", &warnings);
  testEqSize(counters, warnings.size(), 1u, "non-object data warns once");

  values.set("speed", BehaviourValue{ BehaviourFieldKind::Number, 2.5 });
  const std::string data =
    BehaviourSchema::encode(spinner, values, R"({"zz_extra":true})");
  testTrue(counters,
           data.find("\"speed\":2.5") != std::string::npos &&
             data.find("\"zz_extra\":true") != std::string::npos &&
             data.find("\"steps\":3") != std::string::npos &&
             data.front() == '{' &&
             data.find("\"axis\"") < data.find("\"mesh\""),
           "encoding writes every field, keeps unknown members and sorts keys");

  // The encoded data survives an .ilsc round trip byte for byte.
  SceneDocument document;
  SceneNode node;
  node.id = "n";
  SceneComponent component;
  component.value = SceneOpaqueComponent{ spinner.type, data };
  node.components.push_back(component);
  document.nodes.push_back(node);
  SceneDocument parsed;
  std::string error;
  const bool roundTrip =
    IlscCodec::parse(IlscCodec::encode(document), parsed, error);
  testTrue(counters,
           roundTrip && std::get<SceneOpaqueComponent>(
                          parsed.nodes.front().components.front().value)
                            .data == data,
           "encoded data is already in IlscCodec's canonical form");

  const SceneOpaqueComponent fresh = BehaviourSchema::defaultComponent(spinner);
  const BehaviourValues defaults =
    BehaviourSchema::decode(spinner, fresh.data, &warnings);
  testTrue(counters,
           fresh.type == "test.spinner" && defaults.number("speed") == 1.0 &&
             defaults.text("mode") == "loop",
           "a default component holds every default");
  return counters.failures;
}

// Records every call into a shared log so tests can check order and timing.
struct BehaviourLog
{
  std::vector<std::string> calls;
};

static BehaviourLog* g_log = nullptr;

class LoggingBehaviour final : public SceneBehaviour
{
public:
  void start(SceneBehaviourContext& context) override
  {
    g_log->calls.push_back("start " + context.nodeId());
  }
  void update(SceneBehaviourContext& context, double elapsed) override
  {
    (void)elapsed;
    g_log->calls.push_back("update " + context.nodeId());
  }
  void valuesChanged(SceneBehaviourContext& context) override
  {
    g_log->calls.push_back("changed " + context.nodeId());
  }
  void stop(SceneBehaviourContext& context) override
  {
    g_log->calls.push_back("stop " + context.nodeId() +
                           (context.node() != nullptr ? " present" : " gone"));
  }
};

// Moves its node along X at `speed` units per second; spawns a child once
// and destroys a node named by `target` once.
class SpinnerBehaviour final : public SceneBehaviour
{
public:
  void update(SceneBehaviourContext& context, double elapsed) override
  {
    const SceneNode* node = context.node();
    if (node != nullptr) {
      Transform3D transform = node->transform;
      transform.position.x +=
        static_cast<float>(context.values().number("speed") * elapsed);
      context.setTransform(transform);
    }
    if (!m_spawned && context.values().text("label") == "spawn") {
      SceneNode child;
      child.id = context.scene().uniqueId("spawned");
      child.parentId = context.nodeId();
      child.name = "Spawned";
      context.createNode(child);
      m_spawned = true;
    }
    const std::string& target = context.values().text("target");
    if (!target.empty() && !m_destroyed) {
      context.destroyNode(target);
      m_destroyed = true;
    }
  }

private:
  bool m_spawned = false;
  bool m_destroyed = false;
};

static SceneNode
behaviourNode(const std::string& id,
              const std::string& parent,
              const std::string& type,
              const std::string& data = "{}")
{
  SceneNode node;
  node.id = id;
  node.parentId = parent;
  node.name = id;
  if (!type.empty()) {
    SceneComponent component;
    component.value = SceneOpaqueComponent{ type, data };
    node.components.push_back(component);
  }
  return node;
}

static int
testSceneBehavioursLifecycle()
{
  TestCounters counters;
  BehaviourLog log;
  g_log = &log;
  const BehaviourSchema schema = testSchema();
  BehaviourRegistry registry;
  testTrue(counters,
           registry.add<LoggingBehaviour>("test.logger") &&
             registry.add<SpinnerBehaviour>("test.spinner") &&
             !registry.add<LoggingBehaviour>("test.logger") &&
             !registry.add<LoggingBehaviour>("nodot"),
           "registering refuses duplicates and plain names");
  testTrue(counters,
           registry.compare(schema).empty(),
           "code and file describe the same types");

  SceneDocument document;
  document.nodes.push_back(behaviourNode("parent", "", "test.logger"));
  document.nodes.push_back(behaviourNode("child", "parent", "test.logger"));
  document.nodes.push_back(
    behaviourNode("mover", "", "test.spinner", R"({"speed":2})"));
  document.nodes.push_back(behaviourNode("plain", "", "other.thing"));
  SceneInstance scene;
  std::string error;
  scene.load(document, "/app", error);

  SceneBehaviours behaviours(registry, schema);
  behaviours.attach(scene);
  testEqSize(counters,
             behaviours.count(),
             3u,
             "registered components get behaviours, others do not");
  testTrue(counters, log.calls.empty(), "nothing runs before the first update");
  behaviours.update(0.5);
  const std::vector<std::string> first = {
    "start parent", "start child", "update parent", "update child"
  };
  testTrue(counters, log.calls == first, "start, then update in preorder");
  testTrue(counters,
           std::fabs(scene.findNode("mover")->transform.position.x - 1.0f) <
             1e-5f,
           "a behaviour moves its node with its typed values");

  log.calls.clear();
  std::vector<SceneComponent> components = scene.findNode("child")->components;
  std::get<SceneOpaqueComponent>(components.front().value).data = R"({"a":1})";
  scene.setComponents("child", components, error);
  behaviours.update(0.1);
  testTrue(counters,
           !log.calls.empty() && log.calls.front() == "changed child",
           "new component data reports valuesChanged before the update");

  log.calls.clear();
  scene.setComponents("child", {}, error);
  behaviours.update(0.1);
  testTrue(counters,
           log.calls.front() == "stop child present" &&
             behaviours.count() == 2u,
           "removing the component stops its behaviour");

  log.calls.clear();
  scene.insertNode(behaviourNode("late", "", "test.logger"), {}, error);
  testEqSize(counters, behaviours.count(), 3u, "an inserted node attaches");
  behaviours.update(0.1);
  testTrue(counters,
           log.calls.size() >= 2u && log.calls[0] == "start late",
           "an inserted node's behaviour starts on the next update");

  log.calls.clear();
  scene.removeSubtree("parent");
  behaviours.update(0.1);
  testTrue(counters,
           log.calls.front() == "stop parent gone",
           "a removed node's behaviour stops after the node is gone");

  // Queued structural edits run after the pass.
  scene.insertNode(behaviourNode("victim", "", ""), {}, error);
  std::vector<SceneComponent> spinner = scene.findNode("mover")->components;
  std::get<SceneOpaqueComponent>(spinner.front().value).data =
    R"({"speed":0,"label":"spawn","target":"victim"})";
  scene.setComponents("mover", spinner, error);
  const size_t nodes = scene.nodeCount();
  behaviours.update(0.1);
  testTrue(counters,
           scene.findNode("victim") == nullptr && scene.nodeCount() == nodes,
           "queued creation and destruction apply after the update pass");

  log.calls.clear();
  SceneDocument replacement;
  replacement.nodes.push_back(behaviourNode("fresh", "", "test.logger"));
  scene.load(replacement, "/app", error);
  behaviours.update(0.1);
  testTrue(counters,
           log.calls.size() >= 2u && log.calls[0] == "stop late gone" &&
             log.calls[1] == "start fresh",
           "loading stops the old behaviours and starts the new ones");

  log.calls.clear();
  behaviours.detach();
  testTrue(counters,
           log.calls.size() == 1u && log.calls[0] == "stop fresh present" &&
             scene.contentObserver() == nullptr,
           "detach stops behaviours while their nodes still exist");

  // A scene destroyed before the driver stops its behaviours first.
  log.calls.clear();
  {
    SceneInstance temporary;
    temporary.load(replacement, "/app", error);
    behaviours.attach(temporary);
    behaviours.update(0.1);
  }
  testTrue(counters,
           !log.calls.empty() && log.calls.back() == "stop fresh present" &&
             behaviours.scene() == nullptr,
           "a destroyed scene stops its behaviours and releases the driver");
  behaviours.update(0.1);
  g_log = nullptr;
  return counters.failures;
}

void
registerSceneBehavioursTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Content.BehaviourSchemaParse",
               []() { return testBehaviourSchemaParse(); });
  registry.add("Illumo.Content.BehaviourValues",
               []() { return testBehaviourValues(); });
  registry.add("Illumo.Content.SceneBehavioursLifecycle",
               []() { return testSceneBehavioursLifecycle(); });
}

#include "IllEdPlatform.h"
#include "IlscCodec.h"

// Synchronous native oracle used by the workspace tests: every completion runs
// before its request returns, through the replaceable SaveLoad functions.
// Locations and labels are both the file path.
class NativeIllEdPlatform final : public IllEdPlatform
{
public:
  void chooseOpenLocation(const SaveLoadDialogSpec& specification,
                          LocationCallback done) override
  {
    const std::string path = SaveLoad::GetLoadLocation(specification);
    done({ path, path });
  }
  void chooseSaveLocation(const SaveLoadDialogSpec& specification,
                          LocationCallback done) override
  {
    std::string path = SaveLoad::GetSaveLocation(specification);
    if (!path.empty()) {
      path = IlscCodec::withIlscExtension(path);
    }
    done({ path, path });
  }
  void read(const std::string& location, ReadCallback done) override
  {
    std::string text;
    std::string error;
    const bool success = IlscCodec::readText(location, &text, &error);
    done(success, text, error);
  }
  void write(const std::string& location,
             std::string text,
             WriteCallback done) override
  {
    std::string error;
    const bool success = IlscCodec::writeText(location, text, &error);
    done(success, error);
  }
};

IllEdPlatform&
IllEdPlatform::current()
{
  static NativeIllEdPlatform platform;
  return platform;
}

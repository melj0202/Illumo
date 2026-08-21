#pragma once

#include <Illumo/Engine/Illumo.h>

class IllumoTestAccess
{
public:
  static void setWindowFactory(Illumo& host, Illumo::WindowFactory factory)
  {
    host.m_windowFactory = std::move(factory);
  }

  static void setBackendFactory(Illumo& host, Illumo::BackendFactory factory)
  {
    host.m_backendFactory = std::move(factory);
  }
};

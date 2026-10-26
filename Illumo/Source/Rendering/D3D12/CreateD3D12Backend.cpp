#include "CreateD3D12Backend.h"
#include "D3D12Device.h"
#include "Rendering/Gpu/GpuBackend.h"

std::unique_ptr<IBackend>
CreateD3D12Backend(IRenderWindow* window, bool present)
{
  return std::make_unique<GpuBackend>(
    std::make_unique<D3D12Device>(), window, present);
}

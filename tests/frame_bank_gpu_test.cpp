// Developer-only native allocation/fence/readback test. No game data, emulator
// state, replay, swapchain or visible presentation. WARP makes it reproducible.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include "../src/doax2_frame_bank.h"
using Microsoft::WRL::ComPtr;
void Require(bool v) { if (!v) throw std::runtime_error("GPU bank invariant failed"); }
void HR(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D12 operation failed: " + std::to_string(hr)); }
struct Bank {
  ComPtr<ID3D12Resource> color, depth, resolved, readback, upload;
  ComPtr<ID3D12DescriptorHeap> rtv, dsv, views;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  bool used = false;
};
ComPtr<ID3D12Resource> Resource(ID3D12Device* device, D3D12_HEAP_TYPE heap_type,
    D3D12_RESOURCE_DESC desc, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear = nullptr) {
  D3D12_HEAP_PROPERTIES heap{}; heap.Type = heap_type;
  heap.CreationNodeMask = heap.VisibleNodeMask = 1;
  ComPtr<ID3D12Resource> resource;
  HR(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, clear, IID_PPV_ARGS(&resource)));
  return resource;
}
ComPtr<ID3D12DescriptorHeap> Heap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, bool visible = false) {
  D3D12_DESCRIPTOR_HEAP_DESC desc{}; desc.Type = type; desc.NumDescriptors = 1;
  desc.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  ComPtr<ID3D12DescriptorHeap> heap; HR(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap))); return heap;
}
void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
  list->ResourceBarrier(1, &barrier);
}
int main() {
  try {
    ComPtr<IDXGIFactory4> factory; HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> adapter; HR(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device; HR(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC qdesc{}; qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue; HR(device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Fence> fence; HR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    std::array<Bank, 2> banks;
    D3D12_RESOURCE_DESC color{}; color.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    color.Width = color.Height = 64; color.DepthOrArraySize = color.MipLevels = 1;
    color.Format = DXGI_FORMAT_R8G8B8A8_UNORM; color.SampleDesc.Count = 1;
    color.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_RESOURCE_DESC depth = color; depth.Format = DXGI_FORMAT_D32_FLOAT;
    depth.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE depth_clear{}; depth_clear.Format = depth.Format; depth_clear.DepthStencil.Depth = 1;
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = 64*256; buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (auto& bank : banks) {
      bank.color = Resource(device.Get(), D3D12_HEAP_TYPE_DEFAULT, color, D3D12_RESOURCE_STATE_RENDER_TARGET);
      bank.depth = Resource(device.Get(), D3D12_HEAP_TYPE_DEFAULT, depth, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_clear);
      auto output = color; output.Flags = D3D12_RESOURCE_FLAG_NONE;
      bank.resolved = Resource(device.Get(), D3D12_HEAP_TYPE_DEFAULT, output, D3D12_RESOURCE_STATE_COPY_DEST);
      bank.readback = Resource(device.Get(), D3D12_HEAP_TYPE_READBACK, buffer, D3D12_RESOURCE_STATE_COPY_DEST);
      bank.upload = Resource(device.Get(), D3D12_HEAP_TYPE_UPLOAD, buffer, D3D12_RESOURCE_STATE_GENERIC_READ);
      bank.rtv = Heap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
      bank.dsv = Heap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
      bank.views = Heap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
      device->CreateRenderTargetView(bank.color.Get(), nullptr, bank.rtv->GetCPUDescriptorHandleForHeapStart());
      device->CreateDepthStencilView(bank.depth.Get(), nullptr, bank.dsv->GetCPUDescriptorHandleForHeapStart());
      device->CreateShaderResourceView(bank.resolved.Get(), nullptr, bank.views->GetCPUDescriptorHandleForHeapStart());
      HR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&bank.allocator)));
      HR(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, bank.allocator.Get(), nullptr, IID_PPV_ARGS(&bank.list)));
      HR(bank.list->Close());
    }
    Require(banks[0].color.Get() != banks[1].color.Get() && banks[0].resolved.Get() != banks[1].resolved.Get());
    doax2::FrameBanks<2> ownership;
    const auto submit = [&](doax2::FrameBanks<2>::Lease lease, std::array<float,4> value, uint64_t signal) {
      Require(ownership.IsRecording(lease));
      auto& bank = banks[lease.bank];
      HR(bank.allocator->Reset()); HR(bank.list->Reset(bank.allocator.Get(), nullptr));
      if (bank.used) {
        Transition(bank.list.Get(), bank.color.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        Transition(bank.list.Get(), bank.resolved.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      }
      bank.list->ClearRenderTargetView(bank.rtv->GetCPUDescriptorHandleForHeapStart(), value.data(), 0, nullptr);
      bank.list->ClearDepthStencilView(bank.dsv->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
      Transition(bank.list.Get(), bank.color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
      bank.list->CopyResource(bank.resolved.Get(), bank.color.Get());
      Transition(bank.list.Get(), bank.resolved.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
      D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = bank.resolved.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      D3D12_TEXTURE_COPY_LOCATION dest{}; dest.pResource = bank.readback.Get(); dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      dest.PlacedFootprint.Footprint = {color.Format, 64, 64, 1, 256};
      bank.list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr); HR(bank.list->Close());
      ID3D12CommandList* lists[]{bank.list.Get()}; queue->ExecuteCommandLists(1, lists);
      const HRESULT status = queue->Signal(fence.Get(), signal);
      if (FAILED(status)) { ownership.QuarantineAll(); HR(status); }
      if (!ownership.Submitted(lease, signal)) { ownership.QuarantineAll(); Require(false); }
      bank.used = true;
    };
    const auto wait = [&](uint64_t value) {
      HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr); Require(event != nullptr);
      const HRESULT hr = fence->SetEventOnCompletion(value, event);
      if (FAILED(hr)) { CloseHandle(event); ownership.QuarantineAll(); HR(hr); }
      const DWORD result = WaitForSingleObject(event, 10000); CloseHandle(event);
      if (result != WAIT_OBJECT_0 || fence->GetCompletedValue() == UINT64_MAX) { ownership.QuarantineAll(); Require(false); }
    };
    const auto verify = [&](size_t index, std::array<unsigned char,4> expected) {
      void* data = nullptr; D3D12_RANGE range{0,64*256}; HR(banks[index].readback->Map(0, &range, &data));
      bool match = true;
      for (size_t i=0; i<64*64*4; ++i) match &= static_cast<unsigned char*>(data)[i] == expected[i%4];
      D3D12_RANGE written{0,0}; banks[index].readback->Unmap(0, &written); Require(match);
    };
    const auto first = ownership.Acquire(0), second = ownership.Acquire(0); Require(first && second);
    submit(*first, {1,0,0,1}, 1); submit(*second, {0,0,1,1}, 2);
    Require(!ownership.Acquire(0)); // No reported completion: reuse must reject.
    wait(2); verify(first->bank, {255,0,0,255}); verify(second->bank, {0,0,255,255});
    const auto reused = ownership.Acquire(fence->GetCompletedValue()); Require(reused && !ownership.IsRecording(*first));
    submit(*reused, {0,1,0,1}, 3); wait(3);
    verify(reused->bank, {0,255,0,255}); verify(second->bank, {0,0,255,255});
    std::cout << "PASS: private D3D12 banks, readback isolation, fence-gated allocator reuse. No game frame/presentation tested.\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

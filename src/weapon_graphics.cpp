// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "weapon_graphics.h"

#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "cameraunlock/hooks/hook_manager.h"
#include "logging.h"
#include "weapon_shader.h"

namespace tow_ht::weapon_graphics {
namespace {
using Microsoft::WRL::ComPtr;
using cameraunlock::hooks::HookManager;
using cameraunlock::hooks::HookStatus;

struct Cache {
    std::mutex mutex;
    std::map<std::array<std::uint32_t, 5>, std::vector<std::uint32_t>> shaders;
    unsigned patched = 0;
};

Cache& Shaders() {
    // The pinned module's hooks can outlive CRT static destruction.
    static auto* cache = new Cache;
    return *cache;
}

D3D12_SHADER_BYTECODE Rewrite(const void* bytes, std::size_t size) {
    if (!size) return {bytes, size};
    if (!bytes || size < 32 || size > UINT32_MAX)
        throw std::invalid_argument("invalid shader bytecode extent");
    std::array<std::uint32_t, 5> key{};
    std::memcpy(key.data(), static_cast<const char*>(bytes) + 4, 16);
    key[4] = static_cast<std::uint32_t>(size);
    auto& cache = Shaders();
    std::lock_guard<std::mutex> lock(cache.mutex);
    auto found = cache.shaders.find(key);
    if (found == cache.shaders.end()) {
        auto replacement = weapon_shader::Rewrite(bytes, size);
        if (!replacement.empty() && ++cache.patched == 1)
            Log::Line("weapon: material camera correction active (current and previous view)");
        found = cache.shaders.emplace(key, std::move(replacement)).first;
    }
    const auto& replacement = found->second;
    return replacement.empty() ? D3D12_SHADER_BYTECODE{bytes, size} :
        D3D12_SHADER_BYTECODE{replacement.data(), replacement.size() * sizeof(std::uint32_t)};
}

using VertexFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T,
                                            ID3D11ClassLinkage*, ID3D11VertexShader**);
using GraphicsFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
using PipelineFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device2*,
    const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);
VertexFn g_vertex = nullptr;
GraphicsFn g_graphics = nullptr;
PipelineFn g_pipeline = nullptr;

HRESULT STDMETHODCALLTYPE Vertex(ID3D11Device* device, const void* bytes, SIZE_T size,
                                 ID3D11ClassLinkage* linkage, ID3D11VertexShader** result) {
    try {
        const auto shader = Rewrite(bytes, size);
        return g_vertex(device, shader.pShaderBytecode, shader.BytecodeLength, linkage, result);
    } catch (const std::exception& error) {
        Log::Line("weapon: D3D11 shader rejected: %s", error.what());
        if (result) *result = nullptr;
        return E_INVALIDARG;
    }
}

HRESULT STDMETHODCALLTYPE Graphics(ID3D12Device* device,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID iid, void** result) {
    if (!desc) return E_INVALIDARG;
    try {
        auto copy = *desc;
        copy.VS = Rewrite(desc->VS.pShaderBytecode, desc->VS.BytecodeLength);
        if (copy.VS.pShaderBytecode != desc->VS.pShaderBytecode) copy.CachedPSO = {};
        return g_graphics(device, &copy, iid, result);
    } catch (const std::exception& error) {
        Log::Line("weapon: D3D12 graphics pipeline rejected: %s", error.what());
        if (result) *result = nullptr;
        return E_INVALIDARG;
    }
}

template<class T> struct alignas(void*) Subobject {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
    T value;
};

std::size_t SubobjectSize(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type) {
    switch (type) {
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: return sizeof(Subobject<ID3D12RootSignature*>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: return sizeof(Subobject<D3D12_SHADER_BYTECODE>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: return sizeof(Subobject<D3D12_STREAM_OUTPUT_DESC>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: return sizeof(Subobject<D3D12_BLEND_DESC>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK: return sizeof(Subobject<UINT>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER: return sizeof(Subobject<D3D12_RASTERIZER_DESC>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL: return sizeof(Subobject<D3D12_DEPTH_STENCIL_DESC>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: return sizeof(Subobject<D3D12_DEPTH_STENCIL_DESC1>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: return sizeof(Subobject<D3D12_INPUT_LAYOUT_DESC>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE: return sizeof(Subobject<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY: return sizeof(Subobject<D3D12_PRIMITIVE_TOPOLOGY_TYPE>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: return sizeof(Subobject<D3D12_RT_FORMAT_ARRAY>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT: return sizeof(Subobject<DXGI_FORMAT>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC: return sizeof(Subobject<DXGI_SAMPLE_DESC>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO: return sizeof(Subobject<D3D12_CACHED_PIPELINE_STATE>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS: return sizeof(Subobject<D3D12_PIPELINE_STATE_FLAGS>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: return sizeof(Subobject<D3D12_VIEW_INSTANCING_DESC>);
        default: throw std::invalid_argument("unsupported D3D12 pipeline subobject " + std::to_string(type));
    }
}

HRESULT STDMETHODCALLTYPE Pipeline(ID3D12Device2* device,
    const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID iid, void** result) {
    if (!desc || !desc->pPipelineStateSubobjectStream || desc->SizeInBytes % sizeof(void*))
        return E_INVALIDARG;
    try {
        std::vector<std::uintptr_t> storage(desc->SizeInBytes / sizeof(void*));
        std::memcpy(storage.data(), desc->pPipelineStateSubobjectStream, desc->SizeInBytes);
        auto* bytes = reinterpret_cast<std::uint8_t*>(storage.data());
        D3D12_CACHED_PIPELINE_STATE* cached = nullptr;
        bool changed = false;
        for (std::size_t at = 0; at < desc->SizeInBytes;) {
            if (desc->SizeInBytes - at < sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE))
                throw std::invalid_argument("truncated D3D12 pipeline subobject type");
            const auto type = *reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(bytes + at);
            const auto length = SubobjectSize(type);
            if (length > desc->SizeInBytes - at)
                throw std::invalid_argument("truncated D3D12 pipeline subobject");
            if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS) {
                auto& vertex = reinterpret_cast<Subobject<D3D12_SHADER_BYTECODE>*>(bytes + at)->value;
                const auto rewritten = Rewrite(vertex.pShaderBytecode, vertex.BytecodeLength);
                changed |= rewritten.pShaderBytecode != vertex.pShaderBytecode;
                vertex = rewritten;
            }
            if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO)
                cached = &reinterpret_cast<Subobject<D3D12_CACHED_PIPELINE_STATE>*>(bytes + at)->value;
            at += length;
        }
        if (changed && cached) *cached = {};
        auto copy = *desc;
        copy.pPipelineStateSubobjectStream = storage.data();
        return g_pipeline(device, &copy, iid, result);
    } catch (const std::exception& error) {
        Log::Line("weapon: D3D12 pipeline stream rejected: %s", error.what());
        if (result) *result = nullptr;
        return E_INVALIDARG;
    }
}

void Hook(void* target, void* detour, void** original) {
    auto& manager = HookManager::Instance();
    auto status = manager.CreateHook(target, detour, original);
    if (status == HookStatus::Ok) status = manager.EnableHook(target);
    if (status != HookStatus::Ok)
        throw std::runtime_error(cameraunlock::hooks::HookStatusToString(status));
}

FARPROC SystemExport(const wchar_t* library, const char* symbol) {
    const auto module = LoadLibraryExW(library, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) throw std::runtime_error("LoadLibraryEx failed: " + std::to_string(GetLastError()));
    const auto function = GetProcAddress(module, symbol);
    if (!function) throw std::runtime_error(std::string(symbol) + ": " + std::to_string(GetLastError()));
    return function;
}
}

bool Install() {
    try {
        const auto create11 = reinterpret_cast<decltype(&D3D11CreateDevice)>(
            SystemExport(L"d3d11.dll", "D3D11CreateDevice"));
        ComPtr<ID3D11Device> device11;
        const auto hr11 = create11(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &device11, nullptr, nullptr);
        if (FAILED(hr11)) throw std::runtime_error("D3D11CreateDevice: " + std::to_string(hr11));
        const auto table11 = *reinterpret_cast<void***>(device11.Get());
        Hook(table11[12], reinterpret_cast<void*>(&Vertex), reinterpret_cast<void**>(&g_vertex));

        const std::wstring commandLine = GetCommandLineW();
        if (commandLine.find(L"-dx11") == std::wstring::npos &&
            commandLine.find(L"-d3d11") == std::wstring::npos) {
            const auto create12 = reinterpret_cast<decltype(&D3D12CreateDevice)>(
                SystemExport(L"d3d12.dll", "D3D12CreateDevice"));
            ComPtr<ID3D12Device2> device12;
            const auto hr12 = create12(nullptr, D3D_FEATURE_LEVEL_11_0,
                __uuidof(ID3D12Device2), reinterpret_cast<void**>(device12.GetAddressOf()));
            if (FAILED(hr12)) throw std::runtime_error("D3D12CreateDevice: " + std::to_string(hr12));
            const auto table12 = *reinterpret_cast<void***>(device12.Get());
            Hook(table12[10], reinterpret_cast<void*>(&Graphics), reinterpret_cast<void**>(&g_graphics));
            Hook(table12[47], reinterpret_cast<void*>(&Pipeline), reinterpret_cast<void**>(&g_pipeline));
        }
        Log::Line("weapon: graphics hooks installed");
        return true;
    } catch (const std::exception& error) {
        Log::Line("FATAL: weapon graphics hook installation failed: %s", error.what());
        return false;
    }
}

} // namespace tow_ht::weapon_graphics

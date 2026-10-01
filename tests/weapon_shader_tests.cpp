// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "weapon_shader.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
using Vector = std::array<float, 4>;

namespace {
void Check(bool value, const char* diagnostic) {
    if (!value) throw std::runtime_error(diagnostic);
}
void HResult(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}
ComPtr<ID3DBlob> Compile(const std::string& source, const char* profile) {
    ComPtr<ID3DBlob> code, errors;
    const auto result = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
        "main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(result) && errors)
        throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    HResult(result, "D3DCompile");
    return code;
}

class Device {
public:
    Device() {
        HResult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context), "D3D11CreateDevice");
        const auto code = Compile(
            "struct V { float4 p:SV_Position; }; [maxvertexcount(1)] "
            "void main(point V v[1], inout PointStream<V> output) { output.Append(v[0]); }",
            "gs_5_0");
        const D3D11_SO_DECLARATION_ENTRY entry{0, "SV_Position", 0, 0, 4, 0};
        const UINT stride = sizeof(Vector);
        HResult(device->CreateGeometryShaderWithStreamOutput(code->GetBufferPointer(),
            code->GetBufferSize(), &entry, 1, &stride, 1,
            D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &geometry), "CreateGeometryShaderWithStreamOutput");
        output = Buffer(sizeof(Vector), D3D11_BIND_STREAM_OUTPUT, nullptr);
        D3D11_BUFFER_DESC readDesc{};
        readDesc.ByteWidth = sizeof(Vector);
        readDesc.Usage = D3D11_USAGE_STAGING;
        readDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        HResult(device->CreateBuffer(&readDesc, nullptr, &readback), "CreateBuffer readback");
        const std::uint32_t id = 0;
        vertex = Buffer(sizeof(id), D3D11_BIND_VERTEX_BUFFER, &id);
    }

    ComPtr<ID3D11Buffer> Buffer(UINT size, UINT flags, const void* bytes,
                              UINT misc = 0, UINT stride = 0) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = size;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = flags;
        desc.MiscFlags = misc;
        desc.StructureByteStride = stride;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = bytes;
        ComPtr<ID3D11Buffer> result;
        HResult(device->CreateBuffer(&desc, bytes ? &data : nullptr, &result), "CreateBuffer");
        return result;
    }

    Vector Execute(const void* code, std::size_t size,
                   const std::array<Vector, 147>& view,
                   const std::array<Vector, 37>& primitive, bool gpuScene) {
        ComPtr<ID3D11VertexShader> shader;
        HResult(device->CreateVertexShader(code, size, nullptr, &shader), "CreateVertexShader");
        const auto viewBuffer = Buffer(sizeof(view), D3D11_BIND_CONSTANT_BUFFER, view.data());
        const auto primitiveBuffer = Buffer(sizeof(primitive),
            gpuScene ? D3D11_BIND_SHADER_RESOURCE : D3D11_BIND_CONSTANT_BUFFER, primitive.data(),
            gpuScene ? D3D11_RESOURCE_MISC_BUFFER_STRUCTURED : 0, gpuScene ? sizeof(Vector) : 0);
        ComPtr<ID3D11ShaderResourceView> resource;
        ComPtr<ID3D11InputLayout> layout;
        if (gpuScene) {
            HResult(device->CreateShaderResourceView(primitiveBuffer.Get(), nullptr, &resource),
                    "CreateShaderResourceView");
            ID3D11ShaderResourceView* resources[] = {resource.Get()};
            context->VSSetShaderResources(0, 1, resources);
            const D3D11_INPUT_ELEMENT_DESC element{
                "ATTRIBUTE", 13, DXGI_FORMAT_R32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
            HResult(device->CreateInputLayout(&element, 1, code, size, &layout), "CreateInputLayout");
        }
        ID3D11Buffer* constants[] = {viewBuffer.Get(), gpuScene ? nullptr : primitiveBuffer.Get()};
        context->VSSetConstantBuffers(0, 2, constants);
        context->VSSetShader(shader.Get(), nullptr, 0);
        context->GSSetShader(geometry.Get(), nullptr, 0);
        context->IASetInputLayout(layout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        ID3D11Buffer* vertices[] = {vertex.Get()};
        const UINT stride = 4, offset = 0;
        context->IASetVertexBuffers(0, 1, vertices, &stride, &offset);
        ID3D11Buffer* targets[] = {output.Get()};
        context->SOSetTargets(1, targets, &offset);
        context->Draw(1, 0);
        context->SOSetTargets(0, nullptr, nullptr);
        context->CopyResource(readback.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HResult(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map stream output");
        Vector result;
        std::memcpy(result.data(), mapped.pData, sizeof(result));
        context->Unmap(readback.Get(), 0);
        context->ClearState();
        return result;
    }
private:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11GeometryShader> geometry;
    ComPtr<ID3D11Buffer> output, readback, vertex;
};

void Equal(const Vector& actual, const Vector& expected) {
    for (unsigned i = 0; i < 4; ++i) {
        if (std::fabs(actual[i] - expected[i]) < 0.001f) continue;
        std::cerr << "component " << i << ": got " << actual[i] << ", expected " << expected[i] << '\n';
        throw std::runtime_error("shader output differs");
    }
}
}

int main() {
    Device device;
    std::array<Vector, 147> view{};
    std::array<Vector, 37> primitive{};
    for (unsigned i = 0; i < view.size(); ++i)
        view[i] = {float(i), float(i + 100), float(i + 200), float(i + 300)};
    for (unsigned i = 0; i < primitive.size(); ++i)
        primitive[i] = {float(i + 400), float(i + 500), float(i + 600), float(i + 700)};
    const std::array<unsigned, 12> rows{16, 17, 18, 20, 21, 22, 99, 100, 101, 103, 104, 105};
    for (const bool gpuScene : {false, true}) {
        std::string source = "cbuffer View:register(b0){float4 v[147];};";
        source += gpuScene ? "StructuredBuffer<float4> p:register(t0);" :
                             "cbuffer Primitive:register(b1){float4 p[37];};";
        source += gpuScene ? "float4 main(uint id:ATTRIBUTE13):SV_Position {float4 r=p[id*37+36];" :
                             "float4 main():SV_Position {float4 r=p[36];";
        for (const auto row : rows) source += "r+=v[" + std::to_string(row) + "];";
        source += "return r+v[146];}";
        const auto original = Compile(source, "vs_5_0");
        const auto patched = tow_ht::weapon_shader::Rewrite(original->GetBufferPointer(), original->GetBufferSize());
        Check(!patched.empty(), "test shader was not matched");
        for (const std::uint32_t marker : {0u, 0x3f800000u, tow_ht::weapon_shader::kPrimitiveMarker}) {
            std::memcpy(&primitive[35][3], &marker, sizeof(marker));
            Vector expected{};
            for (unsigned component = 0; component < 4; ++component) {
                expected[component] = primitive[36][component] + view[146][component];
                for (unsigned i = 0; i < rows.size(); ++i) {
                    float value = view[rows[i]][component];
                    if (marker == tow_ht::weapon_shader::kPrimitiveMarker && component < 3)
                        value = i < 9 ? primitive[27 + i][component] : primitive[33 + component][i - 9];
                    expected[component] += value;
                }
            }
            Equal(device.Execute(patched.data(), patched.size() * 4, view, primitive, gpuScene), expected);
            if (marker != tow_ht::weapon_shader::kPrimitiveMarker)
                Equal(device.Execute(original->GetBufferPointer(), original->GetBufferSize(),
                                     view, primitive, gpuScene), expected);
        }
    }
    const auto ordinary = Compile("float4 main():SV_Position{return float4(1,2,3,4);}", "vs_5_0");
    Check(tow_ht::weapon_shader::Rewrite(ordinary->GetBufferPointer(), ordinary->GetBufferSize()).empty(),
          "ordinary vertex shader was changed");
    const auto pixel = Compile("float4 main():SV_Target{return float4(1,2,3,4);}", "ps_5_0");
    Check(tow_ht::weapon_shader::Rewrite(pixel->GetBufferPointer(), pixel->GetBufferSize()).empty(),
          "pixel shader was changed");
    std::cout << "Weapon shader WARP checks passed: both primitive paths, current and previous rotations, marker isolation\n";
}

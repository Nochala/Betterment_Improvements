#include "WaterReflections.h"
#include "../../Headers/Logger.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <limits>

namespace
{
    struct ShaderModuleCreateInfo
    {
        std::uint32_t sType;
        const void* pNext;
        std::uint32_t flags;
        std::size_t codeSize;
        const std::uint32_t* pCode;
    };

    using GetProcAddressFunction = FARPROC(WINAPI*)(HMODULE, LPCSTR);
    using GetInstanceProcAddressFunction = FARPROC(WINAPI*)(void*, const char*);
    using GetDeviceProcAddressFunction = FARPROC(WINAPI*)(void*, const char*);
    using CreateShaderModuleFunction = std::int32_t(WINAPI*)(void*, const ShaderModuleCreateInfo*, const void*, std::uint64_t*);

    std::atomic<GetProcAddressFunction> originalGetProcAddress{ nullptr };
    std::atomic<GetInstanceProcAddressFunction> originalGetInstanceProcAddress{ nullptr };
    std::atomic<GetDeviceProcAddressFunction> originalGetDeviceProcAddress{ nullptr };
    std::atomic<CreateShaderModuleFunction> originalCreateShaderModule{ nullptr };
    std::atomic<void* volatile*> importSlot{ nullptr };
    std::atomic<bool> overscanCropEnabled{ false };
    std::atomic<int> overscanCropTestMode{ 0 };
    std::atomic<unsigned int> presentationShaderMatches{ 0 };
    std::atomic<unsigned int> presentationShaderPatches{ 0 };
    std::atomic<bool> reportedPresentationPatch{ false };
    std::atomic<bool> reportedPresentationFailure{ false };
    std::atomic<unsigned int> transitionShaderMatches{ 0 };
    std::atomic<unsigned int> transitionShaderPatches{ 0 };
    std::atomic<bool> reportedTransitionPatch{ false };
    std::atomic<bool> reportedTransitionFailure{ false };

    float overscanScale = 1.25f;
    float cameraFovOffset = 0.0f;
    float cameraFovScale = 1.0f;
    bool overscanFovEnabled = false;
    bool ssrCoverageEnabled = false;

    constexpr unsigned int maxFovProbeSamples = 48;
    struct FovProbeSample
    {
        std::atomic<bool> ready{ false };
        ULONGLONG elapsedMs = 0;
        float original = 0.0f;
        float expanded = 0.0f;
        std::uintptr_t camera = 0;
        std::uintptr_t context = 0;
    };
    FovProbeSample fovProbeSamples[maxFovProbeSamples];
    std::atomic<bool> fovProbeActive{ false };
    std::atomic<unsigned int> fovProbeCalls{ 0 };
    std::atomic<unsigned int> fovProbeCount{ 0 };
    std::atomic<int> fovProbeLastBucket{ -999999 };
    std::atomic<ULONGLONG> fovProbeLastCapture{ 0 };
    ULONGLONG fovProbeStart = 0;
    ULONGLONG fovProbeLastPoll = 0;
    ULONGLONG fovProbeNextStatus = 10000;
    unsigned int fovProbeReported = 0;

    void CaptureFovProbe(float original, float expanded, std::uintptr_t camera, std::uintptr_t context)
    {
        if (!fovProbeActive.load(std::memory_order_acquire))
            return;
        fovProbeCalls.fetch_add(1, std::memory_order_relaxed);
        if (!std::isfinite(original) || !std::isfinite(expanded) ||
            original < 1.0f || original > 170.0f ||
            fovProbeCount.load(std::memory_order_relaxed) >= maxFovProbeSamples)
            return;
        const int bucket = static_cast<int>(original / 2.0f);
        if (bucket == fovProbeLastBucket.load(std::memory_order_relaxed))
            return;
        const ULONGLONG now = GetTickCount64();
        ULONGLONG previous = fovProbeLastCapture.load(std::memory_order_relaxed);
        if (now - previous < 200 ||
            !fovProbeLastCapture.compare_exchange_strong(previous, now, std::memory_order_acq_rel))
            return;
        fovProbeLastBucket.store(bucket, std::memory_order_relaxed);
        const unsigned int slot = fovProbeCount.fetch_add(1, std::memory_order_acq_rel);
        if (slot >= maxFovProbeSamples)
            return;
        FovProbeSample& sample = fovProbeSamples[slot];
        sample.elapsedMs = now - fovProbeStart;
        sample.original = original;
        sample.expanded = expanded;
        sample.camera = camera;
        sample.context = context;
        sample.ready.store(true, std::memory_order_release);
    }

    bool IsReadableProjectionRange(const void* address, std::size_t bytes)
    {
        MEMORY_BASIC_INFORMATION region = {};
        if (!VirtualQuery(address, &region, sizeof(region)) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD protection = region.Protect & 0xff;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY &&
            protection != PAGE_READONLY && protection != PAGE_EXECUTE_READ)
            return false;
        const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(address);
        const std::uintptr_t regionEnd = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        return regionEnd >= start && bytes <= regionEnd - start;
    }

    std::uint64_t HashShader(const std::uint8_t* data, std::size_t length)
    {
        std::uint64_t value = 0xCBF29CE484222325ULL;
        for (std::size_t index = 0; index < length; ++index)
        {
            value ^= data[index];
            value *= 0x100000001B3ULL;
        }
        return value;
    }

    struct ShaderInstruction
    {
        std::size_t index = 0;
        std::uint32_t opcode = 0;
        std::uint32_t length = 0;
    };

    bool PatchPresentationShader(const ShaderModuleCreateInfo* info, float scale,
        std::vector<std::uint32_t>& patched)
    {
        if (!info || info->sType != 16 || !info->pCode ||
            info->codeSize < 96 || info->codeSize > 8192 ||
            (info->codeSize & 3) != 0 || info->pCode[0] != 0x07230203 ||
            !std::isfinite(scale) || (scale != -1.0f && (scale < 1.0f || scale > 2.0f)))
            return false;

        const std::uint32_t* words = info->pCode;
        const std::size_t count = info->codeSize / sizeof(std::uint32_t);
        const std::uint32_t bound = words[3];
        if (bound < 16 || bound > 4096 || words[4] != 0)
            return false;

        struct Variable
        {
            std::uint32_t binding = (std::numeric_limits<std::uint32_t>::max)();
            std::uint32_t set = (std::numeric_limits<std::uint32_t>::max)();
            std::uint32_t location = (std::numeric_limits<std::uint32_t>::max)();
            std::uint32_t storage = (std::numeric_limits<std::uint32_t>::max)();
        };
        std::vector<Variable> variables(bound);
        std::uint32_t floatType = 0;
        std::uint32_t vec2Type = 0;
        std::uint32_t input = 0;
        std::uint32_t output = 0;
        std::uint32_t texture = 0;
        std::uint32_t sampler = 0;
        std::uint32_t sampledImage = 0;
        std::uint32_t coordinate = 0;
        std::uint32_t color = 0;
        std::uint32_t sampledImageCount = 0;
        std::uint32_t sampleCount = 0;
        std::uint32_t storeCount = 0;
        std::uint32_t entryPoints = 0;
        std::uint32_t functions = 0;
        std::uint32_t bindings = 0;
        std::size_t functionStart = 0;
        std::size_t sampleStart = 0;
        bool fragment = false;

        for (std::size_t index = 5; index < count;)
        {
            const std::uint32_t opcode = words[index] & 0xFFFF;
            const std::size_t length = words[index] >> 16;
            if (!length || length > count - index)
                return false;
            if (opcode == 15)
            {
                ++entryPoints;
                fragment = length >= 4 && words[index + 1] == 4;
            }
            else if (opcode == 22 && length == 3 && words[index + 2] == 32)
                floatType = words[index + 1];
            else if (opcode == 23 && length == 4 && words[index + 3] == 2 &&
                words[index + 2] == floatType)
                vec2Type = words[index + 1];
            else if (opcode == 71 && length == 4 && words[index + 1] < bound)
            {
                Variable& variable = variables[static_cast<std::size_t>(words[index + 1])];
                if (words[index + 2] == 33)
                {
                    variable.binding = words[index + 3];
                    ++bindings;
                }
                else if (words[index + 2] == 34)
                    variable.set = words[index + 3];
                else if (words[index + 2] == 30)
                    variable.location = words[index + 3];
            }
            else if (opcode == 59 && length >= 4 && words[index + 2] < bound)
                variables[static_cast<std::size_t>(words[index + 2])].storage = words[index + 3];
            else if (opcode == 54)
            {
                ++functions;
                if (!functionStart)
                    functionStart = index;
            }
            index += length;
        }

        if (entryPoints != 1 || !fragment || functions != 1 || !functionStart ||
            !floatType || !vec2Type || floatType >= bound || vec2Type >= bound ||
            bindings != 2)
            return false;

        for (std::uint32_t id = 1; id < bound; ++id)
        {
            const Variable& variable = variables[id];
            if (variable.storage == 1 && variable.location == 0)
            {
                if (input)
                    return false;
                input = id;
            }
            else if (variable.storage == 3 && variable.location == 0)
            {
                if (output)
                    return false;
                output = id;
            }
            else if (variable.storage == 0 && variable.set == 1 && variable.binding == 186)
            {
                if (texture)
                    return false;
                texture = id;
            }
            else if (variable.storage == 0 && variable.set == 1 && variable.binding == 40)
            {
                if (sampler)
                    return false;
                sampler = id;
            }
        }
        if (!input || !output || !texture || !sampler)
            return false;

        std::uint32_t uvLoad = 0;
        std::uint32_t texLoad = 0;
        std::uint32_t samplerLoad = 0;
        for (std::size_t index = functionStart; index < count;)
        {
            const std::uint32_t opcode = words[index] & 0xFFFF;
            const std::size_t length = words[index] >> 16;
            if (!length || length > count - index)
                return false;
            if (opcode == 61 && length >= 4 && words[index + 2] < bound)
            {
                const std::uint32_t source = words[index + 3];
                if (source == input && words[index + 1] == vec2Type)
                {
                    if (uvLoad)
                        return false;
                    uvLoad = words[index + 2];
                }
                else if (source == texture)
                    texLoad = words[index + 2];
                else if (source == sampler)
                    samplerLoad = words[index + 2];
            }
            else if (opcode == 86 && length == 5)
            {
                ++sampledImageCount;
                if (words[index + 3] == texLoad && words[index + 4] == samplerLoad &&
                    texLoad && samplerLoad)
                    sampledImage = words[index + 2];
            }
            else if (opcode == 87 && length >= 5)
            {
                ++sampleCount;
                if (words[index + 3] == sampledImage && sampledImage &&
                    words[index + 4] == uvLoad && uvLoad)
                {
                    sampleStart = index;
                    coordinate = words[index + 4];
                    color = words[index + 2];
                }
            }
            else if (opcode == 62 && length == 3 && words[index + 1] == output)
            {
                ++storeCount;
                if (words[index + 2] != color || !color)
                    return false;
            }
            else if (opcode != 54 && opcode != 56 && opcode != 248 &&
                opcode != 61 && opcode != 86 && opcode != 87 &&
                opcode != 62 && opcode != 253 && opcode != 0 &&
                opcode != 8 && opcode != 317)
                return false;
            index += length;
        }

        if (sampledImageCount != 1 || sampleCount != 1 || storeCount != 1 ||
            !sampleStart || !coordinate || !color || sampleStart <= functionStart ||
            bound > (std::numeric_limits<std::uint32_t>::max)() - 6)
            return false;

        const std::uint32_t half = bound;
        const std::uint32_t inverse = bound + 1;
        const std::uint32_t center = bound + 2;
        const std::uint32_t shifted = bound + 3;
        const std::uint32_t scaled = bound + 4;
        const std::uint32_t result = bound + 5;
        const float reciprocal = 1.0f / scale;
        std::uint32_t inverseBits = 0;
        std::memcpy(&inverseBits, &reciprocal, sizeof(inverseBits));
        const std::uint32_t constants[] =
        {
            (4u << 16) | 43u, floatType, half, 0x3F000000,
            (4u << 16) | 43u, floatType, inverse, inverseBits,
            (5u << 16) | 44u, vec2Type, center, half, half
        };
        const std::uint32_t operations[] =
        {
            (5u << 16) | 131u, vec2Type, shifted, coordinate, center,
            (5u << 16) | 142u, vec2Type, scaled, shifted, inverse,
            (5u << 16) | 129u, vec2Type, result, scaled, center
        };

        patched.clear();
        const std::size_t constantWords = sizeof(constants) / sizeof(constants[0]);
        const std::size_t operationWords = sizeof(operations) / sizeof(operations[0]);
        patched.reserve(count + constantWords + operationWords);
        patched.insert(patched.end(), words, words + functionStart);
        patched.insert(patched.end(), constants, constants + constantWords);
        patched.insert(patched.end(), words + functionStart, words + sampleStart);
        patched.insert(patched.end(), operations, operations + operationWords);
        patched.insert(patched.end(), words + sampleStart, words + count);
        const std::size_t replacedSample = sampleStart + constantWords + operationWords;
        patched[replacedSample + 4] = result;
        patched[3] = bound + 6;
        return true;
    }

    bool PatchCameraTransitionShader(const ShaderModuleCreateInfo* info,
        std::vector<std::uint32_t>& patched)
    {
        if (!info || info->sType != 16 || !info->pCode ||
            info->codeSize < 1024 || info->codeSize > 64 * 1024 ||
            (info->codeSize & 3) != 0 || info->pCode[0] != 0x07230203)
            return false;

        const std::uint32_t* words = info->pCode;
        const std::size_t count = info->codeSize / sizeof(std::uint32_t);
        const std::uint32_t bound = words[3];
        if (bound < 32 || bound > 4096 || words[4] != 0)
            return false;

        struct Variable
        {
            std::uint32_t binding = 0xFFFFFFFF;
            std::uint32_t set = 0xFFFFFFFF;
            std::uint32_t location = 0xFFFFFFFF;
            std::uint32_t builtIn = 0xFFFFFFFF;
            std::uint32_t storage = 0xFFFFFFFF;
            std::uint32_t pointerType = 0;
        };
        std::vector<Variable> variables(bound);
        std::vector<ShaderInstruction> definitions(bound);
        std::vector<std::uint32_t> pointeeTypes(bound);
        std::vector<std::uint32_t> imageDimensions(bound, 0xFFFFFFFF);
        std::vector<bool> samplerTypes(bound);
        std::uint32_t floatType = 0;
        std::uint32_t vec2Type = 0;
        std::uint32_t vec4Type = 0;
        std::uint32_t glslSet = 0;
        unsigned int entryPoints = 0;
        unsigned int functions = 0;
        unsigned int descriptors = 0;
        std::size_t firstFunction = 0;

        for (std::size_t index = 5; index < count;)
        {
            const std::size_t length = words[index] >> 16;
            const std::uint32_t opcode = words[index] & 0xFFFF;
            if (!length || length > count - index)
                return false;
            if (opcode == 15)
            {
                ++entryPoints;
                if (length < 5 || words[index + 1] != 4 ||
                    std::memcmp(words + index + 3, "VkMain", 7) != 0)
                    return false;
            }
            else if (opcode == 11 && length >= 6 &&
                std::memcmp(words + index + 2, "GLSL.std.450", 13) == 0)
                glslSet = words[index + 1];
            else if (opcode == 22 && length == 3 && words[index + 2] == 32)
                floatType = words[index + 1];
            else if (opcode == 23 && length == 4 && words[index + 2] == floatType)
            {
                if (words[index + 3] == 2)
                    vec2Type = words[index + 1];
                else if (words[index + 3] == 4)
                    vec4Type = words[index + 1];
            }
            else if (opcode == 25 && length >= 9 && words[index + 1] < bound)
                imageDimensions[static_cast<std::size_t>(words[index + 1])] = words[index + 3];
            else if (opcode == 26 && length == 2 && words[index + 1] < bound)
                samplerTypes[static_cast<std::size_t>(words[index + 1])] = true;
            else if (opcode == 32 && length == 4 && words[index + 1] < bound)
                pointeeTypes[static_cast<std::size_t>(words[index + 1])] = words[index + 3];
            else if (opcode == 71 && length == 4 && words[index + 1] < bound)
            {
                Variable& variable = variables[static_cast<std::size_t>(words[index + 1])];
                if (words[index + 2] == 33)
                {
                    variable.binding = words[index + 3];
                    ++descriptors;
                }
                else if (words[index + 2] == 34)
                    variable.set = words[index + 3];
                else if (words[index + 2] == 30)
                    variable.location = words[index + 3];
                else if (words[index + 2] == 11)
                    variable.builtIn = words[index + 3];
            }
            else if (opcode == 59 && length >= 4 && words[index + 2] < bound)
            {
                Variable& variable = variables[static_cast<std::size_t>(words[index + 2])];
                variable.pointerType = words[index + 1];
                variable.storage = words[index + 3];
            }
            else if (opcode == 54)
            {
                ++functions;
                if (!firstFunction)
                    firstFunction = index;
            }

            if ((opcode == 43 || opcode == 46 || opcode == 61 || opcode == 81 || opcode == 82 ||
                opcode == 86 || opcode == 87 || opcode == 88 || opcode == 12) &&
                length >= 4 && words[index + 2] > 0 && words[index + 2] < bound)
                definitions[static_cast<std::size_t>(words[index + 2])] = { index, opcode, static_cast<std::uint32_t>(length) };
            index += length;
        }

        if (entryPoints != 1 || functions != 1 || !firstFunction ||
            !floatType || !vec2Type || !vec4Type || !glslSet || descriptors != 7 ||
            bound == 0xFFFFFFFF)
            return false;

        const std::uint32_t expectedSets[7] = { 1, 0, 0, 1, 1, 1, 0 };
        const std::uint32_t expectedBindings[7] = { 34, 96, 23, 22, 37, 128, 131 };
        std::uint32_t resources[7] = {};
        std::uint32_t input0 = 0;
        std::uint32_t input1 = 0;
        std::uint32_t input2 = 0;
        std::uint32_t output = 0;
        unsigned int fragCoord = 0;
        unsigned int inputs = 0;
        unsigned int outputs = 0;
        for (std::uint32_t id = 1; id < bound; ++id)
        {
            const Variable& variable = variables[id];
            if (variable.storage == 0 || variable.storage == 2)
            {
                if (variable.binding == 0xFFFFFFFF || variable.set == 0xFFFFFFFF)
                    continue;
                bool matched = false;
                for (unsigned int resource = 0; resource < 7; ++resource)
                {
                    if (variable.set != expectedSets[resource] ||
                        variable.binding != expectedBindings[resource])
                        continue;
                    if (resources[resource] || variable.storage != (resource == 1 ||
                        resource == 2 || resource == 3 ? 2u : 0u))
                        return false;
                    resources[resource] = id;
                    matched = true;
                    break;
                }
                if (!matched)
                    return false;
            }
            else if (variable.storage == 1)
            {
                ++inputs;
                if (variable.builtIn == 15)
                    ++fragCoord;
                else if (variable.location == 0)
                    input0 = id;
                else if (variable.location == 1)
                    input1 = id;
                else if (variable.location == 2)
                    input2 = id;
                else
                    return false;
            }
            else if (variable.storage == 3)
            {
                ++outputs;
                if (variable.location != 0 || output)
                    return false;
                output = id;
            }
        }
        if (inputs != 4 || outputs != 1 || fragCoord != 1 ||
            !input0 || !input1 || !input2 || !output)
            return false;
        for (std::uint32_t resource : resources)
        {
            if (!resource)
                return false;
        }

        const auto pointee = [&](std::uint32_t id) -> std::uint32_t
        {
            if (!id || id >= bound || variables[id].pointerType >= bound)
                return 0;
            return pointeeTypes[variables[id].pointerType];
        };
        if (pointee(input0) != vec4Type || pointee(input1) != vec4Type ||
            pointee(input2) != vec2Type || pointee(output) != vec4Type ||
            !pointee(resources[5]) || pointee(resources[5]) >= bound ||
            imageDimensions[pointee(resources[5])] != 1 ||
            !pointee(resources[6]) || pointee(resources[6]) >= bound ||
            imageDimensions[pointee(resources[6])] != 2 ||
            !pointee(resources[0]) || pointee(resources[0]) >= bound ||
            !samplerTypes[pointee(resources[0])] ||
            !pointee(resources[4]) || pointee(resources[4]) >= bound ||
            !samplerTypes[pointee(resources[4])])
            return false;

        const auto definition = [&](std::uint32_t id) -> const ShaderInstruction*
        {
            if (!id || id >= bound || !definitions[id].index)
                return nullptr;
            return &definitions[id];
        };
        const auto loadOf = [&](std::uint32_t id, std::uint32_t variable) -> bool
        {
            const ShaderInstruction* item = definition(id);
            return item && item->opcode == 61 && item->length == 4 &&
                words[item->index + 3] == variable;
        };
        const auto floatConstant = [&](std::uint32_t id, std::uint32_t bits) -> bool
        {
            const ShaderInstruction* item = definition(id);
            return item && ((item->opcode == 43 && item->length == 4 &&
                words[item->index + 1] == floatType && words[item->index + 3] == bits) ||
                (bits == 0 && item->opcode == 46 && item->length == 3 &&
                    words[item->index + 1] == floatType));
        };

        std::uint32_t sampled2D = 0;
        std::uint32_t sampled3D = 0;
        unsigned int sampled2DCount = 0;
        unsigned int sampled3DCount = 0;
        unsigned int sample2DCount = 0;
        unsigned int sample3DCount = 0;
        unsigned int outputStores = 0;
        std::size_t outputStore = 0;
        for (std::size_t index = firstFunction; index < count;)
        {
            const std::size_t length = words[index] >> 16;
            const std::uint32_t opcode = words[index] & 0xFFFF;
            if (opcode == 86 && length == 5)
            {
                if (loadOf(words[index + 3], resources[5]) &&
                    loadOf(words[index + 4], resources[4]))
                {
                    sampled2D = words[index + 2];
                    ++sampled2DCount;
                }
                if (loadOf(words[index + 3], resources[6]) &&
                    loadOf(words[index + 4], resources[0]))
                {
                    sampled3D = words[index + 2];
                    ++sampled3DCount;
                }
            }
            else if (opcode == 87 && length >= 5 && sampled2D &&
                words[index + 3] == sampled2D && loadOf(words[index + 4], input2))
                ++sample2DCount;
            else if (opcode == 88 && length >= 6 && sampled3D &&
                words[index + 3] == sampled3D)
                ++sample3DCount;
            else if (opcode == 62 && length == 3 && words[index + 1] == output)
            {
                ++outputStores;
                outputStore = index;
            }
            index += length;
        }
        if (sampled2DCount != 1 || sampled3DCount != 1 ||
            sample2DCount != 1 || sample3DCount != 1 || outputStores != 1)
            return false;

        const ShaderInstruction* insertion = definition(words[outputStore + 2]);
        if (!insertion || insertion->opcode != 82 || insertion->length != 6 ||
            words[insertion->index + 1] != vec4Type || words[insertion->index + 5] != 3)
            return false;
        const ShaderInstruction* clamp = definition(words[insertion->index + 3]);
        if (!clamp || clamp->opcode != 12 || clamp->length != 8 ||
            words[clamp->index + 1] != floatType ||
            words[clamp->index + 3] != glslSet || words[clamp->index + 4] != 43 ||
            !floatConstant(words[clamp->index + 6], 0) ||
            !floatConstant(words[clamp->index + 7], 0x3F800000))
            return false;
        const ShaderInstruction* alpha = definition(words[clamp->index + 5]);
        if (!alpha || alpha->opcode != 81 || alpha->length != 5 ||
            words[alpha->index + 1] != floatType ||
            words[alpha->index + 3] != words[insertion->index + 4] ||
            words[alpha->index + 4] != 3)
            return false;

        patched.assign(words, words + count);
        patched.insert(patched.begin() + firstFunction, { 0x0003002E, vec4Type, bound });
        patched[outputStore + 5] = bound;
        patched[3] = bound + 1;
        return true;
    }

    std::int32_t WINAPI InterceptCreateShaderModule(void* device, const ShaderModuleCreateInfo* info,
        const void* allocator, std::uint64_t* module)
    {
        CreateShaderModuleFunction original = nullptr;
        const auto getDeviceProcAddress = originalGetDeviceProcAddress.load();
        if (getDeviceProcAddress)
            original = reinterpret_cast<CreateShaderModuleFunction>(getDeviceProcAddress(device, "vkCreateShaderModule"));
        if (!original)
            original = originalCreateShaderModule.load();
        if (!original)
            return -3;

        if (overscanCropEnabled.load(std::memory_order_acquire))
        {
            const int cropMode = overscanCropTestMode.load(std::memory_order_relaxed);
            const bool candidateMatch = info && info->sType == 16 && info->pCode &&
                (((cropMode == 5 || cropMode == 7) && info->codeSize == 728 &&
                    HashShader(reinterpret_cast<const std::uint8_t*>(info->pCode), info->codeSize) ==
                        0x2D451DC0C0A804B2ULL) ||
                 (cropMode != 4 && cropMode != 5 && cropMode != 7 && info->codeSize == 640 &&
                    HashShader(reinterpret_cast<const std::uint8_t*>(info->pCode), info->codeSize) ==
                        0xBC9EC2F443CE346BULL));
            std::vector<std::uint32_t> cropped;
            const float cropScale = cropMode == 6 ? 1.0f :
                (cropMode == 7 || cropMode == 10) ? -1.0f :
                cropMode == 8 ? 1.25f : cropMode == 9 ? 1.5f : overscanScale;
            if (candidateMatch && PatchPresentationShader(info, cropScale, cropped))
            {
                presentationShaderMatches.fetch_add(1);
                ShaderModuleCreateInfo modified = *info;
                modified.pCode = cropped.data();
                modified.codeSize = cropped.size() * sizeof(std::uint32_t);
                const std::int32_t result = original(device, &modified, allocator, module);
                if (result == 0)
                {
                    const unsigned int replacementNumber = presentationShaderPatches.fetch_add(1) + 1;
                    if (replacementNumber <= 8 && Logger::IsDebugEnabled())
                    {
                        char detail[256] = {};
                        std::snprintf(detail, sizeof(detail),
                            "ExpandSSRCoverage: replacement #%u original SPIR-V bytes=%zu hash=%016llX.",
                            replacementNumber, info->codeSize, static_cast<unsigned long long>(HashShader(
                                reinterpret_cast<const std::uint8_t*>(info->pCode), info->codeSize)));
                        Logger::Debug(detail);
                    }
                    if (!reportedPresentationPatch.exchange(true))
                    {
                        char message[256] = {};
                        std::snprintf(message, sizeof(message),
                            "[SUCCESS] ExpandSSRCoverage: Vulkan shader candidate replaced (mode=%d, UVScale=%.3f).", cropMode, cropScale);
                        Logger::Log(message);
                        std::snprintf(message, sizeof(message),
                            "ExpandSSRCoverage: fullscreen-copy candidate SPIR-V bytes=%zu hash=%016llX.",
                            info->codeSize, static_cast<unsigned long long>(HashShader(
                                reinterpret_cast<const std::uint8_t*>(info->pCode), info->codeSize)));
                        Logger::Debug(message);
                    }
                    return result;
                }
                if (!reportedPresentationFailure.exchange(true))
                    Logger::Log("[WARNING] ExpandSSRCoverage: Vulkan rejected presentation crop; original shader retained.");
            }
        }
        if (overscanCropEnabled.load(std::memory_order_acquire) &&
            overscanCropTestMode.load(std::memory_order_relaxed) != 4)
        {
            std::vector<std::uint32_t> neutralized;
            if (PatchCameraTransitionShader(info, neutralized))
            {
                transitionShaderMatches.fetch_add(1);
                ShaderModuleCreateInfo modified = *info;
                modified.pCode = neutralized.data();
                modified.codeSize = neutralized.size() * sizeof(std::uint32_t);
                const std::int32_t result = original(device, &modified, allocator, module);
                if (result == 0)
                {
                    transitionShaderPatches.fetch_add(1);
                    if (!reportedTransitionPatch.exchange(true))
                    {
                        Logger::Log("[SUCCESS] ExpandSSRCoverage: transition shader patched.");
                        char message[192] = {};
                        std::snprintf(message, sizeof(message),
                            "ExpandSSRCoverage: transition shader SPIR-V bytes=%zu hash=%016llX.",
                            info->codeSize, static_cast<unsigned long long>(HashShader(
                                reinterpret_cast<const std::uint8_t*>(info->pCode), info->codeSize)));
                        Logger::Debug(message);
                    }
                    return result;
                }
                if (!reportedTransitionFailure.exchange(true))
                    Logger::Log("[WARNING] ExpandSSRCoverage: Vulkan rejected transition shader patch; original retained.");
            }
        }
        return original(device, info, allocator, module);
    }

    FARPROC WINAPI InterceptGetDeviceProcAddress(void* device, const char* name)
    {
        const auto original = originalGetDeviceProcAddress.load();
        if (!original)
            return nullptr;
        const FARPROC address = original(device, name);
        if (address && name && std::strcmp(name, "vkCreateShaderModule") == 0)
        {
            originalCreateShaderModule.store(reinterpret_cast<CreateShaderModuleFunction>(address));
            return reinterpret_cast<FARPROC>(&InterceptCreateShaderModule);
        }
        return address;
    }

    FARPROC WINAPI InterceptGetInstanceProcAddress(void* instance, const char* name)
    {
        const auto original = originalGetInstanceProcAddress.load();
        if (!original)
            return nullptr;
        const FARPROC address = original(instance, name);
        if (!address || !name)
            return address;
        if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
        {
            originalGetDeviceProcAddress.store(reinterpret_cast<GetDeviceProcAddressFunction>(address));
            return reinterpret_cast<FARPROC>(&InterceptGetDeviceProcAddress);
        }
        if (std::strcmp(name, "vkCreateShaderModule") == 0)
        {
            originalCreateShaderModule.store(reinterpret_cast<CreateShaderModuleFunction>(address));
            return reinterpret_cast<FARPROC>(&InterceptCreateShaderModule);
        }
        return address;
    }

    FARPROC WINAPI InterceptGetProcAddress(HMODULE module, LPCSTR name)
    {
        const auto original = originalGetProcAddress.load();
        if (!original)
            return nullptr;
        const FARPROC address = original(module, name);
        if (!address || reinterpret_cast<std::uintptr_t>(name) < 65536 ||
            module != GetModuleHandleW(L"vulkan-1.dll"))
            return address;

        if (std::strcmp(name, "vkGetInstanceProcAddr") == 0)
        {
            originalGetInstanceProcAddress.store(reinterpret_cast<GetInstanceProcAddressFunction>(address));
            return reinterpret_cast<FARPROC>(&InterceptGetInstanceProcAddress);
        }
        if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
        {
            originalGetDeviceProcAddress.store(reinterpret_cast<GetDeviceProcAddressFunction>(address));
            return reinterpret_cast<FARPROC>(&InterceptGetDeviceProcAddress);
        }
        if (std::strcmp(name, "vkCreateShaderModule") == 0)
        {
            originalCreateShaderModule.store(reinterpret_cast<CreateShaderModuleFunction>(address));
            return reinterpret_cast<FARPROC>(&InterceptCreateShaderModule);
        }
        return address;
    }

    constexpr std::uint32_t fovConstructorCallRva = 0x003182AFu;
    constexpr std::uint32_t fovConstructorRva = 0x026F50E4u;
    constexpr std::uint32_t fovWriteRva = 0x026F518Cu;
    constexpr std::uint32_t fovInputRva = 0x026F5108u;
    constexpr std::uint32_t fovProjectionReadRva = 0x026F658Au;
    constexpr std::uint8_t originalFovProjectionRead[] = { 0xF3, 0x0F, 0x10, 0x83, 0x4C, 0x06, 0x00, 0x00 };
    constexpr std::uint8_t patchedFovProjectionRead[] = { 0xF3, 0x0F, 0x10, 0x83, 0x40, 0x06, 0x00, 0x00 };
    std::uint8_t* fovProjectionReadSite = nullptr;
    constexpr std::uint8_t originalFovWrite[] = { 0xF3, 0x0F, 0x11, 0xB9, 0x40, 0x06, 0x00, 0x00 };
    constexpr std::uint8_t originalFovInput[] = { 0x0F, 0x28, 0xF9 };
    constexpr std::size_t fovCallLength = 5;
    std::uint8_t* fovCallSite = nullptr;
    std::uint8_t originalFovCall[fovCallLength] = {};
    std::uint8_t fovCallPatch[fovCallLength] = {};
    void* fovRelay = nullptr;
    float ExpandCameraFov(float original, std::uintptr_t camera, std::uintptr_t context)
    {
        if (!std::isfinite(original) || original < 1.0f || original > 170.0f)
        {
            CaptureFovProbe(original, original, camera, context);
            return original;
        }
        float result = original;
        if (overscanFovEnabled)
        {
            constexpr float radiansPerDegree = 0.017453292519943295769f;
            constexpr float degreesPerRadian = 57.295779513082320877f;
            const float expanded = 2.0f * std::atan(std::tan(original * radiansPerDegree * 0.5f) *
                overscanScale) * degreesPerRadian;
            result = std::isfinite(expanded) && expanded < 175.0f ? expanded : original;
        }
        if (cameraFovOffset != 0.0f)
        {
            constexpr float radiansPerDegree = 0.017453292519943295769f;
            constexpr float degreesPerRadian = 57.295779513082320877f;
            const float adjusted = 2.0f * std::atan(std::tan(result * radiansPerDegree * 0.5f) *
                cameraFovScale) * degreesPerRadian;
            if (std::isfinite(adjusted) && adjusted >= 1.0f && adjusted < 175.0f)
                result = adjusted;
        }
        CaptureFovProbe(original, result, camera, context);
        return result;
    }

    void* AllocateFovRelay(std::uintptr_t target)
    {
        SYSTEM_INFO system = {};
        GetSystemInfo(&system);
        const std::uintptr_t granularity = system.dwAllocationGranularity;
        const std::uintptr_t center = target & ~(granularity - 1u);
        for (std::uintptr_t offset = 0; offset <= 0x40000000u; offset += granularity)
        {
            for (unsigned int side = 0; side < 2; ++side)
            {
                if (!offset && side)
                    continue;
                if (side == 0 && center > (std::numeric_limits<std::uintptr_t>::max)() - offset)
                    continue;
                if (side == 1 && center < offset)
                    continue;
                const std::uintptr_t address = side ? center - offset : center + offset;
                const std::int64_t distance = static_cast<std::int64_t>(address) -
                    static_cast<std::int64_t>(target + 5);
                if (distance < (std::numeric_limits<std::int32_t>::min)() ||
                    distance > (std::numeric_limits<std::int32_t>::max)())
                    continue;
                void* block = VirtualAlloc(reinterpret_cast<void*>(address), 4096,
                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                if (block)
                    return block;
            }
        }
        return nullptr;
    }

    bool InstallOverscanFovHook(float scale)
    {
        const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base)
            return false;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
            return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->FileHeader.TimeDateStamp != 0x65EEC92Eu || nt->OptionalHeader.SizeOfImage != 0x07397600u ||
            fovConstructorCallRva > nt->OptionalHeader.SizeOfImage - fovCallLength ||
            fovWriteRva > nt->OptionalHeader.SizeOfImage - sizeof(originalFovWrite) ||
            fovInputRva > nt->OptionalHeader.SizeOfImage - sizeof(originalFovInput) ||
            fovProjectionReadRva > nt->OptionalHeader.SizeOfImage - sizeof(originalFovProjectionRead))
            return false;

        auto* site = const_cast<std::uint8_t*>(base + fovConstructorCallRva);
        const auto* fovInput = base + fovInputRva;
        const auto* fovWrite = base + fovWriteRva;
        const auto* projectionRead = base + fovProjectionReadRva;
        if (!IsReadableProjectionRange(site, fovCallLength) ||
            !IsReadableProjectionRange(fovInput, sizeof(originalFovInput)) ||
            !IsReadableProjectionRange(fovWrite, sizeof(originalFovWrite)) ||
            !IsReadableProjectionRange(projectionRead, sizeof(originalFovProjectionRead)) ||
            site[0] != 0xE8 ||
            std::memcmp(fovInput, originalFovInput, sizeof(originalFovInput)) != 0 ||
            std::memcmp(fovWrite, originalFovWrite, sizeof(originalFovWrite)) != 0 ||
            std::memcmp(projectionRead, originalFovProjectionRead, sizeof(originalFovProjectionRead)) != 0)
            return false;

        std::int32_t originalDisplacement = 0;
        std::memcpy(&originalDisplacement, site + 1, sizeof(originalDisplacement));
        const auto originalTarget = reinterpret_cast<std::uintptr_t>(site + fovCallLength) +
            static_cast<std::intptr_t>(originalDisplacement);
        if (originalTarget != reinterpret_cast<std::uintptr_t>(base + fovConstructorRva))
            return false;

        void* relay = AllocateFovRelay(reinterpret_cast<std::uintptr_t>(site));
        if (!relay)
            return false;

        std::vector<std::uint8_t> code;
        code.reserve(256);
        const auto emit = [&code](std::initializer_list<std::uint8_t> bytes)
        {
            code.insert(code.end(), bytes.begin(), bytes.end());
        };
        const auto emitAddress = [&code](std::uintptr_t address)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(&address);
            code.insert(code.end(), bytes, bytes + sizeof(address));
        };

        emit({ 0x9C, 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x55 });
        emit({ 0x48, 0x89, 0xE5, 0x48, 0x83, 0xE4, 0xF0, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00 });
        for (std::uint8_t index = 0; index < 6; ++index)
            emit({ 0xF3, 0x0F, 0x7F, static_cast<std::uint8_t>(0x44 + index * 8),
                0x24, static_cast<std::uint8_t>(0x20 + index * 16) });
        emit({ 0x48, 0x89, 0xCA, 0x49, 0x89, 0xF8 });
        emit({ 0xF3, 0x0F, 0x10, 0xC1, 0x48, 0xB8 });
        emitAddress(reinterpret_cast<std::uintptr_t>(&ExpandCameraFov));
        emit({ 0xFF, 0xD0, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x30 });
        for (std::uint8_t index = 0; index < 6; ++index)
            emit({ 0xF3, 0x0F, 0x6F, static_cast<std::uint8_t>(0x44 + index * 8),
                0x24, static_cast<std::uint8_t>(0x20 + index * 16) });
        emit({ 0x48, 0x89, 0xEC, 0x5D, 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59,
            0x58, 0x9D, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 });
        emitAddress(originalTarget);

        if (code.size() > 4096)
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        std::memcpy(relay, code.data(), code.size());
        DWORD relayProtection = 0;
        if (!VirtualProtect(relay, 4096, PAGE_EXECUTE_READ, &relayProtection) ||
            !FlushInstructionCache(GetCurrentProcess(), relay, code.size()))
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }

        const std::int64_t displacement = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(relay)) -
            static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(site + fovCallLength));
        if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
            displacement > (std::numeric_limits<std::int32_t>::max)())
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        const std::int32_t relative = static_cast<std::int32_t>(displacement);
        std::uint8_t patch[fovCallLength] = { 0xE8, 0, 0, 0, 0 };
        std::memcpy(patch + 1, &relative, sizeof(relative));
        DWORD oldProtection = 0;
        if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtection))
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        if (site[0] != 0xE8 || std::memcmp(site + 1, &originalDisplacement, sizeof(originalDisplacement)) != 0)
        {
            DWORD ignored = 0;
            VirtualProtect(site, sizeof(patch), oldProtection, &ignored);
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        overscanScale = scale;
        std::memcpy(originalFovCall, site, fovCallLength);
        std::memcpy(site, patch, sizeof(patch));
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch)) != FALSE;
        DWORD ignored = 0;
        const bool restored = VirtualProtect(site, sizeof(patch), oldProtection, &ignored) != FALSE;
        if (!flushed || !restored)
        {
            bool rolledBack = false;
            DWORD writable = 0;
            if (VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &writable))
            {
                if (std::memcmp(site, patch, sizeof(patch)) == 0)
                {
                    std::memcpy(site, originalFovCall, sizeof(originalFovCall));
                    rolledBack = FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch)) != FALSE;
                }
                VirtualProtect(site, sizeof(patch), oldProtection, &ignored);
            }
            if (rolledBack)
                VirtualFree(relay, 0, MEM_RELEASE);
            else
            {
                fovCallSite = site;
                fovRelay = relay;
                std::memcpy(fovCallPatch, patch, sizeof(patch));
                Logger::Log("[WARNING] ExpandSSRCoverage: constructor call rollback could not be confirmed; relay retained for safety.");
            }
            return false;
        }
        fovCallSite = site;
        fovRelay = relay;
        std::memcpy(fovCallPatch, patch, sizeof(patch));
        return true;
    }

    void RemoveOverscanFovHook()
    {
        if (!fovCallSite)
            return;
        DWORD protection = 0;
        if (!VirtualProtect(fovCallSite, fovCallLength, PAGE_EXECUTE_READWRITE, &protection))
            return;
        const bool owned = std::memcmp(fovCallSite, fovCallPatch, sizeof(fovCallPatch)) == 0;
        if (owned)
        {
            std::memcpy(fovCallSite, originalFovCall, sizeof(originalFovCall));
            FlushInstructionCache(GetCurrentProcess(), fovCallSite, sizeof(originalFovCall));
        }
        DWORD ignored = 0;
        VirtualProtect(fovCallSite, fovCallLength, protection, &ignored);
        fovCallSite = nullptr;
        if (owned && fovRelay)
            VirtualFree(fovRelay, 0, MEM_RELEASE);
        fovRelay = nullptr;
    }

    bool InstallOverscanProjectionPatch()
    {
        if (fovProjectionReadSite)
            return false;
        const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base)
            return false;
        auto* site = const_cast<std::uint8_t*>(base + fovProjectionReadRva);
        if (!IsReadableProjectionRange(site, sizeof(originalFovProjectionRead)) ||
            std::memcmp(site, originalFovProjectionRead, sizeof(originalFovProjectionRead)) != 0)
            return false;
        DWORD protection = 0;
        if (!VirtualProtect(site, sizeof(originalFovProjectionRead), PAGE_EXECUTE_READWRITE, &protection))
            return false;
        if (std::memcmp(site, originalFovProjectionRead, sizeof(originalFovProjectionRead)) != 0)
        {
            DWORD ignored = 0;
            VirtualProtect(site, sizeof(originalFovProjectionRead), protection, &ignored);
            return false;
        }
        site[4] = patchedFovProjectionRead[4];
        fovProjectionReadSite = site;
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, sizeof(patchedFovProjectionRead)) != FALSE;
        DWORD ignored = 0;
        const bool restored = VirtualProtect(site, sizeof(patchedFovProjectionRead), protection, &ignored) != FALSE;
        return flushed && restored &&
            std::memcmp(site, patchedFovProjectionRead, sizeof(patchedFovProjectionRead)) == 0;
    }

    bool RemoveOverscanProjectionPatch()
    {
        if (!fovProjectionReadSite)
            return true;
        DWORD protection = 0;
        if (!VirtualProtect(fovProjectionReadSite, sizeof(patchedFovProjectionRead), PAGE_EXECUTE_READWRITE, &protection))
            return false;
        const bool owned = std::memcmp(fovProjectionReadSite, patchedFovProjectionRead, sizeof(patchedFovProjectionRead)) == 0;
        bool flushed = false;
        if (owned)
        {
            fovProjectionReadSite[4] = originalFovProjectionRead[4];
            flushed = FlushInstructionCache(GetCurrentProcess(), fovProjectionReadSite, sizeof(originalFovProjectionRead)) != FALSE;
        }
        DWORD ignored = 0;
        const bool restored = VirtualProtect(fovProjectionReadSite, sizeof(originalFovProjectionRead), protection, &ignored) != FALSE;
        if (restored && std::memcmp(fovProjectionReadSite, originalFovProjectionRead, sizeof(originalFovProjectionRead)) == 0)
            fovProjectionReadSite = nullptr;
        return owned && flushed && restored;
    }

    bool InstallImportHook()
    {
        const auto base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base)
            return false;
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
            return false;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return false;
        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!directory.VirtualAddress || !directory.Size || directory.VirtualAddress >= nt->OptionalHeader.SizeOfImage)
            return false;
        auto imports = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
        const std::size_t limit = directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
        for (std::size_t index = 0; index < limit && imports[index].Name; ++index)
        {
            const IMAGE_IMPORT_DESCRIPTOR& entry = imports[index];
            const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
            if (!entry.OriginalFirstThunk || !entry.FirstThunk ||
                entry.OriginalFirstThunk >= imageSize || entry.FirstThunk >= imageSize ||
                entry.Name >= imageSize)
                continue;
            const char* library = reinterpret_cast<const char*>(base + entry.Name);
            if (_stricmp(library, "KERNEL32.dll") != 0 && _stricmp(library, "KERNELBASE.dll") != 0)
                continue;

            auto names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + entry.OriginalFirstThunk);
            auto slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(const_cast<std::uint8_t*>(base) + entry.FirstThunk);
            const std::size_t nameCount = (imageSize - entry.OriginalFirstThunk) / sizeof(IMAGE_THUNK_DATA64);
            const std::size_t slotCount = (imageSize - entry.FirstThunk) / sizeof(IMAGE_THUNK_DATA64);
            const std::size_t available = nameCount < slotCount ? nameCount : slotCount;
            for (std::size_t item = 0; item < available && names[item].u1.AddressOfData; ++item)
            {
                if (IMAGE_SNAP_BY_ORDINAL64(names[item].u1.Ordinal))
                    continue;
                if (names[item].u1.AddressOfData >= nt->OptionalHeader.SizeOfImage)
                    continue;
                const auto imported = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names[item].u1.AddressOfData);
                if (std::strcmp(reinterpret_cast<const char*>(imported->Name), "GetProcAddress") != 0)
                    continue;

                auto slot = reinterpret_cast<void* volatile*>(&slots[item].u1.Function);
                void* prior = reinterpret_cast<void*>(slots[item].u1.Function);
                if (!prior || prior == reinterpret_cast<void*>(&InterceptGetProcAddress))
                    return false;
                DWORD oldProtection = 0;
                if (!VirtualProtect(const_cast<void**>(slot), sizeof(void*), PAGE_READWRITE, &oldProtection))
                    return false;
                originalGetProcAddress.store(reinterpret_cast<GetProcAddressFunction>(prior));
                const void* previous = InterlockedCompareExchangePointer(slot,
                    reinterpret_cast<void*>(&InterceptGetProcAddress), prior);
                DWORD ignored = 0;
                VirtualProtect(const_cast<void**>(slot), sizeof(void*), oldProtection, &ignored);
                if (previous != prior)
                    return false;
                importSlot.store(slot);
                return true;
            }
        }
        return false;
    }
}

namespace Main
{
namespace WaterReflections
{
    void ReportStartupStatus()
    {
        if ((!importSlot.load() && !fovCallSite) || !Logger::IsDebugEnabled())
            return;
        char message[256] = {};
        if (overscanCropEnabled.load())
        {
            std::snprintf(message, sizeof(message),
                "ExpandSSRCoverage: fullscreen-copy candidate matches=%u, successful replacements=%u.",
                presentationShaderMatches.load(), presentationShaderPatches.load());
            Logger::Debug(message);
            std::snprintf(message, sizeof(message),
                "ExpandSSRCoverage: transition composite matches=%u, successful replacements=%u.",
                transitionShaderMatches.load(), transitionShaderPatches.load());
            Logger::Debug(message);
        }
        if (fovCallSite)
        {
            fovProbeStart = GetTickCount64();
            fovProbeLastPoll = fovProbeStart;
            fovProbeActive.store(true, std::memory_order_release);
            Logger::Debug(ssrCoverageEnabled ?
                "ExpandSSRCoverage: FOV probe active for 90s (no camera changes)." :
                "CameraFOVOffset: FOV probe active for 90s (no camera changes).");
        }
    }

    void UpdateOverscanDiagnostics()
    {
        if (!fovProbeActive.load(std::memory_order_acquire))
            return;
        const ULONGLONG now = GetTickCount64();
        if (now - fovProbeLastPoll < 500)
            return;
        fovProbeLastPoll = now;
        unsigned int published = fovProbeCount.load(std::memory_order_acquire);
        if (published > maxFovProbeSamples)
            published = maxFovProbeSamples;
        while (fovProbeReported < published &&
            fovProbeSamples[fovProbeReported].ready.load(std::memory_order_acquire))
        {
            const FovProbeSample& sample = fovProbeSamples[fovProbeReported];
            char message[256] = {};
            std::snprintf(message, sizeof(message),
                "%s: FOV sample #%u at +%llums original=%.3f adjusted=%.3f camera=%p context=%p.",
                ssrCoverageEnabled ? "ExpandSSRCoverage" : "CameraFOVOffset",
                fovProbeReported + 1, static_cast<unsigned long long>(sample.elapsedMs),
                sample.original, sample.expanded, reinterpret_cast<void*>(sample.camera),
                reinterpret_cast<void*>(sample.context));
            Logger::Debug(message);
            ++fovProbeReported;
        }
        const ULONGLONG elapsed = now - fovProbeStart;
        if (elapsed >= fovProbeNextStatus || published >= maxFovProbeSamples || elapsed >= 90000)
        {
            char message[200] = {};
            std::snprintf(message, sizeof(message),
                "%s: FOV probe +%llums calls=%u captured=%u reported=%u.",
                ssrCoverageEnabled ? "ExpandSSRCoverage" : "CameraFOVOffset",
                static_cast<unsigned long long>(elapsed), fovProbeCalls.load(),
                published, fovProbeReported);
            Logger::Debug(message);
            fovProbeNextStatus = elapsed + 10000;
            if (published >= maxFovProbeSamples || elapsed >= 90000)
                fovProbeActive.store(false, std::memory_order_release);
        }
    }

    void InitializeOverscanFov(bool enabled, float scale, int testMode, float fovOffset)
    {
        if (!std::isfinite(fovOffset))
            fovOffset = 0.0f;
        cameraFovOffset = fovOffset < -3.0f ? -3.0f : fovOffset > 3.0f ? 3.0f : fovOffset;
        cameraFovScale = std::pow(1.25f, cameraFovOffset);
        if (!enabled && cameraFovOffset == 0.0f)
            return;
        ssrCoverageEnabled = enabled;
        overscanFovEnabled = enabled && testMode < 3;
        const bool constructorEnabled = overscanFovEnabled || cameraFovOffset != 0.0f;
        const bool projectionEnabled = (enabled && (testMode == 0 || testMode == 2)) || cameraFovOffset != 0.0f;
        const bool cropEnabled = enabled && (testMode == 0 || testMode >= 3);
        overscanScale = scale;
        overscanCropTestMode.store(testMode, std::memory_order_relaxed);
        if (constructorEnabled && !InstallOverscanFovHook(overscanFovEnabled ? scale : 1.0f))
        {
            Logger::Log(enabled ?
                "[WARNING] ExpandSSRCoverage: FOV constructor call hook rejected by build, instruction validation, or memory protection; no changes made." :
                "[WARNING] CameraFOVOffset: FOV constructor call hook rejected by build, instruction validation, or memory protection; no changes made.");
            return;
        }
        if (projectionEnabled)
        {
            if (!InstallOverscanProjectionPatch())
            {
                if (!RemoveOverscanProjectionPatch())
                    Logger::Log(enabled ?
                        "[WARNING] ExpandSSRCoverage: projection patch rollback could not be confirmed." :
                        "[WARNING] CameraFOVOffset: projection patch rollback could not be confirmed.");
                RemoveOverscanFovHook();
                Logger::Log(enabled ?
                    "[WARNING] ExpandSSRCoverage: projection FOV read patch rejected; constructor hook rollback attempted." :
                    "[WARNING] CameraFOVOffset: projection FOV read patch rejected; constructor hook rollback attempted.");
                return;
            }
            Logger::Debug(enabled ?
                "ExpandSSRCoverage: projection FOV read +0x026F658A changed from [rbx+0x64C] to [rbx+0x640]." :
                "CameraFOVOffset: projection FOV read +0x026F658A changed from [rbx+0x64C] to [rbx+0x640].");
        }
        if (cropEnabled && (scale > 1.0f || testMode == 6))
        {
            if (importSlot.load() || InstallImportHook())
            {
                overscanCropEnabled.store(true, std::memory_order_release);
                Logger::Debug("ExpandSSRCoverage: Vulkan crop hook ready.");
            }
            else
                Logger::Log("[WARNING] ExpandSSRCoverage: Vulkan interception unavailable; final-image crop inactive.");
        }
        char message[256] = {};
        if (enabled)
        {
            std::snprintf(message, sizeof(message),
                "[INFO] ExpandSSRCoverage: mode=%d, constructor=%s, projection=%s, VulkanShader=%s, scale=%.3f; DX12 crop not implemented.",
                testMode, fovCallSite ? "on" : "off", fovProjectionReadSite ? "on" : "off",
                overscanCropEnabled.load() ? (testMode == 4 ? "pass-through" : testMode == 6 ? "identity" :
                    testMode == 7 ? "flip-728" : testMode == 10 ? "flip-640" : "cropped") : "off",
                testMode == 6 ? 1.0f : (testMode == 7 || testMode == 10) ? -1.0f :
                    testMode == 8 ? 1.25f : testMode == 9 ? 1.5f : scale);
            Logger::Log(message);
        }
        if (cameraFovOffset != 0.0f)
        {
            std::snprintf(message, sizeof(message),
                "[INFO] CameraFOVOffset: strength=%+.2f, FOV scale=%.3f, constructor=%s, projection=%s.",
                cameraFovOffset, cameraFovScale, fovCallSite ? "on" : "off", fovProjectionReadSite ? "on" : "off");
            Logger::Log(message);
        }
    }

    void Shutdown()
    {
        fovProbeActive.store(false, std::memory_order_release);
        overscanCropEnabled.store(false, std::memory_order_release);
        overscanCropTestMode.store(0, std::memory_order_relaxed);
        if (!RemoveOverscanProjectionPatch())
            Logger::Log(ssrCoverageEnabled ?
                "[WARNING] ExpandSSRCoverage: projection FOV read could not be restored during shutdown." :
                "[WARNING] CameraFOVOffset: projection FOV read could not be restored during shutdown.");
        RemoveOverscanFovHook();
        auto slot = importSlot.exchange(nullptr);
        const auto original = originalGetProcAddress.load();
        if (!slot || !original)
            return;
        DWORD oldProtection = 0;
        if (!VirtualProtect(const_cast<void**>(slot), sizeof(void*), PAGE_READWRITE, &oldProtection))
            return;
        InterlockedCompareExchangePointer(slot, reinterpret_cast<void*>(original),
            reinterpret_cast<void*>(&InterceptGetProcAddress));
        DWORD ignored = 0;
        VirtualProtect(const_cast<void**>(slot), sizeof(void*), oldProtection, &ignored);
    }
}
}

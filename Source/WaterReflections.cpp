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
        bool sharpenCrop, std::vector<std::uint32_t>& patched)
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
        std::uint32_t vec4Type = 0;
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
        std::size_t outputStore = 0;
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
            else if (opcode == 23 && length == 4 && words[index + 2] == floatType)
            {
                if (words[index + 3] == 2)
                    vec2Type = words[index + 1];
                else if (words[index + 3] == 4)
                    vec4Type = words[index + 1];
            }
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
                outputStore = index;
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

        if (sharpenCrop && scale > 1.0f && vec4Type && words[sampleStart + 1] == vec4Type &&
            (words[sampleStart] >> 16) == 5)
        {


            const std::uint32_t colorType = words[sampleStart + 1];
            std::uint32_t nextId = patched[3];
            const std::uint32_t strengthId = nextId++;
            const float strength = 0.0625f * (scale - 1.0f);
            std::uint32_t strengthBits = 0;
            std::memcpy(&strengthBits, &strength, sizeof(strengthBits));

            std::vector<std::uint32_t> filter;
            const auto emit = [&](std::uint32_t opcode, std::uint32_t type,
                std::initializer_list<std::uint32_t> operands)
            {
                const std::uint32_t id = nextId++;
                filter.insert(filter.end(), {
                    ((3u + static_cast<std::uint32_t>(operands.size())) << 16) | opcode, type, id
                });
                filter.insert(filter.end(), operands.begin(), operands.end());
                return id;
            };

            constexpr std::uint32_t add = 129;
            constexpr std::uint32_t subtract = 131;
            const auto dx = emit(207, vec2Type, { coordinate });
            const auto dy = emit(208, vec2Type, { coordinate });
            const auto sample = [&](std::uint32_t op, std::uint32_t step)
            {
                const auto uv = emit(op, vec2Type, { result, step });
                return emit(87, colorType, { sampledImage, uv });
            };
            const auto right = sample(add, dx);
            const auto left = sample(subtract, dx);
            const auto down = sample(add, dy);
            const auto up = sample(subtract, dy);
            const auto horizontal = emit(add, colorType, { left, right });
            const auto vertical = emit(add, colorType, { up, down });
            const auto neighbors = emit(add, colorType, { horizontal, vertical });
            const auto twiceCenter = emit(add, colorType, { color, color });
            const auto fourTimesCenter = emit(add, colorType, { twiceCenter, twiceCenter });
            const auto detail = emit(subtract, colorType, { fourTimesCenter, neighbors });
            const auto correction = emit(142, colorType, { detail, strengthId });
            const auto sharpened = emit(add, colorType, { color, correction });
            const auto rgbWithOriginalAlpha = emit(79, colorType, { sharpened, color, 0, 1, 2, 7 });

            patched[outputStore + constantWords + operationWords + 2] = rgbWithOriginalAlpha;
            patched.insert(patched.begin() + replacedSample + 5, filter.begin(), filter.end());
            patched.insert(patched.begin() + functionStart, {
                (4u << 16) | 43u, floatType, strengthId, strengthBits
            });
            patched[3] = nextId;
        }
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
            if (candidateMatch && PatchPresentationShader(info, cropScale, cropMode == 0, cropped))
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


        const int testMode = overscanCropTestMode.load(std::memory_order_relaxed);
        if (overscanCropEnabled.load(std::memory_order_acquire) && testMode >= 3 && testMode != 4)
        {
            std::vector<std::uint32_t> neutralized;
            if (info && PatchCameraTransitionShader(info, neutralized))
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
        if (!Logger::IsDebugEnabled())
            return;
        if (!overscanCropEnabled.load())
            return;
        char message[256] = {};
        std::snprintf(message, sizeof(message),
            "ExpandSSRCoverage: fullscreen-copy candidate matches=%u, successful replacements=%u.",
            presentationShaderMatches.load(), presentationShaderPatches.load());
        Logger::Debug(message);
        std::snprintf(message, sizeof(message),
            "ExpandSSRCoverage: transition composite matches=%u, successful replacements=%u.",
            transitionShaderMatches.load(), transitionShaderPatches.load());
        Logger::Debug(message);
    }

    bool InitializeOverscanCrop(float scale, int testMode)
    {
        overscanScale = scale;
        overscanCropTestMode.store(testMode, std::memory_order_relaxed);
        if (!importSlot.load() && !InstallImportHook())
        {
            Logger::Log("[WARNING] ExpandSSRCoverage: Vulkan interception unavailable; final-image crop inactive.");
            return false;
        }

        overscanCropEnabled.store(true, std::memory_order_release);
        Logger::Debug("ExpandSSRCoverage: Vulkan crop hook ready.");
        return true;
    }

    void Shutdown()
    {
        overscanCropEnabled.store(false, std::memory_order_release);
        overscanCropTestMode.store(0, std::memory_order_relaxed);
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

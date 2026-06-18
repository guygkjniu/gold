#include "engine/StudioModel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

namespace goldsrc
{
namespace
{
constexpr std::int32_t StudioId = 0x54534449; // "IDST"
constexpr std::int32_t StudioVersion = 10;
constexpr std::uint32_t StudioTextureMasked = 0x0040;

template <typename T>
bool readStruct(const std::vector<std::uint8_t>& data, std::size_t offset, T& out)
{
    if (offset + sizeof(T) > data.size())
    {
        return false;
    }

    std::memcpy(&out, data.data() + offset, sizeof(T));
    return true;
}

std::string fixedString(const char* text, std::size_t size)
{
    std::size_t length = 0;
    while (length < size && text[length] != '\0')
    {
        ++length;
    }
    return std::string(text, text + length);
}

struct StudioHeader
{
    std::int32_t id;
    std::int32_t version;
    char name[64];
    std::int32_t length;
    float eyePosition[3];
    float min[3];
    float max[3];
    float bbMin[3];
    float bbMax[3];
    std::int32_t flags;
    std::int32_t boneCount;
    std::int32_t boneIndex;
    std::int32_t boneControllerCount;
    std::int32_t boneControllerIndex;
    std::int32_t hitboxCount;
    std::int32_t hitboxIndex;
    std::int32_t sequenceCount;
    std::int32_t sequenceIndex;
    std::int32_t sequenceGroupCount;
    std::int32_t sequenceGroupIndex;
    std::int32_t textureCount;
    std::int32_t textureIndex;
    std::int32_t textureDataIndex;
    std::int32_t skinRefCount;
    std::int32_t skinFamilyCount;
    std::int32_t skinIndex;
    std::int32_t bodyPartCount;
    std::int32_t bodyPartIndex;
    std::int32_t attachmentCount;
    std::int32_t attachmentIndex;
    std::int32_t soundTable;
    std::int32_t soundIndex;
    std::int32_t soundGroups;
    std::int32_t soundGroupIndex;
    std::int32_t transitionCount;
    std::int32_t transitionIndex;
};

struct StudioBone
{
    char name[32];
    std::int32_t parent;
    std::int32_t flags;
    std::int32_t boneController[6];
    float value[6];
    float scale[6];
};

struct StudioBodyPart
{
    char name[64];
    std::int32_t modelCount;
    std::int32_t base;
    std::int32_t modelIndex;
};

struct StudioSequenceDesc
{
    char label[32];
    float fps;
    std::int32_t flags;
    std::int32_t activity;
    std::int32_t activityWeight;
    std::int32_t eventCount;
    std::int32_t eventIndex;
    std::int32_t frameCount;
    std::int32_t pivotCount;
    std::int32_t pivotIndex;
    std::int32_t motionType;
    std::int32_t motionBone;
    float linearMovement[3];
    std::int32_t autoMovePositionIndex;
    std::int32_t autoMoveAngleIndex;
    float bbMin[3];
    float bbMax[3];
    std::int32_t blendCount;
    std::int32_t animIndex;
    std::int32_t blendType[2];
    float blendStart[2];
    float blendEnd[2];
    std::int32_t blendParent;
    std::int32_t sequenceGroup;
    std::int32_t entryNode;
    std::int32_t exitNode;
    std::int32_t nodeFlags;
    std::int32_t nextSequence;
};

struct StudioSubModel
{
    char name[64];
    std::int32_t type;
    float boundingRadius;
    std::int32_t meshCount;
    std::int32_t meshIndex;
    std::int32_t vertexCount;
    std::int32_t vertexInfoIndex;
    std::int32_t vertexIndex;
    std::int32_t normalCount;
    std::int32_t normalInfoIndex;
    std::int32_t normalIndex;
    std::int32_t groupCount;
    std::int32_t groupIndex;
};

struct StudioMesh
{
    std::int32_t triangleCount;
    std::int32_t triangleIndex;
    std::int32_t skinRef;
    std::int32_t normalCount;
    std::int32_t normalIndex;
};

struct StudioTextureHeader
{
    char name[64];
    std::int32_t flags;
    std::int32_t width;
    std::int32_t height;
    std::int32_t index;
};

struct Matrix3x4
{
    float m[3][4] = {};
};

struct Quaternion
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct StudioAnim
{
    std::uint16_t offset[6];
};

struct StudioAnimValueHeader
{
    std::uint8_t valid;
    std::uint8_t total;
};

Vec3 transformPoint(const Matrix3x4& matrix, const Vec3& point)
{
    return {
        point.x * matrix.m[0][0] + point.y * matrix.m[0][1] + point.z * matrix.m[0][2] + matrix.m[0][3],
        point.x * matrix.m[1][0] + point.y * matrix.m[1][1] + point.z * matrix.m[1][2] + matrix.m[1][3],
        point.x * matrix.m[2][0] + point.y * matrix.m[2][1] + point.z * matrix.m[2][2] + matrix.m[2][3],
    };
}

Matrix3x4 concatTransforms(const Matrix3x4& parent, const Matrix3x4& child)
{
    Matrix3x4 out;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            out.m[i][j] = parent.m[i][0] * child.m[0][j] +
                          parent.m[i][1] * child.m[1][j] +
                          parent.m[i][2] * child.m[2][j] +
                          (j == 3 ? parent.m[i][3] : 0.0f);
        }
    }
    return out;
}

Matrix3x4 angleMatrix(float pitch, float yaw, float roll, const Vec3& position)
{
    const float sp = std::sin(pitch);
    const float cp = std::cos(pitch);
    const float sy = std::sin(yaw);
    const float cy = std::cos(yaw);
    const float sr = std::sin(roll);
    const float cr = std::cos(roll);

    Matrix3x4 matrix;
    matrix.m[0][0] = cp * cy;
    matrix.m[1][0] = cp * sy;
    matrix.m[2][0] = -sp;
    matrix.m[0][1] = sr * sp * cy + cr * -sy;
    matrix.m[1][1] = sr * sp * sy + cr * cy;
    matrix.m[2][1] = sr * cp;
    matrix.m[0][2] = cr * sp * cy + -sr * -sy;
    matrix.m[1][2] = cr * sp * sy + -sr * cy;
    matrix.m[2][2] = cr * cp;
    matrix.m[0][3] = position.x;
    matrix.m[1][3] = position.y;
    matrix.m[2][3] = position.z;
    return matrix;
}

Quaternion angleQuaternion(const Vec3& angles)
{
    const float sy = std::sin(angles.z * 0.5f);
    const float cy = std::cos(angles.z * 0.5f);
    const float sp = std::sin(angles.y * 0.5f);
    const float cp = std::cos(angles.y * 0.5f);
    const float sr = std::sin(angles.x * 0.5f);
    const float cr = std::cos(angles.x * 0.5f);

    return {
        sr * cp * cy - cr * sp * sy,
        cr * sp * cy + sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
        cr * cp * cy + sr * sp * sy,
    };
}

Matrix3x4 quaternionMatrix(const Quaternion& quaternion, const Vec3& position)
{
    Matrix3x4 matrix;
    matrix.m[0][0] = 1.0f - 2.0f * quaternion.y * quaternion.y - 2.0f * quaternion.z * quaternion.z;
    matrix.m[1][0] = 2.0f * quaternion.x * quaternion.y + 2.0f * quaternion.w * quaternion.z;
    matrix.m[2][0] = 2.0f * quaternion.x * quaternion.z - 2.0f * quaternion.w * quaternion.y;

    matrix.m[0][1] = 2.0f * quaternion.x * quaternion.y - 2.0f * quaternion.w * quaternion.z;
    matrix.m[1][1] = 1.0f - 2.0f * quaternion.x * quaternion.x - 2.0f * quaternion.z * quaternion.z;
    matrix.m[2][1] = 2.0f * quaternion.y * quaternion.z + 2.0f * quaternion.w * quaternion.x;

    matrix.m[0][2] = 2.0f * quaternion.x * quaternion.z + 2.0f * quaternion.w * quaternion.y;
    matrix.m[1][2] = 2.0f * quaternion.y * quaternion.z - 2.0f * quaternion.w * quaternion.x;
    matrix.m[2][2] = 1.0f - 2.0f * quaternion.x * quaternion.x - 2.0f * quaternion.y * quaternion.y;

    matrix.m[0][3] = position.x;
    matrix.m[1][3] = position.y;
    matrix.m[2][3] = position.z;
    return matrix;
}

std::int32_t chooseSequence(const std::vector<StudioSequenceDesc>& sequences, std::int32_t requested)
{
    if (sequences.empty())
    {
        return 0;
    }
    if (requested >= 0 && requested < static_cast<std::int32_t>(sequences.size()))
    {
        return requested;
    }

    for (std::size_t i = 0; i < sequences.size(); ++i)
    {
        std::string label = fixedString(sequences[i].label, sizeof(sequences[i].label));
        std::transform(label.begin(), label.end(), label.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (label.find("idle") != std::string::npos ||
            label.find("stand") != std::string::npos ||
            label.find("look") != std::string::npos)
        {
            return static_cast<std::int32_t>(i);
        }
    }

    return 0;
}

float readAnimValue(const std::vector<std::uint8_t>& data, std::size_t animOffset, int frame)
{
    if (animOffset + sizeof(StudioAnimValueHeader) + sizeof(std::int16_t) > data.size())
    {
        return 0.0f;
    }

    std::size_t cursor = animOffset;
    int k = frame;
    StudioAnimValueHeader header = {};
    std::memcpy(&header, data.data() + cursor, sizeof(header));
    if (header.total < header.valid)
    {
        k = 0;
    }
    while (header.total <= k && header.total > 0)
    {
        k -= header.total;
        cursor += (static_cast<std::size_t>(header.valid) + 1u) * sizeof(std::int16_t);
        if (cursor + sizeof(StudioAnimValueHeader) + sizeof(std::int16_t) > data.size())
        {
            return 0.0f;
        }
        std::memcpy(&header, data.data() + cursor, sizeof(header));
        if (header.total < header.valid)
        {
            k = 0;
        }
    }

    const int valueIndex = header.valid > k ? k + 1 : header.valid;
    const std::size_t valueOffset = cursor + static_cast<std::size_t>(valueIndex) * sizeof(std::int16_t);
    if (valueOffset + sizeof(std::int16_t) > data.size())
    {
        return 0.0f;
    }
    std::int16_t value = 0;
    std::memcpy(&value, data.data() + valueOffset, sizeof(value));
    return static_cast<float>(value);
}

float readAnimValueInterpolated(const std::vector<std::uint8_t>& data, std::size_t animOffset, int frame, float fraction)
{
    const float first = readAnimValue(data, animOffset, frame);
    const float second = readAnimValue(data, animOffset, frame + 1);
    return first * (1.0f - fraction) + second * fraction;
}

void calcBoneFrame(const std::vector<std::uint8_t>& data,
                   const StudioBone& bone,
                   const StudioAnim* anim,
                   std::size_t animBase,
                   int frame,
                   float fraction,
                   Vec3& position,
                   Vec3& angles)
{
    position = {bone.value[0], bone.value[1], bone.value[2]};
    angles = {bone.value[3], bone.value[4], bone.value[5]};
    if (!anim)
    {
        return;
    }

    for (int axis = 0; axis < 3; ++axis)
    {
        if (anim->offset[axis] != 0)
        {
            const float value = readAnimValueInterpolated(data, animBase + anim->offset[axis], frame, fraction) * bone.scale[axis];
            position.x += axis == 0 ? value : 0.0f;
            position.y += axis == 1 ? value : 0.0f;
            position.z += axis == 2 ? value : 0.0f;
        }
        if (anim->offset[axis + 3] != 0)
        {
            const float value = readAnimValueInterpolated(data, animBase + anim->offset[axis + 3], frame, fraction) * bone.scale[axis + 3];
            if (axis == 0)
            {
                angles.x += value;
            }
            else if (axis == 1)
            {
                angles.y += value;
            }
            else
            {
                angles.z += value;
            }
        }
    }
}

std::vector<Matrix3x4> buildBoneTransforms(const std::vector<std::uint8_t>& data,
                                           const std::vector<StudioBone>& bones,
                                           const StudioSequenceDesc* sequence,
                                           float frameFloat)
{
    std::vector<Matrix3x4> boneTransforms;
    boneTransforms.resize(bones.size());
    const bool hasSequence = sequence && sequence->sequenceGroup == 0 && sequence->animIndex >= 0;
    int frame = 0;
    float fraction = 0.0f;
    if (hasSequence && sequence->frameCount > 1)
    {
        const float maxFrame = static_cast<float>(sequence->frameCount - 1);
        if (frameFloat < 0.0f)
        {
            frameFloat = 0.0f;
        }
        if (frameFloat >= maxFrame)
        {
            frameFloat = std::fmod(frameFloat, maxFrame);
        }
        frame = static_cast<int>(frameFloat);
        fraction = frameFloat - static_cast<float>(frame);
    }

    const std::size_t animTableOffset = hasSequence ? static_cast<std::size_t>(sequence->animIndex) : 0u;
    for (std::size_t i = 0; i < bones.size(); ++i)
    {
        StudioAnim anim = {};
        const StudioAnim* animPtr = nullptr;
        const std::size_t animOffset = animTableOffset + i * sizeof(StudioAnim);
        if (hasSequence && readStruct(data, animOffset, anim))
        {
            animPtr = &anim;
        }

        Vec3 position;
        Vec3 angles;
        calcBoneFrame(data, bones[i], animPtr, animOffset, frame, fraction, position, angles);
        const Matrix3x4 local = quaternionMatrix(angleQuaternion(angles), position);
        if (bones[i].parent >= 0 && static_cast<std::size_t>(bones[i].parent) < boneTransforms.size())
        {
            boneTransforms[i] = concatTransforms(boneTransforms[static_cast<std::size_t>(bones[i].parent)], local);
        }
        else
        {
            boneTransforms[i] = local;
        }
    }

    return boneTransforms;
}

bool readFile(const std::filesystem::path& path, std::vector<std::uint8_t>& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0)
    {
        return false;
    }

    out.resize(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(out.data()), size);
    return static_cast<bool>(file);
}

bool validRange(const std::vector<std::uint8_t>& data, std::int32_t offset, std::size_t bytes)
{
    return offset >= 0 && static_cast<std::size_t>(offset) + bytes <= data.size();
}

std::filesystem::path companionTexturePath(const std::filesystem::path& path)
{
    return path.parent_path() / (path.stem().string() + "T" + path.extension().string());
}

void readSkinRefs(const std::vector<std::uint8_t>& data, const StudioHeader& header, std::int32_t skin, std::vector<std::uint16_t>& out)
{
    out.clear();
    if (skin < 0 || skin >= header.skinFamilyCount)
    {
        skin = 0;
    }
    if (header.skinRefCount <= 0 || header.skinIndex < 0 ||
        !validRange(data,
                    header.skinIndex + skin * header.skinRefCount * static_cast<std::int32_t>(sizeof(std::uint16_t)),
                    static_cast<std::size_t>(header.skinRefCount) * sizeof(std::uint16_t)))
    {
        return;
    }

    out.resize(static_cast<std::size_t>(header.skinRefCount));
    std::memcpy(out.data(),
                data.data() + header.skinIndex + skin * header.skinRefCount * static_cast<std::int32_t>(sizeof(std::uint16_t)),
                out.size() * sizeof(std::uint16_t));
}

bool readStudioTextures(const std::vector<std::uint8_t>& data,
                        const StudioHeader& header,
                        std::vector<StudioTexture>& out,
                        std::string& error)
{
    out.clear();
    for (std::int32_t i = 0; i < header.textureCount; ++i)
    {
        StudioTextureHeader textureHeader = {};
        if (!readStruct(data, static_cast<std::size_t>(header.textureIndex) + static_cast<std::size_t>(i) * sizeof(StudioTextureHeader), textureHeader))
        {
            error = "MDL texture table points outside file";
            return false;
        }

        if (textureHeader.width <= 0 || textureHeader.height <= 0 ||
            !validRange(data, textureHeader.index, static_cast<std::size_t>(textureHeader.width) * static_cast<std::size_t>(textureHeader.height) + 256u * 3u))
        {
            continue;
        }

        StudioTexture texture;
        texture.masked = (textureHeader.flags & StudioTextureMasked) != 0;
        texture.image.name = fixedString(textureHeader.name, sizeof(textureHeader.name));
        texture.image.width = static_cast<std::uint32_t>(textureHeader.width);
        texture.image.height = static_cast<std::uint32_t>(textureHeader.height);
        const std::size_t pixelCount = static_cast<std::size_t>(textureHeader.width) * static_cast<std::size_t>(textureHeader.height);
        const std::size_t pixelOffset = static_cast<std::size_t>(textureHeader.index);
        const std::size_t paletteOffset = pixelOffset + pixelCount;
        texture.image.rgba.resize(pixelCount * 4u);
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const std::uint8_t paletteIndex = data[pixelOffset + pixel];
            const std::size_t colorOffset = paletteOffset + static_cast<std::size_t>(paletteIndex) * 3u;
            texture.image.rgba[pixel * 4u + 0u] = data[colorOffset + 0u];
            texture.image.rgba[pixel * 4u + 1u] = data[colorOffset + 1u];
            texture.image.rgba[pixel * 4u + 2u] = data[colorOffset + 2u];
            texture.image.rgba[pixel * 4u + 3u] = texture.masked && paletteIndex == 255 ? 0u : 255u;
        }
        out.push_back(std::move(texture));
    }
    return true;
}
}

bool StudioModel::load(const std::filesystem::path& path, std::int32_t body, std::int32_t skin, std::int32_t sequence, std::string& error)
{
    error.clear();
    name.clear();
    frames.clear();
    textures.clear();
    framesPerSecond = 10.0f;
    selectedSequence = 0;
    mins = {};
    maxs = {};

    std::vector<std::uint8_t> data;
    if (!readFile(path, data))
    {
        error = "could not open MDL file";
        return false;
    }

    StudioHeader header = {};
    if (!readStruct(data, 0, header) || header.id != StudioId || header.version != StudioVersion)
    {
        error = "unsupported Studio MDL header";
        return false;
    }

    name = fixedString(header.name, sizeof(header.name));
    mins = {header.min[0], header.min[1], header.min[2]};
    maxs = {header.max[0], header.max[1], header.max[2]};
    if (body < 0)
    {
        body = 0;
    }

    std::vector<StudioBone> bones;
    bones.resize(static_cast<std::size_t>(std::max(0, header.boneCount)));
    for (std::int32_t i = 0; i < header.boneCount; ++i)
    {
        StudioBone bone = {};
        if (!readStruct(data, static_cast<std::size_t>(header.boneIndex) + static_cast<std::size_t>(i) * sizeof(StudioBone), bone))
        {
            error = "MDL bone table points outside file";
            return false;
        }
        bones[static_cast<std::size_t>(i)] = bone;
    }

    std::vector<StudioSequenceDesc> sequences;
    sequences.resize(static_cast<std::size_t>(std::max(0, header.sequenceCount)));
    for (std::int32_t i = 0; i < header.sequenceCount; ++i)
    {
        readStruct(data, static_cast<std::size_t>(header.sequenceIndex) + static_cast<std::size_t>(i) * sizeof(StudioSequenceDesc), sequences[static_cast<std::size_t>(i)]);
    }
    selectedSequence = chooseSequence(sequences, sequence);
    const StudioSequenceDesc* selectedSequenceDesc = sequences.empty() ? nullptr : &sequences[static_cast<std::size_t>(selectedSequence)];
    if (selectedSequenceDesc)
    {
        framesPerSecond = std::max(1.0f, selectedSequenceDesc->fps);
    }

    std::vector<std::uint16_t> skinRefs;
    readSkinRefs(data, header, skin, skinRefs);
    if (header.textureCount > 0)
    {
        if (!readStudioTextures(data, header, textures, error))
        {
            return false;
        }
    }
    else
    {
        std::vector<std::uint8_t> textureData;
        const std::filesystem::path texturePath = companionTexturePath(path);
        StudioHeader textureHeader = {};
        if (readFile(texturePath, textureData) &&
            readStruct(textureData, 0, textureHeader) &&
            textureHeader.id == StudioId &&
            textureHeader.version == StudioVersion)
        {
            readSkinRefs(textureData, textureHeader, skin, skinRefs);
            if (!readStudioTextures(textureData, textureHeader, textures, error))
            {
                return false;
            }
        }
    }

    const std::int32_t frameCount = selectedSequenceDesc ? std::max(1, selectedSequenceDesc->frameCount) : 1;
    frames.resize(static_cast<std::size_t>(frameCount));
    for (std::int32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        const std::vector<Matrix3x4> boneTransforms = buildBoneTransforms(data, bones, selectedSequenceDesc, static_cast<float>(frameIndex));
        std::vector<StudioVertex>& frameVertices = frames[static_cast<std::size_t>(frameIndex)];

        for (std::int32_t bodyPartIndex = 0; bodyPartIndex < header.bodyPartCount; ++bodyPartIndex)
        {
            StudioBodyPart bodyPart = {};
            if (!readStruct(data, static_cast<std::size_t>(header.bodyPartIndex) + static_cast<std::size_t>(bodyPartIndex) * sizeof(StudioBodyPart), bodyPart))
            {
                error = "MDL bodypart table points outside file";
                return false;
            }

            std::int32_t selectedModelIndex = 0;
            if (bodyPart.modelCount > 1 && bodyPart.base > 0)
            {
                selectedModelIndex = (body / bodyPart.base) % bodyPart.modelCount;
                if (selectedModelIndex < 0)
                {
                    selectedModelIndex = 0;
                }
            }

            StudioSubModel model = {};
            if (!readStruct(data,
                            static_cast<std::size_t>(bodyPart.modelIndex) + static_cast<std::size_t>(selectedModelIndex) * sizeof(StudioSubModel),
                            model))
            {
                error = "MDL model table points outside file";
                return false;
            }

            if (!validRange(data, model.vertexIndex, static_cast<std::size_t>(std::max(0, model.vertexCount)) * sizeof(float) * 3u) ||
                !validRange(data, model.vertexInfoIndex, static_cast<std::size_t>(std::max(0, model.vertexCount))))
            {
                continue;
            }

            const auto* vertexBone = reinterpret_cast<const std::uint8_t*>(data.data() + model.vertexInfoIndex);
            const auto* rawVertices = reinterpret_cast<const float*>(data.data() + model.vertexIndex);

            for (std::int32_t meshIndex = 0; meshIndex < model.meshCount; ++meshIndex)
            {
                StudioMesh mesh = {};
                if (!readStruct(data, static_cast<std::size_t>(model.meshIndex) + static_cast<std::size_t>(meshIndex) * sizeof(StudioMesh), mesh))
                {
                    error = "MDL mesh table points outside file";
                    return false;
                }

                std::size_t cursor = static_cast<std::size_t>(mesh.triangleIndex);
                while (cursor + sizeof(std::int16_t) <= data.size())
                {
                    std::int16_t command = 0;
                    std::memcpy(&command, data.data() + cursor, sizeof(command));
                    cursor += sizeof(command);
                    if (command == 0)
                    {
                        break;
                    }

                    const bool fan = command < 0;
                    const std::size_t count = static_cast<std::size_t>(std::abs(command));
                    if (cursor + count * 4u * sizeof(std::int16_t) > data.size())
                    {
                        break;
                    }

                    struct TriVert
                    {
                        std::int16_t vertex;
                        std::int16_t normal;
                        std::int16_t s;
                        std::int16_t t;
                    };
                    std::vector<TriVert> strip(count);
                    std::memcpy(strip.data(), data.data() + cursor, count * sizeof(TriVert));
                    cursor += count * sizeof(TriVert);

                    auto makeVertex = [&](const TriVert& triVert) {
                        StudioVertex out;
                        if (triVert.vertex < 0 || triVert.vertex >= model.vertexCount)
                        {
                            return out;
                        }

                        const std::size_t vertexIndex = static_cast<std::size_t>(triVert.vertex);
                        Vec3 position = {
                            rawVertices[vertexIndex * 3u + 0u],
                            rawVertices[vertexIndex * 3u + 1u],
                            rawVertices[vertexIndex * 3u + 2u],
                        };
                        const std::uint8_t boneIndex = vertexBone[vertexIndex];
                        if (boneIndex < boneTransforms.size())
                        {
                            position = transformPoint(boneTransforms[boneIndex], position);
                        }
                        out.position = position;
                        std::uint32_t textureIndex = static_cast<std::uint32_t>(std::max(0, mesh.skinRef));
                        if (mesh.skinRef >= 0 && static_cast<std::size_t>(mesh.skinRef) < skinRefs.size())
                        {
                            textureIndex = skinRefs[static_cast<std::size_t>(mesh.skinRef)];
                        }
                        out.textureIndex = textureIndex;
                        if (out.textureIndex < textures.size() && textures[out.textureIndex].image.width > 0 && textures[out.textureIndex].image.height > 0)
                        {
                            out.u = static_cast<float>(triVert.s) / static_cast<float>(textures[out.textureIndex].image.width);
                            out.v = static_cast<float>(triVert.t) / static_cast<float>(textures[out.textureIndex].image.height);
                        }
                        return out;
                    };

                    for (std::size_t i = 2; i < strip.size(); ++i)
                    {
                        if (fan)
                        {
                            frameVertices.push_back(makeVertex(strip[0]));
                            frameVertices.push_back(makeVertex(strip[i - 1]));
                            frameVertices.push_back(makeVertex(strip[i]));
                        }
                        else if ((i & 1u) == 0)
                        {
                            frameVertices.push_back(makeVertex(strip[i - 2]));
                            frameVertices.push_back(makeVertex(strip[i - 1]));
                            frameVertices.push_back(makeVertex(strip[i]));
                        }
                        else
                        {
                            frameVertices.push_back(makeVertex(strip[i - 1]));
                            frameVertices.push_back(makeVertex(strip[i - 2]));
                            frameVertices.push_back(makeVertex(strip[i]));
                        }
                    }
                }
            }
        }
    }

    return true;
}
}

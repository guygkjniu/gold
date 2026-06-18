#include "engine/BspMap.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <cstdlib>
#include <map>
#include <sstream>
#include <vector>

namespace goldsrc
{
const std::string* BspEntity::value(const std::string& key) const
{
    const auto found = keyValues.find(key);
    return found == keyValues.end() ? nullptr : &found->second;
}

namespace
{
constexpr std::int32_t BspVersion = 30;
constexpr std::size_t LumpCount = 15;
constexpr std::int32_t ContentsSolid = -2;
constexpr float TraceEpsilon = 0.03125f;

enum LumpIndex : std::size_t
{
    Entities = 0,
    Planes = 1,
    Textures = 2,
    Vertices = 3,
    Visibility = 4,
    Nodes = 5,
    TextureInfo = 6,
    Faces = 7,
    Lighting = 8,
    ClipNodes = 9,
    Leaves = 10,
    MarkSurfaces = 11,
    Edges = 12,
    SurfEdges = 13,
    Models = 14,
};

struct Lump
{
    std::int32_t offset = 0;
    std::int32_t length = 0;
};

template <typename T>
std::uint32_t countRecords(const Lump& lump)
{
    return static_cast<std::uint32_t>(lump.length / static_cast<std::int32_t>(sizeof(T)));
}

template <typename T>
bool readRecordLump(std::ifstream& file, const Lump& lump, std::vector<T>& out, std::string& error, const char* name)
{
    out.resize(countRecords<T>(lump));
    if (out.empty())
    {
        return true;
    }

    file.seekg(lump.offset, std::ios::beg);
    file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size() * sizeof(T)));
    if (!file)
    {
        std::ostringstream message;
        message << "could not read " << name << " lump";
        error = message.str();
        return false;
    }

    return true;
}

bool validateLump(const Lump& lump, std::uintmax_t fileSize, std::string& error, std::size_t index)
{
    if (lump.offset < 0 || lump.length < 0)
    {
        std::ostringstream message;
        message << "lump " << index << " has a negative offset or length";
        error = message.str();
        return false;
    }

    const auto offset = static_cast<std::uintmax_t>(lump.offset);
    const auto length = static_cast<std::uintmax_t>(lump.length);
    if (offset > fileSize || length > fileSize || offset + length > fileSize)
    {
        std::ostringstream message;
        message << "lump " << index << " points outside the BSP file";
        error = message.str();
        return false;
    }

    return true;
}

template <typename T>
bool validateRecordLump(const Lump& lump, std::string& error, std::size_t index, const char* name)
{
    if ((lump.length % static_cast<std::int32_t>(sizeof(T))) != 0)
    {
        std::ostringstream message;
        message << name << " lump " << index << " has a partial record";
        error = message.str();
        return false;
    }

    return true;
}

struct BspPlane
{
    float normal[3];
    float distance;
    std::int32_t type;
};

struct BspVertex
{
    float point[3];
};

struct BspNode
{
    std::int32_t plane;
    std::int16_t children[2];
    std::int16_t mins[3];
    std::int16_t maxs[3];
    std::uint16_t firstFace;
    std::uint16_t faceCount;
};

struct BspTextureInfo
{
    float vectors[2][4];
    std::int32_t mipTexture;
    std::int32_t flags;
};

struct BspMipTexture
{
    std::string name;
    std::uint32_t width = 1;
    std::uint32_t height = 1;
};

struct FaceBuildVertex
{
    Vec3 position;
    float s = 0.0f;
    float t = 0.0f;
};

struct BspFace
{
    std::uint16_t plane;
    std::uint16_t side;
    std::int32_t firstEdge;
    std::uint16_t edgeCount;
    std::uint16_t textureInfo;
    std::uint8_t styles[4];
    std::int32_t lightOffset;
};

struct BspClipNode
{
    std::int32_t plane;
    std::int16_t children[2];
};

struct BspLeaf
{
    std::int32_t contents;
    std::int32_t visibilityOffset;
    std::int16_t mins[3];
    std::int16_t maxs[3];
    std::uint16_t firstMarkSurface;
    std::uint16_t markSurfaceCount;
    std::uint8_t ambientLevels[4];
};

struct BspEdge
{
    std::uint16_t vertices[2];
};

struct BspModel
{
    float mins[3];
    float maxs[3];
    float origin[3];
    std::int32_t headNodes[4];
    std::int32_t visibilityLeaves;
    std::int32_t firstFace;
    std::int32_t faceCount;
};

std::string fixedString(const char* text, std::size_t size)
{
    std::size_t length = 0;
    while (length < size && text[length] != '\0')
    {
        ++length;
    }
    return std::string(text, text + length);
}

std::vector<std::filesystem::path> parseWadPaths(const std::string& entities)
{
    std::vector<std::filesystem::path> paths;
    const std::string key = "\"wad\"";
    const std::size_t keyPosition = entities.find(key);
    if (keyPosition == std::string::npos)
    {
        return paths;
    }

    const std::size_t valueStartQuote = entities.find('"', keyPosition + key.size());
    if (valueStartQuote == std::string::npos)
    {
        return paths;
    }

    const std::size_t valueEndQuote = entities.find('"', valueStartQuote + 1);
    if (valueEndQuote == std::string::npos)
    {
        return paths;
    }

    std::string value = entities.substr(valueStartQuote + 1, valueEndQuote - valueStartQuote - 1);
    std::replace(value.begin(), value.end(), '/', '\\');

    std::size_t start = 0;
    while (start < value.size())
    {
        const std::size_t end = value.find(';', start);
        std::string item = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front())))
        {
            item.erase(item.begin());
        }
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back())))
        {
            item.pop_back();
        }

        if (!item.empty())
        {
            paths.emplace_back(item);
        }

        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }

    return paths;
}

std::string normalizeTextureName(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return name;
}

bool isVisibleTexture(const std::string& textureName)
{
    const std::string name = normalizeTextureName(textureName);
    return name != "aaatrigger" &&
           name != "clip" &&
           name != "origin" &&
           name != "null" &&
           name != "hint" &&
           name != "skip" &&
           name != "trigger";
}

bool isSkyTexture(const std::string& textureName)
{
    return normalizeTextureName(textureName) == "sky";
}

bool isTranslucentTexture(const std::string& textureName)
{
    const std::string name = normalizeTextureName(textureName);
    return !name.empty() &&
           (name.front() == '{' ||
            name.find("glass") != std::string::npos ||
            name.find("grate") != std::string::npos ||
            name.find("fence") != std::string::npos ||
            name.find("trans") != std::string::npos);
}

std::string parseQuotedToken(const std::string& text, std::size_t& cursor)
{
    const std::size_t start = text.find('"', cursor);
    if (start == std::string::npos)
    {
        cursor = text.size();
        return {};
    }

    const std::size_t end = text.find('"', start + 1);
    if (end == std::string::npos)
    {
        cursor = text.size();
        return {};
    }

    cursor = end + 1;
    return text.substr(start + 1, end - start - 1);
}

bool parseOrigin(const std::string& text, Vec3& origin)
{
    std::istringstream stream(text);
    return static_cast<bool>(stream >> origin.x >> origin.y >> origin.z);
}

std::int32_t parseIntOrZero(const std::string& text)
{
    std::istringstream stream(text);
    std::int32_t value = 0;
    stream >> value;
    return value;
}

std::int32_t parseIntOrDefault(const std::string& text, std::int32_t fallback)
{
    std::istringstream stream(text);
    std::int32_t value = fallback;
    stream >> value;
    return value;
}

std::int32_t parseBrushModelIndex(const std::string& model)
{
    if (model.size() < 2 || model.front() != '*')
    {
        return -1;
    }
    return parseIntOrDefault(model.substr(1), -1);
}

void parseEntityAngles(const std::map<std::string, std::string>& keys, Vec3& angles)
{
    const auto fullAngles = keys.find("angles");
    if (fullAngles != keys.end())
    {
        parseOrigin(fullAngles->second, angles);
        return;
    }

    const auto angle = keys.find("angle");
    if (angle == keys.end())
    {
        return;
    }

    const std::int32_t yaw = parseIntOrZero(angle->second);
    if (yaw == -1)
    {
        angles.x = -90.0f;
    }
    else if (yaw == -2)
    {
        angles.x = 90.0f;
    }
    else
    {
        angles.y = static_cast<float>(yaw);
    }
}

std::vector<BspEntity> parseEntities(const std::string& entities)
{
    std::vector<BspEntity> parsed;
    std::size_t cursor = 0;

    while (cursor < entities.size())
    {
        const std::size_t open = entities.find('{', cursor);
        if (open == std::string::npos)
        {
            break;
        }

        const std::size_t close = entities.find('}', open + 1);
        if (close == std::string::npos)
        {
            break;
        }

        const std::string block = entities.substr(open + 1, close - open - 1);
        BspEntity entity;
        entity.index = parsed.size();
        std::size_t blockCursor = 0;
        while (blockCursor < block.size())
        {
            std::string key = parseQuotedToken(block, blockCursor);
            std::string value = parseQuotedToken(block, blockCursor);
            if (key.empty())
            {
                break;
            }
            entity.keyValues[std::move(key)] = std::move(value);
        }

        const auto origin = entity.keyValues.find("origin");
        if (origin != entity.keyValues.end())
        {
            entity.hasOrigin = parseOrigin(origin->second, entity.origin);
        }
        parseEntityAngles(entity.keyValues, entity.angles);

        const auto body = entity.keyValues.find("body");
        if (body != entity.keyValues.end())
        {
            entity.body = parseIntOrZero(body->second);
        }
        const auto skin = entity.keyValues.find("skin");
        if (skin != entity.keyValues.end())
        {
            entity.skin = parseIntOrZero(skin->second);
        }
        const auto sequence = entity.keyValues.find("sequence");
        if (sequence != entity.keyValues.end())
        {
            entity.sequence = parseIntOrZero(sequence->second);
        }
        const auto className = entity.keyValues.find("classname");
        if (className != entity.keyValues.end())
        {
            entity.className = className->second;
        }
        const auto targetName = entity.keyValues.find("targetname");
        if (targetName != entity.keyValues.end())
        {
            entity.targetName = targetName->second;
        }
        const auto target = entity.keyValues.find("target");
        if (target != entity.keyValues.end())
        {
            entity.target = target->second;
        }
        const auto model = entity.keyValues.find("model");
        if (model != entity.keyValues.end())
        {
            entity.model = model->second;
            entity.modelIndex = parseBrushModelIndex(entity.model);
            entity.brushEntity = entity.modelIndex > 0;
        }

        parsed.push_back(std::move(entity));

        cursor = close + 1;
    }

    return parsed;
}

std::vector<BspEntityMarker> buildEntityMarkers(const std::vector<BspEntity>& entities)
{
    std::vector<BspEntityMarker> markers;
    for (const BspEntity& entity : entities)
    {
        if (!entity.hasOrigin)
        {
            continue;
        }

        BspEntityMarker marker;
        marker.origin = entity.origin;
        marker.angles = entity.angles;
        marker.body = entity.body;
        marker.skin = entity.skin;
        marker.sequence = entity.sequence;
        marker.className = entity.className;
        marker.model = entity.model;
        markers.push_back(std::move(marker));
    }
    return markers;
}

bool readByteLump(std::ifstream& file, const Lump& lump, std::vector<std::uint8_t>& out, std::string& error, const char* name)
{
    out.resize(static_cast<std::size_t>(lump.length));
    if (out.empty())
    {
        return true;
    }

    file.seekg(lump.offset, std::ios::beg);
    file.read(reinterpret_cast<char*>(out.data()), lump.length);
    if (!file)
    {
        std::ostringstream message;
        message << "could not read " << name << " lump";
        error = message.str();
        return false;
    }

    return true;
}

template <typename T>
T readPod(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    T value = {};
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

bool parseMipTextures(const std::vector<std::uint8_t>& data, std::vector<BspMipTexture>& out, std::string& error)
{
    out.clear();
    if (data.size() < sizeof(std::int32_t))
    {
        return true;
    }

    const std::int32_t count = readPod<std::int32_t>(data, 0);
    if (count < 0)
    {
        error = "texture lump has a negative texture count";
        return false;
    }

    const std::size_t directoryBytes = sizeof(std::int32_t) + static_cast<std::size_t>(count) * sizeof(std::int32_t);
    if (directoryBytes > data.size())
    {
        error = "texture lump directory points outside the lump";
        return false;
    }

    out.resize(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i)
    {
        const std::int32_t offset = readPod<std::int32_t>(data, sizeof(std::int32_t) + static_cast<std::size_t>(i) * sizeof(std::int32_t));
        if (offset < 0)
        {
            continue;
        }

        const std::size_t textureOffset = static_cast<std::size_t>(offset);
        if (textureOffset + 40u > data.size())
        {
            error = "texture lump entry points outside the lump";
            return false;
        }

        out[static_cast<std::size_t>(i)].name = fixedString(reinterpret_cast<const char*>(data.data() + textureOffset), 16);
        out[static_cast<std::size_t>(i)].width = readPod<std::uint32_t>(data, textureOffset + 16u);
        out[static_cast<std::size_t>(i)].height = readPod<std::uint32_t>(data, textureOffset + 20u);
        if (out[static_cast<std::size_t>(i)].width == 0 || out[static_cast<std::size_t>(i)].height == 0)
        {
            out[static_cast<std::size_t>(i)].width = 1;
            out[static_cast<std::size_t>(i)].height = 1;
        }
    }

    return true;
}

void attachFaceLightmap(BspRenderFace& renderFace,
                        const std::vector<FaceBuildVertex>& buildVertices,
                        const BspFace& face,
                        const std::vector<std::uint8_t>& lightingBytes)
{
    if (face.lightOffset < 0 || lightingBytes.empty() || buildVertices.empty())
    {
        return;
    }

    float minS = std::numeric_limits<float>::max();
    float minT = std::numeric_limits<float>::max();
    float maxS = std::numeric_limits<float>::lowest();
    float maxT = std::numeric_limits<float>::lowest();
    for (const FaceBuildVertex& vertex : buildVertices)
    {
        minS = std::min(minS, vertex.s);
        minT = std::min(minT, vertex.t);
        maxS = std::max(maxS, vertex.s);
        maxT = std::max(maxT, vertex.t);
    }

    const int lightMinS = static_cast<int>(std::floor(minS / 16.0f));
    const int lightMinT = static_cast<int>(std::floor(minT / 16.0f));
    const int lightMaxS = static_cast<int>(std::ceil(maxS / 16.0f));
    const int lightMaxT = static_cast<int>(std::ceil(maxT / 16.0f));
    const int lightWidth = (lightMaxS - lightMinS) + 1;
    const int lightHeight = (lightMaxT - lightMinT) + 1;
    if (lightWidth <= 0 || lightHeight <= 0)
    {
        return;
    }

    const std::size_t sampleCount = static_cast<std::size_t>(lightWidth) * static_cast<std::size_t>(lightHeight);
    const std::size_t lightOffset = static_cast<std::size_t>(face.lightOffset);
    if (lightOffset + sampleCount * 3u > lightingBytes.size())
    {
        return;
    }

    renderFace.lightmapWidth = static_cast<std::uint32_t>(lightWidth);
    renderFace.lightmapHeight = static_cast<std::uint32_t>(lightHeight);
    renderFace.lightmapRgba.resize(sampleCount * 4u);
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
        const std::uint8_t r = lightingBytes[lightOffset + i * 3u + 0u];
        const std::uint8_t g = lightingBytes[lightOffset + i * 3u + 1u];
        const std::uint8_t b = lightingBytes[lightOffset + i * 3u + 2u];
        renderFace.lightmapRgba[i * 4u + 0u] = r;
        renderFace.lightmapRgba[i * 4u + 1u] = g;
        renderFace.lightmapRgba[i * 4u + 2u] = b;
        renderFace.lightmapRgba[i * 4u + 3u] = 255;
    }

    for (std::size_t i = 0; i < renderFace.vertices.size() && i < buildVertices.size(); ++i)
    {
        renderFace.vertices[i].lightU =
            (buildVertices[i].s - static_cast<float>(lightMinS * 16) + 8.0f) /
            static_cast<float>(lightWidth * 16);
        renderFace.vertices[i].lightV =
            (buildVertices[i].t - static_cast<float>(lightMinT * 16) + 8.0f) /
            static_cast<float>(lightHeight * 16);
    }
}
}

bool BspMap::load(const std::filesystem::path& path, std::string& error)
{
    error.clear();
    path_ = path;
    summary_ = {};
    renderFaces_.clear();
    entities_.clear();
    entityMarkers_.clear();
    brushModels_.clear();
    wadPaths_.clear();
    collisionPlanes_.clear();
    collisionNodes_.clear();
    collisionModels_.clear();
    visibilityNodes_.clear();
    visibilityLeaves_.clear();
    markSurfaces_.clear();
    originalFaceToRenderFace_.clear();
    visibilityBytes_.clear();

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "could not open BSP file";
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff fileSizeStream = file.tellg();
    if (fileSizeStream < 0)
    {
        error = "could not determine BSP file size";
        return false;
    }

    const auto fileSize = static_cast<std::uintmax_t>(fileSizeStream);
    file.seekg(0, std::ios::beg);

    std::int32_t version = 0;
    std::array<Lump, LumpCount> lumps = {};

    file.read(reinterpret_cast<char*>(&version), sizeof(version));

    char magic[4] = {};
    std::memcpy(magic, &version, sizeof(magic));
    if (magic[0] == 'V' && magic[1] == 'B' && magic[2] == 'S' && magic[3] == 'P')
    {
        error = "Source-engine VBSP file detected; GoldSrc maps must be BSP version 30";
        return false;
    }

    file.read(reinterpret_cast<char*>(lumps.data()), static_cast<std::streamsize>(lumps.size() * sizeof(Lump)));

    if (!file)
    {
        error = "BSP file is too small for a header";
        return false;
    }

    if (version != BspVersion)
    {
        std::ostringstream message;
        message << "unsupported BSP version " << version << " (expected " << BspVersion << ")";
        error = message.str();
        return false;
    }

    for (std::size_t i = 0; i < lumps.size(); ++i)
    {
        if (!validateLump(lumps[i], fileSize, error, i))
        {
            return false;
        }
    }

    if (!validateRecordLump<BspPlane>(lumps[Planes], error, Planes, "planes") ||
        !validateRecordLump<BspVertex>(lumps[Vertices], error, Vertices, "vertices") ||
        !validateRecordLump<BspNode>(lumps[Nodes], error, Nodes, "nodes") ||
        !validateRecordLump<BspTextureInfo>(lumps[TextureInfo], error, TextureInfo, "texture info") ||
        !validateRecordLump<BspFace>(lumps[Faces], error, Faces, "faces") ||
        !validateRecordLump<BspClipNode>(lumps[ClipNodes], error, ClipNodes, "clip nodes") ||
        !validateRecordLump<BspLeaf>(lumps[Leaves], error, Leaves, "leaves") ||
        !validateRecordLump<std::uint16_t>(lumps[MarkSurfaces], error, MarkSurfaces, "mark surfaces") ||
        !validateRecordLump<BspEdge>(lumps[Edges], error, Edges, "edges") ||
        !validateRecordLump<std::int32_t>(lumps[SurfEdges], error, SurfEdges, "surf edges") ||
        !validateRecordLump<BspModel>(lumps[Models], error, Models, "models"))
    {
        return false;
    }

    summary_.version = version;
    summary_.entitiesBytes = static_cast<std::uint32_t>(lumps[Entities].length);
    summary_.planes = countRecords<BspPlane>(lumps[Planes]);
    summary_.texturesBytes = static_cast<std::uint32_t>(lumps[Textures].length);
    summary_.vertices = countRecords<BspVertex>(lumps[Vertices]);
    summary_.visibilityBytes = static_cast<std::uint32_t>(lumps[Visibility].length);
    summary_.nodes = countRecords<BspNode>(lumps[Nodes]);
    summary_.textureInfo = countRecords<BspTextureInfo>(lumps[TextureInfo]);
    summary_.faces = countRecords<BspFace>(lumps[Faces]);
    summary_.lightingBytes = static_cast<std::uint32_t>(lumps[Lighting].length);
    summary_.clipNodes = countRecords<BspClipNode>(lumps[ClipNodes]);
    summary_.leaves = countRecords<BspLeaf>(lumps[Leaves]);
    summary_.markSurfaces = countRecords<std::uint16_t>(lumps[MarkSurfaces]);
    summary_.edges = countRecords<BspEdge>(lumps[Edges]);
    summary_.surfEdges = countRecords<std::int32_t>(lumps[SurfEdges]);
    summary_.models = countRecords<BspModel>(lumps[Models]);

    std::vector<BspPlane> planes;
    std::vector<BspVertex> vertices;
    std::vector<BspTextureInfo> textureInfos;
    std::vector<BspFace> faces;
    std::vector<BspNode> nodes;
    std::vector<BspLeaf> leaves;
    std::vector<std::uint16_t> markSurfaces;
    std::vector<BspClipNode> clipNodes;
    std::vector<BspEdge> edges;
    std::vector<BspModel> models;
    std::vector<std::int32_t> surfEdges;
    std::vector<std::uint8_t> textureBytes;
    std::vector<std::uint8_t> entityBytes;
    std::vector<std::uint8_t> lightingBytes;

    if (!readRecordLump(file, lumps[Planes], planes, error, "planes") ||
        !readRecordLump(file, lumps[Vertices], vertices, error, "vertices") ||
        !readRecordLump(file, lumps[Nodes], nodes, error, "nodes") ||
        !readRecordLump(file, lumps[TextureInfo], textureInfos, error, "texture info") ||
        !readRecordLump(file, lumps[Faces], faces, error, "faces") ||
        !readRecordLump(file, lumps[ClipNodes], clipNodes, error, "clip nodes") ||
        !readRecordLump(file, lumps[Leaves], leaves, error, "leaves") ||
        !readRecordLump(file, lumps[MarkSurfaces], markSurfaces, error, "mark surfaces") ||
        !readRecordLump(file, lumps[Edges], edges, error, "edges") ||
        !readRecordLump(file, lumps[Models], models, error, "models") ||
        !readRecordLump(file, lumps[SurfEdges], surfEdges, error, "surf edges") ||
        !readByteLump(file, lumps[Textures], textureBytes, error, "textures") ||
        !readByteLump(file, lumps[Visibility], visibilityBytes_, error, "visibility") ||
        !readByteLump(file, lumps[Lighting], lightingBytes, error, "lighting") ||
        !readByteLump(file, lumps[Entities], entityBytes, error, "entities"))
    {
        renderFaces_.clear();
        return false;
    }

    std::vector<BspMipTexture> mipTextures;
    if (!parseMipTextures(textureBytes, mipTextures, error))
    {
        return false;
    }

    if (!entityBytes.empty())
    {
        const std::string entities(reinterpret_cast<const char*>(entityBytes.data()), entityBytes.size());
        wadPaths_ = parseWadPaths(entities);
        entities_ = parseEntities(entities);
        entityMarkers_ = buildEntityMarkers(entities_);
    }

    visibilityNodes_.reserve(nodes.size());
    for (const BspNode& node : nodes)
    {
        visibilityNodes_.push_back({node.plane, {node.children[0], node.children[1]}, -1, node.firstFace, node.faceCount});
    }

    visibilityLeaves_.reserve(leaves.size());
    for (const BspLeaf& leaf : leaves)
    {
        visibilityLeaves_.push_back({leaf.contents, leaf.visibilityOffset, leaf.firstMarkSurface, leaf.markSurfaceCount, -1});
    }
    markSurfaces_ = std::move(markSurfaces);

    for (std::size_t nodeIndex = 0; nodeIndex < visibilityNodes_.size(); ++nodeIndex)
    {
        for (std::int32_t child : visibilityNodes_[nodeIndex].children)
        {
            if (child >= 0)
            {
                if (static_cast<std::size_t>(child) < visibilityNodes_.size())
                {
                    visibilityNodes_[static_cast<std::size_t>(child)].parent = static_cast<std::int32_t>(nodeIndex);
                }
                continue;
            }

            const std::int32_t leafIndex = -child - 1;
            if (leafIndex >= 0 && static_cast<std::size_t>(leafIndex) < visibilityLeaves_.size())
            {
                visibilityLeaves_[static_cast<std::size_t>(leafIndex)].parent = static_cast<std::int32_t>(nodeIndex);
            }
        }
    }

    std::vector<std::int32_t> faceModelIndices(faces.size(), 0);
    for (std::size_t modelIndex = 0; modelIndex < models.size(); ++modelIndex)
    {
        const BspModel& model = models[modelIndex];
        if (model.firstFace < 0 || model.faceCount <= 0)
        {
            continue;
        }

        const std::size_t firstFace = static_cast<std::size_t>(model.firstFace);
        const std::size_t endFace = std::min(faces.size(), firstFace + static_cast<std::size_t>(model.faceCount));
        for (std::size_t faceIndex = firstFace; faceIndex < endFace; ++faceIndex)
        {
            faceModelIndices[faceIndex] = static_cast<std::int32_t>(modelIndex);
        }
    }

    collisionPlanes_.reserve(planes.size());
    for (const BspPlane& plane : planes)
    {
        collisionPlanes_.push_back({{plane.normal[0], plane.normal[1], plane.normal[2]}, plane.distance});
    }

    collisionNodes_.reserve(clipNodes.size());
    for (const BspClipNode& node : clipNodes)
    {
        collisionNodes_.push_back({node.plane, {node.children[0], node.children[1]}});
    }

    collisionModels_.reserve(models.size());
    brushModels_.reserve(models.size());
    for (std::size_t modelIndex = 0; modelIndex < models.size(); ++modelIndex)
    {
        const BspModel& model = models[modelIndex];
        BspCollisionModel collisionModel;
        collisionModel.mins = {model.mins[0], model.mins[1], model.mins[2]};
        collisionModel.maxs = {model.maxs[0], model.maxs[1], model.maxs[2]};
        collisionModel.origin = {model.origin[0], model.origin[1], model.origin[2]};
        for (std::size_t i = 0; i < 4; ++i)
        {
            collisionModel.headNodes[i] = model.headNodes[i];
        }
        collisionModels_.push_back(collisionModel);
        brushModels_.push_back({static_cast<std::int32_t>(modelIndex),
                                {model.mins[0], model.mins[1], model.mins[2]},
                                {model.maxs[0], model.maxs[1], model.maxs[2]},
                                {model.origin[0], model.origin[1], model.origin[2]}});
    }

    renderFaces_.reserve(faces.size());
    originalFaceToRenderFace_.assign(faces.size(), -1);
    for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex)
    {
        const BspFace& face = faces[faceIndex];
        if (face.edgeCount < 3)
        {
            continue;
        }

        if (face.plane >= planes.size())
        {
            error = "face references an invalid plane";
            renderFaces_.clear();
            return false;
        }

        if (face.firstEdge < 0 ||
            static_cast<std::uint32_t>(face.firstEdge) + face.edgeCount > surfEdges.size())
        {
            error = "face references surfedges outside the surfedge lump";
            renderFaces_.clear();
            return false;
        }

        if (face.textureInfo >= textureInfos.size())
        {
            error = "face references invalid texture info";
            renderFaces_.clear();
            return false;
        }

        BspRenderFace renderFace;
        renderFace.vertices.reserve(face.edgeCount);
        renderFace.modelIndex = faceModelIndices[faceIndex];
        renderFace.brushEntity = renderFace.modelIndex > 0;
        renderFace.pvsControlled = !renderFace.brushEntity;
        std::vector<FaceBuildVertex> buildVertices;
        buildVertices.reserve(face.edgeCount);

        const BspPlane& plane = planes[face.plane];
        renderFace.color[0] = 0.25f + 0.65f * std::abs(plane.normal[0]);
        renderFace.color[1] = 0.25f + 0.65f * std::abs(plane.normal[1]);
        renderFace.color[2] = 0.25f + 0.65f * std::abs(plane.normal[2]);

        const BspTextureInfo& textureInfo = textureInfos[face.textureInfo];
        if (textureInfo.mipTexture >= 0 && static_cast<std::size_t>(textureInfo.mipTexture) < mipTextures.size())
        {
            const BspMipTexture& texture = mipTextures[static_cast<std::size_t>(textureInfo.mipTexture)];
            renderFace.textureName = texture.name;
            renderFace.textureWidth = texture.width;
            renderFace.textureHeight = texture.height;
            renderFace.sky = isSkyTexture(texture.name);
            renderFace.translucent = isTranslucentTexture(texture.name);
            renderFace.visible = isVisibleTexture(texture.name);
        }

        for (std::uint16_t i = 0; i < face.edgeCount; ++i)
        {
            const std::int32_t surfEdge = surfEdges[static_cast<std::size_t>(face.firstEdge) + i];
            const std::size_t edgeIndex = static_cast<std::size_t>(std::abs(surfEdge));
            if (edgeIndex >= edges.size())
            {
                error = "face references an invalid edge";
                renderFaces_.clear();
                return false;
            }

            const BspEdge& edge = edges[edgeIndex];
            const std::uint16_t vertexIndex = surfEdge >= 0 ? edge.vertices[0] : edge.vertices[1];
            if (vertexIndex >= vertices.size())
            {
                error = "edge references an invalid vertex";
                renderFaces_.clear();
                return false;
            }

            const BspVertex& vertex = vertices[vertexIndex];
            const Vec3 position = {vertex.point[0], vertex.point[1], vertex.point[2]};
            const float s = position.x * textureInfo.vectors[0][0] +
                            position.y * textureInfo.vectors[0][1] +
                            position.z * textureInfo.vectors[0][2] +
                            textureInfo.vectors[0][3];
            const float t = position.x * textureInfo.vectors[1][0] +
                            position.y * textureInfo.vectors[1][1] +
                            position.z * textureInfo.vectors[1][2] +
                            textureInfo.vectors[1][3];
            renderFace.vertices.push_back({position, s / static_cast<float>(renderFace.textureWidth), t / static_cast<float>(renderFace.textureHeight)});
            buildVertices.push_back({position, s, t});
        }

        attachFaceLightmap(renderFace, buildVertices, face, lightingBytes);
        originalFaceToRenderFace_[faceIndex] = static_cast<std::int32_t>(renderFaces_.size());
        renderFaces_.push_back(std::move(renderFace));
    }

    return true;
}

const std::filesystem::path& BspMap::path() const
{
    return path_;
}

const BspMapSummary& BspMap::summary() const
{
    return summary_;
}

const std::vector<BspRenderFace>& BspMap::renderFaces() const
{
    return renderFaces_;
}

const std::vector<BspEntity>& BspMap::entities() const
{
    return entities_;
}

const std::vector<BspEntityMarker>& BspMap::entityMarkers() const
{
    return entityMarkers_;
}

const std::vector<BspBrushModel>& BspMap::brushModels() const
{
    return brushModels_;
}

const std::vector<std::filesystem::path>& BspMap::wadPaths() const
{
    return wadPaths_;
}

std::vector<std::uint8_t> BspMap::visibleRenderFacesFromPoint(const Vec3& point) const
{
    std::vector<std::uint8_t> visible(renderFaces_.size(), 0);
    for (std::size_t i = 0; i < renderFaces_.size(); ++i)
    {
        if (!renderFaces_[i].pvsControlled)
        {
            visible[i] = 1;
        }
    }

    if (renderFaces_.empty())
    {
        return visible;
    }

    const std::int32_t currentLeaf = findVisibilityLeaf(point);
    if (currentLeaf < 0 || static_cast<std::size_t>(currentLeaf) >= visibilityLeaves_.size())
    {
        std::fill(visible.begin(), visible.end(), static_cast<std::uint8_t>(1));
        return visible;
    }

    const std::vector<std::uint8_t> pvs = decompressPvs(visibilityLeaves_[static_cast<std::size_t>(currentLeaf)].visibilityOffset);
    if (pvs.empty())
    {
        std::fill(visible.begin(), visible.end(), static_cast<std::uint8_t>(1));
        return visible;
    }

    auto markOriginalFace = [this, &visible](std::uint32_t originalFace) {
        if (originalFace >= originalFaceToRenderFace_.size())
        {
            return;
        }

        const std::int32_t renderFace = originalFaceToRenderFace_[originalFace];
        if (renderFace >= 0 && static_cast<std::size_t>(renderFace) < visible.size())
        {
            visible[static_cast<std::size_t>(renderFace)] = 1;
        }
    };

    auto markNodeFaces = [this, &markOriginalFace](std::int32_t nodeIndex) {
        while (nodeIndex >= 0 && static_cast<std::size_t>(nodeIndex) < visibilityNodes_.size())
        {
            const BspVisibilityNode& node = visibilityNodes_[static_cast<std::size_t>(nodeIndex)];
            const std::uint32_t first = node.firstFace;
            const std::uint32_t end = first + node.faceCount;
            for (std::uint32_t faceIndex = first; faceIndex < end; ++faceIndex)
            {
                markOriginalFace(faceIndex);
            }
            nodeIndex = node.parent;
        }
    };

    auto markLeafFaces = [this, &markOriginalFace, &markNodeFaces](const BspVisibilityLeaf& leaf) {
        const std::size_t first = leaf.firstMarkSurface;
        const std::size_t count = leaf.markSurfaceCount;
        if (first < markSurfaces_.size())
        {
            const std::size_t end = std::min(markSurfaces_.size(), first + count);
            for (std::size_t i = first; i < end; ++i)
            {
                markOriginalFace(markSurfaces_[i]);
            }
        }

        markNodeFaces(leaf.parent);
    };

    markLeafFaces(visibilityLeaves_[static_cast<std::size_t>(currentLeaf)]);
    for (std::size_t pvsLeafIndex = 0; pvsLeafIndex + 1u < visibilityLeaves_.size(); ++pvsLeafIndex)
    {
        const std::size_t byteIndex = pvsLeafIndex >> 3u;
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << (pvsLeafIndex & 7u));
        if (byteIndex < pvs.size() && (pvs[byteIndex] & bit) != 0)
        {
            const std::size_t leafIndex = pvsLeafIndex + 1u;
            markLeafFaces(visibilityLeaves_[leafIndex]);
        }
    }

    return visible;
}

BspTraceResult BspMap::traceHull(const Vec3& start, const Vec3& end, int hull) const
{
    return traceHullForModel(0, start, end, hull, {});
}

BspTraceResult BspMap::traceHullForModel(std::int32_t modelIndex, const Vec3& start, const Vec3& end, int hull, const Vec3& modelOffset) const
{
    BspTraceResult trace;
    trace.allSolid = true;
    trace.endPosition = end;

    const Vec3 localStart = {start.x - modelOffset.x, start.y - modelOffset.y, start.z - modelOffset.z};
    const Vec3 localEnd = {end.x - modelOffset.x, end.y - modelOffset.y, end.z - modelOffset.z};

    std::int32_t headNode = 0;
    if (!hullHeadNodeForModel(modelIndex, hull, headNode))
    {
        trace.allSolid = true;
        trace.startSolid = true;
        trace.hit = true;
        trace.fraction = 0.0f;
        trace.endPosition = start;
        return trace;
    }

    recursiveHullTrace(headNode, headNode, 0.0f, 1.0f, localStart, localEnd, trace);
    trace.endPosition = {trace.endPosition.x + modelOffset.x, trace.endPosition.y + modelOffset.y, trace.endPosition.z + modelOffset.z};
    if (trace.fraction < 1.0f)
    {
        trace.hit = true;
    }

    if (trace.allSolid)
    {
        trace.startSolid = true;
        trace.fraction = 0.0f;
        trace.endPosition = start;
        trace.hit = true;
    }
    return trace;
}

std::int32_t BspMap::findVisibilityLeaf(const Vec3& point) const
{
    if (visibilityNodes_.empty() || collisionPlanes_.empty())
    {
        return -1;
    }

    std::int32_t nodeIndex = 0;
    while (nodeIndex >= 0)
    {
        if (static_cast<std::size_t>(nodeIndex) >= visibilityNodes_.size())
        {
            return -1;
        }

        const BspVisibilityNode& node = visibilityNodes_[static_cast<std::size_t>(nodeIndex)];
        if (node.plane < 0 || static_cast<std::size_t>(node.plane) >= collisionPlanes_.size())
        {
            return -1;
        }

        const BspCollisionPlane& plane = collisionPlanes_[static_cast<std::size_t>(node.plane)];
        const float distance = point.x * plane.normal.x + point.y * plane.normal.y + point.z * plane.normal.z - plane.distance;
        nodeIndex = node.children[distance < 0.0f ? 1 : 0];
    }

    return -nodeIndex - 1;
}

std::vector<std::uint8_t> BspMap::decompressPvs(std::int32_t visibilityOffset) const
{
    const std::size_t visibleLeafCount = visibilityLeaves_.empty() ? 0u : visibilityLeaves_.size() - 1u;
    const std::size_t rowBytes = (visibleLeafCount + 7u) >> 3u;
    if (rowBytes == 0)
    {
        return {};
    }

    if (visibilityOffset < 0 || static_cast<std::size_t>(visibilityOffset) >= visibilityBytes_.size())
    {
        return std::vector<std::uint8_t>(rowBytes, 0xff);
    }

    std::vector<std::uint8_t> decompressed;
    decompressed.reserve(rowBytes);
    std::size_t cursor = static_cast<std::size_t>(visibilityOffset);
    while (decompressed.size() < rowBytes && cursor < visibilityBytes_.size())
    {
        const std::uint8_t value = visibilityBytes_[cursor++];
        if (value != 0)
        {
            decompressed.push_back(value);
            continue;
        }

        if (cursor >= visibilityBytes_.size())
        {
            break;
        }

        const std::uint8_t zeroCount = visibilityBytes_[cursor++];
        for (std::uint8_t i = 0; i < zeroCount && decompressed.size() < rowBytes; ++i)
        {
            decompressed.push_back(0);
        }
    }

    if (decompressed.size() != rowBytes)
    {
        return {};
    }

    return decompressed;
}

std::int32_t BspMap::pointContentsForHull(const Vec3& point, int hull) const
{
    return pointContentsForModel(0, point, hull, {});
}

std::int32_t BspMap::pointContentsForModel(std::int32_t modelIndex, const Vec3& point, int hull, const Vec3& modelOffset) const
{
    std::int32_t headNode = 0;
    if (!hullHeadNodeForModel(modelIndex, hull, headNode))
    {
        return ContentsSolid;
    }

    const Vec3 localPoint = {point.x - modelOffset.x, point.y - modelOffset.y, point.z - modelOffset.z};
    return hullPointContents(headNode, localPoint);
}

bool BspMap::hullHeadNodeForModel(std::int32_t modelIndex, int hull, std::int32_t& headNode) const
{
    if (collisionModels_.empty() || collisionNodes_.empty() || collisionPlanes_.empty())
    {
        return false;
    }

    if (modelIndex < 0 || static_cast<std::size_t>(modelIndex) >= collisionModels_.size())
    {
        return false;
    }

    const int safeHull = hull < 1 || hull > 3 ? 1 : hull;
    headNode = collisionModels_[static_cast<std::size_t>(modelIndex)].headNodes[safeHull];
    return headNode >= 0 && static_cast<std::size_t>(headNode) < collisionNodes_.size();
}

bool BspMap::recursiveHullTrace(std::int32_t nodeIndex,
                                std::int32_t rootNode,
                                float startFraction,
                                float endFraction,
                                const Vec3& start,
                                const Vec3& end,
                                BspTraceResult& trace) const
{
    if (trace.fraction <= startFraction)
    {
        return false;
    }

    if (nodeIndex < 0)
    {
        if (nodeIndex == ContentsSolid)
        {
            trace.startSolid = true;
            return true;
        }

        trace.allSolid = false;
        return true;
    }

    if (static_cast<std::size_t>(nodeIndex) >= collisionNodes_.size())
    {
        return false;
    }

    const BspCollisionNode& node = collisionNodes_[static_cast<std::size_t>(nodeIndex)];
    if (node.plane < 0 || static_cast<std::size_t>(node.plane) >= collisionPlanes_.size())
    {
        return false;
    }

    const BspCollisionPlane& plane = collisionPlanes_[static_cast<std::size_t>(node.plane)];
    const float startDistance = start.x * plane.normal.x + start.y * plane.normal.y + start.z * plane.normal.z - plane.distance;
    const float endDistance = end.x * plane.normal.x + end.y * plane.normal.y + end.z * plane.normal.z - plane.distance;

    if (startDistance >= 0.0f && endDistance >= 0.0f)
    {
        return recursiveHullTrace(node.children[0], rootNode, startFraction, endFraction, start, end, trace);
    }
    if (startDistance < 0.0f && endDistance < 0.0f)
    {
        return recursiveHullTrace(node.children[1], rootNode, startFraction, endFraction, start, end, trace);
    }

    const int side = startDistance < 0.0f ? 1 : 0;
    const float denominator = startDistance - endDistance;
    float fraction = 0.0f;
    if (denominator != 0.0f)
    {
        if (startDistance < 0.0f)
        {
            fraction = (startDistance + TraceEpsilon) / denominator;
        }
        else
        {
            fraction = (startDistance - TraceEpsilon) / denominator;
        }
    }
    fraction = std::max(0.0f, std::min(1.0f, fraction));

    const float middleFraction = startFraction + (endFraction - startFraction) * fraction;
    const Vec3 middle = {
        start.x + (end.x - start.x) * fraction,
        start.y + (end.y - start.y) * fraction,
        start.z + (end.z - start.z) * fraction,
    };

    if (!recursiveHullTrace(node.children[side], rootNode, startFraction, middleFraction, start, middle, trace))
    {
        return false;
    }

    if (hullPointContents(node.children[side ^ 1], middle) == ContentsSolid)
    {
        if (trace.allSolid)
        {
            return false;
        }

        trace.planeNormal = side == 0
                                ? plane.normal
                                : Vec3{-plane.normal.x, -plane.normal.y, -plane.normal.z};
        float safeFraction = fraction;
        float safeMiddleFraction = middleFraction;
        Vec3 safeMiddle = middle;
        while (hullPointContents(rootNode, safeMiddle) == ContentsSolid)
        {
            safeFraction -= 0.1f;
            if (safeFraction < 0.0f)
            {
                trace.fraction = safeMiddleFraction;
                trace.endPosition = safeMiddle;
                return false;
            }

            safeMiddleFraction = startFraction + (endFraction - startFraction) * safeFraction;
            safeMiddle = {
                start.x + (end.x - start.x) * safeFraction,
                start.y + (end.y - start.y) * safeFraction,
                start.z + (end.z - start.z) * safeFraction,
            };
        }

        trace.fraction = safeMiddleFraction;
        trace.endPosition = safeMiddle;
        return false;
    }

    return recursiveHullTrace(node.children[side ^ 1], rootNode, middleFraction, endFraction, middle, end, trace);
}

std::int32_t BspMap::hullPointContents(std::int32_t nodeIndex, const Vec3& point) const
{
    while (nodeIndex >= 0)
    {
        if (static_cast<std::size_t>(nodeIndex) >= collisionNodes_.size())
        {
            return ContentsSolid;
        }

        const BspCollisionNode& node = collisionNodes_[static_cast<std::size_t>(nodeIndex)];
        if (node.plane < 0 || static_cast<std::size_t>(node.plane) >= collisionPlanes_.size())
        {
            return ContentsSolid;
        }

        const BspCollisionPlane& plane = collisionPlanes_[static_cast<std::size_t>(node.plane)];
        const float distance = point.x * plane.normal.x + point.y * plane.normal.y + point.z * plane.normal.z - plane.distance;
        nodeIndex = node.children[distance < 0.0f ? 1 : 0];
    }

    return nodeIndex;
}
}

#include "engine/Engine.h"
#include "engine/StudioModel.h"
#include "engine/WadFile.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace goldsrc
{
namespace
{
std::string normalizeTextureName(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return name;
}

bool hasMdlExtension(const std::string& path)
{
    std::string normalized = normalizeTextureName(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return normalized.size() > 4 && normalized.substr(normalized.size() - 4) == ".mdl";
}

std::string inferredModelForClass(const std::string& className)
{
    const std::string name = normalizeTextureName(className);
    static const std::unordered_map<std::string, std::string> models = {
        {"ammo_357", "models/w_357ammobox.mdl"},
        {"ammo_9mmar", "models/w_9mmarclip.mdl"},
        {"ammo_9mmbox", "models/w_chainammo.mdl"},
        {"ammo_9mmclip", "models/w_9mmclip.mdl"},
        {"ammo_argrenades", "models/w_argrenade.mdl"},
        {"ammo_buckshot", "models/w_shotbox.mdl"},
        {"ammo_crossbow", "models/w_crossbow_clip.mdl"},
        {"ammo_gaussclip", "models/w_gaussammo.mdl"},
        {"ammo_glockclip", "models/w_9mmclip.mdl"},
        {"ammo_mp5clip", "models/w_9mmarclip.mdl"},
        {"ammo_mp5grenades", "models/w_argrenade.mdl"},
        {"ammo_rpgclip", "models/w_rpgammo.mdl"},
        {"item_antidote", "models/w_antidote.mdl"},
        {"item_battery", "models/w_battery.mdl"},
        {"item_healthkit", "models/w_medkit.mdl"},
        {"item_longjump", "models/w_longjump.mdl"},
        {"item_security", "models/w_security.mdl"},
        {"item_suit", "models/w_suit.mdl"},
        {"monster_alien_controller", "models/controller.mdl"},
        {"monster_alien_grunt", "models/agrunt.mdl"},
        {"monster_alien_slave", "models/islave.mdl"},
        {"monster_apache", "models/apache.mdl"},
        {"monster_babycrab", "models/baby_headcrab.mdl"},
        {"monster_barney", "models/barney.mdl"},
        {"monster_barney_dead", "models/barney.mdl"},
        {"monster_barnacle", "models/barnacle.mdl"},
        {"monster_bigmomma", "models/big_mom.mdl"},
        {"monster_bloater", "models/floater.mdl"},
        {"monster_bullchicken", "models/bullsquid.mdl"},
        {"monster_cockroach", "models/roach.mdl"},
        {"monster_furniture", "models/filecabinet.mdl"},
        {"monster_gargantua", "models/garg.mdl"},
        {"monster_gman", "models/gman.mdl"},
        {"monster_headcrab", "models/headcrab.mdl"},
        {"monster_hevsuit_dead", "models/player.mdl"},
        {"monster_hgrunt_dead", "models/hgrunt.mdl"},
        {"monster_houndeye", "models/houndeye.mdl"},
        {"monster_human_assassin", "models/hassassin.mdl"},
        {"monster_human_grunt", "models/hgrunt.mdl"},
        {"monster_ichthyosaur", "models/icky.mdl"},
        {"monster_leech", "models/leech.mdl"},
        {"monster_miniturret", "models/miniturret.mdl"},
        {"monster_nihilanth", "models/nihilanth.mdl"},
        {"monster_osprey", "models/osprey.mdl"},
        {"monster_rat", "models/bigrat.mdl"},
        {"monster_scientist", "models/scientist.mdl"},
        {"monster_scientist_dead", "models/scientist.mdl"},
        {"monster_sentry", "models/sentry.mdl"},
        {"monster_sitting_scientist", "models/scientist.mdl"},
        {"monster_snark", "models/w_squeak.mdl"},
        {"monster_tentacle", "models/tentacle2.mdl"},
        {"monster_turret", "models/turret.mdl"},
        {"monster_zombie", "models/zombie.mdl"},
        {"weapon_357", "models/w_357.mdl"},
        {"weapon_9mmar", "models/w_9mmar.mdl"},
        {"weapon_9mmhandgun", "models/w_9mmhandgun.mdl"},
        {"weapon_crossbow", "models/w_crossbow.mdl"},
        {"weapon_crowbar", "models/w_crowbar.mdl"},
        {"weapon_egon", "models/w_egon.mdl"},
        {"weapon_gauss", "models/w_gauss.mdl"},
        {"weapon_glock", "models/w_9mmhandgun.mdl"},
        {"weapon_handgrenade", "models/w_grenade.mdl"},
        {"weapon_hornetgun", "models/w_hgun.mdl"},
        {"weapon_mp5", "models/w_9mmar.mdl"},
        {"weapon_python", "models/w_357.mdl"},
        {"weapon_rpg", "models/w_rpg.mdl"},
        {"weapon_satchel", "models/w_satchel.mdl"},
        {"weapon_shotgun", "models/w_shotgun.mdl"},
        {"weapon_snark", "models/w_squeak.mdl"},
        {"weapon_tripmine", "models/w_tripmine.mdl"},
    };

    const auto found = models.find(name);
    if (found != models.end())
    {
        return found->second;
    }
    return {};
}
}

Engine::Engine(LaunchOptions options)
    : options_(std::move(options)),
      fileSystem_(options_.baseDirectory, options_.gameDirectory)
{
    soundSystem_.setFileSystem(&fileSystem_);
    renderer_.setSoundSystem(&soundSystem_);
    registerBuiltins();
}

int Engine::run()
{
    std::cout << "Open GoldSrc executable " << versionString() << '\n';
    std::cout << "base: " << fileSystem_.baseDirectory().string() << '\n';
    std::cout << "game: " << fileSystem_.gameDirectory().string() << '\n';

    if (!fileSystem_.gameDirectoryExists())
    {
        std::cout << "warning: game directory was not found; assets are not mounted yet\n";
    }

    if (!renderer_.initialize("Open GoldSrc - 3D Scene", 960, 540))
    {
        std::cout << "warning: renderer could not start; continuing without a 3D window\n";
    }

    for (const std::string& command : options_.startupCommands)
    {
        console_.execute(command);
    }

    running_ = true;
    if (options_.runOnce)
    {
        frame();
        renderer_.shutdown();
        return 0;
    }

    while (running_)
    {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    renderer_.shutdown();
    return 0;
}

void Engine::frame()
{
    ++frameNumber_;

    if (renderer_.isOpen())
    {
        running_ = renderer_.renderFrame();
    }

    if ((frameNumber_ % 300) == 0)
    {
        std::cout << "frame " << frameNumber_ << '\n';
    }
}

std::string Engine::versionString() const
{
    return "0.1.0";
}

void Engine::registerBuiltins()
{
    console_.registerCommand("echo", [](const std::vector<std::string>& args) {
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (i != 0)
            {
                std::cout << ' ';
            }
            std::cout << args[i];
        }
        std::cout << '\n';
    });

    console_.registerCommand("quit", [this](const std::vector<std::string>&) {
        running_ = false;
    });

    console_.registerCommand("map", [this](const std::vector<std::string>& args) {
        if (args.empty())
        {
            std::cout << "usage: map <name>\n";
            return;
        }

        const std::string relativeMapPath = "maps/" + args.front() + ".bsp";
        const std::filesystem::path mapPath = fileSystem_.resolve(relativeMapPath);
        if (mapPath.empty())
        {
            std::cout << "map not found in mounted paths: " << relativeMapPath << '\n';
            return;
        }

        std::string error;
        if (!loadedMap_.load(mapPath, error))
        {
            std::cout << "map load failed: " << error << '\n';
            return;
        }

        const BspMapSummary& summary = loadedMap_.summary();
        renderer_.setCollisionMap(&loadedMap_);
        renderer_.setWorldFaces(loadedMap_.renderFaces());
        renderer_.setMapEntities(loadedMap_.entities(), loadedMap_.brushModels());
        renderer_.setEntityMarkers(loadedMap_.entityMarkers());

        std::size_t pointEntityCount = 0;
        std::size_t brushEntityCount = 0;
        std::size_t targetNamedEntityCount = 0;
        std::size_t targetedEntityCount = 0;
        std::size_t triggerEntityCount = 0;
        std::size_t autoTriggerEntityCount = 0;
        std::size_t multiManagerCount = 0;
        std::unordered_map<std::string, std::size_t> entityClassCounts;
        for (const BspEntity& entity : loadedMap_.entities())
        {
            if (entity.brushEntity)
            {
                ++brushEntityCount;
            }
            else
            {
                ++pointEntityCount;
            }
            if (!entity.targetName.empty())
            {
                ++targetNamedEntityCount;
            }
            if (!entity.target.empty())
            {
                ++targetedEntityCount;
            }
            if (!entity.className.empty())
            {
                ++entityClassCounts[entity.className];
                if (normalizeTextureName(entity.className).find("trigger_") == 0)
                {
                    ++triggerEntityCount;
                }
                if (normalizeTextureName(entity.className) == "trigger_auto")
                {
                    ++autoTriggerEntityCount;
                }
                if (normalizeTextureName(entity.className) == "multi_manager")
                {
                    ++multiManagerCount;
                }
            }
        }
        std::vector<std::pair<std::string, std::size_t>> entityClassSummary(entityClassCounts.begin(), entityClassCounts.end());
        std::sort(entityClassSummary.begin(), entityClassSummary.end(), [](const auto& left, const auto& right) {
            if (left.second != right.second)
            {
                return left.second > right.second;
            }
            return left.first < right.first;
        });

        std::unordered_map<std::string, std::size_t> studioModelIndices;
        std::vector<StudioModelSceneEntry> studioModels;
        std::size_t missingStudioModels = 0;
        for (const BspEntityMarker& marker : loadedMap_.entityMarkers())
        {
            std::string entityModel = marker.model;
            if (!hasMdlExtension(entityModel))
            {
                entityModel = inferredModelForClass(marker.className);
            }

            if (!hasMdlExtension(entityModel))
            {
                continue;
            }

            std::string normalizedModelPath = entityModel;
            std::replace(normalizedModelPath.begin(), normalizedModelPath.end(), '\\', '/');
            const std::int32_t body = std::max(0, marker.body);
            const std::int32_t skin = std::max(0, marker.skin);
            const std::int32_t sequence = marker.sequence;
            const std::string normalizedKey = normalizeTextureName(normalizedModelPath) +
                                              "#body=" + std::to_string(body) +
                                              "#skin=" + std::to_string(skin) +
                                              "#seq=" + std::to_string(sequence);
            auto existing = studioModelIndices.find(normalizedKey);
            if (existing == studioModelIndices.end())
            {
                const std::filesystem::path modelPath = fileSystem_.resolve(normalizedModelPath);
                if (modelPath.empty())
                {
                    ++missingStudioModels;
                    continue;
                }

                StudioModel model;
                std::string modelError;
                if (!model.load(modelPath, body, skin, sequence, modelError))
                {
                    std::cout << "  MDL load failed: " << modelPath.string() << " (" << modelError << ")\n";
                    ++missingStudioModels;
                    continue;
                }

                StudioModelSceneEntry entry;
                entry.model = std::move(model);
                studioModelIndices[normalizedKey] = studioModels.size();
                studioModels.push_back(std::move(entry));
                existing = studioModelIndices.find(normalizedKey);
            }

            StudioModelInstance instance;
            instance.modelPath = normalizedModelPath;
            instance.origin = marker.origin;
            instance.angles = marker.angles;
            instance.body = body;
            instance.skin = skin;
            instance.sequence = sequence;
            studioModels[existing->second].instances.push_back(std::move(instance));
        }
        const std::size_t studioModelInstanceCount = [&studioModels]() {
            std::size_t count = 0;
            for (const StudioModelSceneEntry& entry : studioModels)
            {
                count += entry.instances.size();
            }
            return count;
        }();
        renderer_.setStudioModels(std::move(studioModels));

        std::vector<std::string> requiredTextureNames;
        std::unordered_set<std::string> seenTextureNames;
        std::size_t lightmappedFaceCount = 0;
        std::size_t skyFaceCount = 0;
        std::size_t brushEntityFaceCount = 0;
        std::size_t translucentFaceCount = 0;
        for (const BspRenderFace& face : loadedMap_.renderFaces())
        {
            if (!face.lightmapRgba.empty())
            {
                ++lightmappedFaceCount;
            }
            if (face.sky)
            {
                ++skyFaceCount;
            }
            if (face.brushEntity)
            {
                ++brushEntityFaceCount;
            }
            if (face.translucent)
            {
                ++translucentFaceCount;
            }

            if (face.textureName.empty() || face.sky)
            {
                continue;
            }

            const std::string normalized = normalizeTextureName(face.textureName);
            if (seenTextureNames.insert(normalized).second)
            {
                requiredTextureNames.push_back(face.textureName);
            }
        }

        std::vector<WadFile> wads;
        for (const std::filesystem::path& wadPath : loadedMap_.wadPaths())
        {
            const std::filesystem::path resolvedWadPath = fileSystem_.resolveAssetPath(wadPath);
            if (resolvedWadPath.empty())
            {
                std::cout << "  missing WAD: " << wadPath.string() << '\n';
                continue;
            }

            std::string wadError;
            WadFile wad;
            if (!wad.load(resolvedWadPath, wadError))
            {
                std::cout << "  WAD load failed: " << resolvedWadPath.string() << " (" << wadError << ")\n";
                continue;
            }

            wads.push_back(std::move(wad));
        }

        std::vector<TextureImage> textures;
        std::unordered_set<std::string> loadedTextureNames;
        for (const std::string& textureName : requiredTextureNames)
        {
            for (const WadFile& wad : wads)
            {
                TextureImage image;
                std::string textureError;
                if (wad.findTexture(textureName, image, textureError))
                {
                    loadedTextureNames.insert(normalizeTextureName(textureName));
                    textures.push_back(std::move(image));
                    break;
                }
                if (!textureError.empty())
                {
                    std::cout << "  texture load failed: " << textureName << " (" << textureError << ")\n";
                    break;
                }
            }
        }
        renderer_.setTextureImages(std::move(textures));

        std::cout << "loaded BSP: " << mapPath.string() << '\n';
        std::cout << "  version: " << summary.version << '\n';
        std::cout << "  models: " << summary.models << '\n';
        std::cout << "  planes: " << summary.planes << '\n';
        std::cout << "  vertices: " << summary.vertices << '\n';
        std::cout << "  faces: " << summary.faces << '\n';
        std::cout << "  renderable faces: " << loadedMap_.renderFaces().size() << '\n';
        std::cout << "  entities parsed: " << loadedMap_.entities().size() << '\n';
        std::cout << "  point entities: " << pointEntityCount << '\n';
        std::cout << "  brush entities: " << brushEntityCount << '\n';
        std::cout << "  named targets: " << targetNamedEntityCount << '\n';
        std::cout << "  target links: " << targetedEntityCount << '\n';
        std::cout << "  triggers: " << triggerEntityCount << '\n';
        std::cout << "  trigger_auto: " << autoTriggerEntityCount << '\n';
        std::cout << "  multi_manager: " << multiManagerCount << '\n';
        std::cout << "  entity classes: " << entityClassSummary.size() << '\n';
        if (!entityClassSummary.empty())
        {
            std::cout << "  top entity classes:";
            const std::size_t shownClasses = std::min<std::size_t>(entityClassSummary.size(), 8);
            for (std::size_t i = 0; i < shownClasses; ++i)
            {
                std::cout << ' ' << entityClassSummary[i].first << '=' << entityClassSummary[i].second;
            }
            std::cout << '\n';
        }
        std::cout << "  entity markers: " << loadedMap_.entityMarkers().size() << '\n';
        std::cout << "  edges: " << summary.edges << '\n';
        std::cout << "  surfedges: " << summary.surfEdges << '\n';
        std::cout << "  leaves: " << summary.leaves << '\n';
        std::cout << "  texture info: " << summary.textureInfo << '\n';
        std::cout << "  WAD files referenced: " << loadedMap_.wadPaths().size() << '\n';
        std::cout << "  textures referenced: " << requiredTextureNames.size() << '\n';
        std::cout << "  textures loaded: " << loadedTextureNames.size() << '\n';
        std::cout << "  lighting bytes: " << summary.lightingBytes << '\n';
        std::cout << "  lightmapped faces: " << lightmappedFaceCount << '\n';
        std::cout << "  sky faces: " << skyFaceCount << '\n';
        std::cout << "  brush entity faces: " << brushEntityFaceCount << '\n';
        std::cout << "  translucent faces: " << translucentFaceCount << '\n';
        std::cout << "  studio models loaded: " << studioModelIndices.size() << '\n';
        std::cout << "  studio model instances: " << studioModelInstanceCount << '\n';
        std::cout << "  studio models missing: " << missingStudioModels << '\n';
        for (const BspEntityMarker& marker : loadedMap_.entityMarkers())
        {
            if (marker.className == "info_player_start")
            {
                std::cout << "  player start: "
                          << static_cast<int>(marker.origin.x) << ','
                          << static_cast<int>(marker.origin.y) << ','
                          << static_cast<int>(marker.origin.z)
                          << " hull contents h1/h2/h3: "
                          << loadedMap_.pointContentsForHull(marker.origin, 1) << '/'
                          << loadedMap_.pointContentsForHull(marker.origin, 2) << '/'
                          << loadedMap_.pointContentsForHull(marker.origin, 3) << '\n';
                break;
            }
        }
        if (summary.texturesBytes == 0)
        {
            std::cout << "  textures: external or empty texture lump\n";
        }
        else
        {
            std::cout << "  texture lump bytes: " << summary.texturesBytes << '\n';
        }
    });
}
}

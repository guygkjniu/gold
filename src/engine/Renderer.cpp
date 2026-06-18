#include "engine/Renderer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#endif

namespace goldsrc
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float degreesToRadians = pi / 180.0f;
constexpr float epsilon = 0.0001f;

float parseFloatOrDefault(const std::string* text, float fallback)
{
    if (!text || text->empty())
    {
        return fallback;
    }
    char* end = nullptr;
    const float value = std::strtof(text->c_str(), &end);
    return end == text->c_str() ? fallback : value;
}

int parseIntOrDefault(const std::string* text, int fallback)
{
    return static_cast<int>(std::lround(parseFloatOrDefault(text, static_cast<float>(fallback))));
}

std::string lowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

Vec3 add(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 sub(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 mul(Vec3 a, float s)
{
    return {a.x * s, a.y * s, a.z * s};
}

float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float length(Vec3 v)
{
    return std::sqrt(dot(v, v));
}

Vec3 normalize(Vec3 v)
{
    const float len = length(v);
    if (len <= epsilon)
    {
        return {};
    }
    return mul(v, 1.0f / len);
}

Vec3 doorDirectionFromAngles(Vec3 angles)
{
    if (std::fabs(angles.y + 1.0f) < 0.01f)
    {
        return {0.0f, 0.0f, 1.0f};
    }
    if (std::fabs(angles.y + 2.0f) < 0.01f)
    {
        return {0.0f, 0.0f, -1.0f};
    }

    const float yaw = angles.y * degreesToRadians;
    return normalize({std::cos(yaw), std::sin(yaw), 0.0f});
}

bool aabbContains(Vec3 point, Vec3 mins, Vec3 maxs)
{
    return point.x >= mins.x && point.x <= maxs.x &&
           point.y >= mins.y && point.y <= maxs.y &&
           point.z >= mins.z && point.z <= maxs.z;
}

bool isBrushModel(const BspEntity& entity)
{
    return entity.modelIndex > 0 || (!entity.model.empty() && entity.model.front() == '*');
}

bool isDoorClass(const std::string& className)
{
    const std::string name = lowerCopy(className);
    return name == "func_door" || name == "func_door_rotating";
}

bool isTriggerClass(const std::string& className)
{
    const std::string name = lowerCopy(className);
    return name.rfind("trigger_", 0) == 0 && name != "trigger_auto" && name != "trigger_relay";
}

bool isLogicKey(const std::string& key)
{
    static const std::array<const char*, 11> reserved = {
        "classname", "targetname", "target", "origin", "angles", "model",
        "spawnflags", "delay", "body", "skin", "sequence",
    };
    return std::find(reserved.begin(), reserved.end(), key) != reserved.end();
}

void drawBoxLines(Vec3 mins, Vec3 maxs)
{
#ifdef _WIN32
    const Vec3 p[8] = {
        {mins.x, mins.y, mins.z}, {maxs.x, mins.y, mins.z}, {maxs.x, maxs.y, mins.z}, {mins.x, maxs.y, mins.z},
        {mins.x, mins.y, maxs.z}, {maxs.x, mins.y, maxs.z}, {maxs.x, maxs.y, maxs.z}, {mins.x, maxs.y, maxs.z},
    };
    const int e[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
        {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    glBegin(GL_LINES);
    for (const auto& edge : e)
    {
        glVertex3f(p[edge[0]].x, p[edge[0]].y, p[edge[0]].z);
        glVertex3f(p[edge[1]].x, p[edge[1]].y, p[edge[1]].z);
    }
    glEnd();
#else
    (void)mins;
    (void)maxs;
#endif
}
}

struct Renderer::PlatformState
{
#ifdef _WIN32
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HDC deviceContext = nullptr;
    HGLRC glContext = nullptr;
    bool keyDown[256] = {};
#endif
};

#ifdef _WIN32
namespace
{
Renderer* rendererFromWindow(HWND hwnd)
{
    return reinterpret_cast<Renderer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}
#endif

Renderer::~Renderer()
{
    shutdown();
}

bool Renderer::initialize(const std::string& title, int width, int height)
{
    width_ = width;
    height_ = height;
#ifdef _WIN32
    platform_ = new PlatformState();
    platform_->instance = GetModuleHandleW(nullptr);

    WNDCLASSW wc = {};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = platform_->instance;
    wc.lpszClassName = L"OpenGoldSrcWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    RECT rect = {0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    platform_->window = CreateWindowExW(0,
                                        wc.lpszClassName,
                                        L"Open GoldSrc",
                                        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                        CW_USEDEFAULT,
                                        CW_USEDEFAULT,
                                        rect.right - rect.left,
                                        rect.bottom - rect.top,
                                        nullptr,
                                        nullptr,
                                        platform_->instance,
                                        nullptr);
    if (!platform_->window)
    {
        return false;
    }
    SetWindowLongPtrW(platform_->window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    platform_->deviceContext = GetDC(platform_->window);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = ChoosePixelFormat(platform_->deviceContext, &pfd);
    if (format == 0 || !SetPixelFormat(platform_->deviceContext, format, &pfd))
    {
        return false;
    }
    platform_->glContext = wglCreateContext(platform_->deviceContext);
    if (!platform_->glContext || !wglMakeCurrent(platform_->deviceContext, platform_->glContext))
    {
        return false;
    }

    SetWindowTextA(platform_->window, title.c_str());
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.05f, 0.06f, 0.07f, 1.0f);
    configureProjection();
    open_ = true;
    lastFrameTime_ = std::chrono::steady_clock::now();
    return true;
#else
    (void)title;
    open_ = false;
    return false;
#endif
}

bool Renderer::isOpen() const
{
    return open_;
}

void Renderer::shutdown()
{
#ifdef _WIN32
    destroyTextures();
    if (platform_)
    {
        if (platform_->glContext)
        {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(platform_->glContext);
        }
        if (platform_->window && platform_->deviceContext)
        {
            ReleaseDC(platform_->window, platform_->deviceContext);
        }
        if (platform_->window)
        {
            DestroyWindow(platform_->window);
        }
        delete platform_;
        platform_ = nullptr;
    }
#endif
    open_ = false;
}

bool Renderer::renderFrame()
{
#ifdef _WIN32
    MSG msg = {};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            open_ = false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (!open_)
    {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const float deltaSeconds = std::min(0.05f, std::chrono::duration<float>(now - lastFrameTime_).count());
    lastFrameTime_ = now;
    angleDegrees_ += deltaSeconds * 45.0f;
    studioAnimationTime_ += deltaSeconds;

    updateCamera(deltaSeconds);
    updatePlayerPhysics(deltaSeconds);
    updateMapLogic(deltaSeconds);
    syncCameraToPlayer();
    uploadPendingTextures();
    uploadLightmapTextures();
    uploadStudioModelTextures();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(cameraPitchDegrees_, 1.0f, 0.0f, 0.0f);
    glRotatef(-cameraYawDegrees_ + 90.0f, 0.0f, 1.0f, 0.0f);
    glTranslatef(-cameraPosition_.x, -cameraPosition_.y, -cameraPosition_.z);

    renderWorld();
    renderStudioModels();
    renderEntityMarkers();
    if (worldFaces_.empty())
    {
        renderCube(angleDegrees_);
    }

    SwapBuffers(platform_->deviceContext);
    updateDebugTitle();
    return true;
#else
    return false;
#endif
}

void Renderer::setWorldFaces(std::vector<BspRenderFace> faces)
{
    worldFaces_ = std::move(faces);
    if (worldFaces_.empty())
    {
        return;
    }
    worldMins_ = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    worldMaxs_ = {-worldMins_.x, -worldMins_.y, -worldMins_.z};
    for (const BspRenderFace& face : worldFaces_)
    {
        for (const BspRenderVertex& vertex : face.vertices)
        {
            worldMins_.x = std::min(worldMins_.x, vertex.position.x);
            worldMins_.y = std::min(worldMins_.y, vertex.position.y);
            worldMins_.z = std::min(worldMins_.z, vertex.position.z);
            worldMaxs_.x = std::max(worldMaxs_.x, vertex.position.x);
            worldMaxs_.y = std::max(worldMaxs_.y, vertex.position.y);
            worldMaxs_.z = std::max(worldMaxs_.z, vertex.position.z);
        }
    }
    worldCenter_ = mul(add(worldMins_, worldMaxs_), 0.5f);
    const Vec3 extents = sub(worldMaxs_, worldMins_);
    const float maxExtent = std::max({extents.x, extents.y, extents.z, 1.0f});
    worldScale_ = 8.0f / maxExtent;
    textureIds_.clear();
    lightmapTextureIds_.clear();
}

void Renderer::setMapEntities(std::vector<BspEntity> entities, std::vector<BspBrushModel> brushModels)
{
    mapEntities_ = std::move(entities);
    brushModels_ = std::move(brushModels);
    targetNameIndex_.clear();
    doors_.clear();
    triggers_.clear();
    autoTriggers_.clear();
    multiManagers_.clear();
    removedLogicEntities_.clear();

    for (std::size_t i = 0; i < mapEntities_.size(); ++i)
    {
        const BspEntity& entity = mapEntities_[i];
        if (!entity.targetName.empty())
        {
            targetNameIndex_[lowerCopy(entity.targetName)].push_back(i);
        }

        if (isDoorClass(entity.className) && isBrushModel(entity))
        {
            RuntimeDoor door;
            door.entityIndex = static_cast<std::int32_t>(i);
            door.modelIndex = entity.modelIndex;
            door.direction = doorDirectionFromAngles(entity.angles);
            door.speed = std::max(1.0f, parseFloatOrDefault(entity.value("speed"), 100.0f));
            door.wait = parseFloatOrDefault(entity.value("wait"), 4.0f);
            door.moveSound = parseIntOrDefault(entity.value("movesnd"), 0);
            door.stopSound = parseIntOrDefault(entity.value("stopsnd"), 0);
            if (door.modelIndex >= 0 && static_cast<std::size_t>(door.modelIndex) < brushModels_.size())
            {
                door.mins = brushModels_[door.modelIndex].mins;
                door.maxs = brushModels_[door.modelIndex].maxs;
                const Vec3 size = sub(door.maxs, door.mins);
                door.distance = std::fabs(dot(size, door.direction)) - parseFloatOrDefault(entity.value("lip"), 8.0f);
                door.distance = std::max(1.0f, door.distance);
            }
            doors_.push_back(door);
        }
        else if (isTriggerClass(entity.className) && isBrushModel(entity))
        {
            RuntimeTrigger trigger;
            trigger.entityIndex = static_cast<std::int32_t>(i);
            trigger.modelIndex = entity.modelIndex;
            trigger.target = entity.target;
            if (trigger.modelIndex >= 0 && static_cast<std::size_t>(trigger.modelIndex) < brushModels_.size())
            {
                trigger.mins = brushModels_[trigger.modelIndex].mins;
                trigger.maxs = brushModels_[trigger.modelIndex].maxs;
            }
            triggers_.push_back(trigger);
        }
        else if (lowerCopy(entity.className) == "trigger_auto")
        {
            RuntimeAutoTrigger autoTrigger;
            autoTrigger.entityIndex = static_cast<std::int32_t>(i);
            autoTrigger.target = entity.target;
            autoTrigger.timer = 0.1f + std::max(0.0f, parseFloatOrDefault(entity.value("delay"), 0.0f));
            autoTrigger.fireOnce = (parseIntOrDefault(entity.value("spawnflags"), 0) & 1) != 0;
            autoTriggers_.push_back(autoTrigger);
        }
        else if (lowerCopy(entity.className) == "multi_manager")
        {
            RuntimeMultiManager manager;
            manager.entityIndex = static_cast<std::int32_t>(i);
            for (const auto& [key, value] : entity.keyValues)
            {
                if (isLogicKey(lowerCopy(key)))
                {
                    continue;
                }
                RuntimeMultiManager::TargetEvent event;
                event.target = key;
                event.delay = std::max(0.0f, parseFloatOrDefault(&value, 0.0f));
                manager.events.push_back(event);
            }
            std::sort(manager.events.begin(), manager.events.end(), [](const auto& a, const auto& b) {
                return a.delay < b.delay;
            });
            multiManagers_.push_back(std::move(manager));
        }
    }
}

void Renderer::setCollisionMap(const BspMap* map)
{
    collisionMap_ = map;
}

void Renderer::setSoundSystem(SoundSystem* soundSystem)
{
    soundSystem_ = soundSystem;
}

void Renderer::setEntityMarkers(std::vector<BspEntityMarker> markers)
{
    entityMarkers_ = std::move(markers);
    for (const BspEntityMarker& marker : entityMarkers_)
    {
        if (marker.className == "info_player_start")
        {
            playerOriginMap_ = marker.origin;
            cameraYawDegrees_ = marker.angles.y;
            break;
        }
    }
}

void Renderer::setTextureImages(std::vector<TextureImage> textures)
{
    pendingTextures_ = std::move(textures);
}

void Renderer::setStudioModels(std::vector<StudioModelSceneEntry> models)
{
    studioModels_ = std::move(models);
    studioTextureIds_.clear();
}

void Renderer::configureProjection()
{
#ifdef _WIN32
    if (!platform_ || !platform_->window)
    {
        return;
    }
    RECT client = {};
    GetClientRect(platform_->window, &client);
    width_ = std::max(1L, client.right - client.left);
    height_ = std::max(1L, client.bottom - client.top);
    glViewport(0, 0, width_, height_);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const float fovY = 75.0f * degreesToRadians;
    const float nearPlane = 0.02f;
    const float farPlane = 500.0f;
    const float top = std::tan(fovY * 0.5f) * nearPlane;
    const float bottom = -top;
    const float right = top * aspect;
    const float left = -right;
    glFrustum(left, right, bottom, top, nearPlane, farPlane);
    glMatrixMode(GL_MODELVIEW);
#endif
}

void Renderer::updateCamera(float)
{
#ifdef _WIN32
    if (!platform_ || !platform_->window)
    {
        return;
    }

    const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escapeDown)
    {
        mouseReleasedByEscape_ = true;
        mouseLookActive_ = false;
        ClipCursor(nullptr);
        ShowCursor(TRUE);
    }
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0)
    {
        mouseReleasedByEscape_ = false;
    }
    if (!mouseReleasedByEscape_)
    {
        mouseLookActive_ = true;
    }

    if (mouseLookActive_)
    {
        RECT rect = {};
        GetClientRect(platform_->window, &rect);
        POINT center = {(rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2};
        ClientToScreen(platform_->window, &center);
        RECT clip = rect;
        POINT tl = {clip.left, clip.top};
        POINT br = {clip.right, clip.bottom};
        ClientToScreen(platform_->window, &tl);
        ClientToScreen(platform_->window, &br);
        clip = {tl.x, tl.y, br.x, br.y};
        ClipCursor(&clip);
        ShowCursor(FALSE);

        POINT pos = {};
        GetCursorPos(&pos);
        const int dx = pos.x - center.x;
        const int dy = pos.y - center.y;
        cameraYawDegrees_ -= static_cast<float>(dx) * 0.12f;
        cameraPitchDegrees_ += static_cast<float>(dy) * 0.12f;
        cameraPitchDegrees_ = std::clamp(cameraPitchDegrees_, -89.0f, 89.0f);
        SetCursorPos(center.x, center.y);
    }

    const bool bDown = (GetAsyncKeyState('B') & 0x8000) != 0;
    if (bDown && !bKeyWasDown_)
    {
        showEntityMarkers_ = !showEntityMarkers_;
    }
    bKeyWasDown_ = bDown;

    const bool nDown = (GetAsyncKeyState('N') & 0x8000) != 0;
    if (nDown && !nKeyWasDown_)
    {
        noClip_ = !noClip_;
    }
    nKeyWasDown_ = nDown;

    const bool eDown = (GetAsyncKeyState('E') & 0x8000) != 0;
    if (eDown && !eKeyWasDown_)
    {
        float bestDistance = 96.0f;
        RuntimeDoor* bestDoor = nullptr;
        for (RuntimeDoor& door : doors_)
        {
            const Vec3 center = mul(add(door.mins, door.maxs), 0.5f);
            const float distanceToDoor = length(sub(center, playerOriginMap_));
            if (distanceToDoor < bestDistance)
            {
                bestDistance = distanceToDoor;
                bestDoor = &door;
            }
        }
        if (bestDoor)
        {
            useDoor(*bestDoor);
        }
    }
    eKeyWasDown_ = eDown;
#endif
}

void Renderer::syncCameraToPlayer()
{
    cameraPosition_ = toRenderSpace(add(playerOriginMap_, {0.0f, 0.0f, playerEyeHeight_}));
}

bool Renderer::unstickPlayerHull()
{
    return true;
}

Vec3 Renderer::buildWishVelocity(float maxSpeed)
{
    float forwardMove = 0.0f;
    float sideMove = 0.0f;
#ifdef _WIN32
    if (GetAsyncKeyState('W') & 0x8000) forwardMove += 1.0f;
    if (GetAsyncKeyState('S') & 0x8000) forwardMove -= 1.0f;
    if (GetAsyncKeyState('D') & 0x8000) sideMove += 1.0f;
    if (GetAsyncKeyState('A') & 0x8000) sideMove -= 1.0f;
#endif
    lastMoveX_ = sideMove;
    lastMoveY_ = forwardMove;
    const float yaw = cameraYawDegrees_ * degreesToRadians;
    const Vec3 forward = {std::cos(yaw), std::sin(yaw), 0.0f};
    const Vec3 right = {std::sin(yaw), -std::cos(yaw), 0.0f};
    Vec3 wish = add(mul(forward, forwardMove), mul(right, sideMove));
    if (length(wish) > 1.0f)
    {
        wish = normalize(wish);
    }
    return mul(wish, maxSpeed);
}

void Renderer::categorizePlayerPosition(int hull)
{
    const Vec3 start = playerOriginMap_;
    const Vec3 end = add(start, {0.0f, 0.0f, -2.0f});
    const BspTraceResult trace = tracePlayerHull(start, end, hull);
    grounded_ = trace.hit || trace.fraction < 1.0f;
    lastHullContents_ = pointContentsWithBrushes(playerOriginMap_, hull);
}

void Renderer::applyPlayerFriction(float deltaSeconds)
{
    if (!grounded_)
    {
        return;
    }
    const float speed = std::sqrt(playerVelocity_.x * playerVelocity_.x + playerVelocity_.y * playerVelocity_.y);
    if (speed <= epsilon)
    {
        return;
    }
    const float drop = speed * 6.0f * deltaSeconds;
    const float newSpeed = std::max(0.0f, speed - drop) / speed;
    playerVelocity_.x *= newSpeed;
    playerVelocity_.y *= newSpeed;
}

void Renderer::acceleratePlayer(const Vec3& wishDirection, float wishSpeed, float acceleration, float deltaSeconds)
{
    const float currentSpeed = dot(playerVelocity_, wishDirection);
    const float addSpeed = wishSpeed - currentSpeed;
    if (addSpeed <= 0.0f)
    {
        return;
    }
    const float accelSpeed = std::min(addSpeed, acceleration * wishSpeed * deltaSeconds);
    playerVelocity_ = add(playerVelocity_, mul(wishDirection, accelSpeed));
}

BspTraceResult Renderer::tracePlayerHull(const Vec3& start, const Vec3& end, int hull) const
{
    BspTraceResult best;
    best.endPosition = end;
    if (collisionMap_)
    {
        best = collisionMap_->traceHull(start, end, hull);
    }

    for (const RuntimeDoor& door : doors_)
    {
        if (door.modelIndex <= 0 || door.state == DoorState::Open)
        {
            continue;
        }
        const Vec3 offset = brushModelOffset(door.modelIndex);
        const BspTraceResult trace = collisionMap_ ? collisionMap_->traceHullForModel(door.modelIndex, start, end, hull, offset) : BspTraceResult{};
        if ((trace.hit || trace.startSolid || trace.allSolid) && trace.fraction < best.fraction)
        {
            best = trace;
        }
    }
    return best;
}

std::int32_t Renderer::pointContentsWithBrushes(const Vec3& point, int hull) const
{
    std::int32_t contents = collisionMap_ ? collisionMap_->pointContentsForHull(point, hull) : 0;
    for (const RuntimeDoor& door : doors_)
    {
        if (door.modelIndex <= 0 || door.state == DoorState::Open)
        {
            continue;
        }
        const Vec3 offset = brushModelOffset(door.modelIndex);
        const std::int32_t doorContents = collisionMap_ ? collisionMap_->pointContentsForModel(door.modelIndex, point, hull, offset) : 0;
        if (doorContents < 0)
        {
            contents = doorContents;
        }
    }
    return contents;
}

BspTraceResult Renderer::flyMovePlayer(float deltaSeconds, int hull, bool recordTrace)
{
    Vec3 start = playerOriginMap_;
    Vec3 end = add(start, mul(playerVelocity_, deltaSeconds));
    BspTraceResult trace = tracePlayerHull(start, end, hull);
    if (trace.fraction > 0.0f)
    {
        playerOriginMap_ = trace.endPosition;
    }
    if (trace.hit)
    {
        const float backoff = dot(playerVelocity_, trace.planeNormal);
        playerVelocity_ = sub(playerVelocity_, mul(trace.planeNormal, backoff));
    }
    if (recordTrace)
    {
        lastMoveTraceHit_ = trace.hit;
        lastMoveTraceStartSolid_ = trace.startSolid;
        lastMoveTraceAllSolid_ = trace.allSolid;
        lastMoveTraceFraction_ = trace.fraction;
    }
    return trace;
}

BspTraceResult Renderer::stepMovePlayer(float deltaSeconds, int hull)
{
    return flyMovePlayer(deltaSeconds, hull, true);
}

void Renderer::updatePlayerPhysics(float deltaSeconds)
{
    const int hull = 0;
    lastMovementHull_ = hull;
    categorizePlayerPosition(hull);

    const Vec3 wishVelocity = buildWishVelocity(noClip_ ? 320.0f : 180.0f);
    if (noClip_)
    {
        Vec3 vertical;
#ifdef _WIN32
        if (GetAsyncKeyState(VK_SPACE) & 0x8000) vertical.z += 160.0f;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) vertical.z -= 160.0f;
#endif
        playerVelocity_ = add(wishVelocity, vertical);
        playerOriginMap_ = add(playerOriginMap_, mul(playerVelocity_, deltaSeconds));
        return;
    }

    applyPlayerFriction(deltaSeconds);
    const float wishSpeed = length(wishVelocity);
    if (wishSpeed > epsilon)
    {
        acceleratePlayer(normalize(wishVelocity), wishSpeed, grounded_ ? 10.0f : 2.0f, deltaSeconds);
    }
#ifdef _WIN32
    const bool jumpPressed = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (grounded_ && jumpPressed)
    {
        playerVelocity_.z = 270.0f;
        grounded_ = false;
    }
#endif
    if (grounded_)
    {
        const float savedZ = playerVelocity_.z;
        playerVelocity_.z = 0.0f;
        flyMovePlayer(deltaSeconds, hull, true);
        playerVelocity_.z = savedZ;

        const Vec3 start = playerOriginMap_;
        const Vec3 end = add(start, {0.0f, 0.0f, -2.0f});
        const BspTraceResult groundTrace = tracePlayerHull(start, end, hull);
        if (groundTrace.fraction < 1.0f)
        {
            playerOriginMap_ = groundTrace.endPosition;
        }
        categorizePlayerPosition(hull);
        return;
    }

    playerVelocity_.z -= 800.0f * deltaSeconds;
    stepMovePlayer(deltaSeconds, hull);
    categorizePlayerPosition(hull);
}

void Renderer::updateMapLogic(float deltaSeconds)
{
    for (RuntimeDoor& door : doors_)
    {
        if (door.state == DoorState::Opening)
        {
            door.progress += (door.speed * deltaSeconds) / door.distance;
            if (door.progress >= 1.0f)
            {
                door.progress = 1.0f;
                door.state = DoorState::Open;
                door.waitTimer = door.wait;
                if (soundSystem_)
                {
                    soundSystem_->playDoorStop(door.stopSound);
                }
            }
        }
        else if (door.state == DoorState::Closing)
        {
            door.progress -= (door.speed * deltaSeconds) / door.distance;
            if (door.progress <= 0.0f)
            {
                door.progress = 0.0f;
                door.state = DoorState::Closed;
                if (soundSystem_)
                {
                    soundSystem_->playDoorStop(door.stopSound);
                }
            }
        }
        else if (door.state == DoorState::Open && door.wait >= 0.0f)
        {
            door.waitTimer -= deltaSeconds;
            if (door.waitTimer <= 0.0f)
            {
                door.state = DoorState::Closing;
                if (soundSystem_)
                {
                    soundSystem_->playDoorMove(door.moveSound);
                }
            }
        }
    }

    for (RuntimeTrigger& trigger : triggers_)
    {
        const bool touching = aabbContains(playerOriginMap_, trigger.mins, trigger.maxs);
        if (touching && !trigger.touching)
        {
            fireEntityTarget(trigger.target);
        }
        trigger.touching = touching;
    }

    for (RuntimeAutoTrigger& trigger : autoTriggers_)
    {
        if (trigger.fired)
        {
            continue;
        }
        trigger.timer -= deltaSeconds;
        if (trigger.timer <= 0.0f)
        {
            fireEntityTarget(trigger.target);
            trigger.fired = true;
            if (trigger.fireOnce)
            {
                removedLogicEntities_.insert(static_cast<std::size_t>(trigger.entityIndex));
            }
        }
    }

    for (RuntimeMultiManager& manager : multiManagers_)
    {
        if (!manager.active)
        {
            continue;
        }
        manager.elapsed += deltaSeconds;
        bool allFired = true;
        for (auto& event : manager.events)
        {
            if (!event.fired && manager.elapsed >= event.delay)
            {
                fireEntityTarget(event.target);
                event.fired = true;
            }
            allFired = allFired && event.fired;
        }
        if (allFired)
        {
            manager.active = false;
            manager.elapsed = 0.0f;
            for (auto& event : manager.events)
            {
                event.fired = false;
            }
        }
    }
}

void Renderer::fireEntityTarget(const std::string& targetName)
{
    if (targetName.empty())
    {
        return;
    }
    const auto found = targetNameIndex_.find(lowerCopy(targetName));
    if (found == targetNameIndex_.end())
    {
        return;
    }
    for (std::size_t index : found->second)
    {
        useEntity(index);
    }
}

void Renderer::useEntity(std::size_t entityIndex)
{
    if (entityIndex >= mapEntities_.size() || removedLogicEntities_.count(entityIndex) != 0)
    {
        return;
    }
    const BspEntity& entity = mapEntities_[entityIndex];
    const std::string className = lowerCopy(entity.className);
    if (isDoorClass(className))
    {
        for (RuntimeDoor& door : doors_)
        {
            if (door.entityIndex == static_cast<std::int32_t>(entityIndex))
            {
                useDoor(door);
                return;
            }
        }
    }
    else if (className == "trigger_relay")
    {
        fireEntityTarget(entity.target);
        if ((parseIntOrDefault(entity.value("spawnflags"), 0) & 1) != 0)
        {
            removedLogicEntities_.insert(entityIndex);
        }
    }
    else if (className == "multi_manager")
    {
        for (RuntimeMultiManager& manager : multiManagers_)
        {
            if (manager.entityIndex == static_cast<std::int32_t>(entityIndex))
            {
                manager.active = true;
                manager.elapsed = 0.0f;
                for (auto& event : manager.events)
                {
                    event.fired = false;
                }
                return;
            }
        }
    }
}

void Renderer::useDoor(RuntimeDoor& door)
{
    if (door.state == DoorState::Closed || door.state == DoorState::Closing)
    {
        door.state = DoorState::Opening;
        if (soundSystem_)
        {
            soundSystem_->playDoorMove(door.moveSound);
        }
    }
    else if (door.state == DoorState::Open || door.state == DoorState::Opening)
    {
        door.state = DoorState::Closing;
        if (soundSystem_)
        {
            soundSystem_->playDoorMove(door.moveSound);
        }
    }
}

void Renderer::updateDebugTitle()
{
#ifdef _WIN32
    if (!platform_ || !platform_->window)
    {
        return;
    }
    std::ostringstream title;
    title << "Open GoldSrc"
          << " | pos " << static_cast<int>(playerOriginMap_.x) << ',' << static_cast<int>(playerOriginMap_.y) << ',' << static_cast<int>(playerOriginMap_.z)
          << " | move " << static_cast<int>(lastMoveX_) << ',' << static_cast<int>(lastMoveY_)
          << " | hull " << lastMovementHull_
          << " contents " << lastHullContents_
          << " | vis " << lastVisibleFaceCount_
          << " frac " << lastMoveTraceFraction_
          << " | " << (lastMoveTraceHit_ ? "hit" : "clear")
          << " " << (grounded_ ? "grounded" : "air")
          << " | noclip " << (noClip_ ? "ON" : "OFF")
          << " | boxes " << (showEntityMarkers_ ? "ON" : "OFF");
    if (soundSystem_)
    {
        title << " | snd " << soundSystem_->playedCount() << '/' << soundSystem_->missingCount();
    }
    SetWindowTextA(platform_->window, title.str().c_str());
#endif
}

void Renderer::renderCube(float angleDegrees)
{
#ifdef _WIN32
    glPushMatrix();
    glRotatef(angleDegrees, 1.0f, 1.0f, 0.0f);
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glColor3f(1, 0, 0); glVertex3f(-1, -1, 1); glVertex3f(1, -1, 1); glVertex3f(1, 1, 1); glVertex3f(-1, 1, 1);
    glColor3f(0, 1, 0); glVertex3f(-1, -1, -1); glVertex3f(-1, 1, -1); glVertex3f(1, 1, -1); glVertex3f(1, -1, -1);
    glColor3f(0, 0, 1); glVertex3f(-1, 1, -1); glVertex3f(-1, 1, 1); glVertex3f(1, 1, 1); glVertex3f(1, 1, -1);
    glColor3f(1, 1, 0); glVertex3f(-1, -1, -1); glVertex3f(1, -1, -1); glVertex3f(1, -1, 1); glVertex3f(-1, -1, 1);
    glColor3f(1, 0, 1); glVertex3f(1, -1, -1); glVertex3f(1, 1, -1); glVertex3f(1, 1, 1); glVertex3f(1, -1, 1);
    glColor3f(0, 1, 1); glVertex3f(-1, -1, -1); glVertex3f(-1, -1, 1); glVertex3f(-1, 1, 1); glVertex3f(-1, 1, -1);
    glEnd();
    glColor3f(1, 1, 1);
    glEnable(GL_TEXTURE_2D);
    glPopMatrix();
#endif
}

void Renderer::renderWorld()
{
#ifdef _WIN32
    std::vector<std::uint8_t> visible;
    lastVisibleFaceCount_ = 0;

    auto drawPass = [&](bool translucent) {
        if (translucent)
        {
            glEnable(GL_BLEND);
            glDepthMask(GL_FALSE);
        }
        else
        {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }
        for (std::size_t i = 0; i < worldFaces_.size(); ++i)
        {
            const BspRenderFace& face = worldFaces_[i];
            if (face.translucent != translucent || face.sky)
            {
                continue;
            }
            ++lastVisibleFaceCount_;
            emitFaceVertex(face, {});
            if (!translucent)
            {
                emitFaceLightmap(face, i);
            }
        }
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    };
    drawPass(false);
    drawPass(true);
#endif
}

void Renderer::renderEntityMarkers()
{
#ifdef _WIN32
    if (!showEntityMarkers_)
    {
        return;
    }
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    for (const BspEntityMarker& marker : entityMarkers_)
    {
        if (!marker.model.empty())
        {
            continue;
        }
        Vec3 half = {6.0f, 6.0f, 6.0f};
        const std::string name = lowerCopy(marker.className);
        if (name.rfind("monster_", 0) == 0)
        {
            half = {8.0f, 8.0f, 24.0f};
            glColor3f(0.1f, 0.9f, 0.2f);
        }
        else if (name.rfind("light", 0) == 0)
        {
            half = {4.0f, 4.0f, 4.0f};
            glColor3f(1.0f, 0.9f, 0.1f);
        }
        else if (name.rfind("trigger_", 0) == 0)
        {
            half = {10.0f, 10.0f, 3.0f};
            glColor3f(1.0f, 0.2f, 0.1f);
        }
        else
        {
            glColor3f(0.2f, 0.7f, 1.0f);
        }
        const Vec3 mins = toRenderSpace(sub(marker.origin, half));
        const Vec3 maxs = toRenderSpace(add(marker.origin, half));
        drawBoxLines({std::min(mins.x, maxs.x), std::min(mins.y, maxs.y), std::min(mins.z, maxs.z)},
                     {std::max(mins.x, maxs.x), std::max(mins.y, maxs.y), std::max(mins.z, maxs.z)});
    }
    glLineWidth(1.0f);
    glColor3f(1, 1, 1);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
#endif
}

void Renderer::renderStudioModels()
{
#ifdef _WIN32
    glEnable(GL_TEXTURE_2D);
    for (std::size_t modelIndex = 0; modelIndex < studioModels_.size(); ++modelIndex)
    {
        const StudioModelSceneEntry& entry = studioModels_[modelIndex];
        if (entry.model.frames.empty())
        {
            continue;
        }
        const std::size_t frameIndex = static_cast<std::size_t>(studioAnimationTime_ * entry.model.framesPerSecond) % entry.model.frames.size();
        const auto& vertices = entry.model.frames[frameIndex];
        for (const StudioModelInstance& instance : entry.instances)
        {
            glPushMatrix();
            const Vec3 origin = toRenderSpace(instance.origin);
            glTranslatef(origin.x, origin.y, origin.z);
            glRotatef(-instance.angles.y, 0.0f, 1.0f, 0.0f);
            glScalef(worldScale_, worldScale_, worldScale_);
            for (std::size_t i = 0; i + 2 < vertices.size(); i += 3)
            {
                const std::uint32_t texIndex = vertices[i].textureIndex;
                if (modelIndex < studioTextureIds_.size() && texIndex < studioTextureIds_[modelIndex].size())
                {
                    glBindTexture(GL_TEXTURE_2D, studioTextureIds_[modelIndex][texIndex]);
                }
                const bool masked = texIndex < entry.model.textures.size() && entry.model.textures[texIndex].masked;
                if (masked)
                {
                    glEnable(GL_BLEND);
                    glAlphaFunc(GL_GREATER, 0.5f);
                    glEnable(GL_ALPHA_TEST);
                }
                glColor3f(1.0f, 1.0f, 1.0f);
                glBegin(GL_TRIANGLES);
                for (std::size_t j = 0; j < 3; ++j)
                {
                    const StudioVertex& v = vertices[i + j];
                    glTexCoord2f(v.u, v.v);
                    glVertex3f(v.position.x, v.position.z, -v.position.y);
                }
                glEnd();
                if (masked)
                {
                    glDisable(GL_ALPHA_TEST);
                    glDisable(GL_BLEND);
                }
            }
            glPopMatrix();
        }
    }
#endif
}

Vec3 Renderer::brushModelOffset(std::int32_t modelIndex) const
{
    for (const RuntimeDoor& door : doors_)
    {
        if (door.modelIndex == modelIndex)
        {
            return mul(door.direction, door.distance * door.progress);
        }
    }
    return {};
}

Vec3 Renderer::toRenderSpace(const Vec3& point) const
{
    return {
        (point.x - worldCenter_.x) * worldScale_,
        (point.z - worldCenter_.z) * worldScale_,
        -(point.y - worldCenter_.y) * worldScale_,
    };
}

Vec3 Renderer::toMapSpace(const Vec3& point) const
{
    return {
        point.x / worldScale_ + worldCenter_.x,
        -(point.z / worldScale_) + worldCenter_.y,
        point.y / worldScale_ + worldCenter_.z,
    };
}

Vec3 Renderer::faceVertexPosition(const BspRenderFace& face, const BspRenderVertex& vertex) const
{
    Vec3 position = vertex.position;
    if (face.brushEntity && face.modelIndex > 0)
    {
        position = add(position, brushModelOffset(face.modelIndex));
    }
    return toRenderSpace(position);
}

void Renderer::emitFaceVertex(const BspRenderFace& face, const BspRenderVertex&) const
{
#ifdef _WIN32
    const auto found = textureIds_.find(lowerCopy(face.textureName));
    if (found != textureIds_.end())
    {
        glBindTexture(GL_TEXTURE_2D, found->second);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    const bool hasTexture = found != textureIds_.end();
    if (face.translucent)
    {
        if (hasTexture)
        {
            glColor4f(1.0f, 1.0f, 1.0f, 0.45f);
        }
        else
        {
            glColor4f(face.color[0], face.color[1], face.color[2], 0.45f);
        }
    }
    else
    {
        if (hasTexture)
        {
            glColor3f(1.0f, 1.0f, 1.0f);
        }
        else
        {
            glColor3f(face.color[0], face.color[1], face.color[2]);
        }
    }
    glBegin(GL_POLYGON);
    for (const BspRenderVertex& vertex : face.vertices)
    {
        const Vec3 pos = faceVertexPosition(face, vertex);
        glTexCoord2f(vertex.u, vertex.v);
        glVertex3f(pos.x, pos.y, pos.z);
    }
    glEnd();
#else
    (void)face;
#endif
}

void Renderer::emitFaceLightmap(const BspRenderFace& face, std::size_t faceIndex) const
{
#ifdef _WIN32
    if (faceIndex >= lightmapTextureIds_.size() || lightmapTextureIds_[faceIndex] == 0)
    {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, lightmapTextureIds_[faceIndex]);
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_ZERO);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_EQUAL);
    glColor3f(1.0f, 1.0f, 1.0f);

    glBegin(GL_POLYGON);
    for (const BspRenderVertex& vertex : face.vertices)
    {
        const Vec3 pos = faceVertexPosition(face, vertex);
        glTexCoord2f(vertex.lightU, vertex.lightV);
        glVertex3f(pos.x, pos.y, pos.z);
    }
    glEnd();

    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
#else
    (void)face;
    (void)faceIndex;
#endif
}

void Renderer::uploadPendingTextures()
{
#ifdef _WIN32
    for (const TextureImage& image : pendingTextures_)
    {
        if (image.rgba.empty() || image.width == 0 || image.height == 0)
        {
            continue;
        }
        unsigned int texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
        textureIds_[lowerCopy(image.name)] = texture;
    }
    pendingTextures_.clear();
#endif
}

void Renderer::uploadLightmapTextures()
{
#ifdef _WIN32
    if (!lightmapTextureIds_.empty())
    {
        return;
    }
    lightmapTextureIds_.resize(worldFaces_.size(), 0);
    for (std::size_t i = 0; i < worldFaces_.size(); ++i)
    {
        const BspRenderFace& face = worldFaces_[i];
        if (face.lightmapRgba.empty() || face.lightmapWidth == 0 || face.lightmapHeight == 0)
        {
            continue;
        }

        unsigned int texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA,
                     static_cast<GLsizei>(face.lightmapWidth),
                     static_cast<GLsizei>(face.lightmapHeight),
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     face.lightmapRgba.data());
        lightmapTextureIds_[i] = texture;
    }
#endif
}

void Renderer::uploadStudioModelTextures()
{
#ifdef _WIN32
    if (!studioTextureIds_.empty() || studioModels_.empty())
    {
        return;
    }
    studioTextureIds_.resize(studioModels_.size());
    for (std::size_t modelIndex = 0; modelIndex < studioModels_.size(); ++modelIndex)
    {
        const auto& textures = studioModels_[modelIndex].model.textures;
        studioTextureIds_[modelIndex].resize(textures.size(), 0);
        for (std::size_t i = 0; i < textures.size(); ++i)
        {
            const TextureImage& image = textures[i].image;
            if (image.rgba.empty() || image.width == 0 || image.height == 0)
            {
                continue;
            }
            unsigned int texture = 0;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
            studioTextureIds_[modelIndex][i] = texture;
        }
    }
#endif
}

void Renderer::destroyTextures()
{
#ifdef _WIN32
    for (auto& [_, id] : textureIds_)
    {
        glDeleteTextures(1, &id);
    }
    textureIds_.clear();
    for (unsigned int id : lightmapTextureIds_)
    {
        if (id != 0)
        {
            glDeleteTextures(1, &id);
        }
    }
    lightmapTextureIds_.clear();
    for (auto& ids : studioTextureIds_)
    {
        for (unsigned int id : ids)
        {
            if (id != 0)
            {
                glDeleteTextures(1, &id);
            }
        }
    }
    studioTextureIds_.clear();
#endif
}
}

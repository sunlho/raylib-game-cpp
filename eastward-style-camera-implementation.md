# Raylib/Flecs 项目的 Eastward 风格平滑像素相机实现方案

本文针对当前 `raylib-game-cpp` 工程，目标是在保持像素边缘清晰、角色与地图不发生相对抖动的前提下，让相机和移动角色在 30/60/120/240 FPS 下都具有稳定的视觉运动。

本文只以当前的 `src/main.cpp` 手写主循环为基础。已删除的 `src/modules/Runtime/` 不属于本方案，也不应重新引入。

## 1. 结论先行

这个问题不能只靠“给相机坐标取整”解决。完整方案必须同时满足四个条件：

1. 逻辑视野、场景渲染目标和窗口尺寸互相独立。
2. 固定 60 Hz 物理位置在渲染帧之间进行插值。
3. 相机保留浮点平滑状态，只在最终绘制前量化到场景像素栅格。
4. 所有动态物体相对最终渲染相机使用同一套量化规则，不能只特殊处理玩家。

建议首先实现以下基线：

| 项目 | 推荐值 |
| --- | ---: |
| 固定模拟频率 | 60 Hz |
| 逻辑世界视野 | 640 x 360 world units |
| 场景渲染目标 | 1280 x 720 pixels |
| `Camera2D.zoom` | 2.0 |
| 世界单位量化步数 | 2 steps/world unit |
| 最小世界量化步长 | 0.5 world unit |
| 场景纹理过滤 | Point；PixZoom 路径例外 |

在这个配置下：

```text
1280 render pixels / zoom 2 = 640 visible world units
1 render pixel / zoom 2 = 0.5 world unit
```

也就是说，相机和动态角色都应落在 `0.5` world unit 的最终绘制栅格上，而不是简单取整到 `1.0` world unit。

## 2. Eastward 中能够确认的结构

从现有反编译 Lua、MOAI 层的调用关系和运行参数，可以确认 Eastward 的相机不是直接平滑跟随角色本体，而是分层处理：

```text
角色逻辑位置
    |
    v
Hero/_FocusProxy/_Inner       状态和朝向驱动的前视代理
    |
    v
CameraFocusController         第二层相机追踪插值
    |
    v
浮点相机状态
    |
    v
EWPropRenderTransform         绘制变换和像素栅格处理
    |
    v
场景缓冲 / PixZoom2 / 窗口
```

观察到的前视距离为：

| 状态 | 前视距离 |
| --- | ---: |
| Idle | 0 |
| Walk | `0.7 * 10 = 7` |
| Attack | `0.3 * 10 = 3` |
| Run | `1.0 * 10 = 10` |

观察到的代理插值参数为：

- 前视距离增加：`0.1`
- 前视距离收回：`0.02`
- 朝向偏移变化：`0.04`
- 场景默认相机 interpolation：`0.1`
- 默认用户追踪速度 fast：`1.8`
- 第二层插值：`k = interpolation / outputZoom * trackingSpeed`

在 `outputZoom = 2` 时，第二层每个参考帧的系数是：

```text
k = 0.1 / 2 * 1.8 = 0.09
```

Eastward 还把逻辑 Transform 与 `EWPropRenderTransform` 分开。角色上观察到的 `roundStep = 2` 很可能表示：

```cpp
rounded = std::round(position * roundStep) / roundStep;
```

因此 `roundStep = 2` 对应 `0.5` world unit。主场景相机 zoom 为 `2`，正好让 `0.5` world unit 对应一个场景缓冲像素。

> `roundStep` 的公式属于高置信度推断，因为 `EWPropRenderTransform` 的原生实现没有公开。本文不会把它描述成已经得到源码验证的事实。

Eastward 的两种相关渲染路径可以概括为：

- Full：大约 `1280x720` 场景缓冲，camera zoom `2`，动态物体使用半世界单位栅格。
- Half：`640x360` 场景缓冲，粗相机使用整世界单位栅格，同时保留精细相机的半单位残差，在最终放大复制时补偿该残差。

`PixZoom2` 是一种面向像素画的 sharp-bilinear 放大，不是普通 nearest，也不是直接对原始像素画使用普通 bilinear。

## 3. 当前工程审计

### 3.1 主循环

当前 `src/main.cpp`：

- 以 `1/60` 固定步长执行物理和角色更新。
- `cameraFollow` 也在固定步长循环内部执行。
- 绘制发生在固定步长循环之后。
- 已有 `accumulator`，但没有把 `accumulator / fixedTimeStep` 用于渲染插值。

因此在 120 或 240 FPS 下，即使将相机追踪移到绘制前，玩家位置仍然只以 60 Hz 更新。相机会连续追踪一个每 1/60 秒跳一次的目标，画面仍会表现出阶梯运动。

### 3.2 当前相机

`src/modules/Movement.cpp` 中的指数阻尼本身是正确的帧率无关形式：

```cpp
const float blend = 1.0f - std::exp(-followSpeed * deltaTime);
```

问题是它运行在固定模拟管线中，并且平滑结果立即写回 `Camera2D.target` 后再量化。当前 `MainCamera` 还保存了只服务于被跟随玩家的 `followRenderPosition`，导致玩家与其他动态角色走不同的绘制规则。

### 3.3 当前视野与分辨率

当前窗口和 `RenderTargetSize` 都是 `640x360`，相机 zoom 是 `2`：

```text
当前可见世界范围 = 640x360 / 2 = 320x180
```

这不是“640x360 逻辑视野、2 倍像素密度”，而是一个只有 `320x180` world units 的视野。

控制台 `resolution` 命令同时改变窗口和 `RenderTargetSize`，所以切换窗口分辨率会改变：

- 可见世界范围
- 相机边界计算
- loading overlay 坐标空间
- 场景纹理尺寸和显存分配

这些都应该与窗口大小解耦。

### 3.4 当前动态物体量化

`src/modules/Character/CharacterRendering.cpp`：

- 被相机跟随的玩家使用 `followRenderPosition`。
- 其他角色把世界坐标直接 `roundf()` 到整数。

当相机 zoom 为 `2` 时，整数世界坐标等于每次跨越两个场景像素；与此同时玩家可以落在半单位栅格。不同动态物体会出现速度不一致、相对抖动或擦肩时错位。

### 3.5 当前最终复制

`Rendering::DrawScaledRenderTarget()` 使用向上取整的 cover 缩放：

```text
scale = max(ceil(windowWidth / targetWidth), ceil(windowHeight / targetHeight))
```

它会优先铺满窗口并允许裁切，不能提供稳定的 fit/letterbox 行为；在 `1600x900`、`1920x1080` 这类非整数倍率下也没有专门的像素画重采样策略。

## 4. 目标架构

```text
                       固定 60 Hz
输入 -> 物理前处理 -> 物理 -> PostPhysics -> 当前逻辑 Position
                                      |
                                      +-> PreviousPosition / CurrentPosition 快照

                       每个渲染帧
accumulator/fixedDt -> 插值后的 RenderPosition
                              |
                              +-> FocusProxy（状态前视）
                                      |
                                      v
                              SmoothCameraTarget（始终浮点）
                                      |
                                      v
                              RenderCameraTarget（最终量化）
                                      |
                    +-----------------+-----------------+
                    |                                   |
                    v                                   v
          动态物体相机相对量化                    静态 tile 原始网格
                    |                                   |
                    +-----------------+-----------------+
                                      v
                              固定 SceneRenderTarget
                                      |
                           integer nearest 或 PixZoom
                                      v
                                    Window
```

关键所有权原则：

- `Position`：模拟真值，碰撞、AI、寻路只读它。
- `PreviousPosition`：上一个固定 tick 的模拟真值。
- `RenderPosition`：仅用于本帧绘制，可插值和量化，不能写回物理。
- `smoothTarget`：相机连续浮点状态，绝不能每帧被取整后再作为下一帧输入。
- `renderTarget`：由 `smoothTarget` 派生的最终场景像素对齐值。
- `Camera2D.target`：只在绘制前接收 `renderTarget`。

## 5. 推荐数据结构

建议逐步替换 `MainCamera::followRenderPosition`，不要继续增加玩家专用字段。

```cpp
// Rendering.h
namespace Rendering {

struct PreviousPosition {
  Vector2 value = {0.0f, 0.0f};
};

struct RenderPosition {
  Vector2 interpolated = {0.0f, 0.0f};
  Vector2 quantized = {0.0f, 0.0f};
};

struct RenderSettings {
  Vector2 logicalViewSize = {640.0f, 360.0f};
  Vector2 sceneTargetSize = {1280.0f, 720.0f};
  float pixelsPerWorldUnit = 2.0f;
};

} // namespace Rendering
```

```cpp
// Camera.h
namespace GameCamera {

enum class FollowMotion {
  Idle,
  Walk,
  Attack,
  Run,
};

struct FocusProxy {
  Vector2 direction = {0.0f, 1.0f};
  Vector2 offset = {0.0f, 0.0f};
  float distance = 0.0f;
};

struct MainCamera {
  Camera2D value{};
  bool enabled = true;
  bool autoCenterOffset = true;

  Vector2 smoothTarget = {0.0f, 0.0f};
  Vector2 renderTarget = {0.0f, 0.0f};
  FocusProxy focus{};

  float followSpeed = 5.66f;
  float pixelsPerWorldUnit = 2.0f;
  bool snapToRenderGrid = true;
};

} // namespace GameCamera
```

如果暂时不想把设置从 `MainCamera` 抽离，也可以让 `pixelsPerWorldUnit` 保留在相机中；但 `logicalViewSize`、`sceneTargetSize` 和窗口大小仍必须是不同概念。

## 6. 通用数学函数

### 6.1 栅格量化

```cpp
float SnapToGrid(float value, float stepsPerWorldUnit) {
  if (stepsPerWorldUnit <= 0.0f) {
    return value;
  }
  return std::round(value * stepsPerWorldUnit) / stepsPerWorldUnit;
}

Vector2 SnapToGrid(Vector2 value, float stepsPerWorldUnit) {
  return {
      SnapToGrid(value.x, stepsPerWorldUnit),
      SnapToGrid(value.y, stepsPerWorldUnit),
  };
}
```

不要把 `Camera2D.zoom` 隐式当作所有模式下的量化步数。Full 模式下二者都为 `2`，但 Half 模式的场景相机 zoom 是 `1`，精细相机状态仍可保留 `2 steps/world unit`。

### 6.2 动态物体相对相机量化

```cpp
Vector2 QuantizeForCamera(
    Vector2 worldPosition,
    Vector2 renderCamera,
    float pixelsPerWorldUnit) {
  Vector2 relative = Vector2Subtract(worldPosition, renderCamera);
  relative.x = SnapToGrid(relative.x, pixelsPerWorldUnit);
  relative.y = SnapToGrid(relative.y, pixelsPerWorldUnit);
  return Vector2Add(renderCamera, relative);
}
```

这个函数必须用于所有移动角色、移动道具、粒子锚点和其他需要像素稳定的动态实体。静态 tile 已经处于确定的地图网格，不要再次做角色式插值。

### 6.3 帧率无关阻尼

Eastward 观察到的数值可以按“60 Hz 参考帧插值因子”转换成任意渲染帧率下的阻尼：

```cpp
float ReferenceFrameLerp(float referenceFactor, float dt, float referenceHz = 60.0f) {
  const float factor = std::clamp(referenceFactor, 0.0f, 0.999999f);
  const float lambda = -std::log(1.0f - factor) * referenceHz;
  return 1.0f - std::exp(-lambda * std::max(dt, 0.0f));
}
```

近似换算结果：

| 60 Hz 参考 factor | 指数阻尼 lambda |
| ---: | ---: |
| `0.10` | `6.32` |
| `0.02` | `1.21` |
| `0.04` | `2.45` |
| `0.09` | `5.66` |

当前工程的 `followSpeed = 6` 与 Eastward 第二层默认追踪的 `5.66` 已经很接近。

## 7. 固定模拟与渲染插值

### 7.1 快照时序

创建玩家或任何动态实体时：

```cpp
entity
    .set<Rendering::Position>({spawn})
    .set<Rendering::PreviousPosition>({spawn})
    .set<Rendering::RenderPosition>({spawn, spawn});
```

每个固定 tick 开始前，把当前逻辑位置复制到 previous。物理和约束结束后，`Position` 是新的 current：

```cpp
CapturePreviousPositions(world); // previous = Position

ecs_run_pipeline(world, prePhysics, fixedTimeStep);
ecs_run_pipeline(world, physicsStep, fixedTimeStep);
ecs_run_pipeline(world, postPhysics, fixedTimeStep);
ecs_run_pipeline(world, fixedUpdate, fixedTimeStep);
ecs_run_pipeline(world, characterUpdate, fixedTimeStep);
```

当前工程由 `PostPhysics` 把物理位置写回 `Rendering::Position`，所以 previous 必须在 `prePhysics` 之前捕获，不能在 `postPhysics` 之后才覆盖。

### 7.2 计算 alpha

固定循环结束后：

```cpp
const float renderAlpha = std::clamp(
    accumulator / fixedTimeStep,
    0.0f,
    1.0f);
```

为动态实体生成未经量化的渲染位置：

```cpp
renderPosition.interpolated = Vector2Lerp(
    previousPosition.value,
    currentPosition.value,
    renderAlpha);
```

这种经典插值会让显示状态落后模拟最多一个固定 tick，也就是约 `16.67 ms`。这是换取稳定渲染的确定性延迟，通常远小于 60 Hz 阶梯移动带来的视觉问题。

### 7.3 暂停、单步和传送

- 暂停时不要继续增长用于模拟插值的 accumulator。
- 单步后可以令 `renderAlpha = 1`，确保调试截图展示刚执行完成的 tick。
- 传送、地图切换、出生和读档时，将 `previous`、`Position`、`RenderPosition` 同时设置为目的地。
- 同时将相机 `smoothTarget` 和 `renderTarget` 重置到新焦点，避免长距离飞越。

## 8. 每帧相机更新

相机更新应从固定循环中移出，在每个渲染帧、生成 `RenderPosition.interpolated` 后执行一次。

### 8.1 FocusProxy

```cpp
float DesiredLookAhead(GameCamera::FollowMotion motion) {
  switch (motion) {
    case GameCamera::FollowMotion::Walk:   return 7.0f;
    case GameCamera::FollowMotion::Attack: return 3.0f;
    case GameCamera::FollowMotion::Run:    return 10.0f;
    case GameCamera::FollowMotion::Idle:   return 0.0f;
  }
  return 0.0f;
}

void UpdateFocusProxy(
    GameCamera::FocusProxy &focus,
    Vector2 desiredDirection,
    GameCamera::FollowMotion motion,
    float dt) {
  if (Vector2LengthSqr(desiredDirection) > 0.0f) {
    desiredDirection = Vector2Normalize(desiredDirection);
    const float directionBlend = ReferenceFrameLerp(0.04f, dt);
    focus.direction = Vector2Normalize(
        Vector2Lerp(focus.direction, desiredDirection, directionBlend));
  }

  const float desiredDistance = DesiredLookAhead(motion);
  const float distanceFactor = desiredDistance > focus.distance ? 0.10f : 0.02f;
  focus.distance = Lerp(
      focus.distance,
      desiredDistance,
      ReferenceFrameLerp(distanceFactor, dt));

  focus.offset = Vector2Scale(focus.direction, focus.distance);
}
```

朝向优先使用角色明确的 facing direction，而不是低速时带噪声的瞬时速度。Idle 是否立刻归零取决于期望手感；为了接近观察到的行为，可以让距离以 `0.02` 缓慢回收。

### 8.2 浮点相机与最终相机分离

```cpp
Vector2 ClampCameraToMap(
    Vector2 target,
    Vector2 logicalViewSize,
    Vector2 mapSize) {
  const Vector2 halfView = Vector2Scale(logicalViewSize, 0.5f);

  auto clampAxis = [](float value, float halfExtent, float extent) {
    if (extent <= 0.0f) return value;
    if (extent < halfExtent * 2.0f) return extent * 0.5f;
    return std::clamp(value, halfExtent, extent - halfExtent);
  };

  return {
      clampAxis(target.x, halfView.x, mapSize.x),
      clampAxis(target.y, halfView.y, mapSize.y),
  };
}

void UpdateRenderCamera(
    GameCamera::MainCamera &camera,
    Vector2 interpolatedPlayer,
    Vector2 logicalViewSize,
    Vector2 mapSize,
    float dt) {
  const Vector2 desired = ClampCameraToMap(
      Vector2Add(interpolatedPlayer, camera.focus.offset),
      logicalViewSize,
      mapSize);

  const float blend = 1.0f - std::exp(-camera.followSpeed * dt);
  camera.smoothTarget = Vector2Lerp(camera.smoothTarget, desired, blend);
  camera.smoothTarget = ClampCameraToMap(
      camera.smoothTarget,
      logicalViewSize,
      mapSize);

  camera.renderTarget = camera.snapToRenderGrid
      ? SnapToGrid(camera.smoothTarget, camera.pixelsPerWorldUnit)
      : camera.smoothTarget;

  camera.renderTarget = ClampCameraToMap(
      camera.renderTarget,
      logicalViewSize,
      mapSize);

  // Clamp 之后再量化一次，防止地图边界不是 0.5 的倍数。
  if (camera.snapToRenderGrid) {
    camera.renderTarget = SnapToGrid(
        camera.renderTarget,
        camera.pixelsPerWorldUnit);
  }

  camera.value.target = camera.renderTarget;
}
```

实际实现中最好让地图边界本身也符合渲染栅格；否则“边界严格不越界”和“相机严格像素对齐”在最后半个单位上可能冲突。优先统一地图尺寸和逻辑视野尺寸到 `0.5` 的倍数。

### 8.3 Camera2D 配置

Full 基线模式：

```cpp
camera.value.offset = {640.0f, 360.0f}; // 1280x720 scene target center
camera.value.zoom = 2.0f;
camera.value.rotation = 0.0f;
camera.pixelsPerWorldUnit = 2.0f;
```

相机边界必须使用 `logicalViewSize = 640x360`，不能使用窗口尺寸。等价计算是：

```cpp
logicalViewSize = sceneTargetSize / camera.value.zoom;
```

## 9. 动态物体的统一绘制位置

相机完成本帧更新后，为每个动态实体计算：

```cpp
renderPosition.quantized = QuantizeForCamera(
    renderPosition.interpolated,
    camera.renderTarget,
    camera.pixelsPerWorldUnit);
```

之后所有动态对象只使用 `RenderPosition.quantized` 绘制。不要在 `CharacterRenderable::Draw()` 内再次 `roundf(dest.x)` 或 `roundf(dest.y)`。

推荐规则：

| 对象 | 绘制位置来源 |
| --- | --- |
| 玩家 | 插值位置 -> 相机相对量化 |
| NPC | 插值位置 -> 相机相对量化 |
| 移动道具 | 插值位置 -> 相机相对量化 |
| 跟随角色的特效 | 先求浮点锚点，再相机相对量化 |
| 静态 tile/chunk | 地图原始整数网格 |
| 屏幕 UI | 场景相机之外的窗口/界面坐标 |

角色脚底排序也应使用本帧 `RenderPosition.interpolated.y` 或 `quantized.y` 生成渲染排序键。继续使用固定 tick 中缓存的整数 `sortY`，可能在两个角色交叉时产生一帧排序跳变。

## 10. 建议的主循环顺序

下面是与当前 `src/main.cpp` 最接近的迁移形态：

```cpp
const float frameTime = std::min(GetFrameTime(), 0.25f);

// 每个显示帧采样输入，现有 moveUpdate 可以暂时保留。
ecs_run_pipeline(world, moveUpdate, frameTime);

accumulator += frameTime;
while (accumulator >= fixedTimeStep) {
  CapturePreviousPositions(world);

  ecs_run_pipeline(world, prePhysics, fixedTimeStep);
  ecs_run_pipeline(world, physicsStep, fixedTimeStep);
  ecs_run_pipeline(world, postPhysics, fixedTimeStep);
  ecs_run_pipeline(world, fixedUpdate, fixedTimeStep);
  ecs_run_pipeline(world, characterUpdate, fixedTimeStep);

  accumulator -= fixedTimeStep;
}

const float renderAlpha = std::clamp(
    accumulator / fixedTimeStep,
    0.0f,
    1.0f);

PrepareInterpolatedPositions(world, renderAlpha);
UpdateFocusProxyForRender(world, frameTime);
UpdateCameraForRender(world, frameTime);
QuantizeDynamicRenderPositions(world);
UpdateRenderSortKeys(world);

Rendering::BeginFrame(world);
GameCamera::Begin2D(world);
ecs_run_pipeline(world, background, frameTime);
ecs_run_pipeline(world, worldDraw, frameTime);
ecs_run_pipeline(world, sortedWorldDraw, frameTime);
GameCamera::End2D(world);
Rendering::PresentFrame(world);
Rendering::EndFrame();
```

具体注意事项：

- 从固定循环移除 `cameraFollow`。
- 渲染相机使用真实 `frameTime` 更新，而不是 `fixedTimeStep`。
- 建议对异常长帧做上限限制，并限制单帧最大 fixed steps，避免窗口拖动后追赶过久。
- 调试暂停/单步应保留当前项目对 accumulator 的清理语义。

## 11. 固定场景缓冲与窗口输出

### 11.1 分离尺寸

把当前单一 `RenderTargetSize` 的职责拆开：

```cpp
struct RenderSettings {
  Vector2 logicalViewSize = {640.0f, 360.0f};
  Vector2 sceneTargetSize = {1280.0f, 720.0f};
};
```

窗口尺寸始终由 `GetScreenWidth()/GetScreenHeight()` 获取。控制台 `resolution` 命令只调用：

```cpp
SetWindowSize(width, height);
```

它不能修改 `sceneTargetSize`。

`EnsureRenderTarget()` 只根据固定的 `sceneTargetSize` 创建场景缓冲。loading overlay 如果属于游戏画面，应使用场景缓冲坐标；Console/FPS 如果属于系统 UI，则在 `EndTextureMode()` 之后以窗口坐标绘制。

### 11.2 最稳妥的整数 nearest fit

```cpp
int scale = std::max(1, static_cast<int>(std::floor(std::min(
    windowWidth / static_cast<float>(sceneWidth),
    windowHeight / static_cast<float>(sceneHeight)))));

int destinationWidth = sceneWidth * scale;
int destinationHeight = sceneHeight * scale;
int offsetX = (windowWidth - destinationWidth) / 2;
int offsetY = (windowHeight - destinationHeight) / 2;
```

这个模式不会裁切，剩余区域使用 letterbox。它适合：

- `1280x720`：1x
- `2560x1440`：2x

当窗口小于 `1280x720` 时，不能用 `max(1, ...)` 后假装可以完整显示。应明确选择：切换到 Half 模式、允许缩小采样，或不支持该窗口尺寸。

### 11.3 非整数倍率的 PixZoom 路径

`1600x900` 是 1.25x，`1920x1080` 是 1.5x。普通 nearest 会产生不均匀像素宽度，普通 bilinear 会整体模糊。可以增加一个 sharp-bilinear shader，使像素内部保持大面积平坦，只在非整数像素边缘的窄区域混合。

以下 GLSL 330 片段可作为 Raylib 实现起点：

```glsl
#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 sourceSize;
uniform vec2 outputSize;

out vec4 finalColor;

void main() {
    vec2 scale = max(outputSize / sourceSize, vec2(1.0));
    vec2 texel = fragTexCoord * sourceSize;
    vec2 centerDistance = fract(texel) - 0.5;
    vec2 blendLimit = 0.5 - 0.5 / scale;

    vec2 adjusted =
        (centerDistance - clamp(centerDistance, -blendLimit, blendLimit))
        * scale + 0.5;
    vec2 uv = (floor(texel) + adjusted) / sourceSize;

    finalColor = texture(texture0, uv) * colDiffuse * fragColor;
}
```

使用该 shader 时：

- 场景 target 尺寸仍固定。
- destination 使用保持 16:9 的 fit 矩形，不要 cover 裁切。
- 场景纹理需要 linear 采样，由 shader 控制混合区域。
- sprite atlas 本身仍应避免边缘串色，必要时加入 padding/extrusion。
- shader 只处理最终场景复制，不要给每张 sprite 单独套普通 bilinear。

这个 shader 是可移植的 sharp-bilinear 起点，不应声称与未公开的 `PixZoom2` 指令级完全一致；应通过目标分辨率截图再调整采样中心。

## 12. 可选 Half 模式：残差补偿

只有在 Full 基线稳定后再实现这一层。

Half 模式目标：

| 项目 | 值 |
| --- | ---: |
| 逻辑视野 | 640x360 world units |
| 场景缓冲 | 640x360 pixels |
| 场景 Camera2D.zoom | 1 |
| 粗相机栅格 | 1 step/world unit |
| 精细相机栅格 | 2 steps/world unit |
| 最终内部输出 | 1280x720 pixels |

保留相机的精细位置，然后派生粗相机：

```cpp
const Vector2 fineCamera = SnapToGrid(camera.smoothTarget, 2.0f);
const Vector2 coarseCamera = SnapToGrid(fineCamera, 1.0f);

// 先按 coarseCamera 渲染 640x360，再放大 2 倍。
camera.value.target = coarseCamera;
camera.value.zoom = 1.0f;

// 让粗相机画面移动到 fineCamera 本应产生的位置。
const Vector2 finalCopyShift = Vector2Scale(
    Vector2Subtract(coarseCamera, fineCamera),
    2.0f);
```

`finalCopyShift` 的每个轴只会是 `-1/0/1` 个 1280x720 输出像素。符号来自：

```text
desired screen = (world - fineCamera) * 2
coarse screen  = (world - coarseCamera) * 2
shift          = desired - coarse = (coarseCamera - fineCamera) * 2
```

如果直接平移一张恰好 `640x360` 的纹理，边缘会露出空白。至少需要一圈 guard/overscan：

- 渲染比视野每边多 1 个源像素，例如内部 target 使用 `642x362`。
- 最终放大后从中心区域裁切到 `1280x720`。
- 地图裁剪和 chunk 可见性计算也要包含 overscan。

Half 模式中的动态物体要与 coarse camera 使用一致的 `1 step/world unit` 场景栅格；半单位细节由最终复制残差整体补偿，不能对单个角色额外移动一个输出像素，否则会再次产生相对抖动。

## 13. 按文件迁移清单

### `src/main.cpp`

- 将场景 target 初始值改为 `1280x720`，逻辑视野保持 `640x360`。
- 保留 `fixedTimeStep = 1/60` 和 accumulator。
- 每个 fixed tick 前捕获 previous position。
- 从 fixed loop 删除 `cameraFollow`。
- fixed loop 结束后计算 `renderAlpha`。
- 依次运行：插值位置、FocusProxy、渲染相机、动态物体量化、渲染排序。
- 创建/传送玩家时同步初始化 previous/render position 和两个相机 target。

### `src/modules/Rendering.h`

- 增加 `PreviousPosition`、`RenderPosition`、`RenderSettings`。
- 明确 `RenderTargetSize` 是场景 target 尺寸，或用更明确的 `SceneTargetSize` 替代。
- 不要在 `Position` 中混入绘制专用状态。

### `src/modules/Rendering.cpp`

- 注册新的 Flecs component/singleton。
- 增加本帧插值和动态物体量化系统。
- 绘制动态 Renderable 时读取 `RenderPosition.quantized`。
- 将 `DrawScaledRenderTarget()` 从 cover 改为 fit。
- 增加 integer nearest 输出模式。
- 后续再增加 PixZoom shader 输出模式。
- UI/Console 与场景 overlay 明确各自的坐标空间。

### `src/modules/Camera.h`

- 增加 `smoothTarget`、`renderTarget` 和 `FocusProxy`。
- 删除 `followRenderPosition`、`useFollowRenderPosition` 这类玩家专用字段。
- 把 `pixelsPerWorldUnit` 设为明确参数，不依赖偶然相等的 zoom。

### `src/modules/Camera.cpp`

- `Begin2D()` 的 offset 使用场景 target 中心，不使用窗口中心。
- `Camera2D.target` 只接收当帧已经量化的 `renderTarget`。
- 添加 teleport/snap reset API，例如 `SnapTo(Vector2 focus)`。

### `src/modules/Movement.cpp`

- 把当前 `Follow Camera Target` 从 fixed pipeline 改成每渲染帧执行的相机准备函数或独立 pipeline。
- 目标来源改为玩家的 `RenderPosition.interpolated`，不是固定 tick 的 `Position`。
- 保留指数阻尼，但只对 `smoothTarget` 运算。
- 边界 half extent 来自固定逻辑视野。
- 将状态、朝向和速度信息输入 FocusProxy。

### `src/modules/Physics.cpp`

- 保持物理只写逻辑 `Position`。
- 确保 previous snapshot 在物理写回之前捕获。
- 传送/Relocate API 增加“重置插值历史”的调用约定。

### `src/modules/Character/CharacterRendering.cpp`

- 删除 `CameraFollowTag` 的特殊绘制分支。
- 删除 Draw 内部的世界整数 `roundf()`。
- 所有角色统一使用 `RenderPosition.quantized`。
- origin、sprite frame rectangle 仍保持整数像素。
- 渲染排序键改由本帧 render position 生成。

### `src/modules/Map/MapRendering.cpp`

- 静态 tile 继续使用地图网格，不做角色式插值。
- chunk 可见范围使用固定逻辑视野，不使用窗口大小。
- Half 模式加入 guard/overscan 后扩大一圈可见性范围。

### `src/modules/Console/Register.cpp`

- `resolution` 只调整窗口，不再修改场景 render target。
- `camerascale` 如果保留为调试命令，需要同步重新计算逻辑视野或明确标注它会破坏标准像素模式。
- 推荐增加只读诊断命令，显示 window、scene target、logical view、PPU、render mode。

## 14. 推荐初始参数

先使用最小参数集验证基础行为：

```cpp
fixedTimeStep = 1.0f / 60.0f;
logicalViewSize = {640.0f, 360.0f};
sceneTargetSize = {1280.0f, 720.0f};
cameraZoom = 2.0f;
pixelsPerWorldUnit = 2.0f;
cameraFollowSpeed = 5.66f;
```

基础相机稳定后，再启用：

```cpp
walkLookAhead = 7.0f;
attackLookAhead = 3.0f;
runLookAhead = 10.0f;
lookAheadExtendFactor60Hz = 0.10f;
lookAheadRetractFactor60Hz = 0.02f;
directionFactor60Hz = 0.04f;
```

调参顺序：

1. 暂时关闭前视，只验证插值、相机栅格和动态物体栅格。
2. 调整 `cameraFollowSpeed`，让玩家和相机运动关系稳定。
3. 加入 walk 前视。
4. 再加入 run、attack 和方向切换。
5. 最后测试 PixZoom 和 Half 残差路径。

## 15. 验证矩阵

### 15.1 帧率

| FPS | 预期检查 |
| ---: | --- |
| 30 | 每帧通常执行 2 个 fixed ticks；速度与 60 FPS 一致，无相机超调 |
| 60 | 基准行为；无周期性一像素抖动 |
| 120 | 两个渲染帧之间应出现插值位置，不能连续两帧完全相同后跳变 |
| 240 | 四个渲染帧平稳跨过一个 fixed tick；相机阻尼速度与 60 FPS 一致 |

建议录制逐帧截图，记录：

```text
previousPosition
currentPosition
renderAlpha
interpolatedPosition
smoothCameraTarget
renderCameraTarget
quantizedRenderPosition
```

### 15.2 窗口分辨率

| 窗口 | Full 1280x720 target 的建议输出 |
| --- | --- |
| 640x360 | 使用 Half 模式；或明确接受缩小采样，不可称为无损 pixel-perfect |
| 1280x720 | 1x nearest，基准验证 |
| 1600x900 | 1.25x PixZoom fit |
| 1920x1080 | 1.5x PixZoom fit |
| 2560x1440 | 2x nearest；也可对比 PixZoom |

切换窗口尺寸前后，可见世界范围必须始终为 `640x360` world units。

### 15.3 场景用例

- 玩家水平、垂直、对角线慢走。
- 跑步加速阶段和停止阶段。
- 快速连续反向，确认前视方向不会零向量归一化或瞬移。
- 玩家和 NPC 并排同速移动，检查二者相对距离是否逐帧稳定。
- 穿过 tile 边缘、墙边、楼梯和遮挡排序交界。
- 相机碰到地图四边和四角。
- 地图比逻辑视野更小时始终居中。
- `tp`、加载、暂停、单步和恢复。
- 以 0.5、1.0 和非整数 world unit 起点生成角色。
- 各输出分辨率检查 atlas 边缘串色和 letterbox。

## 16. 常见失败模式

### 只把相机更新移出 fixed loop

玩家目标仍以 60 Hz 跳变，120/240 FPS 下问题仍存在。必须先插值物理位置。

### 把量化结果写回 smoothTarget

阻尼会不断从取整后的值开始，产生粘滞、抖动和低速停顿。`smoothTarget` 必须始终保留浮点精度。

### 玩家特殊处理，其他角色直接 roundf

不同角色使用不同世界步长，镜头运动时会产生相对抖动。动态物体必须共享相机相对量化函数。

### 相机和角色使用不同栅格

例如相机按 `0.5` world unit、NPC 按 `1.0` world unit，会造成 NPC 每次跳两个场景像素。

### 双重量化

先生成 `RenderPosition.quantized`，又在 `Draw()` 中 `roundf()`，会破坏半单位结果。最终绘制位置只能量化一次。

### resolution 同时修改 render target

窗口切换会改变逻辑视野和相机边界，甚至重建大纹理。窗口尺寸只能影响最终输出矩形。

### 直接使用普通 bilinear

像素边界会全局变软。非整数倍率应使用 sharp-bilinear/PixZoom 类算法。

### 继续使用 cover 缩放

会裁掉画面边缘，导致逻辑视野与实际显示内容不一致。默认使用 fit；只有明确设计成 overscan 时才使用 crop。

### 忘记重置插值历史

传送或换图后，画面会在一帧内从旧位置插值到新位置，相机也可能横跨整张地图。

### Half 模式没有 guard pixels

最终复制偏移 `1` 个输出像素时会露出边缘。必须 overscan 后再裁切。

## 17. 实施阶段和完成标准

### 阶段 A：尺寸解耦

- 固定 scene target 为 `1280x720`。
- 固定逻辑视野为 `640x360`。
- `resolution` 只调整窗口。
- `1280x720` 和 `2560x1440` 使用 integer nearest fit。

完成标准：任何窗口尺寸下，相机看到的地图范围完全相同。

### 阶段 B：渲染插值

- 增加 previous/current/render position。
- 用 accumulator 计算 alpha。
- 所有动态角色在 120/240 FPS 下具有帧间位置变化。

完成标准：关闭相机移动时，角色本身已经平滑且像素稳定。

### 阶段 C：相机三态

- desired focus、smooth target、render target 分离。
- 相机每个渲染帧更新。
- smooth target 不量化，render target 量化到 `0.5` world unit。

完成标准：玩家静止时地图不抖，移动时相机以单场景像素稳定推进。

### 阶段 D：统一动态渲染变换

- 删除玩家专用 `followRenderPosition`。
- 所有动态对象使用相机相对量化。
- 删除 Draw 内二次取整。

完成标准：玩家与 NPC 并排移动时相对位置没有周期性跳动。

### 阶段 E：Eastward 风格前视

- 增加状态距离、方向缓动和第二层相机追踪。
- 覆盖急转、攻击、跑步、停步和地图边界。

完成标准：前视只改变构图，不破坏像素栅格和帧率一致性。

### 阶段 F：输出质量

- 非整数倍率增加 PixZoom/sharp-bilinear。
- 需要支持 `640x360` 窗口时再加入 Half 模式和残差补偿。

完成标准：五种目标窗口分辨率下构图一致，无裁切、无整体模糊、无边缘露底。

## 18. 最终建议

不要一开始复制 Eastward 的 Half 路径。当前工程最值得先完成的是 Full 基线：

```text
60 Hz 逻辑模拟
      + 渲染位置插值
      + 1280x720 固定场景缓冲
      + zoom 2
      + 0.5 world unit 的统一动态栅格
      + 浮点 smooth camera / 量化 render camera 分离
```

这套结构已经能解决当前最核心的 60 Hz 阶梯、玩家专用补偿、不同角色栅格不一致以及窗口分辨率改变视野的问题。PixZoom 和 Half 残差补偿应作为输出层优化，不应该承担修复模拟或相机架构的职责。

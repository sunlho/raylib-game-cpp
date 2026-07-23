# 移动抖动问题记录

## 现象

角色对角线移动时，即使限制 X/Y 同步移动，画面仍然有抖动感。最初表现为不同步的 `(1,2)` / `(2,1)`；同步后变成多帧 `(1,1)`、偶尔一帧 `(2,2)` 的速度跳变。

## 根因

逻辑移动保留浮点速度，但低分辨率像素渲染要求最终位置取整。60 FPS、速度 100 时，对角线单轴理论增量约为：

```text
100 / sqrt(2) / 60 = 1.178 px/frame
```

整数像素无法表达 `1.178`。只要同时要求整数像素、保留平均速度和每帧位移完全一致，三者就会冲突。

## 尝试过的方案

### X/Y 同步取整

让对角线 X/Y 绑定为相同步长，解决 `(1,2)` / `(2,1)` 的方向抖动，但仍会有 `(1,1)` / `(2,2)` 的速度跳变。

### 余数累加

保留小数余数，累计到足够大时再移动 `2`。这能避免提前移动 `2`，但每四五帧仍会出现一次 `(2,2)`，60 FPS 下仍可见顿挫。

### 只在累计到 2 时移动

不足 `2` 不移动，累计到 `2` 后移动 `(2,2)`。结果是停顿和跳跃交替，观感更不稳定。

## 当前采用的方案

使用原生分辨率渲染加 `2x` 相机缩放：

- 渲染目标从 `640x360` 改为 `1280x720`。
- 相机缩放为 `2.0`，等效视野仍约为 `640x360`。
- 相机按屏幕像素吸附，而不是按世界整数像素吸附。
- `2x` 缩放下，`0.5` 世界像素对应 `1` 个屏幕像素。

这样可以保留浮点物理速度，并把相机可表达的最小移动从 1 个世界像素降低到 0.5 个世界像素，明显减轻原先的跳变。

## 相关代码

- `src/main.cpp`：原生渲染目标和 `2x` 相机缩放。
- `src/modules/Movement.cpp`：相机边界换算和屏幕像素吸附。
- `src/modules/Character/CharacterRendering.cpp`：跟随相机时避免二次整数取整。
- `src/modules/Console/Register.cpp`：切换分辨率时同步更新渲染目标。

## 其他可尝试的方向

### 相机平滑跟随（Lerp/Damping）

#### 原理

当前相机是硬跟随：`camera.target = playerPosition`，角色位置每次量化跳变时，相机同步跳变，抖动直接传递到画面。

平滑跟随的思路是：相机不直接设置为目标位置，而是用插值逐步逼近：

```cpp
camera.target = Lerp(camera.target, desiredTarget, smoothFactor);
// 或者用阻尼：
Vector2 velocity = (desiredTarget - camera.target) * dampingStrength;
camera.target += velocity * deltaTime;
```

这样即使 `desiredTarget` 在 `(10.0, 10.0)` 和 `(10.5, 10.5)` 之间跳变，相机位置会平滑过渡，视觉上掩盖了量化的跳变。

#### 实现位置

修改 `src/modules/Movement.cpp:129-185` 的 `"Follow Camera Target"` 系统：

1. 在 `MainCamera` 结构体中添加平滑参数（如 `float smoothFactor` 或 `float dampingStrength`）。
2. 在计算出 `target` 后，不直接赋值给 `mainCamera.value.target`，而是用插值：
   ```cpp
   const float smoothFactor = mainCamera.smoothFactor; // 0.0-1.0，越大越快跟随
   mainCamera.value.target = Vector2Lerp(mainCamera.value.target, target, smoothFactor);
   ```
3. 如果启用了像素吸附，插值后再做最终吸附：
   ```cpp
   if (snapTargetToPixel) {
     mainCamera.value.target.x = SnapToScreenPixel(mainCamera.value.target.x, pixelScale);
     mainCamera.value.target.y = SnapToScreenPixel(mainCamera.value.target.y, pixelScale);
   }
   ```

#### 权衡

**优点**：
- 视觉上显著减少抖动感，画面更稳定流畅。
- 不需要改变渲染分辨率或物理速度。
- `smoothFactor` 可调，灵活控制跟随速度。

**缺点**：
- 相机滞后于角色，快速转向时会有轻微的"拖尾"感。
- 如果 `smoothFactor` 过小（如 0.1），输入响应会感觉有延迟。
- 需要权衡平滑度和响应性，通常 `0.2-0.3` 是可接受的范围。

#### 与当前方案的关系

平滑跟随可以和 2x 分辨率方案**叠加使用**：
- 2x 分辨率降低量化误差的幅度（从 1px 降到 0.5px）。
- 平滑跟随在时间维度上分散这些误差，避免单帧突变。
- 两者结合能在不完全牺牲像素精确性的前提下，达到接近无抖动的效果。

## 后续取舍

若要完全消除整数像素速度抖动，必须牺牲一个条件：修改实际速度使每帧增量固定、允许亚像素渲染，或使用更高内部采样精度。

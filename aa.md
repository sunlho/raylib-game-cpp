已完成第一轮分析。结论是：农场模式没有独立实现一套相机跟随，而是复用通用相机系统，并通过 `HeroControllerFarm` 关闭普通模式的“移动方向前视”，形成以角色为中心的平滑跟随。

### 核心调用链

```text
进入手动控制
  → HeroController:grabCameraFocus()
  → MainCameraController:setFocus()
  → CameraFocusController:setTarget()
  → CameraFocusController:onUpdate()
  → 更新 CM_Main 的 X/Y
  → 子节点 _Camera 随之移动
```

1. 农场场景复用了通用 `scene/design/F_Camera.scene/CM_Main` 相机预制体，场景模式是 `farm`，默认缩放为 `normal`。

2. 角色进入 `manual` 状态后调用 `grabCameraFocus()`：

   - 焦点区域判断目标设为角色实体。
   - 实际相机跟随目标设为 `_FocusProxy/_Inner`。

   相关代码：[HeroController.lua](/E:/project/game/asset/script/actor/HeroController.lua:921)、[HeroController.lua](/E:/project/game/asset/script/actor/HeroController.lua:3528)

3. `MainCameraController` 主要是对外接口层，把焦点、速度、偏移、缩放等操作转交给 `CameraFocusController`。

   相关代码：[MainCameraController.lua](/E:/project/game/asset/script/actor/MainCameraController.lua:161)

### 农场模式的关键差异

普通模式会根据角色朝向和移动状态，平滑移动 `_FocusProxy`，产生“镜头向前看”的效果：

```lua
currentDistance = lerp(currentDistance, targetDistance, ...)
x = lerp(x, x1 * 2, 0.04)
y = lerp(y, y1 * 1.5 + 10, 0.04)
```

见 [HeroController.lua](/E:/project/game/asset/script/actor/HeroController.lua:6590)。

但农场控制器重写了这个协程，每帧都把代理 Pivot 清零：

```lua
focusProxyInner:setPiv(0, 0, 0)
focusProxy:setPiv(0, 0, 0)
```

见 [HeroControllerFarm.lua](/E:/project/game/asset/script/actor/HeroControllerFarm.lua:2231)。

因此：

- 普通模式：跟随角色 + 朝移动方向前视。
- 农场模式：焦点基本保持在角色中心。
- 农场的“平滑感”主要由相机控制器完成，不由焦点代理完成。

### 平滑跟随公式

真正的跟随发生在 [CameraFocus.lua](/E:/project/game/asset/script/actor/CameraFocus.lua:898)。

每帧最终计算等价于：

```text
新位置 = 目标位置 × k + 当前位置 × (1 - k)
```

即指数插值：

```lua
x1 = x * k + x0 * (1 - k)
y1 = y * k + y0 * (1 - k)
```

跟随系数大致为：

```text
k = interpolation / outputZoom × trackingSpeed × 区域速度系数
```

农场主场景的典型参数是：

- 相机预制体 `interpolation = 0.1`
- `defaultZoom = normal`，所以 `outputZoom = 1`
- 默认相机速度是 `fast = 1.8`
- 农场焦点区域速度为 `normal = 1`

所以普通情况下：

```text
k = 0.1 / 1 × 1.8 × 1 = 0.18
```

即每帧消除约 18% 的位置误差。设置中的速度映射见 [EWGameSettings.lua](/E:/project/game/asset/script/EWGameSettings.lua:115)。

### 焦点区域与地图边界

农场场景还放置了一个 `MI_CMArea`，范围约为：

- 宽度：1601
- 高度：1255
- 模式：继承预制体的 `limit`

区域配置见 [Farm.scene](/E:/project/game/asset/farm/scene/Farm.scene/default.scene_group:5369)。

`CameraFocusController` 会：

- 使用角色本体判断当前焦点区域，而不是使用可能偏移的 `_FocusProxy`。
- 根据屏幕宽高计算相机中心允许移动的范围。
- 将目标位置限制在区域边界内，避免镜头看到地图外部。
- 用约 3 次更新的延迟切换区域，减少边界附近频繁跳变。

此外，焦点区域支持：

- `limit`：限制相机中心。
- `mapping`：把角色在内部区域的位置映射到完整相机区域。
- `veryslow/slow/normal/fast/instant`：修改局部跟随速度。

见 [CameraFocus.lua](/E:/project/game/asset/script/actor/CameraFocus.lua:50) 和 [CameraFocus.lua](/E:/project/game/asset/script/actor/CameraFocus.lua:198)。

### 其他平滑层

相机目标经过最终插值前，还会叠加：

- `CameraAttractor`：将镜头软拉向场景重点，吸引偏移本身按 `70% 旧值 + 30% 新值` 平滑。
- `offsetX/offsetY`：菜单、剧情或交互附加的镜头偏移。
- 世界 Z 高度：以 `y += z` 投影到画面位置。
- 对话人物：以 `0.1` 权重把镜头拉向说话者。
- X/Y 锁定以及单方向跟随。
- 焦点区域边界裁剪。

### `EWCamera` 的实际作用

[EWCamera.lua](/E:/project/game/asset/script/map/EWCamera.lua:729) 主要负责：

- 输出缩放。
- 渲染目标和后处理。
- 坐标转换。
- 半分辨率渲染下的亚像素补偿。

它不是角色跟随插值的主要实现。

当启用半分辨率时，[EWCamera.lua](/E:/project/game/asset/script/map/EWCamera.lua:1333) 会在渲染同步前根据相机位置的小数部分修正最终画面 Pivot，从而缓解低分辨率渲染产生的像素级跳动。名为 `actionSmoothOutputScroll()` 的协程实际上是空循环，真正逻辑位于 `preSyncRenderState()`。

### 特殊接管

农场系统还能临时改写这条链路：

- 椅子交互把焦点切到椅子的 `_Focus`，并临时修改跟随速度，站起后恢复：[FarmChair.lua](/E:/project/game/asset/farm/script/objects/FarmChair.lua:54)
- 烹饪菜单临时修改偏移、插值和缩放，关闭后恢复。
- `CameraFocusGrabber` 可在剧情中锁定其他焦点，结束后恢复原目标。
- `syncLoc()` 会让后续 10 次更新使用 `k = 1`，用于传送或 Warp 后立即同步镜头，避免镜头慢慢追过来。
- 农场矿洞小游戏的 `MineCameraController` 是另一套直接跟随逻辑，不属于主农场平滑相机。

### 值得注意的问题

- `CameraFocusController:onUpdate(dt)` 没有使用 `dt`，插值按“每次更新”计算。因此除非引擎保证固定更新频率，否则不同帧率下的实际跟随时间会不同。
- 农场版 `actionFocusProxyInterpolation()` 保留了不少未使用的局部变量，本质上只是在不断清零 Pivot。
- 焦点区域的 `prevFocusArea` 只在找到区域时更新，因此离开全部区域后可能继续沿用上一个区域边界；这可能是有意设计的“区域粘滞”行为。
- 区域速度系数在 `k` 被限制到 1 后才相乘，若未来同时使用较大的插值和 `instant` 区域，理论上可能得到 `k > 1` 并产生过冲。

本次仅进行了只读分析，没有修改任何文件。

# MessageCenter 专项指南：主线程模式与时间切片

`MessageCenter` 是一个基于字符串 Topic 的高性能事件/消息系统。本文档重点介绍其针对游戏引擎（如 Unity, Unreal, Cocos, 自研引擎）设计的 **主线程模式 (MainThread Mode)**。

## 1. 为什么需要主线程模式？

在游戏开发中，绝大多数操作（如 UI 更新、场景物体变换、渲染调用）都不是线程安全的，必须在**主线程**中执行。
- **Async 模式**：虽然不阻塞主线程，但回调在后台线程执行，直接操作 UI 会导致崩溃。
- **Sync 模式**：立即执行，逻辑简单但如果处理函数太重，会导致发布消息的当前帧卡顿。
- **MainThread 模式（默认）**：兼顾非阻塞和安全性。消息发布后存入队列，由主线程在每帧的固定时机统一处理。

## 2. 核心用法

### 2.1 订阅消息
系统会自动推导参数类型。

```cpp
#include "EventSystem/MessageCenter.h"

// 订阅一个 UI 更新消息
eventsystem::SubscribeMessage("update_score", [](int newScore) {
    // 这里的逻辑保证在主线程执行，可以安全操作 UI
    ScoreUI::SetText(std::to_string(newScore));
});
```

### 2.2 发布消息
由于默认模式已设置为 `MainThread`，直接发布即可。

```cpp
// 在任何线程（如网络线程、计算线程）发布
eventsystem::PublishMessage("update_score", 100); 
```

### 2.3 驱动更新（至关重要）
你必须在游戏引擎的每帧更新逻辑（如 `Update` 或 `Tick` 函数）中调用驱动接口。

```cpp
void MyGameApp::Update() {
    // 处理所有积压的主线程消息
    eventsystem::UpdateMessageCenter();
}
```

## 3. 高级特性

### 3.1 时间切片 (Time Slicing)
如果某一帧消息过多（例如瞬间爆发了 1000 条），全部处理完可能会导致明显的掉帧。你可以限制单帧的最大处理时间：

```cpp
// 限制 MessageCenter 每帧最多处理 2.0 毫秒的消息
eventsystem::SetMessageCenterMaxUpdateDuration(2.0);
```

**工作原理**：
1. `Update()` 开始执行。
2. 每执行完一个回调，检查当前已耗时。
3. 如果耗时超过 2.0ms，停止当前批次处理，并打印警告日志。
4. 剩余消息保留在队列中，下一帧 `Update()` 时继续处理。

### 3.2 零开销延迟线程 (Lazy Thread)
`MessageCenter` 内部包含一个用于处理 `Async` 模式的后台线程。
- **优化**：该线程是延迟创建的。
- **效果**：如果你只使用默认的 `MainThread` 模式或 `Sync` 模式，该线程**永远不会被创建**，不会占用任何系统资源。

## 4. 最佳实践建议

1.  **UI 交互**：始终使用默认模式（MainThread）进行 UI 相关的解耦。
2.  **性能监控**：在开发阶段关注日志。如果看到 `Warning: Update time limit exceeded`，说明消息负载过高，应考虑优化订阅者的执行效率或调大时间切片阈值。
3.  **显示指定**：如果你明确知道某个逻辑需要后台并行（如文件写入、大规模数据计算），请使用 `PublishMessageAsync`。
4.  **生命周期**：在模块或游戏退出时，记得调用 `eventsystem::DestroyMessageCenter()` 释放资源。

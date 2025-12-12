### **最终版 Prompt：设计并实现一个高级、多范式的 C++17 异步事件系统**

**核心目标：**
创建一个高性能、线程安全、支持多种编程范式、且 API 友好的 C++17 异步事件分发系统。系统需在便利性、灵活性和内存安全性上达到优秀的平衡。

**核心架构：`EventCenter`**
*   **单例模式：** 必须实现为线程安全的单例，通过静态 `instance()` 方法提供全局访问点。
*   **异步处理：** 内部必须拥有一个工作线程来异步分发所有事件。事件的发布（`postEvent`）必须是完全非阻塞的。
*   **懒加载：** 工作线程应在第一次有事件发布时才被“懒加载”创建，而非在 `EventCenter` 构造时。创建过程需保证线程安全。
*   **优雅关闭：** `EventCenter` 析构时必须能处理完事件队列中的剩余任务，并安全地关闭工作线程。

**API 设计要求：**

**1. 事件发布 API：**
*   提供一个全局模板函数 `publish_event<TEvent>(event)`，作为系统中发布事件的唯一推荐方式，实现便捷的“即发即忘”调用。

**2. 处理器注册 API (需支持以下所有范式):**

*   **A) 基于接口的处理器 (`IEventHandler`):**
    *   **强引用注册 (便利模式):** 提供 `registerHandler(std::shared_ptr<IEventHandler>)`。`EventCenter` 将共享处理器所有权，保证其存活直至被注销。此模式用于“即发即忘”式的类处理器注册。
    *   **弱引用注册 (安全模式):** 提供 `registerWeakHandler(std::shared_ptr<IEventHandler>)`。`EventCenter` 只观察处理器，不增加其引用计数。当处理器在外部被销毁时，它会自动失效，从而防止因忘记注销而导致的内存泄漏。

*   **B) 基于回调的处理器 (`std::function`):**
    *   提供 `registerHandler(std::function<void(const TEvent&)>)`。此方法直接接受 Lambda 表达式或函数作为处理器。
    *   必须返回一个 `SubscriptionHandle` (例如 `size_t`)，用于后续的精确注销。

*   **C) 基于静态处理器的事件 (极简模式):**
    *   支持一种约定：事件结构体内部可以定义一个公开的静态方法 `static void handle(const TEvent& event)`。
    *   提供一个全局辅助模板函数 `registerStaticEventHandler<TEvent>()`，它能自动将 `TEvent::handle` 注册为该事件的处理器，并返回一个 `SubscriptionHandle`。

**3. 处理器注销 API：**
*   `unregisterHandler(std::shared_ptr<IEventHandler>)`：注销一个指定的 `IEventHandler` 处理器（会同时从强、弱引用列表中移除）。
*   `unregisterHandler(SubscriptionHandle)`：通过句柄注销一个回调或静态处理器。
*   `unregisterAllHandlers<TEvent>()`：一次性注销所有（无论何种类型）监听特定 `TEvent` 事件的处理器。

**代码与交付物要求：**

1.  **`EventSystem.h`:**
    *   包含 `EventCenter`、`IEventHandler` 以及所有全局辅助函数的完整实现。
    *   所有代码、接口、注释必须使用清晰、专业的**英文**。
    .
2.  **`main.cpp`:**
    *   编写一个**功能完整的演示程序**，而非压力测试。
    *   程序必须分步、清晰地展示上述**所有**注册和注销范式的使用方法，包括：
        *   强引用 `registerHandler` 的“即发即忘”用法。
        *   弱引用 `registerWeakHandler` 在外部管理生命周期下的表现（手动销毁对象后事件不再被接收）。
        *   回调 `registerHandler` 的注册与按句柄注销。
        *   `registerStaticEventHandler` 的极简用法。
    *   通过控制台输出和注释，详细解释每一步操作的意图和结果。

3.  **`CMakeLists.txt`:**
    *   一个可以正确构建项目的 CMake 配置文件，需启用 C++17。

4.  **`build.bat`:**
    *   一个健壮的 Windows 批处理脚本，能自动化执行 `cmake配置` -> `编译` -> `运行` 的完整流程，并能正确处理错误。
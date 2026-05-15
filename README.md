# 线程安全型队列课程设计

这是一个基于 **C++17** 的操作系统课程设计项目，主题是“线程安全型队列的实现与并发测试”。

项目特点：

- 队列使用手写链表实现，不使用 `std::queue` 代替核心结构。
- 使用 `std::shared_mutex` 实现读者-写者同步。
- 写者函数：`InitQueue`、`EnQueue`、`DeQueue`、`Clear`
- 读者函数：`Find`、`Snapshot`、`Print`、`Size`、`Check`
- 包含自动化单元测试、并发集成测试、压力测试
- 包含“读者和读者并发”的证明测试

## 目录说明

- `include/`：头文件
- `src/`：核心源码和程序入口
- `tests/`：测试代码
- `docs/`：项目文档和报告提纲
- `cailiao/`：课程原始材料、模板和报告文档

更详细的项目说明见：

- `docs/project_overview.md`

## 编译与运行

在项目根目录打开 PowerShell，执行：

```powershell
cmake -S . -B build -G Ninja
cmake --build build
.\build\queue_tests.exe
```

如果你只想重新运行测试，而不重新编译：

```powershell
.\build\queue_tests.exe
```

## 运行结果说明

程序会自动运行全部测试，并输出如下风格的结果：

```text
[PASS] Initial state
[PASS] FIFO order and empty DeQueue
...
Reader concurrency maxActiveReaders = 8
[PASS] Reader-reader concurrency proof
Total passed: 11, failed: 0
```

其中：

- 每一项 `[PASS]` 表示该测试通过
- `Reader concurrency maxActiveReaders = N` 用来证明多个读者线程可以并发执行
- 最后一行给出总通过数和失败数

## 说明

- 构建目录 `build/` 属于编译中间产物，不属于源码内容，重新执行 CMake 命令后会自动生成。
- 如果环境变量已经配置好，`cmake`、`cl`、`ninja` 可以直接使用。
- 如果老师要求现场讲解，建议优先熟悉 `EnQueue`、`DeQueue`、`Find`、`Clear` 和测试判定逻辑。

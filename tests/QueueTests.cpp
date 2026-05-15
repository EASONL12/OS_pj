#include "QueueTests.h"

#include "ThreadSafeQueue.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

/*
 * TestRunner 是一个辅助工具，帮我自动跑测试、自动统计结果。
 * 每个测试函数只要返回 true 就表示通过，返回 false 就是失败。
 * 如果测试函数抛了异常，也会被捕获并记为失败，不会影响后面的测试继续跑。
 */
struct TestRunner {
    int passed = 0;
    int failed = 0;

    void Run(const std::string& name, const std::function<bool()>& test) {
        // 执行测试函数，同时用 try-catch 兜底，防止某个测试崩溃导致程序直接挂掉
        bool ok = false;
        try {
            ok = test();
        } catch (const std::exception& ex) {
            // 捕获到标准异常，打印异常信息
            std::cout << "[FAIL] " << name << " exception: " << ex.what() << '\n';
            failed++;
            return;
        } catch (...) {
            // 捕获所有其他未知异常（比如并发场景下的系统异常）
            std::cout << "[FAIL] " << name << " unknown exception\n";
            failed++;
            return;
        }

        // 根据返回值判断通过还是失败
        if (ok) {
            std::cout << "[PASS] " << name << '\n';
            passed++;
        } else {
            std::cout << "[FAIL] " << name << '\n';
            failed++;
        }
    }
};

//
// ======================== 第一部分：单元测试 ========================
// 下面 6 个测试各自只测一个函数，如果哪个失败了，马上就能知道是哪个函数出问题。
//

/*
 * 测试1：检查一个全新创建的队列是不是空队列。
 * 空队列必须同时满足三个条件：
 *   - Size() 返回 0          （队列里没有结点）
 *   - Snapshot() 返回空列表  （实际遍历链表也是空的）
 *   - Check() 返回 true      （队列的 front/rear/count 三者一致，没有矛盾）
 * 三个条件同时检查，比只检查一个更可靠。
 */
bool TestInitialState() {
    // 创建一个空队列（什么都不放，直接检查）
    ThreadSafeQueue queue;

    // 三个条件用 && 连起来：全部满足才返回 true
    return queue.Size() == 0 && queue.Snapshot().empty() && queue.Check();
}

/*
 * 测试2：验证队列的"先进先出"（FIFO）顺序是否正确。
 * 思路：先按顺序放入 1,2,3,4,5，再逐个取出来，取出来的顺序必须也是 1,2,3,4,5。
 * 最后还要试一下：队列已经空了，再调用 DeQueue，应该返回 false。
 */
bool TestFifoOrder() {
    ThreadSafeQueue queue;

    // 第一步：依次插入 1 到 5
    for (int i = 1; i <= 5; ++i) {
        queue.EnQueue(i);
    }

    // 第二步：依次取出，每次取出的值必须和期望值一致
    for (int i = 1; i <= 5; ++i) {
        int value = 0;
        if (!queue.DeQueue(value) || value != i) {
            return false;  // 要么没取到（队列提前空了），要么值对不上（顺序乱了）
        }
    }

    // 第三步：队列应该已经空了，再 DeQueue 应该失败
    int ignored = 0;
    return !queue.DeQueue(ignored) && queue.Size() == 0 && queue.Check();
}

/*
 * 测试3：验证 Find 函数的查找功能。
 * 往队列里放 10、20、30 三个数，然后：
 *   - 查 10、20、30 都应该返回 true（确实在队列里）
 *   - 查 40 应该返回 false（根本不在队列里）
 * 两种结果都要验证：如果只测"找得到"的情况，发现不了"什么都返回 true"的 bug。
 */
bool TestFind() {
    ThreadSafeQueue queue;

    // 放入三个测试数据
    queue.EnQueue(10);
    queue.EnQueue(20);
    queue.EnQueue(30);

    // 三个"应该找到" + 一个"应该找不到" + 结构检查
    return queue.Find(10) && queue.Find(20) && queue.Find(30) &&
           !queue.Find(40) && queue.Check();
}

/*
 * 测试4：验证 Print 函数的输出内容是否正确。
 * Print 会输出到屏幕（std::cout），这里用一个技巧：
 * 把 cout 的输出重定向到一个 stringstream 里，这样就能在代码里检查输出内容了。
 * 验证标准：输出里必须包含 "Queue:" 这个前缀，以及 "11 22 33" 这个顺序。
 */
bool TestPrintOutput() {
    ThreadSafeQueue queue;

    // 放入三个数
    queue.EnQueue(11);
    queue.EnQueue(22);
    queue.EnQueue(33);

    // 关键步骤：把 cout 的输出"劫持"到 stringstream 里
    std::ostringstream captured;
    std::streambuf* oldBuffer = std::cout.rdbuf(captured.rdbuf());
    queue.Print();                         // 正常调用 Print
    std::cout.rdbuf(oldBuffer);            // 恢复 cout，后续测试不受影响

    // 检查捕获到的输出字符串
    std::string output = captured.str();
    return output.find("Queue:") != std::string::npos &&   // 有前缀 "Queue:"
           output.find("11 22 33") != std::string::npos &&  // 有 "11 22 33"
           queue.Check();
}

/*
 * 测试5：验证 Clear 函数的三个场景。
 * 场景A：清空一个有数据的队列 → 大小应该变 0
 * 场景B：对已经清空的队列再 Clear 一次 → 不应该崩溃（边界情况）
 * 场景C：清空后重新插入数据 → 队列应该还能正常使用（Clear 不能破坏队列结构）
 * 场景C 特别重要，因为如果 Clear 把 front/rear 搞乱了，后续入队就会出错。
 */
bool TestClear() {
    ThreadSafeQueue queue;

    // 先往队列里放 100 个数据（0 到 99）
    for (int i = 0; i < 100; ++i) {
        queue.EnQueue(i);
    }

    // 场景A：清空后检查是否真的空了
    queue.Clear();
    bool firstClearOk = queue.Size() == 0 && queue.Snapshot().empty() && queue.Check();

    // 场景B：再 Clear 一次空队列，检查会不会崩溃
    queue.Clear();
    bool secondClearOk = queue.Size() == 0 && queue.Snapshot().empty() && queue.Check();

    // 场景C：清空后重新插入 123，然后取出来，看是否正常
    queue.EnQueue(123);
    int value = 0;
    bool reusable = queue.DeQueue(value) && value == 123 && queue.Size() == 0;

    // 三个场景全部通过才算通过
    return firstClearOk && secondClearOk && reusable && queue.Check();
}

/*
 * 测试6：验证 InitQueue 函数。
 * InitQueue 和 Clear 做的事情很像（都是清空队列），但它们的"身份"不同：
 * InitQueue 是"初始化"，Clear 是"清空"。
 * 这里往队列放 50 个数据，然后调用 InitQueue 重置，验证重置后队列可用。
 */
bool TestInitQueue() {
    ThreadSafeQueue queue;

    // 先放 50 个数据
    for (int i = 0; i < 50; ++i) {
        queue.EnQueue(i);
    }

    // 调用 InitQueue 重置，检查是否回到了空状态
    queue.InitQueue();
    bool resetOk = queue.Size() == 0 && queue.Snapshot().empty() && queue.Check();

    // 重置后插入一个数 7，再取出来，验证队列仍然正常
    queue.EnQueue(7);
    int value = 0;
    return resetOk && queue.DeQueue(value) && value == 7 && queue.Check();
}

//
// ======================== 第二部分：集成测试 ========================
// 核心思想：启动多个线程，每个线程执行上万次操作，最后用"总数守恒"来自动判断对错。
// 因为上万次操作不可能用眼睛看输出来验证，所以必须依赖数据统计。
//

/*
 * 集成测试1：多个线程同时往队列里插数据（多生产者模型）。
 * 启动 8 个线程，每个线程插入 10000 次，预期最终队列里有 80000 个结点。
 *
 * 为了让不同的线程产生的数不会重复，我用了一个技巧：
 *   线程 0 写入的范围是 0 ~ 9999
 *   线程 1 写入的范围是 1000000 ~ 1009999
 *   线程 2 写入的范围是 2000000 ~ 2009999 ... 依此类推
 * 这样每个线程的"号码段"互不重叠，方便最后排序比对。
 *
 * 验证分两步：
 *   第1步：看队列的 Size() 是不是等于 80000（数量对不对）
 *   第2步：把队列内容全部取出来，排序后逐项检查（有没有丢数据或重复插入）
 */
bool TestMultiProducer() {
    // 8 个线程，每个线程执行 10000 次入队，总共预期 80000 个
    constexpr int threadCount = 8;
    constexpr int operationsPerThread = 10000;
    constexpr int expectedTotal = threadCount * operationsPerThread;

    ThreadSafeQueue queue;
    std::vector<std::thread> threads;

    // 启动 8 个线程，每个线程执行 10000 次 EnQueue
    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([&queue, t, operationsPerThread]() {
            for (int i = 0; i < operationsPerThread; ++i) {
                // 每个线程用自己的编号 t 乘以 1000000 作为起始值
                // 这样不同线程的值范围不会重叠
                queue.EnQueue(t * 1000000 + i);
            }
        });
    }

    // 等待所有线程完成
    for (std::thread& thread : threads) {
        thread.join();
    }

    // 第1步：检查总数量
    if (queue.Size() != expectedTotal || !queue.Check()) {
        return false;
    }

    // 第2步：把队列内容拷贝出来，排序后逐项比对
    std::vector<int> values = queue.Snapshot();  // 拷贝一份（读操作）
    std::sort(values.begin(), values.end());     // 排序

    if (static_cast<int>(values.size()) != expectedTotal) {
        return false;
    }

    // 逐项检查排序后的结果：第0项应该是0，第9999项应该是9999，
    // 第10000项应该是1000000，依此类推
    for (int t = 0; t < threadCount; ++t) {
        for (int i = 0; i < operationsPerThread; ++i) {
            int expectedValue = t * 1000000 + i;        // 期望值
            int index = t * operationsPerThread + i;    // 在排序数组中的位置
            if (values[index] != expectedValue) {
                return false;  // 数据丢失或重复
            }
        }
    }

    return true;
}

/*
 * 集成测试2：多个线程同时从队列里取数据（多消费者模型）。
 *
 * 设计思路：
 *   1. 主线程先往队列里放入 80000 个数（0 到 79999），这一步没有并发
 *   2. 启动 8 个线程同时从队列取数据，每个线程把自己取到的数记在各自的列表里
 *   3. 等所有线程结束后，把 8 个列表合并、排序
 *   4. 排序后应该是 0,1,2,...,79999，一个不少、一个不重
 *
 * 这个测试专门检验 DeQueue 在多线程竞争下的正确性。
 * 因为入队阶段是单线程的，所以如果出错了，问题一定出在 DeQueue 上。
 */
bool TestMultiConsumer() {
    constexpr int totalValues = 80000;   // 总共插入 80000 个数据
    constexpr int consumerCount = 8;     // 8 个消费者线程

    ThreadSafeQueue queue;

    // 主线程先一次性插入 80000 个数据（单线程操作，不存在并发问题）
    for (int i = 0; i < totalValues; ++i) {
        queue.EnQueue(i);
    }

    // consumedCount: 用原子变量记录总共取出了多少个（多线程安全计数）
    // threadResults: 每个线程一个独立的列表，记录自己取到了哪些数
    std::atomic<int> consumedCount{0};
    std::vector<std::vector<int>> threadResults(consumerCount);
    std::vector<std::thread> threads;

    // 启动 8 个消费者线程
    for (int t = 0; t < consumerCount; ++t) {
        threads.emplace_back([&queue, &consumedCount, &threadResults, t]() {
            int value = 0;
            // 不停尝试 DeQueue，直到队列为空
            while (queue.DeQueue(value)) {
                threadResults[t].push_back(value);     // 记录自己取到的数
                consumedCount.fetch_add(1);             // 原子地 +1
            }
        });
    }

    // 等待所有消费者线程完成
    for (std::thread& thread : threads) {
        thread.join();
    }

    // 检查一：原子计数器是否等于预期总数
    if (consumedCount.load() != totalValues || queue.Size() != 0 || !queue.Check()) {
        return false;
    }

    // 检查二：把 8 个线程的记录合并到一起
    std::vector<int> values;
    values.reserve(totalValues);
    for (const std::vector<int>& part : threadResults) {
        values.insert(values.end(), part.begin(), part.end());
    }

    // 排序后应该是 0,1,2,...,79999
    std::sort(values.begin(), values.end());
    if (static_cast<int>(values.size()) != totalValues) {
        return false;
    }

    for (int i = 0; i < totalValues; ++i) {
        if (values[i] != i) {
            return false;  // 丢数据或重复取了
        }
    }

    return true;
}

/*
 * 集成测试3：生产者、消费者、读者 三种角色同时运行（混合并发）。
 * 这是最接近实际使用场景的测试。
 *
 * 角色分配：
 *   - 4 个生产者：不停 EnQueue
 *   - 4 个消费者：不停 DeQueue
 *   - 2 个读者：  不停 Find + Check（只读不写）
 *
 * 线程的退出时机需要协调，否则消费者可能提前退出导致数据没取完：
 *   - 主线程等所有"生产者"结束后，设置 producersDone = true
 *   - "消费者"看到 producersDone==true 且队列空了，才安全退出
 *   - "读者"通过单独的信号 readersDone 来控制退出
 *
 * 验证标准："入队总量 = 出队总量 + 还在队列里的数量"
 * 这其实就是"生产 = 消费 + 库存"，简单可靠。
 */
bool TestProducerConsumerMixed() {
    constexpr int producerCount = 4;
    constexpr int consumerCount = 4;
    constexpr int readerCount = 2;
    constexpr int operationsPerProducer = 20000;  // 每个生产者执行 20000 次
    constexpr int expectedProduced = producerCount * operationsPerProducer;

    ThreadSafeQueue queue;

    // 原子计数器：记录入队次数、出队次数
    std::atomic<int> producedCount{0};
    std::atomic<int> consumedCount{0};
    // 原子标志：控制线程退出时机
    std::atomic<bool> producersDone{false};
    std::atomic<bool> readersDone{false};

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::vector<std::thread> readers;

    // ===== 启动生产者 =====
    for (int p = 0; p < producerCount; ++p) {
        producers.emplace_back([&queue, &producedCount, p, operationsPerProducer]() {
            for (int i = 0; i < operationsPerProducer; ++i) {
                queue.EnQueue(p * 1000000 + i);
                producedCount.fetch_add(1);  // 入队成功 +1
            }
        });
    }

    // ===== 启动消费者 =====
    // 退出条件：生产者已全部结束 且 队列已空
    for (int c = 0; c < consumerCount; ++c) {
        consumers.emplace_back([&queue, &consumedCount, &producersDone]() {
            int value = 0;
            while (!producersDone.load() || queue.Size() > 0) {
                if (queue.DeQueue(value)) {
                    consumedCount.fetch_add(1);  // 出队成功 +1
                } else {
                    std::this_thread::yield();   // 队列暂时空，让出 CPU
                }
            }
        });
    }

    // ===== 启动读者 =====
    // 读者不关心数据具体是什么，只管不断查找和检查结构
    for (int r = 0; r < readerCount; ++r) {
        readers.emplace_back([&queue, &readersDone, r]() {
            int target = r * 1000000;
            while (!readersDone.load()) {
                queue.Find(target);   // 查找某个数
                queue.Check();        // 检查队列结构完整性
                std::this_thread::yield();
            }
        });
    }

    // ===== 等待所有线程结束 =====
    // 先等生产者全部结束
    for (std::thread& thread : producers) {
        thread.join();
    }
    producersDone.store(true);  // 通知消费者：生产者已全部结束

    // 等消费者全部结束
    for (std::thread& thread : consumers) {
        thread.join();
    }

    // 通知读者退出
    readersDone.store(true);
    for (std::thread& thread : readers) {
        thread.join();
    }

    // ===== 验证结果 =====
    // 还在队列里的数量 = 生产总量 - 消费总量
    int remaining = queue.Size();
    return producedCount.load() == expectedProduced &&                         // 生产数量对
           producedCount.load() == consumedCount.load() + remaining &&         // 守恒：生产=消费+剩余
           queue.Check();                                                      // 结构完整
}

/*
 * 压力测试：12 个线程 × 50000 次 = 总共 60 万次操作。
 * 每个线程不是固定做一件事，而是随机决定每次做什么：
 *   - 40% 概率入队    （模拟写的负载）
 *   - 30% 概率出队    （模拟读+删除的负载）
 *   - 25% 概率查找    （模拟纯读的负载）
 *   - 5%  概率快照    （模拟批量读的负载）
 *
 * 为什么用随机而不是固定模式？
 * 因为固定模式下每次测试的线程调度顺序都一样，可能刚好"碰巧"不触发 bug。
 * 而随机模式下，入队和出队的交错方式每次都不一样，更容易暴露出隐藏的问题。
 *
 * 验证方式同样是：入队次数 == 出队次数 + 队列剩余数量。
 */
bool TestStressRandomOperations() {
    constexpr int threadCount = 12;
    constexpr int operationsPerThread = 50000;  // 每个线程 5 万次操作

    ThreadSafeQueue queue;
    std::atomic<int> enqueued{0};   // 记录总共入队了多少次
    std::atomic<int> dequeued{0};   // 记录总共出队了多少次
    std::vector<std::thread> threads;

    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([&queue, &enqueued, &dequeued, t, operationsPerThread]() {
            // 每个线程有自己的随机数生成器（种子不同，保证随机序列不同）
            std::mt19937 rng(1234 + t);
            std::uniform_int_distribution<int> operationDist(1, 100);   // 1~100 随机，决定做什么操作
            std::uniform_int_distribution<int> valueDist(0, 1000000);   // 0~1000000 随机，决定查什么值

            for (int i = 0; i < operationsPerThread; ++i) {
                int operation = operationDist(rng);  // 随机决定本次做什么

                if (operation <= 40) {
                    // 40% 概率：入队
                    queue.EnQueue(t * 1000000 + i);
                    enqueued.fetch_add(1);
                } else if (operation <= 70) {
                    // 30% 概率：出队
                    int value = 0;
                    if (queue.DeQueue(value)) {
                        dequeued.fetch_add(1);
                    }
                } else if (operation <= 95) {
                    // 25% 概率：查找
                    queue.Find(valueDist(rng));
                } else {
                    // 5% 概率：快照
                    queue.Snapshot();
                }
            }
        });
    }

    // 等待所有线程结束
    for (std::thread& thread : threads) {
        thread.join();
    }

    // 验证：入队次数 = 出队次数 + 还在队列里的数量
    int remaining = queue.Size();
    return enqueued.load() == dequeued.load() + remaining && queue.Check();
}

//
// ======================== 第三部分：读者并发证明测试 ========================
// 课设要求"允许多个线程同时执行查找操作"，这个测试就是用数据证明做到了这一点。
//
// 证明思路：
//   1. 启动 8 个读者线程
//   2. 每个线程在持有"读锁"的时候，用一个原子计数器 +1，记录此刻有多少个读者
//   3. 等所有线程结束，看这个计数器曾经达到的最大值是多少
//   4. 如果最大值 > 1，就说明至少有两个读者曾经同时进入了读区间
//      → "读者-读者"并发访问成立
//
// 打个比方：8 个人同时进图书馆的阅览室（读者），安保系统记录"此刻阅览室里有几个人"，
// 如果记录到的最大值是 8，那说明这 8 个人确实可以同时在里面。
// 如果每次只能进一个人（最大值始终是 1），那说明阅览室实际上只能单人使用。
//
bool TestReaderConcurrency() {
    constexpr int readerCount = 8;       // 启动 8 个读者线程
    constexpr int loopsPerReader = 20;   // 每个线程反复执行 20 次

    ThreadSafeQueue queue;

    // 先在队列里放一些数据，保证读者有东西可查
    for (int i = 0; i < 2000; ++i) {
        queue.EnQueue(i);
    }

    // activeReaders：当前有多少个读者正持有读锁
    // maxActiveReaders：曾经达到的最大读者数量
    std::atomic<int> activeReaders{0};
    std::atomic<int> maxActiveReaders{0};
    std::vector<std::thread> readers;

    for (int r = 0; r < readerCount; ++r) {
        readers.emplace_back([&queue, &activeReaders, &maxActiveReaders, r, loopsPerReader]() {
            for (int i = 0; i < loopsPerReader; ++i) {
                // 先做一次普通的 Find（读锁内）
                queue.Find(r * 10);

                // 再调用测试辅助函数：在持有读锁期间记录并发数量
                // 参数 2 表示持锁后停留 2 毫秒，给其他读者也进入留出时间窗口
                queue.TestHoldReadLock(2, activeReaders, maxActiveReaders);
            }
        });
    }

    // 等待所有读者结束
    for (std::thread& thread : readers) {
        thread.join();
    }

    // 打印统计到的最大并发读者数
    std::cout << "Reader concurrency maxActiveReaders = "
              << maxActiveReaders.load() << '\n';

    // 如果 maxActiveReaders > 1，说明确实支持多读者并发
    return maxActiveReaders.load() > 1 && queue.Check();
}

} // namespace

/*
 * RunAllQueueTests: 所有测试的统一入口函数。
 * 执行顺序是精心安排的：
 *   先跑 6 个单元测试（基础功能）  → 如果这里就挂了，后面的不用跑也知道
 *   再跑 4 个集成测试（并发场景）  → 确认多线程下正确
 *   最后跑 1 个证明测试（读者并发） → 用数据证明读者-读者可并发
 *
 * 这种"由简到繁"的顺序，方便快速定位问题。
 */
int RunAllQueueTests() {
    TestRunner runner;

    // ---- 单元测试（基础功能） ----
    runner.Run("Initial state", TestInitialState);
    runner.Run("FIFO order and empty DeQueue", TestFifoOrder);
    runner.Run("Find existing and missing values", TestFind);
    runner.Run("Print output", TestPrintOutput);
    runner.Run("Clear and reuse", TestClear);
    runner.Run("InitQueue reset", TestInitQueue);

    // ---- 集成测试（并发场景） ----
    runner.Run("Multi-producer integration", TestMultiProducer);
    runner.Run("Multi-consumer integration", TestMultiConsumer);
    runner.Run("Producer-consumer mixed integration", TestProducerConsumerMixed);

    // ---- 压力测试（大规模随机） ----
    runner.Run("Random stress test", TestStressRandomOperations);

    // ---- 读者并发证明 ----
    runner.Run("Reader-reader concurrency proof", TestReaderConcurrency);

    // 输出最终统计
    std::cout << "Total passed: " << runner.passed
              << ", failed: " << runner.failed << '\n';

    // 全部通过返回 0，有任何失败返回 1
    return runner.failed == 0 ? 0 : 1;
}

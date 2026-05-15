#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <shared_mutex>
#include <atomic>
#include <vector>

// ThreadSafeQueue 是为操作系统课程设计手工实现的链式队列。
// 本实现采用“读者-写者”模型保护临界资源：
// - 写操作使用 unique_lock，执行期间独占队列；
// - 读操作使用 shared_lock，多个读线程可以并发执行。
class ThreadSafeQueue {
public:
    ThreadSafeQueue();
    ~ThreadSafeQueue();

    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    // 将队列重置为空队列。
    // 这是写操作，需要独占访问队列。
    void InitQueue();

    // 在队尾插入一个新元素。
    // 这是写操作，需要独占访问队列。
    void EnQueue(int value);

    // 删除队头元素，并通过引用参数返回该元素的值。
    // 当队列为空时返回 false。
    // 这是写操作，需要独占访问队列。
    bool DeQueue(int& value);

    // 删除队列中的全部结点，使队列恢复为空状态。
    // 这是写操作，需要独占访问队列。
    void Clear();

    // 查找指定元素是否存在于队列中，不修改队列内容。
    // 当前实现返回 bool，表示“找到/未找到”；
    // 不返回结点指针，避免读锁释放后出现悬空指针问题。
    // 这是读操作，可以与其他读操作并发执行。
    bool Find(int value) const;

    // 按先进先出的顺序返回当前队列内容的一个副本。
    // 这是读操作，可以与其他读操作并发执行。
    std::vector<int> Snapshot() const;

    // 打印当前队列中的所有元素。
    // 实现时先在读锁保护下获取快照，再输出快照内容。
    void Print() const;

    // 返回当前队列中的结点个数。
    // 这是读操作，可以与其他读操作并发执行。
    int Size() const;

    // 检查队列关键不变量是否成立，例如：
    // - count 是否与实际结点数一致；
    // - front 和 rear 是否与空/非空状态匹配；
    // - rear->next 是否保持为 nullptr。
    // 这是供自动化测试使用的读操作。
    bool Check() const;

    // 这是一个测试辅助函数，用于验证“读者-读者”可以并发执行。
    // 函数在持有共享读锁期间更新计数器并短暂等待，
    // 以便测试程序统计同时处于读区间的线程数量。
    void TestHoldReadLock(int milliseconds,
                          std::atomic<int>& activeReaders,
                          std::atomic<int>& maxActiveReaders) const;

private:
    // 结点结构刻意保持简单，便于展示链式队列的基本组织方式，
    // 也便于课程设计答辩时说明各个操作的实现过程。
    struct Node {
        int data;
        Node* next;
    };

    // front 指向队头结点，rear 指向队尾结点。
    // 当队列为空时，必须满足：
    // front == nullptr，rear == nullptr，count == 0。
    Node* front;
    Node* rear;
    int count;

    // shared_mutex 用于实现课程要求的“读者-写者”并发控制模型。
    mutable std::shared_mutex rwlock;

    // 在调用者已经持有写锁的前提下，删除所有结点。
    void ClearWithoutLock();
};

#endif

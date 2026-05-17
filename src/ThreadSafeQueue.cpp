#include "ThreadSafeQueue.h"

#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

// 构造函数负责建立一个合法的空队列初始状态。
// 此时没有任何结点，因此 front 和 rear 都为空，count 为 0。
ThreadSafeQueue::ThreadSafeQueue() : front(nullptr), rear(nullptr), count(0) {}

// 析构时统一调用 Clear() 释放全部结点。
// 这样可以复用已有清理逻辑，避免析构函数与清空函数各写一套删除代码。
ThreadSafeQueue::~ThreadSafeQueue() {
    Clear();
}

// InitQueue 的作用是把当前队列重新置为空状态。
// 无论队列里原来有没有数据，执行结束后都应满足空队列不变量。
void ThreadSafeQueue::InitQueue() {
    // InitQueue 会修改整个队列结构，因此必须持有独占写锁。
    std::unique_lock<std::shared_mutex> lock(rwlock);
    // 复用统一的清理逻辑，确保重置后重新回到空队列状态。
    ClearWithoutLock();
}

// EnQueue 在队尾插入一个新结点。
// 这是典型写操作，因为它会修改 rear、可能修改 front，并更新 count。
void ThreadSafeQueue::EnQueue(int value) {
    // 写线程进入临界区前先获取独占锁，防止与其他读写线程并发修改队列。
    std::unique_lock<std::shared_mutex> lock(rwlock);

    
    // 动态创建新结点，并先把 next 置空。
    // 作为新的队尾结点，它后面暂时不应再连接任何结点。
    Node* node = new Node;
    node->data = value;
    node->next = nullptr;

    // 空队列插入首个结点后，队头和队尾都应指向该结点。
    if (rear == nullptr) {
        front = node;
        rear = node;
    } else {
        rear->next = node;
        rear = node;
    }

    count++;
}

// DeQueue 删除队头结点，并把被删除结点的数据写入引用参数 value。
// 如果队列为空，则本次删除失败，返回 false。
bool ThreadSafeQueue::DeQueue(int& value) {
    // 删除结点会修改链表结构，因此必须持有独占写锁。
    std::unique_lock<std::shared_mutex> lock(rwlock);


    // 空队列没有可删除结点，直接返回失败。
    if (front == nullptr) {
        return false;
    }

    // 先保存原队头指针，后续既要读取它的数据，也要释放它的内存。
    Node* oldFront = front;
    value = oldFront->data;

    // 队头后移到下一个结点，表示原队头已经从逻辑上出队。
    front = front->next;

    // 删除最后一个结点后，rear 也必须同步置为空。
    if (front == nullptr) {
        rear = nullptr;
    }

    // 原队头已经不再属于队列，必须及时释放，避免内存泄漏。
    delete oldFront;
    count--;
    return true;
}

// Clear 删除当前队列中的全部结点。
// 由于会重置整个链表结构，因此属于写操作。
void ThreadSafeQueue::Clear() {
    // 清空过程不能与任何其他读写操作并发执行。
    std::unique_lock<std::shared_mutex> lock(rwlock);
    ClearWithoutLock();
}

// Find 用于判断指定元素是否存在。
// 当前实现只返回“找到/未找到”，不向外暴露结点指针。
bool ThreadSafeQueue::Find(int value) const {
    // 查找不修改队列，因此只需要共享读锁。
    // 多个读线程可以同时执行这里的遍历逻辑。
    std::shared_lock<std::shared_mutex> lock(rwlock);

    Node* current = front;
    while (current != nullptr) {
        if (current->data == value) {
            return true;
        }
        current = current->next;
    }

    return false;
}

// Snapshot 在读锁保护下复制一份当前队列内容。
// 返回的是副本而不是原始结点，因此调用者离开函数后也不会依赖内部链表。
std::vector<int> ThreadSafeQueue::Snapshot() const {
    std::shared_lock<std::shared_mutex> lock(rwlock);

    std::vector<int> values;

    // 提前按 count 预留空间，减少 push_back 过程中扩容的次数。
    values.reserve(count > 0 ? static_cast<std::size_t>(count) : 0);

    Node* current = front;
    while (current != nullptr) {
        values.push_back(current->data);
        current = current->next;
    }

    return values;
}

// Print 打印当前队列中的所有元素。
// 它不直接在持锁状态下逐个输出，而是先快照、后打印。
void ThreadSafeQueue::Print() const {
    // 只在复制数据时持有读锁。
    // 后续打印基于快照进行，避免控制台输出阶段长时间占用队列锁。
    std::vector<int> values = Snapshot();

    std::cout << "Queue: ";
    for (int value : values) {
        std::cout << value << ' ';
    }
    std::cout << '\n';
}

// Size 返回当前结点数量。
// 虽然只是读取一个整数，但 count 仍然属于共享状态，因此也要加读锁。
int ThreadSafeQueue::Size() const {
    std::shared_lock<std::shared_mutex> lock(rwlock);
    return count;
}

// Check 是一个供测试使用的结构自检函数。
// 它不关心业务值是否“合理”，只关心队列内部链表结构是否保持一致。
bool ThreadSafeQueue::Check() const {
    std::shared_lock<std::shared_mutex> lock(rwlock);

    // 结点数不允许出现负值，这是最基本的非法状态。
    if (count < 0) {
        return false;
    }

    // 当 count 为 0 时，front 和 rear 都必须为空。
    if (count == 0) {
        return front == nullptr && rear == nullptr;
    }

    // 非空队列中，front 和 rear 不允许为空；
    // 同时 rear 作为队尾结点，它的 next 必须为空。
    if (front == nullptr || rear == nullptr || rear->next != nullptr) {
        return false;
    }

    // 遍历整个链表，重新统计结点数，并确认最后一个结点确实是 rear。
    int actualCount = 0;
    Node* current = front;
    Node* last = nullptr;
    while (current != nullptr) {
        actualCount++;
        last = current;
        current = current->next;
    }

    return actualCount == count && last == rear;
}

// TestHoldReadLock 是“读者并发证明测试”的辅助函数。
// 它会在持有共享读锁期间统计并发读者数量，并短暂停留，便于制造读者重叠区间。
void ThreadSafeQueue::TestHoldReadLock(int milliseconds,
                                       std::atomic<int>& activeReaders,
                                       std::atomic<int>& maxActiveReaders) const {
    std::shared_lock<std::shared_mutex> lock(rwlock);

    // 进入读临界区的线程数加 1，now 表示当前并发读者数量。
    int now = activeReaders.fetch_add(1, std::memory_order_acq_rel) + 1;

    // 使用 CAS 循环刷新历史最大并发读者数。
    // 如果其他线程已经写入了更大的值，compare_exchange_weak 会失败，
    // 此时 oldMax 会被更新为最新值，循环继续比较即可。
    int oldMax = maxActiveReaders.load(std::memory_order_relaxed);
    while (now > oldMax &&
           !maxActiveReaders.compare_exchange_weak(oldMax, now,
                                                   std::memory_order_relaxed)) {
    }

    // 在持有共享读锁时短暂休眠，给其他读线程制造并发进入读区间的机会。
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));

    activeReaders.fetch_sub(1, std::memory_order_acq_rel);
}

// ClearWithoutLock 假定调用者已经持有写锁。
// 它只负责“删除结点并恢复空队列状态”，不再重复处理加锁问题。
void ThreadSafeQueue::ClearWithoutLock() {
    // 逐个释放链表结点，避免内存泄漏。
    Node* current = front;
    while (current != nullptr) {
        Node* next = current->next;
        delete current;
        current = next;
    }

    // 清理结束后，统一恢复为空队列的不变量。
    front = nullptr;
    rear = nullptr;
    count = 0;
}

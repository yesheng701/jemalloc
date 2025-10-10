#include <atomic>
#include <csignal>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

const size_t NUM_THREADS = []() {
    const char* env_var = std::getenv("THREAD_CNT");
    if (env_var) {
        size_t num_threads = std::stoull(env_var);
        return num_threads;
    }
    return static_cast<size_t>(8);
}();

const size_t TARGET_MEMORY_PER_THREAD = []() {
    const char* env_var = std::getenv("MAX_MEMORY");
    if (env_var) {
        size_t total_memory = std::stoull(env_var) * 1024 * 1024; // Convert MB to bytes
        return total_memory / NUM_THREADS;
    }
    return static_cast<size_t>(256 * 1024 * 1024); // 默认每个线程 256MB
}();

const size_t TOTAL_TARGET_MEMORY      = NUM_THREADS * TARGET_MEMORY_PER_THREAD;  // ~2GB
const size_t MAX_BLOCK_SIZE           = 64 * 1024;                               // 最大块 64KB (避免过多mmap)
const size_t MIN_BLOCK_SIZE           = 32;                                      // 最小块 32B
const size_t POINTER_VECTOR_SIZE      = 10000;                                   // 每个线程存储指针的向量大小

// 全局统计
std::atomic<size_t> total_allocated_bytes{0};
std::atomic<size_t> total_deallocated_bytes{0};
std::atomic<size_t> allocation_attempts{0};
std::atomic<size_t> allocation_failures{0};
std::mutex print_mutex;

class TestObject {
 public:
  int id;
  double value;
  char padding[64];

  TestObject(int i) : id(i), value(i * 3.14) {
    std::memset(padding, 0xAA, sizeof(padding));
  }
};

// 线程工作函数
void thread_work(int thread_id) {
  // 线程局部随机数生成器
  std::random_device rd;
  std::mt19937 gen(rd() ^ (thread_id << 16));
  std::uniform_int_distribution<size_t> size_dist(MIN_BLOCK_SIZE, MAX_BLOCK_SIZE);
  std::uniform_int_distribution<int> obj_id_dist(1, 1000);
  std::bernoulli_distribution alloc_type_dist(0.4);  // 40% 概率选择 C++ 分配 (new/make_shared), 60% malloc
  std::bernoulli_distribution smart_ptr_dist(0.5);   // 在C++分配中，50%用new, 50%用make_shared
  std::bernoulli_distribution dealloc_dist(0.3);     // 30% 概率在分配后立即释放旧指针

  // 存储分配的指针 (混合类型)
  std::vector<void*> raw_ptrs;
  std::vector<std::unique_ptr<TestObject>> unique_objs;
  std::vector<std::shared_ptr<TestObject>> shared_objs;

  raw_ptrs.reserve(POINTER_VECTOR_SIZE);
  unique_objs.reserve(POINTER_VECTOR_SIZE / 3);
  shared_objs.reserve(POINTER_VECTOR_SIZE / 3);

  size_t allocated_this_thread = 0;
  auto start_time = std::chrono::steady_clock::now();

  try {
    while (allocated_this_thread < TARGET_MEMORY_PER_THREAD) {
      allocation_attempts++;

      void* ptr = nullptr;
      size_t alloc_size = 0;
      bool use_cpp = alloc_type_dist(gen);

      try {
        if (use_cpp) {
          // C++ 分配
          if (smart_ptr_dist(gen)) {
            // 使用 make_shared
            auto shared_obj = std::make_shared<TestObject>(obj_id_dist(gen));
            if (shared_obj) {
              // 存储 shared_ptr (会管理内存)
              shared_objs.push_back(shared_obj);
              alloc_size = sizeof(TestObject) + sizeof(std::shared_ptr<void>);  // 粗略估计大小
            } else {
              throw std::bad_alloc();
            }
          } else {
            // 使用 new
            TestObject* obj = new TestObject(obj_id_dist(gen));
            unique_objs.push_back(std::unique_ptr<TestObject>(obj));  // 转移所有权
            alloc_size = sizeof(TestObject);
          }
        } else {
          // C 分配
          alloc_size = size_dist(gen);
          ptr = std::malloc(alloc_size);
          if (!ptr) throw std::bad_alloc();
          std::memset(ptr, 0xAA, alloc_size);  // 使用内存
          raw_ptrs.push_back(ptr);
        }

        allocated_this_thread += alloc_size;
        total_allocated_bytes += alloc_size;

        // 随机释放一些旧的分配以模拟生命周期和增加压力
        if (dealloc_dist(gen)) {
          if (!raw_ptrs.empty() && gen() % 3 == 0) {
            size_t idx = gen() % raw_ptrs.size();
            if (raw_ptrs[idx]) {
              std::free(raw_ptrs[idx]);
              total_deallocated_bytes +=
                  (MIN_BLOCK_SIZE + MAX_BLOCK_SIZE) / 2;  // 估计
              raw_ptrs[idx] = raw_ptrs.back();
              raw_ptrs.pop_back();
              allocated_this_thread -= alloc_size;  // 粗略调整
            }
          }
          if (!unique_objs.empty() && gen() % 3 == 1) {
            // unique_ptr 在析构时自动释放，这里通过 pop_back 触发
            unique_objs.pop_back();
            // 无法精确知道释放了多少，不更新 total_deallocated_bytes
          }
          if (!shared_objs.empty() && gen() % 3 == 2) {
            // shared_ptr 在引用计数为0时释放，这里通过 pop_back 移除一个引用
            shared_objs.pop_back();
            // 无法精确知道释放了多少，不更新 total_deallocated_bytes
          }
        }

        // 控制 vector 大小，防止无限增长
        if (raw_ptrs.size() > POINTER_VECTOR_SIZE) {
          // 释放一半
          size_t half = raw_ptrs.size() / 2;
          for (size_t i = 0; i < half; ++i) {
            if (raw_ptrs[i]) {
              std::free(raw_ptrs[i]);
              total_deallocated_bytes += (MIN_BLOCK_SIZE + MAX_BLOCK_SIZE) / 2;
            }
          }
          raw_ptrs.erase(raw_ptrs.begin(), raw_ptrs.begin() + half);
          allocated_this_thread -=
              half * ((MIN_BLOCK_SIZE + MAX_BLOCK_SIZE) / 2);
        }
        if (unique_objs.size() > POINTER_VECTOR_SIZE / 3) {
          unique_objs.erase(unique_objs.begin(),
                            unique_objs.begin() + unique_objs.size() / 2);
        }
        if (shared_objs.size() > POINTER_VECTOR_SIZE / 3) {
          shared_objs.erase(shared_objs.begin(),
                            shared_objs.begin() + shared_objs.size() / 2);
        }

      } catch (const std::bad_alloc& e) {
        allocation_failures++;
        // 失败时，尝试释放一些内存或继续
        if (raw_ptrs.size() > 100) {
          // 释放最后100个
          for (int i = 0; i < 100 && !raw_ptrs.empty(); ++i) {
            if (raw_ptrs.back()) {
              std::free(raw_ptrs.back());
              total_deallocated_bytes += (MIN_BLOCK_SIZE + MAX_BLOCK_SIZE) / 2;
            }
            raw_ptrs.pop_back();
          }
        }
      } catch (...) {
        allocation_failures++;
      }
    }
  } catch (...) {
    // 可能因内存不足退出循环
  }

  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  {
    std::lock_guard<std::mutex> lock(print_mutex);
    std::cout << "线程 " << thread_id
              << " | 分配目标: " << (TARGET_MEMORY_PER_THREAD / (1024.0 * 1024.0)) << "MB"
              << " | 实际分配: " << (allocated_this_thread / (1024.0 * 1024.0)) << "MB"
              << " | 耗时: " << duration.count() << "ms"
              << " | 失败次数: " << allocation_failures.load()
              << std::endl;
  }

  for (void* p : raw_ptrs) {
    if (p) std::free(p);
  }
}

void signal_ctrl_c(int) { exit(0); }

int main() {
  freopen("output.log", "w", stdout);
  freopen("output.log", "a", stderr);

  signal(SIGINT, signal_ctrl_c);

  std::cout << "=== C++ 内存分配器压力测试 ===" << std::endl;
  std::cout << "平台: QNX (8核, 可用内存 ~4GB)" << std::endl;
  std::cout << "线程数: " << NUM_THREADS << std::endl;
  std::cout << "每线程目标分配: ~"
            << (TARGET_MEMORY_PER_THREAD / (1024.0 * 1024.0)) << "MB"
            << std::endl;
  std::cout << "总目标分配: ~" << (TOTAL_TARGET_MEMORY / (1024.0 * 1024.0))
            << "MB" << std::endl;
  std::cout << "分配类型: malloc/free (60%), new/delete (20%), make_shared (20%)"
            << std::endl;
  std::cout << "块大小: " << MIN_BLOCK_SIZE << " - " << MAX_BLOCK_SIZE
            << " 字节" << std::endl;
  std::cout << "注意: 释放字节数统计不完整 (尤其C++智能指针)。" << std::endl;
  std::cout << "运行前请确保环境准备就绪。" << std::endl << std::endl;

  auto overall_start = std::chrono::steady_clock::now();
  std::vector<std::thread> threads;
  threads.reserve(NUM_THREADS);

  for (int i = 0; i < NUM_THREADS; ++i) {
    threads.emplace_back(thread_work, i);
  }

  for (auto& t : threads) {
    t.join();
  }

  auto overall_end = std::chrono::steady_clock::now();
  auto overall_duration = std::chrono::duration_cast<std::chrono::seconds>(
      overall_end - overall_start);

  std::cout << "\n=== 测试完成 ===" << std::endl;
  std::cout << "总分配尝试: " << allocation_attempts.load() << std::endl;
  std::cout << "总分配失败: " << allocation_failures.load() << std::endl;
  std::cout << "总分配字节数: " << total_allocated_bytes.load() << " ("
            << (total_allocated_bytes.load() / (1024.0 * 1024.0)) << " MB)"
            << std::endl;
  std::cout << "总释放字节数: " << total_deallocated_bytes.load() << " ("
            << (total_deallocated_bytes.load() / (1024.0 * 1024.0)) << " MB)"
            << std::endl;
  std::cout << "总耗时: " << overall_duration.count() << " 秒" << std::endl;

  return 0;
}
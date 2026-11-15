#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <functional>
#include <chrono>

namespace Kernel {

enum class ExecKind { PROCESS, THREAD, COROUTINE };
enum class ExecState { READY, RUNNING, BLOCKED, DEAD };

struct ExecUnit {
    int       id = 0;
    ExecKind  kind = ExecKind::PROCESS;
    ExecState state = ExecState::READY;
    
    std::string              func_name;
    std::vector<std::string> args;
    std::string              retval;
    
    bool done = false;
    
    std::mutex               mtx;
    std::condition_variable  cv;
    std::condition_variable  resume_cv;
};

class Scheduler {
public:
    static Scheduler& get();

    int spawn_process  (const std::string& fn, const std::vector<std::string>& args);
    int spawn_thread   (const std::string& fn, const std::vector<std::string>& args);
    int spawn_coroutine(const std::string& fn, const std::vector<std::string>& args);

    // 🚀 等待执行完成并获取结果。注意：await 只负责取结果，不负责销毁内存。
    std::string await(int id);
    
    bool is_done(int id);

    void suspend(int id);
    void resume(int id);

    // ============================================================
    // 🚀 【核心新增】交给内核的回收接口
    // 只有内核（或父进程）显式调用 reap，这个僵尸任务才会从内存中彻底释放。
    // 如果任务还在运行中，内核调用 reap 可以强制使其失效并清理。
    // ============================================================
    void reap(int id);

    using ExecutorFn = std::function<std::string(const std::string&, const std::vector<std::string>&)>;
    void set_executor(ExecutorFn fn) { 
        std::unique_lock lock(global_mtx);
        m_executor = std::move(fn); 
    }

private:
    Scheduler() = default;
    ~Scheduler() = default;
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    mutable std::shared_mutex global_mtx;
    
    std::map<int, std::shared_ptr<ExecUnit>> units;
    int next_id = 1;
    
    ExecutorFn m_executor;
    std::mutex executor_mtx;

    int spawn(ExecKind kind, const std::string& fn, const std::vector<std::string>& args);
};

// ================= 底层系统调用接口 =================
int         sys_spawn_process  (const std::string& fn, const std::vector<std::string>& args);
int         sys_spawn_thread   (const std::string& fn, const std::vector<std::string>& args);
int         sys_spawn_coroutine(const std::string& fn, const std::vector<std::string>& args);
std::string sys_await          (int id);

} // namespace Kernel
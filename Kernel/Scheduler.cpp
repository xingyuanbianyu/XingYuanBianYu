#include "Scheduler.h"
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <stdexcept>

namespace Kernel {

Scheduler& Scheduler::get() {
    static Scheduler inst;
    return inst;
}

// ============================================================
// 🚀 内部通用 spawn
// ============================================================
int Scheduler::spawn(ExecKind kind, const std::string& fn,
                     const std::vector<std::string>& args) {
    std::unique_lock<std::shared_mutex> lk(global_mtx); // 🚀 修正为 unique_lock
    int id = next_id++;
    auto unit = std::make_shared<ExecUnit>();
    unit->id        = id;
    unit->kind      = kind;
    unit->func_name = fn;
    unit->args      = args;
    units[id] = unit;
    return id;
}

// ============================================================
// 🚀 安全获取执行器（防竞态）
// ============================================================
static Scheduler::ExecutorFn get_safe_executor(Scheduler::ExecutorFn& m_executor, std::mutex& executor_mtx) {
    std::lock_guard<std::mutex> lk(executor_mtx);
    return m_executor;
}

// ============================================================
// 🚀 多线程
// ============================================================
int Scheduler::spawn_thread(const std::string& fn, const std::vector<std::string>& args) {
    int id = spawn(ExecKind::THREAD, fn, args);

    std::thread([this, id, fn, args]() {
        // 🚀 核心修复 1：安全获取 unit，防止内核在这期间调用了 reap
        std::shared_ptr<ExecUnit> unit;
        {
            std::shared_lock<std::shared_mutex> lk(global_mtx);
            auto it = units.find(id);
            if (it == units.end()) return;
            unit = it->second;
        }

        ExecutorFn exec = get_safe_executor(m_executor, executor_mtx);
        std::string ret;
        if (!exec) {
            ret = "[Scheduler] Error: executor not set";
        } else {
            try {
                ret = exec(fn, args);
            } catch (const std::exception& e) {
                ret = std::string("ERROR: ") + e.what();
            } catch (...) {
                ret = "ERROR: Unknown exception";
            }
        }

        {
            std::lock_guard<std::mutex> l(unit->mtx);
            if (unit->state != ExecState::DEAD) {
                unit->retval = ret;
                unit->done   = true;
                unit->state  = ExecState::DEAD;
            }
        }
        unit->cv.notify_all();
    }).detach();

    return id;
}

// ============================================================
// 🚀 多进程：fork + pipe
// ============================================================
int Scheduler::spawn_process(const std::string& fn, const std::vector<std::string>& args) {
    int id = spawn(ExecKind::PROCESS, fn, args);

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        std::cerr << "[Scheduler] pipe 创建失败\n";
        return -1;
    }

    // 🚀 核心修复 2：在 fork 之前把 m_executor 拷贝出来！
    // 否则子进程里访问父进程的 std::function 会死锁/崩溃
    ExecutorFn safe_exec = get_safe_executor(m_executor, executor_mtx);

    pid_t pid = fork();
    if (pid == 0) {
        // === 子进程 ===
        close(pipefd[0]); // 关闭读端
        std::string ret;
        if (!safe_exec) {
            ret = "[Scheduler] Error: executor not set (child)";
        } else {
            try {
                ret = safe_exec(fn, args);
            } catch (const std::exception& e) {
                ret = std::string("ERROR: ") + e.what();
            } catch (...) {
                ret = "ERROR: 未知异常";
            }
        }

        size_t total_written = 0;
        while (total_written < ret.size()) {
            ssize_t n = write(pipefd[1], ret.data() + total_written, ret.size() - total_written);
            if (n <= 0) break;
            total_written += n;
        }

        close(pipefd[1]); // 关闭写端，通知父进程数据结束
        _exit(0);
    }
    else if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        std::cerr << "[Scheduler] fork 失败\n";
        return -1;
    }

    // === 父进程 ===
    close(pipefd[1]); // 关闭写端

    std::thread([this, id, pid, rfd = pipefd[0]]() {
        // 🚀 核心修复 3：安全获取 unit
        std::shared_ptr<ExecUnit> unit;
        {
            std::shared_lock<std::shared_mutex> lk(global_mtx);
            auto it = units.find(id);
            if (it == units.end()) {
                close(rfd);
                waitpid(pid, nullptr, 0);
                return;
            }
            unit = it->second;
        }

        std::string result;
        char buf[4096];
        ssize_t n;

        while ((n = read(rfd, buf, sizeof(buf))) > 0) {
            result.append(buf, n);
        }
        close(rfd);

        int status = 0;
        waitpid(pid, &status, 0);

        std::string final_result = result;
        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            if (exit_code != 0) {
                final_result = "ERROR: 子进程异常退出，代码 " + std::to_string(exit_code);
            }
        } else if (WIFSIGNALED(status)) {
            final_result = "ERROR: 子进程崩溃，信号 " + std::to_string(WTERMSIG(status));
        }

        {
            std::lock_guard<std::mutex> l(unit->mtx);
            if (unit->state != ExecState::DEAD) {
                unit->retval = final_result;
                unit->done   = true;
                unit->state  = ExecState::DEAD;
            }
        }
        unit->cv.notify_all();
    }).detach();

    return id;
}

// ============================================================
// 🚀 多协程（异步化防阻塞）
// ============================================================
int Scheduler::spawn_coroutine(const std::string& fn, const std::vector<std::string>& args) {
    // 🚀 核心修复 4：协程不能阻塞主调用者。
    // 这里用异步线程模拟，或者你未来接入 ucontext 实现真正的挂起恢复。
    return spawn_thread(fn, args); // 暂时复用线程逻辑，保证异步
}

// ============================================================
// 🚀 等待与状态查询
// ============================================================
std::string Scheduler::await(int id) {
    std::shared_ptr<ExecUnit> unit;
    {
        std::shared_lock<std::shared_mutex> lk(global_mtx); // 🚀 读锁
        auto it = units.find(id);
        if (it == units.end()) return "[Scheduler Error] Invalid or reaped ID";
        unit = it->second;
    }
    
    std::unique_lock<std::mutex> lk(unit->mtx);
    unit->cv.wait(lk, [&]{ return unit->done; });
    return unit->retval;
}

bool Scheduler::is_done(int id) {
    std::shared_lock<std::shared_mutex> lk(global_mtx); // 🚀 读锁
    auto it = units.find(id);
    return it != units.end() && it->second->done;
}

// ============================================================
// 🚀 挂起与恢复
// ============================================================
void Scheduler::suspend(int id) {
    std::shared_ptr<ExecUnit> unit;
    {
        std::shared_lock<std::shared_mutex> lk(global_mtx);
        auto it = units.find(id);
        if (it != units.end()) unit = it->second;
    }
    if (!unit) return;

    std::unique_lock<std::mutex> lk(unit->mtx);
    if (unit->state == ExecState::RUNNING) {
        unit->state = ExecState::BLOCKED;
        unit->resume_cv.wait(lk, [&] { return unit->state != ExecState::BLOCKED; });
    }
}

void Scheduler::resume(int id) {
    std::shared_ptr<ExecUnit> unit;
    {
        std::shared_lock<std::shared_mutex> lk(global_mtx);
        auto it = units.find(id);
        if (it != units.end()) unit = it->second;
    }
    if (!unit) return;

    std::lock_guard<std::mutex> lk(unit->mtx);
    if (unit->state == ExecState::BLOCKED) {
        unit->state = ExecState::RUNNING;
        unit->resume_cv.notify_all();
    }
}

// ============================================================
// 🚀 内核级回收（终极杀招）
// ============================================================
void Scheduler::reap(int id) {
    std::unique_lock<std::shared_mutex> lk(global_mtx); // 🚀 写锁
    auto it = units.find(id);
    if (it == units.end()) return;

    auto unit = it->second;
    {
        std::lock_guard<std::mutex> ulock(unit->mtx);
        // 强制标记死亡，防止有残留的唤醒逻辑报错
        unit->state = ExecState::DEAD;
        unit->done = true; // 保证如果有卡在 await 的线程，立刻唤醒它们
    }
    
    // 🚀 唤醒所有可能等待该任务的线程
    unit->cv.notify_all();
    unit->resume_cv.notify_all();
    
    // 🚀 从调度表彻底移除
    units.erase(it);
}

// ============================================================
// 系统调用入口
// ============================================================
int sys_spawn_process(const std::string& fn, const std::vector<std::string>& args) {
    return Scheduler::get().spawn_process(fn, args);
}
int sys_spawn_thread(const std::string& fn, const std::vector<std::string>& args) {
    return Scheduler::get().spawn_thread(fn, args);
}
int sys_spawn_coroutine(const std::string& fn, const std::vector<std::string>& args) {
    return Scheduler::get().spawn_coroutine(fn, args);
}
std::string sys_await(int id) {
    return Scheduler::get().await(id);
}

} // namespace Kernel
#pragma once

#include <atomic>
#include <concepts>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <semaphore>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <vector>

#include <blockworlds/bw_base.h>

template<typename F>
concept JobCallable = std::invocable<F>;

/**
 * Modern C++20 Job Pool for executing tasks asynchronously.
 *
 * Example usage:
 * @code
 *   JobPool pool(4); // 4 worker threads
 *
 *   // Submit a task and get a future
 *   auto future = pool.submit([] { return 42; });
 *   int result = future.get();
 *
 *   // Fire and forget
 *   pool.submit([] { std::cout << "Hello from worker!\n"; });
 *
 *   // With stop_token for cancellable tasks
 *   auto cancellable = pool.submit([](std::stop_token st) {
 *       while (!st.stop_requested()) {
 *           // do work...
 *       }
 *   });
 * @endcode
 */
class CModernJobPool {
public:
	/**
	 * Creates a job pool with the specified number of worker threads.
	 *
	 * @param NumThreads Number of worker threads (default: hardware concurrency)
	 */
	explicit CModernJobPool(size_t NumThreads = std::thread::hardware_concurrency()) : m_Semaphore(0) {
		m_Workers.reserve(NumThreads);
		for (size_t i = 0; i < NumThreads; ++i) {
			m_Workers.emplace_back([this](const std::stop_token& St) { WorkerLoop(St); });
		}
	}

	/**
	 * Destructor - waits for all tasks to complete and shuts down workers.
	 */
	~CModernJobPool() {
		Shutdown();
	}

	CModernJobPool(const CModernJobPool &) = delete;
	CModernJobPool &operator=(const CModernJobPool &) = delete;
	CModernJobPool(CModernJobPool &&) = delete;
	CModernJobPool &operator=(CModernJobPool &&) = delete;

	/**
	 * Submit a task for execution.
	 *
	 * @tparam F Callable type (lambda, function, functor)
	 * @tparam Args Argument types for the callable
	 * @param f The callable to execute
	 * @param args Arguments to pass to the callable
	 * @return std::future with the result of the callable
	 *
	 * @code
	 *   auto future = pool.submit([] { return 42; });
	 *   auto result = future.get(); // blocks until task completes
	 * @endcode
	 */
	template<typename F, typename... Args>
	auto Submit(F &&f, Args &&... args) -> std::future<std::invoke_result_t<F, Args...>> {
		using return_type = std::invoke_result_t<F, Args...>;

		auto Task = std::make_shared<std::packaged_task<return_type()> >(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...));

		std::future<return_type> result = Task->get_future(); {
			std::scoped_lock Lock(m_Mutex);
			dbg_assert(!m_Shutdown, "Cannot submit task to shutdown pool");
			m_Tasks.emplace_back([Task]() { (*Task)(); });
		}

		m_Semaphore.release();
		return result;
	}

	/**
	 * Submit a cancellable task that receives a std::stop_token.
	 *
	 * @tparam F Callable that takes std::stop_token as first parameter
	 * @tparam Args Additional argument types
	 * @param f The callable to execute
	 * @param args Additional arguments
	 * @return std::future with the result
	 *
	 * @code
	 *   auto future = pool.submit_cancellable([](std::stop_token st) {
	 *       while (!st.stop_requested()) {
	 *           // do work...
	 *       }
	 *       return 42;
	 *   });
	 *   // Task can be cancelled via pool shutdown
	 * @endcode
	 */
	template<typename F, typename... Args>
	auto SubmitCancellable(F &&f, Args &&... args) -> std::future<std::invoke_result_t<F, std::stop_token, Args...>> {
		using return_type = std::invoke_result_t<F, std::stop_token, Args...>;

		auto Task = std::make_shared<std::packaged_task<return_type(std::stop_token)> >(
			std::bind(std::forward<F>(f), std::placeholders::_1, std::forward<Args>(args)...));

		std::future<return_type> result = Task->get_future(); {
			std::scoped_lock Lock(m_Mutex);
			dbg_assert(!m_Shutdown, "Cannot submit task to shutdown pool");
			m_Tasks.emplace_back([Task, this]() {
				auto it = std::find_if(m_Workers.begin(), m_Workers.end(),
				                       [id = std::this_thread::get_id()](const std::jthread &t) {
					                       return t.get_id() == id;
				                       });

				if (it != m_Workers.end()) {
					(*Task)(it->get_stop_token());
				} else {
					// Fallback if we can't find the thread (shouldn't happen)
					(*Task)(std::stop_token{});
				}
			});
		}

		m_Semaphore.release();
		return result;
	}

	/**
	 * Returns the number of worker threads.
	 */
	size_t ThreadCount() const {
		return m_Workers.size();
	}

	/**
	 * Returns the approximate number of pending tasks.
	 * This is approximate due to race conditions.
	 */
	size_t PendingTasks() const {
		std::scoped_lock Lock(m_Mutex);
		return m_Tasks.size();
	}

	/**
	 * Wait for all pending tasks to complete.
	 * Does not prevent new tasks from being added.
	 */
	void WaitIdle() const {
		while (true) {
			std::scoped_lock Lock(m_Mutex);
			if (m_Tasks.empty() && m_active_workers == 0) {
				break;
			}
			std::this_thread::yield();
		}
	}

	/**
	 * Shutdown the pool gracefully.
	 * Waits for all pending tasks to complete, then stops all workers.
	 */
	void Shutdown() { {
			std::scoped_lock Lock(m_Mutex);
			if (m_Shutdown) {
				return; // Already shut down
			}
			m_Shutdown = true;
		}

		for (size_t i = 0; i < m_Workers.size(); ++i) {
			m_Semaphore.release();
		}

		for (auto &Worker: m_Workers) {
			Worker.request_stop();
		}

		for (auto &Worker: m_Workers) {
			if (Worker.joinable()) {
				Worker.join();
			}
		}
	}

private:
	void WorkerLoop(const std::stop_token &StopToken) {
		while (!StopToken.stop_requested()) {
			m_Semaphore.acquire();

			if (StopToken.stop_requested()) {
				break;
			}

			std::function<void()> Task; {
				std::scoped_lock Lock(m_Mutex);
				if (m_Tasks.empty()) {
					if (m_Shutdown) {
						break;
					}
					continue;
				}

				Task = std::move(m_Tasks.front());
				m_Tasks.pop_front();
				++m_active_workers;
			}

			if (Task) {
				Task();
			}

			// Mark worker as idle
			{
				std::scoped_lock Lock(m_Mutex);
				--m_active_workers;
			}
		}
	}

	std::vector<std::jthread> m_Workers;
	std::counting_semaphore<> m_Semaphore;

	mutable std::mutex m_Mutex;
	std::deque<std::function<void()> > m_Tasks;
	std::atomic<size_t> m_active_workers{0};
	bool m_Shutdown{false};
};

/**
 * Helper function to create a job pool with specified number of threads.
 *
 * @param NumThreads Number of worker threads
 * @return Unique pointer to JobPool
 */
inline std::unique_ptr<CModernJobPool> CreateJobPool(size_t NumThreads = std::thread::hardware_concurrency()) {
	return std::make_unique<CModernJobPool>(NumThreads);
}

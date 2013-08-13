/* 
 * hCraft - A custom Minecraft server.
 * Copyright (C) 2012-2013	Jacob Zhitomirsky (BizarreCake)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "threadpool.hpp"

#include <iostream> // DEBUG


namespace hCraft {
	
	thread_pool::thread_pool ()
	{
		this->terminating = false;
	}
	
	
	
	/* 
	 * Starts up @{thread_count} amount of worker threads and begins processing
	 * tasks.
	 */
	void
	thread_pool::start (int thread_count)
	{
		for (int i = 0; i < thread_count; ++i)
			{
				this->workers.emplace_back (worker_thread (this, std::thread (
					std::bind (std::mem_fn (&hCraft::thread_pool::main_loop), this))));
			}
	}
	
	/* 
	 * Terminates all running pool threads.
	 */
	void
	thread_pool::stop ()
	{
		this->terminating = true;
		this->cv.notify_all ();
		
		while (!this->workers.empty ())
			{
				worker_thread& w = this->workers.back ();
				w.th.join ();
				this->workers.pop_back ();
			}
	}
	
	
	
	/* 
	 * The function ran by worker threads.
	 */
	void
	thread_pool::main_loop ()
	{
		for (;;)
			{
				task t = this->get_task ();
				if (this->terminating)
					break;
				
				t.callback (t.context);
			}
	}
	
	/* 
	 * Returns the next available task. If none are currently available, the
	 * function blocks until one is.
	 */
	thread_pool::task
	thread_pool::get_task ()
	{
		bool& terminating = this->terminating;
		if (terminating)
			return task ();
		
		decltype (this->tasks)& task_ref = this->tasks;
		
		std::unique_lock<std::mutex> guard {this->task_lock};
		this->cv.wait (guard, [&task_ref, &terminating] { return !task_ref.empty () || terminating; });
		if (terminating)
			return task ();
			
		task task = this->tasks.front ();
		this->tasks.pop ();
		return task;
	}
	
	
	
	/* 
	 * Schedules the specified task to be ran by a pooled thread.
	 */
	void
	thread_pool::enqueue (std::function<void (void *)>&& cb, void *context)
	{
		std::unique_lock<std::mutex> guard {this->task_lock};
		this->tasks.emplace (std::move (cb), context);
		this->cv.notify_one ();
	}
}


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

#ifndef _hCraft__GENERATOR_H_
#define _hCraft__GENERATOR_H_

#include <thread>
#include <queue>
#include <mutex>
#include <vector>
#include <map>


namespace hCraft {
	
	class world;
	class chunk;
	
	
	struct gen_request
	{
		int pid;
		world *w;
		int cx, cz; // chunk coordinates.
		int flags;
		int extra;
	};
	
	struct gen_response
	{
		world *w;
		int cx, cz;
		chunk *ch;
		int flags;
		int extra;
	};
	
	enum gen_flags
	{
		GFL_NONE = 0,
		
		
		// generation aborted due to some reason
		GFL_ABORTED = (1 << 0),
		
		
		// makes the generator drop the chunk once it's generated.
		GFL_NODELIVER = (1 << 1),
		
		// with this flag on, the generator will not make any attempts to cancel
		// the generation of a chunk, for whatever reason.
		GFL_NOABORT = (1 << 2),
	};
	
	
	struct generator_queue {
		int pid;
		std::queue<gen_request> requests;
		unsigned int counter;
	};
	
	/* 
	 * A continuous loop that runs in its own thread that supplies players with
	 * chunks once they have been generated.
	 * 
	 * Note that this class doesn't really do any "real" world generation, that
	 * kind of stuff is handled elsewhere.
	 */
	class chunk_generator
	{
		std::thread *th;
		bool _running;
		
		std::vector<generator_queue *> queues;
		std::map<int, int> index_map;
		std::mutex request_mutex;
		
	private:
		/* 
		 * Where everything happens.
		 */
		void main_loop ();
		
	public:
		chunk_generator ();
		~chunk_generator ();
		
		
		
		/* 
		 * Starts the internal thread and begins accepting generation requests.
		 */
		void start ();
		
		/* 
		 * Stops the generation thread and cleans up resources.
		 */
		void stop ();
		
		
		
		/* 
		 * Requests the chunk located at the given coordinates to be generated.
		 * The specified player is then informed when it's ready.
		 */
		void request (world *w, int cx, int cz, int pid, int flags = 0, int extra = 0);
		
		/* 
		 * Cancels all chunk requests for the given world.
		 */
		void cancel_requests (world *w);
	};
}

#endif


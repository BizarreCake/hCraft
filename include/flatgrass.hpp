/* 
 * hCraft - A custom Minecraft server.
 * Copyright (C) 2012	Jacob Zhitomirsky
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

#ifndef _hCraft__MAPGENERATOR__FLATGRASS_H_
#define _hCraft__MAPGENERATOR__FLATGRASS_H_

#include "mapgenerator.hpp"
#include <random>


namespace hCraft {
	
	/* 
	 * Simple flatgrass generation.
	 */
	class flatgrass_map_generator: public map_generator
	{
		std::minstd_rand rnd;
		long seed;
		
	public:
		/* 
		 * Constructs a new flatgrass map generator.
		 */
		flatgrass_map_generator (long seed);
		
		
		/* 
		 * Generates flatgrass terrain on the specified chunk.
		 */
		virtual void generate (map& mp, chunk *out, int cx, int cz);
	};
}

#endif

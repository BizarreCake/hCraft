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

#ifndef _hCraft__WORLD_H_
#define _hCraft__WORLD_H_

#include "util/position.hpp"
#include "chunk.hpp"
#include "generation/worldgenerator.hpp"
#include "providers/worldprovider.hpp"
#include "lighting.hpp"
#include "physics/blocks/physics_block.hpp"
#include "physics/physics.hpp"
#include "drawing/editstage.hpp"
#include "portal.hpp"
#include "block_history.hpp"
#include "world_security.hpp"
#include "zone.hpp"

#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <deque>
#include <memory>
#include <thread>
#include <chrono>
#include <functional>
#include <stdexcept>


namespace hCraft {
	
	class server;
	class entity;
	class logger;
	class player;
	class player_list;
	class world_transaction;
	
	
	/* 
	 * The whereabouts of a block that must be modified and sent to close players.
	 */
	struct block_update
	{
		int x;
		int y;
		int z;
		
		unsigned short id;
		unsigned char  meta;
		int extra;
		
		player *pl; // the player that initated the update.
		bool physics;
		int data;
		void *ptr;
		
		block_update (int x, int y, int z, unsigned short id, unsigned char meta,
			int extra, int data, void *ptr, player *pl, bool physics)
		{
			this->x = x;
			this->y = y;
			this->z = z;
			this->id = id;
			this->meta = meta;
			this->pl = pl;
			this->extra = extra;
			this->data = data;
			this->ptr = ptr;
			this->physics = physics;
		}
	};
	
	enum world_physics_state
	{
		PHY_ON,
		PHY_OFF,
		PHY_PAUSED,
	};
	
	
	
	enum world_type
	{
		// 
		WT_NORMAL = 0,
		
		// Worlds of this type do not keep any chunks in memory, and only load them
		// to commit accumulated changes every once in while.
		WT_LIGHT,
	};
	
	
	struct tagged_chunk
	{
		int cx, cz;
		chunk *ch;
	};
	
	
	
	/* 
	 * The world provides methods to easily retreive or modify chunks, and
	 * get/set individual blocks within those chunks. In addition to that,
	 * the world also manages a list of players.
	 */
	class world
	{
		world_type typ;
		
		server &srv;
		logger &log;
		char name[33]; // 32 chars max
		player_list *players;
		
		std::unique_ptr<std::thread> th;
		bool th_running;
		
		std::deque<block_update> updates;
		world_physics_state ph_state;
		unsigned long long ticks;
		unsigned long long wtime;
		bool wtime_frozen;
		
		std::unordered_map<unsigned long long, chunk *> chunks;
		std::vector<tagged_chunk> bad_chunks;
		std::mutex chunk_lock;
		std::mutex bad_chunk_lock;
		
		struct { int x, z; chunk *ch; } last_chunk;
		
		std::unordered_set<entity *> entities;
		std::mutex entity_lock;
		
		int width;
		int depth;
		entity_pos spawn_pos;
		chunk *edge_chunk;
		
		world_generator *gen;
		world_provider *prov;
		std::mutex gen_lock;
		
		std::vector<portal *> portals;
		std::mutex portal_lock;
		
		world_security wsec;
		zone_manager zman;
		
	public:
		bool auto_lighting;
		physics_manager physics;
		lighting_manager lm;
		block_history_manager blhi;
		
		dense_edit_stage estage;
		std::mutex estage_lock;
		std::mutex update_lock;
		
		bool pvp;
		int def_gm;
		std::string def_inv;
		int use_def_inv;
		
		int id;
		
	public:
		inline server& get_server () const { return this->srv; }
		inline world_type get_type () const { return this->typ; }
		inline const char* get_name () const { return this->name; }
		inline player_list& get_players () { return *this->players; }
		
		inline world_provider* get_provider () { return this->prov; }
		inline const char* get_path () { return this->prov->get_path (); }
		
		inline int get_width () const { return this->width; }
		inline int get_depth () const { return this->depth; }
		void set_width (int width);
		void set_depth (int depth);
		void set_size (int width, int depth)
			{ set_width (width); set_depth (depth); }
		chunk* get_edge_chunk () const { return this->edge_chunk; }
		
		// world time
		inline unsigned long long get_time () const { return this->wtime; }
		inline void set_time (unsigned long long v) { this->wtime = v; }
		inline void stop_time () { this->wtime_frozen = true; }
		inline void resume_time () { this->wtime_frozen = false; }
		inline bool is_time_frozen () const { return this->wtime_frozen; }
		
		inline entity_pos get_spawn () const { return this->spawn_pos; }
		inline void set_spawn (const entity_pos& pos) { this->spawn_pos = pos; }
		
		inline world_physics_state physics_state () const { return this->ph_state; }
		inline std::mutex& get_update_lock () { return this->update_lock; }
		inline std::mutex& get_chunk_lock () { return this->chunk_lock; }
		
		inline world_security& security () { return this->wsec; }
		inline zone_manager& get_zones () { return this->zman; }
		
	public:
		std::string get_colored_name ();
		
		inline world_generator* get_generator () { return this->gen; }
		void set_generator (world_generator *gen);
		
	private:
		/* 
		 * The function ran by the world's thread.
		 */
		void worker ();
		
		std::unordered_set<entity *>::iterator
		despawn_entity_nolock (std::unordered_set<entity *>::iterator itr);
		
		void get_information (world_information& inf);
		
	public:
		/* 
		 * Constructs a new empty world.
		 */
		world (world_type typ, server &srv, const char *name, logger &log,
			world_generator *gen,
			world_provider *provider);
			
		static world* load_world (server &srv, const char *name);
		
		/* 
		 * Class destructor.
		 */
		~world ();
		
		
		
		/* 
		 * Reloads the world from the specified path.
		 */
		void reload_world (const char *name);
		
	//----
		
		/* 
		 * Checks whether the specified string can be used to name a world.
		 */
		static bool is_valid_name (const char *wname);
		
	//----
		
		/* 
		 * Starts the world's "physics"-handling thread.
		 */
		void start ();
		
		/* 
		 * Stops the world's thread.
		 */
		void stop ();
		
		
		
		/* 
		 * Saves all modified chunks to disk.
		 */
		void save_all ();
		
		/* 
		 * Saves metadata to disk (width, depth, spawn pos, etc...).
		 */
		void save_meta ();
		
		
		
		/* 
		 * Loads up a grid of radius x radius chunks around the given point
		 * (specified in chunk coordinates).
		 */
		void load_grid (chunk_pos cpos, int radius);
		
		/* 
		 * Calls load_grid around () {x: 0, z: 0}, and attempts to find a suitable
		 * spawn position. 
		 */
		void prepare_spawn (int radius, bool calc_spawn_point = false);
		
		
		
		/* 
		 * Inserts the specified chunk into this world at the given coordinates.
		 */
		void put_chunk (int x, int z, chunk *ch);
		void put_chunk_nolock (int x, int z, chunk *ch, bool lock = false);
		
		/* 
		 * Searches the chunk world for a chunk located at the specified coordinates.
		 */
		chunk* get_chunk (int x, int z);
		chunk* get_chunk_nolock (int x, int z, bool lock = false);
		
		/* 
		 * Returns the chunk located at the given block coordinates.
		 */
		chunk* get_chunk_at (int bx, int bz);
		
		/* 
		 * Same as get_chunk (), but if the chunk does not exist, it will be either
		 * loaded from a file (if such a file exists), or completely generated from
		 * scratch.
		 */
		chunk* load_chunk (int x, int z);
		chunk* load_chunk_at (int bx, int bz);
		chunk* load_chunk_nolock (int x, int z, bool lock = false);
		
		/* 
		 * Unloads and saves (if save = true) the chunk located at the specified
		 * coordinates.
		 */
		void remove_chunk (int x, int z, bool save);
		
		/* 
		 * Removes all chunks from the world and optionally saves them to disk.
		 */
		void clear_chunks (bool save, bool del = false);
		
		/* 
		 * Checks whether a block exists at the given coordinates.
		 */
		bool in_bounds (int x, int y, int z);
		bool chunk_in_bounds (int cx, int cz);
		
		
		
		/* 
		 * Spawns the specified entity into the world.
		 */
		void spawn_entity (entity *e);
		
		/* 
		 * Removes the specified etnity from this world.
		 */
		void despawn_entity (entity *e);
		
		/* 
		 * Calls the given function on all entities in the world.
		 */
		void all_entities (std::function<void (entity *e)> f);
		
		
		
		/* 
		 * Block interaction: 
		 */
		
		void set_id (int x, int y, int z, unsigned short id);
		unsigned short get_id (int x, int y, int z);
		
		void set_meta (int x, int y, int z, unsigned char val);
		unsigned char get_meta (int x, int y, int z);
		
		void set_block_light (int x, int y, int z, unsigned char val);
		unsigned char get_block_light (int x, int y, int z);
		
		void set_sky_light (int x, int y, int z, unsigned char val);
		unsigned char get_sky_light (int x, int y, int z);
		
		void set_block (int x, int y, int z, unsigned short id, unsigned char meta, unsigned char ex = 0);
		
		void set_extra (int x, int y, int z, unsigned char ex);
		unsigned char get_extra (int x, int y, int z);
		
		block_data get_block (int x, int y, int z);
		
		bool has_physics_at (int x, int y, int z);
		physics_block* get_physics_at (int x, int y, int z);
		
		/* 
		 * Instead of fetching the block from the underlying chunk, and attempt
		 * to query the edit stage is made first.
		 */
		blocki get_final_block (int x, int y, int z);
		
	//----
		
		/* 
		 * Enqueues an update that should be made to a block in this world
		 * and sent to nearby players.
		 */
		
		void queue_update (int x, int y, int z, unsigned short id,
			unsigned char meta = 0, int extra = 0, int data = 0, void *ptr = nullptr,
			player *pl = nullptr, bool physics = true);
		
		void queue_update_nolock (int x, int y, int z, unsigned short id,
			unsigned char meta = 0, int extra = 0, int data = 0, void *ptr = nullptr,
			player *pl = nullptr, bool physics = true);
		
		void queue_update (world_transaction *tr);
		
		void queue_lighting (int x, int y, int z)
			{ this->lm.enqueue (x, y, z); }
		void queue_lighting_nolock (int x, int y, int z)
			{ this->lm.enqueue_nolock (x, y, z); }
		
		void queue_physics (int x, int y, int z, int extra = 0,
			void *ptr = nullptr, int tick_delay = 20, physics_params *params = nullptr,
			physics_block_callback cb = nullptr);
		
		// does nothing if the block is already queued to be handled.
		void queue_physics_once (int x, int y, int z, int extra = 0,
			void *ptr = nullptr, int tick_delay = 20, physics_params *params = nullptr,
			physics_block_callback cb = nullptr);
		
		
		void start_physics ();
		void stop_physics ();
		void pause_physics ();
		
		
		
	//----
		
		/* 
		 * Checks whether the specified player can modify the block located at the
		 * given coordinates.
		 */
		bool can_build_at (int x, int y, int z, player *pl);
		
		
	//----
		
		/* 
		 * Finds and returns the portal located at the given block coordinates,
		 * or null, if one is not found.
		 */
		portal* get_portal (int x, int y, int z);
		
		/* 
		 * Adds the specified portal to the world's portal list
		 */
		void add_portal (portal *ptl);
	};
}

#endif


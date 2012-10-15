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

#include "server.hpp"
#include <memory>
#include <fstream>
#include <cstring>
#include <yaml-cpp/yaml.h>
#include <arpa/inet.h>
#include <sys/stat.h>


namespace hCraft {
	
	// constructor.
	server::initializer::initializer (std::function<void ()>&& init,
		std::function<void ()>&& destroy)
		: init (std::move (init)), destroy (std::move (destroy))
		{ this->initialized = false; }
	
	// move constructor.
	server::initializer::initializer (initializer&& other)
		: init (std::move (other.init)), destroy (std::move (other.destroy)),
			initialized (other.initialized)
		{ }
	
	
	
	// constructor.
	server::worker::worker (struct event_base *base, std::thread&& th)
		: evbase (base), th (std::move (th)), event_count (0)
		{ }
	
	// move constructor.
	server::worker::worker (worker&& w)
		: evbase (w.evbase), th (std::move (w.th)),
			event_count (w.event_count.load ())
		{ }
	
	
	
	/* 
	 * Constructs a new server.
	 */
	server::server (logger &log)
		: log (log)
	{
		// add <init, destory> pairs
		
		this->inits.push_back (initializer (
			std::bind (std::mem_fn (&hCraft::server::init_config), this),
			std::bind (std::mem_fn (&hCraft::server::destroy_config), this)));
		
		this->inits.push_back (initializer (
			std::bind (std::mem_fn (&hCraft::server::init_core), this),
			std::bind (std::mem_fn (&hCraft::server::destroy_core), this)));
		
		this->inits.push_back (initializer (
			std::bind (std::mem_fn (&hCraft::server::init_commands), this),
			std::bind (std::mem_fn (&hCraft::server::destroy_commands), this)));
		
		this->inits.push_back (initializer (
			std::bind (std::mem_fn (&hCraft::server::init_worlds), this),
			std::bind (std::mem_fn (&hCraft::server::destroy_worlds), this)));
		
		this->inits.push_back (initializer (
			std::bind (std::mem_fn (&hCraft::server::init_workers), this),
			std::bind (std::mem_fn (&hCraft::server::destroy_workers), this)));
		
		this->inits.push_back (initializer (
			std::bind (std::mem_fn (&hCraft::server::init_listener), this),
			std::bind (std::mem_fn (&hCraft::server::destroy_listener), this)));
		
		this->running = false;
	}
	
	/* 
	 * Class destructor.
	 * 
	 * Calls `stop ()' if the server is still running.
	 */
	server::~server ()
	{
		this->stop ();
	}
	
	
	
	/* 
	 * The function executed by worker threads.
	 * Waits for incoming connections.
	 */
	void
	server::work ()
	{
		while (!this->workers_ready)
			{
				if (this->workers_stop)
					return;
				
				std::this_thread::sleep_for (std::chrono::milliseconds (10));
			}
		
		// find the worker that spawned this thread.
		std::thread::id this_id = std::this_thread::get_id ();
		worker *w = nullptr;
		for (auto itr = this->workers.begin (); itr != this->workers.end (); ++itr)
			{
				worker &t = *itr;
				if (t.th.get_id () == this_id)
					w = &t;
			}
		
		while (!this->workers_stop)
			{
				event_base_loop (w->evbase, EVLOOP_NONBLOCK);
				std::this_thread::sleep_for (std::chrono::milliseconds (1));
			}
	}
	
	/* 
	 * Returns the worker that has the least amount of events associated with
	 * it.
	 */
	server::worker&
	server::get_min_worker ()
	{
		auto min_itr = this->workers.begin ();
		for (auto itr = (min_itr + 1); itr != this->workers.end (); ++itr)
			{
				if (itr->event_count.load () < min_itr->event_count.load ())
					min_itr = itr;
			}
		
		return *min_itr;
	}
	
	/* 
	 * Wraps the accepted connection around a player object and associates it
	 * with a server worker.
	 */
	void
	server::handle_accept (struct evconnlistener *listener, evutil_socket_t sock,
		struct sockaddr *addr, int len, void *ptr)
	{
		server &srv = *static_cast<server *> (ptr);
		
		// get IP address
		char ip[16];
		if (!inet_ntop (AF_INET, &(((struct sockaddr_in *)addr)->sin_addr), ip,
			sizeof ip))
			{
				srv.log (LT_WARNING) << "Received a connection from an invalid IP address." << std::endl;
				evutil_closesocket (sock);
				return;
			}
		
		worker &w = srv.get_min_worker ();
		player *pl = new player (srv, w.evbase, sock, ip);
		
		{
			std::lock_guard<std::mutex> guard {srv.connecting_lock};
			srv.connecting.push_back (pl);
		}
	}
	
	
	
	/* 
	 * Removes and destroys disconnected players.
	 */
	void
	server::cleanup_players (scheduler_task& task)
	{
		server &srv = *(static_cast<server *> (task.get_context ()));
		
		// check list of logged-in players.
		srv.get_players ().remove_if (
			[] (player *pl) -> bool
				{
					if (pl->bad ())		
						return true;
					
					return false;
				}, true);
		
		// and the list of players that haven't fully logged-in yet.
		{
			std::lock_guard<std::mutex> guard {srv.connecting_lock};
			for (auto itr = srv.connecting.begin (); itr != srv.connecting.end (); )
				{
					player *pl = *itr;
					if (pl->bad ())
						{
							itr = srv.connecting.erase (itr);
							delete pl;
						}
					else
						++ itr;
				}
		}
	}
	
	
	
	/* 
	 * Attempts to start the server up.
	 * Throws `server_error' on failure.
	 */
	void
	server::start ()
	{
		if (this->running)
			throw server_error ("server already running");
		
		try
			{
				for (auto itr = this->inits.begin (); itr != this->inits.end (); ++itr)
					{
						initializer& init = *itr;
						init.init ();
						init.initialized = true;
					}
			}
		catch (const std::exception& ex)
			{
				for (auto itr = this->inits.rbegin (); itr != this->inits.rend (); ++itr)
					{
						initializer &init = *itr;
						if (init.initialized)
							{
								init.destroy ();
								init.initialized = false;
							}
					}
			}
		
		this->running = true;
	}
	
	/* 
	 * Stops the server, kicking all connected players and freeing resources
	 * previously allocated by the start () member function.
	 */
	void
	server::stop ()
	{
		if (!this->running)
			return;
		
		for (auto itr = this->inits.rbegin (); itr != this->inits.rend (); ++itr)
			{
				initializer &init = *itr;
				init.destroy ();
				init.initialized = false;
			}
		
		this->running = false;
	}
	
	
	
	/* 
	 * Attempts to insert the specifed world into the server's world list.
	 * Returns true on success, and false on failure (due to a name collision).
	 */
	bool
	server::add_world (world *w)
	{
		std::lock_guard<std::mutex> guard {this->world_lock};
		
		cistring name {w->get_name ()};
		auto itr = this->worlds.find (name);
		if (itr != this->worlds.end ())
			return false;
		
		this->worlds[std::move (name)] = w;
		return true;
	}
	
	/* 
	 * Removes the specified world from the server's world list.
	 */
	void
	server::remove_world (world *w)
	{
		std::lock_guard<std::mutex> guard {this->world_lock};
		
		for (auto itr = this->worlds.begin (); itr != this->worlds.end (); ++itr)
			{
				world *other = itr->second;
				if (other == w)
					{
						this->worlds.erase (itr);
						delete other;
						break;
					}
			}
	}
	
	void
	server::remove_world (const char *name)
	{
		std::lock_guard<std::mutex> guard {this->world_lock};
		
		auto itr = this->worlds.find (name);
		if (itr != this->worlds.end ())
			this->worlds.erase (itr);
	}
	
	/* 
	 * Searches the server's world list for a world that has the specified name.
	 */
	world*
	server::find_world (const char *name)
	{
		std::lock_guard<std::mutex> guard {this->world_lock};
		
		auto itr = this->worlds.find (name);
		if (itr != this->worlds.end ())
			return itr->second;
		return nullptr;
	}
	
	
	
	/* 
	 * Returns a unique number that can be used for entity identification.
	 */
	int
	server::next_entity_id ()
	{
		std::lock_guard<std::mutex> guard {this->id_lock};
		int id = this->id_counter ++;
		if (this->id_counter < 0)
			this->id_counter = 0; // overflow.
		return id;
	}
	
	/* 
	 * Removes the specified player from the "connecting" list, and then inserts
	 * that player into the global player list.
	 * 
	 * If the server is full, or if the same player connected twice, the function
	 * returns false and the player is kicked with an appropriate message.
	 */
	bool
	server::done_connecting (player *pl)
	{
		bool can_stay = true;
		
		if (this->players->count () == this->get_config ().max_players)
			{ pl->kick ("§bThe server is full", "server full");
				can_stay = false; }
		else if (this->players->add (pl) == false)
			{ pl->kick ("§4You're already logged in", "already logged in");
				can_stay = false; }
		
		{
			std::lock_guard<std::mutex> guard {this->connecting_lock};
			for (auto itr = this->connecting.begin (); itr != this->connecting.end (); ++itr)
				{
					player *other = *itr;
					if (other == pl)
						{
							this->connecting.erase (itr);
							break;
						}
				}
		}
		
		return can_stay;
	}
	
	
	
/*******************************************************************************
		
		<init, destroy> pairs:
		
********************************************************************************/
	
//----
	// init_config (), destroy_config ():
	/* 
	 * Loads settings from the configuration file ("server-config.yaml",
	 * in YAML form) into the server's `cfg' structure. If "server-config.yaml"
	 * does not exist, it will get created with default settings.
	 */
	
	static void
	default_config (server_config& out)
	{
		std::strcpy (out.srv_name, "hCraft server");
		std::strcpy (out.srv_motd, "Welcome to my server!");
		out.max_players = 12;
		std::strcpy (out.main_world, "main");
		
		std::strcpy (out.ip, "0.0.0.0");
		out.port = 25565;
	}
	
	static void
	write_config (std::ofstream& strm, const server_config& in)
	{
		YAML::Emitter out;
		
		out << YAML::BeginMap;
		
		out << YAML::Key << "server";
		out << YAML::Value << YAML::BeginMap;
		
		out << YAML::Key << "general" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "server-name" << YAML::Value << in.srv_name;
		out << YAML::Key << "server-motd" << YAML::Value << in.srv_motd;
		out << YAML::Key << "max-players" << YAML::Value << in.max_players;
		out << YAML::Key << "main-world" << YAML::Value << in.main_world;
		out << YAML::EndMap;
		
		out << YAML::Key << "network" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "ip-address" << YAML::Value << in.ip;
		out << YAML::Key << "port" << YAML::Value << in.port;
		out << YAML::EndMap;
		
		out << YAML::EndMap;
		out << YAML::EndMap;
		
		strm << out.c_str () << std::flush;
	}
	
	
	
	static void
	_cfg_read_general_map (logger& log, const YAML::Node *general_map, server_config& out)
	{
		const YAML::Node *node;
		std::string str;
		bool error = false;
		
		// server name
		node = general_map->FindValue ("server-name");
		if (node && node->Type () == YAML::NodeType::Scalar)
			{
				*node >> str;
				if (str.size () > 0 && str.size() <= 80)
					std::strcpy (out.srv_name, str.c_str ());
				else
					{
						if (!error)
							log (LT_ERROR) << "Config: at map \"server.general\":" << std::endl;
						log (LT_INFO) << " - Scalar \"server-name\" must contain at "
														 "least one character and no more than 80." << std::endl;
						error = true;
					}
			}
		
		// server motd
		node = general_map->FindValue ("server-motd");
		if (node && node->Type () == YAML::NodeType::Scalar)
			{
				*node >> str;
				if (str.size() <= 80)
					std::strcpy (out.srv_motd, str.c_str ());
				else
					{
						if (!error)
							log (LT_ERROR) << "Config: at map \"server.general\":" << std::endl;
						log (LT_INFO) << " - Scalar \"server-motd\" must contain no more than 80 characters." << std::endl;
						error = true;
					}
			}
		
		// max players
		node = general_map->FindValue ("max-players");
		if (node && node->Type () == YAML::NodeType::Scalar)
			{
				int num;
				*node >> num;
				if (num > 0 && num <= 1024)
					out.max_players = num;
				else
					{
						if (!error)
							log (LT_ERROR) << "Config: at map \"server.general\":" << std::endl;
						log (LT_INFO) << " - Scalar \"max_players\" must be in the range of 1-1024." << std::endl;
						error = true;
					}
			}
		
		// main world
		node = general_map->FindValue ("main-world");
		if (node && node->Type () == YAML::NodeType::Scalar)
			{
				*node >> str;
				if (str.size () > 0 && str.size() <= 32)
					std::strcpy (out.main_world, str.c_str ());
				else
					{
						if (!error)
							log (LT_ERROR) << "Config: at map \"server.general\":" << std::endl;
						log (LT_INFO) << " - Scalar \"main-world\" must contain at no more than 32 characters." << std::endl;
						error = true;
					}
			}
	}
	
	static void
	_cfg_read_network_map (logger& log, const YAML::Node *network_map, server_config& out)
	{
		const YAML::Node *node;
		std::string str;
		bool error = false;
		
		// ip address
		node = network_map->FindValue ("ip-address");
		if (node && node->Type () == YAML::NodeType::Scalar)
			{
				*node >> str;
				if (str.size () == 0)
					std::strcpy (out.ip, "0.0.0.0");
				else
					{
						struct in_addr addr;
						if (inet_pton (AF_INET, str.c_str (), &addr) == 1)
							std::strcpy (out.ip, str.c_str ());
						else
							{
								if (!error)
									log (LT_ERROR) << "Config: at map \"server.network\":" << std::endl;
								log (LT_INFO) << " - Scalar \"ip-address\" is invalid." << std::endl;
								error = true;
							}
					}
			}
		
		// port
		node = network_map->FindValue ("port");
		if (node && node->Type () == YAML::NodeType::Scalar)
			{
				int num;
				*node >> num;
				if (num >= 0 && num <= 65535)
					out.port = num;
				else
					{
						if (!error)
							log (LT_ERROR) << "Config: at map \"server.network\":" << std::endl;
						log (LT_INFO) << " - Scalar \"port\" must be in the range of 0-65535." << std::endl;
						error = true;
					}
			}
	}
	
	static void
	_cfg_read_server_map (logger& log, const YAML::Node *server_map, server_config& out)
	{
		const YAML::Node *general_map = server_map->FindValue ("general");
		if (general_map && general_map->Type () == YAML::NodeType::Map)
			_cfg_read_general_map (log, general_map, out);
		
		const YAML::Node *network_map = server_map->FindValue ("network");
		if (network_map && network_map->Type () == YAML::NodeType::Map)
			_cfg_read_network_map (log, network_map, out);
	}
	
	static void
	read_config (logger& log, std::ifstream& strm, server_config& out)
	{
		YAML::Parser parser (strm);
		
		YAML::Node doc;
		if (!parser.GetNextDocument (doc))
			return;
		
		const YAML::Node *server_map = doc.FindValue ("server");
		if (server_map && server_map->Type () == YAML::NodeType::Map)
			_cfg_read_server_map (log, server_map, out);
	}
	
	
	
	void
	server::init_config ()
	{
		default_config (this->cfg);
		
		log () << "Loading configuration from \"server-config.yaml\"" << std::endl;
		
		std::ifstream strm ("server-config.yaml");
		if (strm.is_open ())
			{
				read_config (this->log, strm, this->cfg);
				strm.close ();
				return;
			}
		
		log () << "Configuration file does not exist, creating one with default settings." << std::endl;
		std::ofstream ostrm ("server-config.yaml");
		if (!ostrm.is_open ())
			{
				log (LT_ERROR) << "Failed to open \"server.cfg\" for writing." << std::endl;
				return;
			}
		
		write_config (ostrm, this->cfg);
	}
	
	void
	server::destroy_config ()
		{ }
	
	
	
	
//---
	// init_core (), destroy_core ():
	/* 
	 * Initializes various data structures and variables needed by the server.
	 */
	
	void
	server::init_core ()
	{
		this->sched.start ();
		
		this->players = new playerlist ();
		this->id_counter = 0;
		
		this->get_scheduler ().new_task (hCraft::server::cleanup_players, this)
			.run_forever (250);
		this->tpool.start (6); // 6 pooled threads
	}
	
	void
	server::destroy_core ()
	{
		this->tpool.stop ();
		this->sched.stop ();
		
		{
			std::lock_guard<std::mutex> guard {this->connecting_lock};
			for (auto itr = this->connecting.begin (); itr != this->connecting.end (); ++itr)
				{
					player *pl = *itr;
					delete pl;
				}
			this->connecting.clear ();
		}
		
		this->players->clear (true);
		delete this->players;
	}
	
	
	
//---
	// init_commands (), destroy_commands ():
	/* 
	 * Loads up commands.
	 */
	
	void
	server::init_commands ()
	{
		this->commands = new command_list ();
		
		this->commands->add (command::create ("help"));
		this->commands->add (command::create ("me"));
		this->commands->add (command::create ("ping"));
	}
	
	void
	server::destroy_commands ()
	{
		delete this->commands;
	}
	
	
	
//---
	// init_worlds (), destroy_worlds ():
	/* 
	 * Loads up and initializes worlds.
	 */
	
	void
	server::init_worlds ()
	{
		mkdir ("worlds", 0744);
		
		/* 
		 * Create and add the main world.
		 */
		
		log () << "Creating main world." << std::endl;
		world *main_world = new world (this->get_config ().main_world,
			map_generator::create ("flatgrass"),
			map_provider::create ("hw", "worlds", "main"));
		main_world->get_map ().set_size (32, 32);
		main_world->get_map ().prepare_spawn (10);
		this->add_world (main_world);
		this->main_world = main_world;
	}
	
	void
	server::destroy_worlds ()
	{
		
		// clear worlds
		{
			std::lock_guard<std::mutex> guard {this->world_lock};
			for (auto itr = this->worlds.begin (); itr != this->worlds.end (); ++itr)
				{
					world *w = itr->second;
					w->get_map ().save_all ();
					delete w;
				}
			this->worlds.clear ();
		}
	}
	
	
	
//----
	// init_workers (), destroy_workers ():
	/* 
	 * Creates and starts server workers.
	 * The total number of workers created depends on how many cores the user
	 * has installed on their system, which means that on a multi-core system,
	 * the work will be parallelized between all cores.
	 */
	
	void
	server::init_workers ()
	{
		this->worker_count = std::thread::hardware_concurrency ();
		if (this->worker_count == 0)
			this->worker_count = 2;
		this->workers.reserve (this->worker_count);
		log () << "Creating " << this->worker_count << " server workers." << std::endl;
		
		this->workers_stop = false;
		this->workers_ready = false;
		for (int i = 0; i < this->worker_count; ++i)
			{
				struct event_base *base = event_base_new ();
				if (!base)
					{
						this->workers_stop = true;
						for (auto itr = this->workers.begin (); itr != this->workers.end (); ++itr)
							{
								worker &w = *itr;
								if (w.th.joinable ())
									w.th.join ();
								
								event_base_free (w.evbase);
							}
						
						throw server_error ("failed to create workers");
					}
				
				std::thread th (std::bind (std::mem_fn (&hCraft::server::work), this));
				this->workers.push_back (worker (base, std::move (th)));
			}
		
		this->workers_ready = true;
	}
	
	void
	server::destroy_workers ()
	{
		this->workers_stop = true;
		for (auto itr = this->workers.begin (); itr != this->workers.end (); ++itr)
			{
				worker &w = *itr;
				if (w.th.joinable ())
					w.th.join ();
				
				event_base_free (w.evbase);
			}
	}
	
	
	
//----
	// init_listener (), destroy_listener ():
	/* 
	 * Creates the listening socket and starts listening on the IP address and
	 * port number specified by the user in the configuration file for incoming
	 * connections.
	 */
	void
	server::init_listener ()
	{
		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port   = htons (this->cfg.port);
		inet_pton (AF_INET, this->cfg.ip, &addr.sin_addr);
		
		worker &w = this->get_min_worker ();
		this->listener = evconnlistener_new_bind (w.evbase,
			&hCraft::server::handle_accept, this, LEV_OPT_CLOSE_ON_FREE
			| LEV_OPT_REUSEABLE, -1, (struct sockaddr *)&addr, sizeof addr);
		if (!this->listener)
			throw server_error ("failed to create listening socket (port taken?)");
		
		++ w.event_count;
		log () << "Started listening on port " << this->cfg.port << "." << std::endl;
	}
	
	void
	server::destroy_listener ()
	{
		evconnlistener_free (this->listener);
	}
}

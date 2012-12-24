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

#include "playerlist.hpp"
#include "player.hpp"
#include <cstring>


namespace hCraft {
	
	/* 
	 * Constructs a new empty player list.
	 */
	playerlist::playerlist ()
		{ }
	
	/* 
	 * Class copy constructor.
	 */
	playerlist::playerlist (const playerlist& other)
		: players (other.players), lock ()
		{ }
	
	/* 
	 * Class destructor.
	 */
	playerlist::~playerlist ()
		{ }
	
	
	
	/* 
	 * Returns the number of players contained in this list.
	 */
	int
	playerlist::count ()
	{
		std::lock_guard<std::mutex> guard {this->lock};
		return this->players.size ();
	}
	
	
	
	/* 
	 * Adds the specified player into the player list.
	 * Returns false if a player with the same name already exists in the list;
	 * true otherwise.
	 */
	bool
	playerlist::add (player *pl)
	{
		const char *username = pl->get_username ();
		player *other = this->find (username, player_find_method::case_insensitive);
		if (other)
			return false;
			
		std::lock_guard<std::mutex> guard {this->lock};
		this->players[username] = pl;
		return true;
	}
	
	/* 
	 * Removes the player that has the specified name from this player list.
	 * NOTE: the search is done using case-INsensitive comparison.
	 */
	void
	playerlist::remove (const char *name, bool delete_player)
	{
		std::lock_guard<std::mutex> guard {this->lock};
		
		auto itr = this->players.find (name);
		if (itr != this->players.end ())
			{
				if (delete_player)
					delete itr->second;
				this->players.erase (itr);
			}
	}
	
	/* 
	 * Removes the specified player from this player list.
	 */
	void
	playerlist::remove (player *pl, bool delete_player)
	{
		std::lock_guard<std::mutex> guard {this->lock};
		
		for (auto itr = this->players.begin (); itr != this->players.end (); ++itr)
			{
				player *other = itr->second;
				if (other == pl)
					{
						if (delete_player)
							delete other;
						this->players.erase (itr);
						break;
					}
			}
	}
	
	/* 
	 * Removes all players from this player list.
	 */
	void
	playerlist::clear (bool delete_players)
	{
		std::lock_guard<std::mutex> guard {this->lock};
		
		if (delete_players)
			{
				for (auto itr = this->players.begin (); itr != this->players.end (); ++itr)
					{
						player *pl = itr->second;
						delete pl;
					}
			}
		
		this->players.clear ();
	}
	
	/* 
	 * Searches the player list for a player that has the specified name.
	 * Uses the given method to determine if names match.
	 */
	player*
	playerlist::find (const char *name, player_find_method method)
	{
		std::lock_guard<std::mutex> guard {this->lock};
		
		switch (method)
			{
				case player_find_method::case_sensitive:
					{
						auto itr = this->players.find (name);
						if (itr != this->players.end ())
							{
								// do another comparison, this time with case sensitivity.
								if (std::strcmp (itr->first.c_str (), name) == 0)
									return itr->second;
							}
						
						return nullptr;
					}
				
				case player_find_method::case_insensitive:
					{
						auto itr = this->players.find (name);
						if (itr != this->players.end ())
							return itr->second;
						
						return nullptr;
					}
				
				case player_find_method::name_completion:
					{
						// TODO
						return nullptr;
					}
			}
		
		// never reached.
		return nullptr;
	}
	
	
	
	/* 
	 * Calls the function @{f} on all players in this list.
	 */
	void
	playerlist::all (std::function<void (player *)> f, player* except)
	{
		std::lock_guard<std::mutex> guard {this->lock};
		
		for (auto itr = this->players.begin (); itr != this->players.end (); ++itr)
			{
				player *pl = itr->second;
				if (pl != except)
					f (pl);
			}
	}
	
	/* 
	 * Calls the function @{f} on all players visible to player @{target} with
	 * exception to @{target} itself.
	 */
	void
	playerlist::all_visible (std::function<void (player *)> f, player *target)
	{
		std::lock_guard<std::mutex> guard {this->lock};
		
		for (auto itr = this->players.begin (); itr != this->players.end (); ++itr)
			{
				player *pl = itr->second;
				if (pl != target && pl->visible_to (target))
					f (pl);
			}
	}
	
	/* 
	 * Iterates through the list, and passes all players to the specified
	 * predicate function. Players that produce a positive value are
	 * removed from the list, and can be optinally destroyed as well.
	 */
	void
	playerlist::remove_if (std::function<bool (player *)> pred, bool delete_players)
	{
		std::lock_guard<std::mutex> guard {this->lock};
		
		for (auto itr = this->players.begin (); itr != this->players.end (); )
			{
				player *pl = itr->second;
				if (pred (pl) == true)
					{
						itr = this->players.erase (itr);
						if (delete_players)
							delete pl;
					}
				else
					++ itr;
			}
	}
	
	
	
	/* 
	 * Inserts all players except player @{except} into vector @{vec}.
	 */
	void
	playerlist::populate (std::vector<player *>& vec, player *except)
	{
		std::lock_guard<std::mutex> guard {this->lock};
		
		for (auto itr = this->players.begin (); itr != this->players.end (); ++itr)
			{
				player *pl = itr->second;
				if (pl != except)
					vec.push_back (pl);
			}
	}
	
	
	
	/* 
	 * Broadcasts the given message to all players in this list.
	 */
	
	void
	playerlist::message (const char *msg, player *except)
	{
		this->all (
			[msg] (player *pl)
				{
					pl->message (msg);
				}, except);
	}
	
	void
	playerlist::message (const std::string& msg, player *except)
	{
		this->message (msg.c_str (), except);
	}
	
	void
	playerlist::message_wrapped (const char *msg, const char *prefix,
		bool first_line, player *except)
	{
		this->all (
			[msg, prefix, first_line] (player *pl)
				{
					pl->message_wrapped (msg, prefix, first_line);
				}, except);
	} 
	
	void
	playerlist::message_wrapped (const std::string& msg, const char *prefix,
		bool first_line, player *except)
	{
		this->message_wrapped (msg.c_str (), prefix, first_line, except);
	}
	
	
	
	/* 
	 * Sends the specified packet to all players in this list.
	 */
	void
	playerlist::send_to_all (packet *pack, player *except)
	{
		std::lock_guard<std::mutex> guard {this->lock};
		
		for (auto itr = this->players.begin (); itr != this->players.end (); ++itr)
			{
				player *pl = itr->second;
				if (pl != except)
					{
						pl->send (new packet (*pack));
					}
			}
		delete pack;
	}
}


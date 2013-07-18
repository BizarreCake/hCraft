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

#include "wordwrap.hpp"
#include <sstream>
#include <cctype>
#include <cstring>


namespace hCraft {
	
	static void
	push_word (std::vector<std::string>& out, std::ostringstream& line,
		std::ostringstream& word, int space_count, int word_length, int max_line)
	{
		if (word.tellp () > max_line)
			{
				// the word is too big, it must be split.
				
				if (line.tellp () > 0)
					while (space_count-- > 0)
						line << ' ';
				
				std::string w {word.str ()};
				while (w.size () > 0)
					{
						int rem = max_line - line.tellp ();
						if ((int)w.size () <= rem)
							rem = w.size ();
						else if (rem < 5)
							{
								out.push_back (line.str ());
								line.str (std::string ());
								line.clear ();
								rem = max_line;
							}
						
						int write = rem;
						if (w.size () - rem > 0)
							-- write; // make room for '-'
						
						line << w.substr (0, write);
						w.erase (0, write);
						if (w.size () > 0)
							line << '-';
					}
				
				word.str (std::string ());
				word.clear ();
			}
		else
			{
				if (line.tellp () + word.tellp () > max_line)
					{
						out.push_back (line.str ());
						line.str (std::string ());
						line.clear ();
					}
				
				if (line.tellp () > 0)
					while (space_count-- > 0)
						line << ' ';
				line << word.str ();
				
				word.str (std::string ());
				word.clear ();
			}
	}
	
	/* 
	 * Performs simple word wrapping on the given string, without doing any
	 * further processing\formatting. The resulting lines are stored in the
	 * vector @{out} with lines that are at most @{max_line} characters long.
	 */
	void
	wordwrap::wrap_simple (std::vector<std::string>& out, const char *in,
		int max_line)
	{
		std::ostringstream line, word;
		int c;
		int word_start = 0;
		int word_length = 0;
		int space_count = 0, prev_space_count = 0;
		
		bool is_color = false;
		
		while ((c = (int)(*in++)))
			{
				if (c == ' ')
					{
						++ space_count;
						++ word_start;
					}
				else
					{
						if (space_count > 0)
							{
								push_word (out, line, word, prev_space_count, word_length, max_line);
								prev_space_count = space_count;
								word_length = space_count = 0;
							}
						
						// we ignore color codes when computing length
						word << (char)c;
						if (c == 0xC2 && ((int)((unsigned char)*in) == 0xA7))
							{
								is_color = true;
								++ in;
							}
						else if (is_color)
							is_color = false;
						else
							++ word_length;
					}
			}
		
		// push the remaining characters (if any).
		if (word.tellp () > 0)
			push_word (out, line, word, prev_space_count, word_length, max_line);
		if (line.tellp () > 0)
			out.push_back (line.str ());
		
		wordwrap::wrap_colors (out);
	}
	
	
	
	/* 
	 * Same as wrap_simple (), but appends the string @{prefix} to all lines
	 * except for the first (unless @{first_line} is true).
	 */
	void
	wordwrap::wrap_prefix (std::vector<std::string>& out, const char *in,
		int max_line, const char *prefix, bool first_line)
	{
		int prefix_len = std::strlen (prefix);
		int n_max_line = max_line - prefix_len;
		
		wordwrap::wrap_simple (out, in, n_max_line);
		
		auto itr = first_line ? out.begin () : (out.begin () + 1);
		for (; itr != out.end (); ++itr)
			{
				std::string &str = *itr;
				str.insert (0, prefix);
			}
	}
	
	/* 
	 * Counts the number of spaces at the beginning of the string before word-
	 * wrapping. The exact amount is then inserted to the beginning of every
	 * line except the first. If @{remove_from_first} is true, then the leading
	 * spaces are removed from the first line.
	 */
	void
	wordwrap::wrap_spaced (std::vector<std::string>& out, const char *in,
		int max_line, bool remove_from_first)
	{
		// skip color codes
		bool has_color = false;
		const char *ptr = in;
		int c;
		
		while ((c = (unsigned char)(*ptr++)) == 0xC2)
			{
				if ((int)((unsigned char)*ptr) == 0xA7)
					{ has_color = true; ptr += 2; }
				else
					break;
			}
		-- ptr;
		
		// count spaces
		int space_count = 0;
		while (*ptr++ == ' ')
			++ space_count;
		if (ptr == in + std::strlen (in))
			return;
			
		std::string space_str;
		space_str.reserve (space_count + 1);
		for (int i = 0; i < space_count; ++i)
			space_str.push_back (' ');
		
		if (remove_from_first)
			{
				// ...
			}
		else
			{
				wordwrap::wrap_simple (out, in, max_line);
				
				auto itr = out.begin ();
				if (space_count > 0 && !has_color)
					{
						std::string& str = *itr;
						str.insert (0, "§f" + space_str);
					}
				
				for (++ itr; itr != out.end (); ++itr)
					{
						std::string& str = *itr;
						int pos = 0;
						for (;;)
							{
								if ((int)(unsigned char)str[pos] == 0xC2
									&& (int)(unsigned char)str[pos + 1] == 0xA7)
									pos += 3;
								else
									break;
							}
						str.insert (pos, space_str);
					}
			}
	}
	
	
	
	/* 
	 * Performs color wrapping on the given line vector.
	 */
	void
	wordwrap::wrap_colors (std::vector<std::string>& lines)
	{
		char last_col[3] = { '\0', '\0', '\0' };
		for (auto itr = lines.begin (); itr != lines.end (); ++itr)
			{
				if ((itr + 1) == lines.end ())
					break;
				
				std::string& str = *itr;
				for (int i = str.size () - 1; i >= 0; --i)
					{
						if ((i >= 2) && is_chat_code (str[i]) && ((str[i - 1] & 0xFF) == 0xA7))
							{ last_col[0] = 0xC2; last_col[1] = 0xA7; last_col[2] = str[i]; break; }
					}
				
				if (last_col[0] != '\0')
					{
						std::string& next = *(itr + 1);
						next.insert (0, last_col, 3);
					}
			}
	}
}


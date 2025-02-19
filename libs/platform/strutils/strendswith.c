/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include <errno.h>
#include <chef/platform.h>
#include <stdlib.h>
#include <string.h>

int strendswith(const char* text, const char* suffix)
{
	size_t textLength   = strlen(text);
	size_t suffixLength = strlen(suffix);
	
	if (textLength < suffixLength) {
		return -1;
	}
	
	return strcmp(text + (textLength - suffixLength), suffix);
}

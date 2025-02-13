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

#include <chef/platform.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int platform_rmdir(const char *path) {
   DIR*   d        = opendir(path);
   size_t path_len = strlen(path);
   int    r        = -1;

   if (d) {
      struct dirent *p;

      r = 0;
      while (!r && (p=readdir(d))) {
         int    r2 = -1;
         char*  buf;
         size_t len;

         if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) {
            continue;
         }

         len = path_len + strlen(p->d_name) + 2; 
         buf = malloc(len);

         if (buf) {
            struct stat statbuf;

            snprintf(buf, len, "%s" CHEF_PATH_SEPARATOR_S "%s", path, p->d_name);
            if (!lstat(buf, &statbuf)) {
               if (S_ISDIR(statbuf.st_mode)) {
                  r2 = platform_rmdir(buf);
               } else {
                  r2 = unlink(buf);
               }
            }
            free(buf);
         }
         r = r2;
      }
      closedir(d);
   }

   if (!r) {
      r = rmdir(path);
   }

   return r;
}

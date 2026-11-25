#pragma once
/* MicroDLNA project
 *
 * http://sourceforge.net/projects/microdlna/
 *
 * MicroDLNA media server
 * Copyright (c) 2025, Michael J. Walsh
 * All rights reserved.
 *
 * This file is part of MicroDLNA.
 *
 * MicroDLNA is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MicroDLNA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MicroDLNA. If not, see <http://www.gnu.org/licenses/>.
 *
 */

extern int listening_port;      /* HTTP Port */

extern int notify_interval;     /* seconds between SSDP announces */

extern int max_connections;     /* max number of simultaneous conenctions */

extern int mode_systemd;        /* systemd-compatible mode or not */

#define FRIENDLYNAME_MAX_LEN 64
extern char friendly_name[FRIENDLYNAME_MAX_LEN]; /* hostname or user preference */

extern char *media_dir;         /* path to the media directory */

#define UUID_LEN 42
extern char uuidvalue[UUID_LEN]; /* uuid identifier, includes uuid: prefix */

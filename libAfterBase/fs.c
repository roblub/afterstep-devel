/*
 * Copyright (C) 1999 Sasha Vasko <sashav@sprintmail.com>
 * merged with envvar.c originally created by :
 * Copyright (C) 1999 Ethan Fischer <allanon@crystaltokyo.com>
 * Copyright (C) 1998 Pierre Clerissi <clerissi@pratique.fr>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "config.h"

/* #define LOCAL_DEBUG */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include "audit.h"
#include "astypes.h"
#include "mystring.h"
#include "safemalloc.h"
#include "fs.h"
#include "output.h"


/*
 * get the date stamp on a file
 */
time_t get_file_modified_time (const char *filename)
{
	struct stat   st;
	time_t        stamp = 0;

	if (stat (filename, &st) != -1)
		stamp = st.st_mtime;
	return stamp;
}

int
check_file_mode (const char *file, int mode)
{
	struct stat   st;

	if ((stat (file, &st) == -1) || (st.st_mode & S_IFMT) != mode)
		return (-1);
	else
		return (0);
}

/* copy file1 into file2 */
int
copy_file (const char *realfilename1, const char *realfilename2)
{
	FILE         *targetfile, *sourcefile;
	int           c;

	targetfile = fopen (realfilename2, "w");
	if (targetfile == NULL)
	{
		fprintf (stderr, "can't open %s !\n", realfilename2);
		return (-1);
	}
	sourcefile = fopen (realfilename1, "r");
	if (sourcefile == NULL)
	{
		fprintf (stderr, "can't open %s !\n", realfilename1);
		return (-2);
	}
	while ((c = getc (sourcefile)) != EOF)
		putc (c, targetfile);

	fclose (targetfile);
	fclose (sourcefile);
	return 0;
}

char         *
make_file_name (const char *path, const char *file)
{
	register int  i = 0;
	register char *ptr;
	char         *filename;
	int 		  len;

	/* getting length */
	for (ptr = (char *)path; ptr[i]; i++);
	len = i+1;
	for (ptr = (char *)file; ptr[i]; i++);
	len += i+1;
	ptr = filename = safemalloc (len);
	/* copying path */
	for (i = 0; path[i]; i++)
		ptr[i] = path[i];
	/* copying filename */
	ptr[i] = '/';
	ptr += i+1 ;
	for (i = 0; file[i]; i++)
		ptr[i] = file[i];
	ptr[i] = '\0';						   /* zero byte */

	return filename;
}

char         *
put_file_home (const char *path_with_home)
{
	static char  *home = NULL;				   /* the HOME environment variable */
	static char   default_home[3] = "./";
	static int    home_len = 0;
	char         *str = NULL, *ptr;
	register int  i;
	if (path_with_home == NULL)
		return NULL;
	/* home dir ? */
	if (path_with_home[0] != '~' || path_with_home[1] != '/')
	{
		char *t = mystrdup (path_with_home);
		return t;
	}

	if (home == NULL)
	{
		if ((home = getenv ("HOME")) == NULL)
			home = &(default_home[0]);
		home_len = strlen (home);
	}

	for (i = 2; path_with_home[i]; i++);
	str = safemalloc (home_len + i);
	for (ptr = str + home_len-1; i > 0; i--)
		ptr[i] = path_with_home[i];
	for (i = 0; i < home_len; i++)
		str[i] = home[i];
	return str;
}


/****************************************************************************
 *
 * Find the specified icon file somewhere along the given path.
 *
 * There is a possible race condition here:  We check the file and later
 * do something with it.  By then, the file might not be accessible.
 * Oh well.
 *
 ****************************************************************************/
/* supposedly pathlist should not include any environment variables
   including things like ~/
 */

char         *
find_file (const char *file, const char *pathlist, int type)
{
	char 		  *path;
	register int   len;
	int            max_path = 0;
	register char *ptr;
	register int   i;
LOCAL_DEBUG_CALLER_OUT( "file %s", file );
	if (file == NULL)
		return NULL;

	if (*file == '/' || *file == '~' || ((pathlist == NULL) || (*pathlist == '\0')))
	{
		path = put_file_home (file);
		if ( access (path, type) == 0 )
		{
			return path;
		}
		free (path);
		return NULL;
	}
/*	return put_file_home(file); */
	for (i = 0; file[i]; i++);
	len = i ;
	for (ptr = (char *)pathlist; *ptr; ptr += i)
	{
		if (*ptr == ':')
			ptr++;
		for (i = 0; ptr[i] && ptr[i] != ':'; i++);
		if (i > max_path)
			max_path = i;
	}

	path = safemalloc (max_path + 1 + len + 1 + 100);
	strcpy( path+max_path+1, file );
	path[max_path] = '/' ;

	ptr = (char*)&(pathlist[0]) ;
	while( ptr[0] != '\0' )
	{
		for( i = 0 ; ptr[i] == ':' ; ++i );
		ptr += i ;
		for( i = 0 ; ptr[i] != ':' && ptr[i] != '\0' ; ++i );
		if( i > 0 && ptr[i-1] == '/' )
			i-- ;
		if( i > 0 )
		{
			register char *try_path = path+max_path-i;
			strncpy( try_path, ptr, i );
LOCAL_DEBUG_OUT( "errno = %d, file %s: checking path \"%s\"", errno, file, try_path );
			if ( access(try_path, type) == 0 )
			{
				char* res = mystrdup(try_path);
				free( path );
LOCAL_DEBUG_OUT( " found at: \"%s\"", try_path );
				return res;
			}
#ifdef LOCAL_DEBUG
			else
				show_system_error(" looking for file %s:", file );
#endif
		}
		if( ptr[i] == '/' )
			ptr += i+1 ;
		else
			ptr += i ;
	}
	free (path);
	return NULL;
}

char         *
find_envvar (char *var_start, int *end_pos)
{
	char          backup, *name_start = var_start;
	register int  i;
	char         *var = NULL;

	if (var_start[0] == '{')
	{
		name_start++;
		for (i = 1; var_start[i] && var_start[i] != '}'; i++);
	} else
		for (i = 0; isalnum (var_start[i]) || var_start[i] == '_'; i++);

	backup = var_start[i];
	var_start[i] = '\0';
	var = getenv (name_start);
	var_start[i] = backup;

	*end_pos = i;
	if (backup == '}')
		(*end_pos)++;
	return var;
}

static char *
do_replace_envvar (char *path)
{
	char         *data = path, *tmp;
	char         *home = getenv ("HOME");
	int           pos = 0, len, home_len = 0;

	if (path == NULL)
		return NULL;
	if (*path == '\0')
		return path;
	len = strlen (path);
	if (home)
		home_len = strlen (home);

	while (*(data + pos))
	{
		char         *var;
		int           var_len, end_pos;

		while (*(data + pos) != '$' && *(data + pos))
		{
			if (*(data + pos) == '~' && *(data + pos + 1) == '/')
			{
				if (pos > 0)
					if (*(data + pos - 1) != ':')
					{
						pos += 2;
						continue;
					}
				if (home == NULL)
					*(data + (pos++)) = '.';
				else
				{
					len += home_len;
					tmp = safemalloc (len);
					strncpy (tmp, data, pos);
					strcpy (tmp + pos, home);
					strcpy (tmp + pos + home_len, data + pos + 1);
					if( data != path )
						free (data);
					data = tmp;
					pos += home_len;
				}
			}
			pos++;
		}
		if (*(data + pos) == '\0')
			break;
		/* found $ sign - trying to replace var */
		if ((var = find_envvar (data + pos + 1, &end_pos)) == NULL)
		{
			++pos;
			continue;
		}
		var_len = strlen (var);
		len += var_len;
		tmp = safemalloc (len);
		strncpy (tmp, data, pos);
		strcpy (tmp + pos, var);
		strcpy (tmp + pos + var_len, data + pos + end_pos + 1);
		if( data != path )
			free (data);
		data = tmp;
	}
	return data;
}

void
replace_envvar (char **path)
{
	char         *res = do_replace_envvar( *path );
	if( res != *path ) 
	{
		free( *path );
		*path = res ;
	}
}

char*
copy_replace_envvar (char *path)
{
	char         *res = do_replace_envvar( path );
	return ( res == path )?mystrdup( res ):res;
}

/*
 * only checks first word in name, to allow full command lines with
 * options to be passed
 */
int
is_executable_in_path (const char *name)
{
	static char  *cache = NULL;
	static int    cache_result = 0, cache_len = 0, cache_size = 0;
	static char  *env_path = NULL;
	static int    max_path = 0;
	register int  i;

	if (name == NULL)
	{
		if (cache)
		{
			free (cache);
			cache = NULL;
		}
		cache_size = 0;
		cache_len = 0;
		if (env_path)
		{
			free (env_path);
			env_path = NULL;
		}
		max_path = 0;
		return 0;
	}

	/* cut leading "exec" enclosed in spaces */
	for (; isspace (*name); name++);
	if (!mystrncasecmp(name, "exec", 4) && isspace (name[4]))
		name += 4;
	for (; isspace (*name); name++);
	if (*name == '\0')
		return 0;

	for (i = 0; name[i] && !isspace (name[i]); i++);
	if (i == 0)
		return 0;

	if (cache)
		if (i == cache_len && strncmp (cache, name, i) == 0)
			return cache_result;

	if (i > cache_size)
	{
		if (cache)
			free (cache);
		/* allocating slightly more space then needed to avoid
		   too many reallocations */
		cache = (char *)safemalloc (i + (i >> 1) + 1);
		cache_size = i + (i >> 1);
	}
	strncpy (cache, name, i);
	cache[i] = '\0';
	cache_len = i;

	if (*cache == '/')
		cache_result = (CheckFile (cache) == 0) ? 1 : 0;
	else
	{
		char         *ptr, *path;
		struct stat   st;

		if (env_path == NULL)
		{
			env_path = mystrdup (getenv ("PATH"));
			replace_envvar (&env_path);
			for (ptr = env_path; *ptr; ptr += i)
			{
				if (*ptr == ':')
					ptr++;
				for (i = 0; ptr[i] && ptr[i] != ':'; i++);
				if (i > max_path)
					max_path = i;
			}
		}
		path = safemalloc (max_path + cache_len + 2);
		cache_result = 0;
		for (ptr = env_path; *ptr && cache_result == 0; ptr += i)
		{
			if (*ptr == ':')
				ptr++;
			for (i = 0; ptr[i] && ptr[i] != ':'; i++)
				path[i] = ptr[i];
			path[i] = '/';
			path[i + 1] = '\0';
			strcat (path, cache);
			if ((stat (path, &st) != -1) && (st.st_mode & S_IXUSR))
				cache_result = 1;
		}
		free (path);
	}
	return cache_result;
}



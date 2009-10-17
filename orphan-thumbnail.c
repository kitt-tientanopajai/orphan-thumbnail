/*
orphan-thumbnail.c

A program to find orphan GNOME thumbnail

v.0.0.1
Copyright 2009 Kitt Tientanopajai <kitty@kitty.in.th>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
MA 02110-1301, USA.


* Sun, 18 Oct 2009 02:31:46 +0700 - v.0.0.1
- Initial release

Todo
----
- Ignore /media directory

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <dirent.h>
#include <getopt.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libpng/png.h>

#define VERSION "0.0.1"

static int list_orphan_thumbnails (const char *);
static char *uri_to_filename (const char *);
static inline unsigned char hex (char);
static int file_exists (const char *);
static void help (const char *);

int delete_thumb = 0;
int show_orphan_only = 0;
int quiet_mode = 0;

int 
main (int argc, char *argv[])
{
  int opt;

  /* parse argument */
  static const struct option longopts[] = 
    {
      {"delete",    0, NULL, 'd'},
      {"help",      0, NULL, 'h'},
      {"orphan",    0, NULL, 'o'},
      {"quiet",     0, NULL, 'q'},
      {"version",   0, NULL, 'v'},
      {0, 0, 0, 0}
    };

  while ((opt = getopt_long (argc, argv, "dhoqv", longopts, 0)) != -1)
    {
      switch (opt)
        {
        case 'd':
          delete_thumb = 1;
          break;
        case 'h':
          help (argv[0]);
          exit (EXIT_SUCCESS);
        case 'o':
          show_orphan_only = 1;
          break;
        case 'q':
          quiet_mode = 1;
          break;
        case 'v':
          printf ("%s %s\n", argv[0], VERSION);
          exit (EXIT_SUCCESS);
        default:
          help (argv[0]);
          exit (EXIT_FAILURE);
        }
    }

  if (delete_thumb == 0) 
    quiet_mode = 0;

  list_orphan_thumbnails ("normal");
  list_orphan_thumbnails ("large");

  return 0;
}

int
list_orphan_thumbnails (const char *thumb_size)
{
  struct dirent **dir_entry;
  struct passwd *pwd;
  char thumb_dir[PATH_MAX];
  int n;

  /* find thumbnail directory */
  pwd = getpwuid (getuid ());
  snprintf (thumb_dir, PATH_MAX, "%s/.thumbnails/%s/", pwd->pw_dir, thumb_size);
  if (!show_orphan_only)
    printf ("\nThumbnail directory: %s\n\n", thumb_dir);

  if ((n = scandir (thumb_dir, &dir_entry, 0, alphasort)) < 0)
    {
      perror ("scan thumbnail directory");
      return -1;
    }
  else 
    {
      int file_total = n - 2;   /* total files in the thumbnail directory (exclude . and ..) */
      int thumb_total = 0;      /* total thumbnail files */
      int thumb_error = 0;      /* total thumbnail error */
      int thumb_orphan = 0;     /* total orphan thumbnail files */
      uint64_t total_bytes = 0; /* total bytes recovered */
      
      while (--n)
        {
          /* only *.png */
          if (strcasestr (dir_entry[n]->d_name, ".png"))
            {
              char thumb_file[PATH_MAX];

              snprintf (thumb_file, PATH_MAX, "%s%s", thumb_dir, dir_entry[n]->d_name);

              FILE *fp = fopen (thumb_file, "rb");
              if (fp == NULL) 
                {
                  printf ("Error opening %s, skip\n", dir_entry[n]->d_name);
                  continue;
                }

              /* check PNG signature */
              char header[8];
              fread (header, 1, 8, fp);
              if (png_sig_cmp(header, 0, 8) != 0)
                {
                  /* PNG signature not found */
                  printf ("%s is not PNG, skip\n", dir_entry[n]->d_name);
                  fclose (fp);
                  continue;
                }
              else
                {
                  thumb_total++;
                  /* create PNG structures for reading */
                  png_structp png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
                  if (!png_ptr)
                    {
                      printf ("%s: error creating read struct, skip\n", dir_entry[n]->d_name);
                      thumb_error++;
                      fclose (fp);
                      continue;
                    }
                  png_infop info_ptr = png_create_info_struct (png_ptr);
                  if (!info_ptr)
                    {
                      printf ("%s: error creating info struct, skip\n", dir_entry[n]->d_name);
                      thumb_error++;
                      fclose (fp);
                      continue;
                    }
                  png_infop end_info_ptr = png_create_info_struct (png_ptr);
                  if (!end_info_ptr)
                    {
                      printf ("%s: error creating end info struct, skip\n", dir_entry[n]->d_name);
                      thumb_error++;
                      fclose (fp);
                      continue;
                    }

                  png_init_io (png_ptr, fp);
                  png_set_sig_bytes (png_ptr, 8);

                  png_read_info (png_ptr, info_ptr);

                  /* read all PNG comments */
                  png_textp text_ptr;
                  int c = png_get_text (png_ptr, info_ptr, &text_ptr, NULL);

                  /* search for Thumb::URI tag in PNG comments */
                  while (--c)
                    {
                      if (strncmp (text_ptr[c].key, "Thumb::URI", 10) == 0)
                        {
                          /* process the file */
                          struct stat file_stat;
                          int orphan = (stat (uri_to_filename (text_ptr[c].text), &file_stat) == -1);

                          if (orphan)
                            {
                              struct stat thumb_stat;
                              stat (thumb_file, &thumb_stat);
                              total_bytes += thumb_stat.st_size;
                              thumb_orphan++;

                              if (show_orphan_only)
                                {
                                  printf ("%s\n", dir_entry[n]->d_name);
                                }
                              else if (delete_thumb)
                                {
                                  if (remove (thumb_file) == 0)
                                    {
                                      if (!quiet_mode)
                                        printf ("%s deleted.\n", dir_entry[n]->d_name);
                                    }
                                  else
                                    {  
                                      thumb_error++;
                                      if (!quiet_mode)
                                        printf ("%s cannot be deleted -- skip.\n", dir_entry[n]->d_name);
                                      perror ("remove");
                                    }
                                }
                              else
                                {
                                  printf ("Thumbnail: %s\n", dir_entry[n]->d_name);
                                  printf ("File:      %s\n", uri_to_filename (text_ptr[c].text));
                                  printf ("Orphan:    yes\n\n");
                                }
                            }
                          else
                            {
                              if (!show_orphan_only && !delete_thumb)
                                {
                                  printf ("Thumbnail: %s\n", dir_entry[n]->d_name);
                                  printf ("File:      %s\n", uri_to_filename (text_ptr[c].text));
                                  printf ("Orphan:    no\n\n");
                                }
                            }

                          /* exit search loop */
                          break;
                        }
                    }
                  png_destroy_read_struct (&png_ptr, &info_ptr, &end_info_ptr);
                }
              fclose (fp);
            }
          free(dir_entry[n]);
        }
      free(dir_entry);
      if (!(show_orphan_only || quiet_mode))
        {
          printf ("%d files total\n", file_total);
          printf ("%d thumbnail total\n", thumb_total);
          printf ("%d thumbnail orphan\n", thumb_orphan);
          printf ("%d thumbnail successfully processed\n", thumb_total - thumb_error);
          printf ("%llu byte recovered\n", total_bytes);
        }
    }
  return 0;
}

static char *
uri_to_filename (const char *uri)
{
  char *str = strdup (uri);
  char *filename = str;
  char *p = str;

  while (*str) 
    {
      if (*str == '%')
        {
          if (*++str)
            *p = hex (*str) * 16;
          if (*++str)
            *p++ += hex (*str);
        }
      else
        *p++ = *str;

      str++;
    }
  *p = 0;
  return filename + 7;
}

/* convert single hex character to integer */
static inline unsigned char
hex (char c)
{
  return c >= '0' && c <= '9' ? c - '0' 
         : c >= 'A' && c <= 'F' ? c - 'A' + 10 
         : c - 'a' + 10;
}

static void
help (const char *progname)
{
  printf ("Usage %-.32s [OPTION...]\n", progname);
  printf ("A program to find orphan GNOME thumbnails.\n");
  printf ("\n");
  printf ("  -d, --delete                    delete orphan thumbnail(s)\n");
  printf ("  -o, --orphan                    list only orphan thumbnail(s)\n");
  printf ("  -q, --quiet                     quiet mode, only if -d is specified\n");
  printf ("  -v, --version                   show version\n");
  printf ("  -h, --help                      print this help\n");
  printf ("\n");
  printf ("Report bugs to kitty@kitty.in.th\n");
  printf ("\n");
}

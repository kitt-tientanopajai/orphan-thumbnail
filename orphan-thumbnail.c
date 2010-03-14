/*
orphan-thumbnail.c

A program to find orphan GNOME thumbnails

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

* Sun, 14 Mar 2010 12:52:38 +0700
- Ascending sort
- Add -s for summary-only output

* Sun, 18 Oct 2009 02:31:46 +0700 - v.0.0.1
- Initial release

Todo
----
- Hash display
- Percent display
- Option to process only path specified

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <getopt.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libpng/png.h>

#define VERSION "0.0.1"

#define PNG_SIG_LEN 8

static int list_orphan_thumbnails (const char *);
static char *uri_to_filename (const char *);
static inline unsigned char hex (char);
static int file_exists (const char *);
static void help (const char *);

int delete_thumb = 0;
int quiet_delete_thumb = 0;
int show_orphan_only = 0;
int show_summary_only = 0;
int exclude_path = 0;
char exclude_path_name[PATH_MAX];

int
main (int argc, char *argv[])
{
  int opt;

  /* parse argument */
  static const struct option longopts[] = {
    {"delete",  no_argument,        NULL, 'd'},
    {"help",    no_argument,        NULL, 'h'},
    {"orphan",  no_argument,        NULL, 'o'},
    {"quiet",   no_argument,        NULL, 'q'},
    {"summary", no_argument,        NULL, 's'},
    {"version", no_argument,        NULL, 'v'},
    {"exclude", required_argument,  NULL, 'x'},
    {0, 0, 0, 0}
  };

  memset (exclude_path_name, '\0', sizeof exclude_path_name);
  while ((opt = getopt_long (argc, argv, "bdhoqsvx:", longopts, 0)) != -1)
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
          quiet_delete_thumb = 1;
          break;
        case 's':
          show_summary_only = 1;
          break;
        case 'x':
          exclude_path = 1;
          strncpy (exclude_path_name, optarg, (sizeof exclude_path_name) - 1);
          break;
        case 'v':
          printf ("%s %s\n", argv[0], VERSION);
          exit (EXIT_SUCCESS);
        default:
          help (argv[0]);
          exit (EXIT_FAILURE);
        }
    }

  if (!delete_thumb)
    quiet_delete_thumb = 0;

  if (show_orphan_only)
    show_summary_only = 0;

  process_orphan_thumbnails ("normal");
  process_orphan_thumbnails ("large");

  return 0;
}

int
process_orphan_thumbnails (const char *thumb_size)
{
  struct dirent **dir_entry;
  struct passwd *pwd;
  char thumb_dir[PATH_MAX];
  int n;

  /* find thumbnail directory */
  pwd = getpwuid (geteuid ());
  snprintf (thumb_dir, PATH_MAX, "%s/.thumbnails/%s/", pwd->pw_dir, thumb_size);
  if (!show_orphan_only && !quiet_delete_thumb)
    printf ("Thumbnail directory: %s\n", thumb_dir);

  if ((n = scandir (thumb_dir, &dir_entry, 0, alphasort)) < 0)
    {
      if (!show_orphan_only && !quiet_delete_thumb)
        {
          if (errno == ENOENT)
            printf ("%s not found -- skip\n", thumb_dir);
          else
            perror ("scan thumbnail directory");
        }

      return -1;
    }
  else
    {
      int file_total = n;       /* total files in the thumbnail directory (exclude . and ..) */
      int thumb_total = 0;      /* total thumbnail files */
      int thumb_error = 0;      /* total thumbnail error */
      int thumb_exclude = 0;    /* total thumbnail excluded */
      int thumb_orphan = 0;     /* total orphan thumbnail files */
      uint64_t total_bytes = 0; /* total bytes recovered */
      
      /* all files */
      int i;
      for (i = 0; i < n; i++)
        {
          if (show_summary_only)
            {
              printf ("\rProcessing file %d of %d (%.0f%%)", i + 1, n, (i + 1) * 100.0 / n);
              fflush (stdout);
            }
          /* only *.png */
          if (strcasestr (dir_entry[i]->d_name, ".png"))
            {
              char thumb_file[PATH_MAX];

              snprintf (thumb_file, PATH_MAX, "%s%s", thumb_dir, dir_entry[i]->d_name);

              FILE *fp = fopen (thumb_file, "rb");
              if (fp == NULL) 
                {
                  if (!show_orphan_only && !quiet_delete_thumb)
                    printf ("Error opening %s -- skip\n", dir_entry[i]->d_name);
                  continue;
                }

              /* check PNG signature */
              char header[PNG_SIG_LEN];
              if (fread (header, 1, PNG_SIG_LEN, fp) < PNG_SIG_LEN)
                {
                  /* fread error */
                  if (!show_orphan_only && !quiet_delete_thumb)
                    printf ("%s cannot be read -- skip\n", dir_entry[i]->d_name);
                  fclose (fp);
                  continue;
                }
              if (png_sig_cmp(header, 0, 8) != 0)
                {
                  /* PNG signature not found */
                  if (!show_orphan_only && !quiet_delete_thumb)
                    printf ("%s is not PNG -- skip\n", dir_entry[i]->d_name);
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
                      if (!show_orphan_only && !quiet_delete_thumb)
                        printf ("%s: error creating read struct -- skip\n", dir_entry[i]->d_name);
                      thumb_error++;
                      fclose (fp);
                      continue;
                    }
                  png_infop info_ptr = png_create_info_struct (png_ptr);
                  if (!info_ptr)
                    {
                      if (!show_orphan_only && !quiet_delete_thumb)
                        printf ("%s: error creating info struct -- skip\n", dir_entry[i]->d_name);
                      thumb_error++;
                      fclose (fp);
                      continue;
                    }
                  png_infop end_info_ptr = png_create_info_struct (png_ptr);
                  if (!end_info_ptr)
                    {
                      if (!show_orphan_only && !quiet_delete_thumb)
                        printf ("%s: error creating end info struct -- skip\n", dir_entry[i]->d_name);
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
                          char *filename = uri_to_filename (text_ptr[c].text);
                          struct stat file_stat;
                          int orphan = (stat (filename, &file_stat) == -1);

                          if (exclude_path && (strncmp (filename, exclude_path_name, strlen (exclude_path_name)) == 0))
                            {
                              thumb_exclude++;
                              free (filename);
                              break;
                            }

                          if (orphan)
                            {
                              struct stat thumb_stat;
                              stat (thumb_file, &thumb_stat);
                              total_bytes += thumb_stat.st_size;
                              thumb_orphan++;

                              if (show_orphan_only)
                                {
                                  printf ("%s\n", dir_entry[i]->d_name);
                                }
                              else if (delete_thumb)
                                {
                                  if (remove (thumb_file) == 0)
                                    {
                                      if (!quiet_delete_thumb)
                                        printf ("%s deleted.\n", dir_entry[i]->d_name);
                                    }
                                  else
                                    {  
                                      thumb_error++;
                                      if (!quiet_delete_thumb)
                                        printf ("%s cannot be deleted -- skip.\n", dir_entry[i]->d_name);

                                      perror ("unable to delete");
                                    }
                                }
                              else if (!show_summary_only)
                                {
                                  printf ("Thumbnail: %s\n", dir_entry[i]->d_name);
                                  printf ("File:      %s\n", filename);
                                  printf ("Orphan:    yes\n\n");
                                }
                            }
                          else
                            {
                              if (!show_orphan_only && !show_summary_only && !delete_thumb) 
                                {
                                  printf ("Thumbnail: %s\n", dir_entry[i]->d_name);
                                  printf ("File:      %s\n", filename);
                                  printf ("Orphan:    no\n\n");
                                }
                            }

                          free (filename);
                          /* exit search loop */
                          break;
                        }
                    }
                  png_destroy_read_struct (&png_ptr, &info_ptr, &end_info_ptr);
                }
              fclose (fp);
            }
          free(dir_entry[i]);
        }
      free(dir_entry);

      if (!(show_orphan_only || quiet_delete_thumb))
        {
          printf ("\n%d files total\n", file_total);
          printf ("%d thumbnails total\n", thumb_total);
          printf ("%d thumbnails excluded\n", thumb_exclude);
          printf ("%d thumbnails orphan\n", thumb_orphan);
          printf ("%d thumbnails successfully processed\n", thumb_total - thumb_error);
          printf ("%llu bytes recovered\n\n", total_bytes);
        }
    }
  return 0;
}

static char *
uri_to_filename (const char *uri)
{
  char *filename = strdup (uri + 7);
  char *str = filename;
  char *p = filename;

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
  
  return filename;
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
  printf ("  -x, --exclude=PATH              do not list/delete thumbnails of files in PATH\n");
  printf ("  -v, --version                   show version\n");
  printf ("  -h, --help                      print this help\n");
  printf ("\n");
  printf ("Report bugs to kitty@kitty.in.th\n");
  printf ("\n");
}

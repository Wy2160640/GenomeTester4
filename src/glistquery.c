/*
 * GenomeTester4
 *
 * A toolkit for creating and manipulating k-mer lists from biological sequences
 * 
 * Copyright (C) 2014 University of Tartu
 *
 * Authors: Maarja Lepamets and Lauris Kaplinski
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

#define MAX_LISTS 1024

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "common.h"
#include "file-array.h"
#include "utils.h"
#include "sequence.h"
#include "sequence-stream.h"
#include "fasta.h"
#include "version.h"
#include "index-map.h"
#include "word-list-stream.h"
#include "word-map.h"
#include "word-table.h"

typedef struct _querystructure {
  GT4WordMap *map;
  parameters *p;
  unsigned int minfreq;
  unsigned int maxfreq;
  int printall;
} querystructure;

void search_one_query_string (GT4WordMap *map, const char *querystring, parameters *p, unsigned int minfreq, unsigned int maxfreq, int printall);
int search_n_query_strings (GT4WordMap *map, const char *queryfile, parameters *p, unsigned int minfreq, unsigned int maxfreq, int printall);
int search_fasta (GT4WordMap *map, const char *seqfilename, parameters *p, unsigned int minfreq, unsigned int maxfreq, int printall);
int search_list (GT4WordMap *map, const char *querylistfilename, parameters *p, unsigned int minfreq, unsigned int maxfreq, int printall);
unsigned int search_lists_multi (const char *list, const char *lists[], unsigned int n_lists);
int process_word (GT4FastaReader *reader, unsigned long long word, void *data);
int print_full_map (AZObject *obj);
void get_statistics (AZObject *obj);
void print_median (AZObject *obj);
void print_distro (GT4WordMap *map, unsigned int size);
void print_gc (GT4WordMap *map);
static int print_files (AZObject *obj);
void print_help (int exitvalue);

int debug = 0;

unsigned int use_scouts = 1;

int main (int argc, const char *argv[])
{
  int argidx, v = 0;
  unsigned int n_lists = 0;
  const char *lists[MAX_LISTS];
  const char *querystring = NULL, *queryfilename = NULL, *seqfilename = NULL, *querylistfilename = NULL;
  parameters p = {0};
  char *end;
  int printall = 0, getstat = 0, getmed = 0;
  unsigned int minfreq = 0, maxfreq = UINT_MAX;
  unsigned int distro = 0;
  unsigned int gc = 0;
  AZObject *obj = NULL;
  GT4WordMap *map = NULL;
  
  for (argidx = 1; argidx < argc; argidx++) {
    if (!strcmp (argv[argidx], "-v") || !strcmp (argv[argidx], "--version")) {
      fprintf (stdout, "glistquery version %u.%u.%u (%s)\n", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, VERSION_QUALIFIER);
      return 0;

    } else if (!strcmp (argv[argidx], "-h") || !strcmp (argv[argidx], "--help") || !strcmp (argv[argidx], "-?")) {
      print_help (0);

    } else if (!strcmp(argv[argidx], "-s") || !strcmp(argv[argidx], "--seqfile")) {
      if (!argv[argidx + 1] || argv[argidx + 1][0] == '-') {
        fprintf(stderr, "Warning: No sequence file name specified!\n");
        argidx += 1;
        continue;
      }
      seqfilename = argv[argidx + 1];
      argidx += 1;
    } else if (!strcmp(argv[argidx], "-l") || !strcmp(argv[argidx], "--listfile")) {
      if (!argv[argidx + 1] || argv[argidx + 1][0] == '-') {
        fprintf(stderr, "Warning: No query list file name specified!\n");
        argidx += 1;
        continue;
      }
      querylistfilename = argv[argidx + 1];
      argidx += 1;
    } else if (!strcmp(argv[argidx], "-f") || !strcmp(argv[argidx], "--queryfile")) {
      if (!argv[argidx + 1] || argv[argidx + 1][0] == '-') {
        fprintf(stderr, "Warning: No query file name specified!\n");
        argidx += 1;
        continue;
      }
      queryfilename = argv[argidx + 1];
      argidx += 1;
    } else if (!strcmp(argv[argidx], "-q") || !strcmp(argv[argidx], "--query")) {
      if (!argv[argidx + 1] || argv[argidx + 1][0] == '-') {
        fprintf(stderr, "Warning: No query specified!\n");
        argidx += 1;
        continue;
      }
      querystring = argv[argidx + 1];
      argidx += 1;
    } else if (!strcmp(argv[argidx], "-p") || !strcmp(argv[argidx], "--perfectmatch")) {
      if (!argv[argidx + 1]) {
        fprintf(stderr, "Warning: No number of 3 prime perfect matches specified! Using the default value: %d.\n", p.pm3);
        argidx += 1;
        continue;
      }
      p.pm3 = strtol (argv[argidx + 1], &end, 10);
      if (*end != 0) {
        fprintf(stderr, "Error: Invalid number of 3 prime perfect matches: %s! Must be an integer.\n", argv[argidx + 1]);
        print_help (1);
      }
      argidx += 1;
    } else if (!strcmp(argv[argidx], "-mm") || !strcmp(argv[argidx], "--mismatch")) {
      if (!argv[argidx + 1]) {
        fprintf(stderr, "Warning: No number of mismatches specified! Using the default value: %d.\n", p.nmm);
        argidx += 1;
        continue;
      }
      p.nmm = strtol (argv[argidx + 1], &end, 10);
      if (*end != 0) {
        fprintf(stderr, "Error: Invalid number of mismatches: %s! Must be an integer.\n", argv[argidx + 1]);
        print_help (1);
      }
      argidx += 1;
    } else if (!strcmp(argv[argidx], "-min") || !strcmp(argv[argidx], "--minfreq")) { 
      if (!argv[argidx + 1]) {
        fprintf(stderr, "Warning: No minimum frequency specified! Using the default value: %d.\n", minfreq);
        argidx += 1;
        continue;
      }
      minfreq = strtol (argv[argidx + 1], &end, 10);
      if (*end != 0) {
        fprintf(stderr, "Error: Invalid minimum frequency: %s! Must be a positive integer.\n", argv[argidx + 1]);
        print_help (1);
      }
      argidx += 1;
    } else if (!strcmp(argv[argidx], "-max") || !strcmp(argv[argidx], "--maxfreq")) {
      if (!argv[argidx + 1]) {
        fprintf(stderr, "Warning: No maximum frequency specified! Using the default value: %d.\n", maxfreq);
        argidx += 1;
        continue;
      }
      maxfreq = strtol (argv[argidx + 1], &end, 10);
      if (*end != 0) {
        fprintf(stderr, "Error: Invalid maximum frequency: %s! Must be a positive integer.\n", argv[argidx + 1]);
        print_help (1);
      }
      argidx += 1;
    } else if (!strcmp(argv[argidx], "-D")) {
      debug += 1;
    } else if (!strcmp(argv[argidx], "-all")) {
      printall = 1;
    } else if (!strcmp(argv[argidx], "-stat")) {  
      getstat = 1;
    } else if (!strcmp(argv[argidx], "-median")) {
      getmed = 1;
    } else if (!strcmp(argv[argidx], "-distribution")) {
      if ((argidx + 1) >= argc) {
        print_help (1);
      }
      argidx += 1;
      distro = strtol (argv[argidx], &end, 10);;
    } else if (!strcmp(argv[argidx], "-gc")) {
      gc = 1;
    } else if (!strcmp(argv[argidx], "--disable_scouts")) {  
      use_scouts = 0;
    } else if (argv[argidx][0] != '-') {
      lists[n_lists++] = argv[argidx];
    } else {
      fprintf(stderr, "Error: Unknown argument: %s!\n", argv[argidx]);
      print_help (1);
    }
  }
  
  if (!n_lists) {
    fprintf(stderr, "Error: Missing a list file!\n");
    print_help (1);      
  }

  /* checking the parameters */
  if (p.nmm < 0) {
    fprintf(stderr, "Error: Invalid number of mismatches: %d! Must be between 0 and the length of the query.\n", p.nmm);
    print_help (1);
  }
  if (p.pm3 < 0) {
    fprintf(stderr, "Error: Invalid number of 3 prime perfect matches: %d! Must be between 0 and the length of the query.\n", p.pm3);
    print_help (1);
  }
  if (minfreq < 0) {
    fprintf(stderr, "Error: Invalid number of minimum frequency: %d! Must be positive integer.\n", minfreq);
    print_help (1);    
  }
  if (maxfreq < 0) {
    fprintf(stderr, "Error: Invalid number of maximum frequency: %d! Must be positive integer.\n", maxfreq);
    print_help (1);    
  }

  if (querylistfilename && n_lists > 1) {
    unsigned int result = search_lists_multi (querylistfilename, lists, n_lists);
    exit (result);
  }

  FILE *ifs;
  unsigned int code;
  ifs = fopen (lists[0], "r");
  fread (&code, 4, 1, ifs);
  fclose (ifs);
  if (code == GT4_LIST_CODE) {
    obj = (AZObject *) gt4_word_map_new (lists[0], VERSION_MAJOR, !getstat && use_scouts);
    map = GT4_WORD_MAP(obj);
  } else if (code == GT4_INDEX_CODE) {
    obj = (AZObject *) gt4_index_map_new (lists[0], VERSION_MAJOR, !getstat && use_scouts);
    if (debug) print_files (obj);
  }
  if (getstat) {
    get_statistics (obj);
    exit (0);
  } else if (getmed) {
    print_median (obj);
    exit (0);
  }

  if (distro) {
    print_distro (map, distro + 1);
    exit (0);
  }
  
  if (gc) {
    print_gc (map);
    exit (0);
  }

  if (!seqfilename && !querylistfilename && !queryfilename && !querystring) {
          print_full_map (obj);
          exit (0);
  }

  if (!map) {
    fprintf (stderr, "Error: Could not make wordmap from file %s!\n", lists[0]);
    return 1;
  }
  
  p.wordlength = map->header->wordlength;

  /* glistquery options */
  if (seqfilename) { /* fasta input */
    search_fasta (map, seqfilename, &p, minfreq, maxfreq, printall);
  } else if (querylistfilename) { /* list input */  
    search_list (map, querylistfilename, &p, minfreq, maxfreq, printall);
  } else if (queryfilename) { /* list of queries */
    v = search_n_query_strings (map, queryfilename, &p, minfreq, maxfreq, printall);
    if (v) return v;
  } else if (querystring) { /* one query */
    /* checking possible errors */
    if (p.wordlength != strlen (querystring)) {
      fprintf (stderr, "Error: Incompatible wordlengths! Wordlength in list: %u, query length: %lu\n", p.wordlength, strlen (querystring));
      return 1;
    }
    if (p.wordlength - p.pm3 < p.nmm) {
      fprintf(stderr, "Error: Number or mismatches specified is too large for %s with %d nucleotides long 3 prime perfect match.\n", querystring, p.pm3);
      return 1;
    }
    search_one_query_string (map, querystring, &p, 0, UINT_MAX, printall);
  }
  exit (0);
}

static int
print_files (AZObject *obj)
{
        GT4FileArrayImplementation *impl;
        GT4FileArrayInstance *inst;
        unsigned int i;
        impl = (GT4FileArrayImplementation *) az_object_get_interface (obj, GT4_TYPE_FILE_ARRAY, (void **) &inst);
        for (i = 0; i < inst->num_files; i++) {
                gt4_file_array_get_file (impl, inst, i);
                fprintf (stderr, "%u\t%s\t%llu\t%llu\n", i, inst->file_name, inst->file_size, inst->n_sequences);
        }
        return 0;
}

/* Print the whole list */
int
print_full_map (AZObject *obj)
{
        GT4WordSListImplementation *impl;
        GT4WordSListInstance *inst;
        impl = (GT4WordSListImplementation *) az_object_get_interface (obj, GT4_TYPE_WORD_SARRAY, (void **) &inst);
        gt4_word_slist_get_first_word (impl, inst);
        while (inst->idx < inst->num_words) {
                char b[64];
                word2string (b, inst->word, inst->word_length);
                fprintf (stdout, "%s\t%u\n", b, inst->count);
                gt4_word_slist_get_next_word (impl, inst);
        }
  return 0;
}

/* use one specific query sequence */
void search_one_query_string (GT4WordMap *map, const char *querystring, parameters *p, unsigned int minfreq, unsigned int maxfreq, int printall)
{
  unsigned int freq = 0;
  unsigned long long query;

  query = string_to_word (querystring, p->wordlength);
  freq = word_map_search_query (map, query, p, printall, 0, 0, NULL);
  if (!printall && freq >= minfreq && freq <= maxfreq) fprintf (stdout, "%s\t%u\n", querystring, freq);
  return;
}

/* use file which consist of many queries */
int search_n_query_strings (GT4WordMap *map, const char *queryfile, parameters *p, unsigned int minfreq, unsigned int maxfreq, int printall)
{
  FILE *f;
  char querystring[256];
  int beg = 1;

  f = fopen (queryfile, "r");
  if (f == NULL) {
    fprintf (stderr, "Error: Cannot open file %s.\n", queryfile);
    return 1;
  }

  while (fscanf (f, "%s\n", querystring) != EOF) {
    if (beg) {
      /* checking possible errors */
      if (p->wordlength != strlen (querystring)) {
        fprintf (stderr, "Error: Incompatible wordlengths! Wordlength in list: %u, query length: %lu\n", p->wordlength, strlen (querystring));
        return 1;
      }
      if (p->wordlength - p->pm3 < p->nmm) {
        fprintf (stderr, "Error: Number or mismatches specified is too large for %s with %d nucleotides long 3 prime perfect match.\n", querystring, p->pm3);
        return 1;
      }
      beg = 0;
    }
    search_one_query_string (map, querystring, p, minfreq, maxfreq, printall);
  }
  return 0;
}

int
search_fasta (GT4WordMap *map, const char *seqfilename, parameters *p, unsigned int minfreq, unsigned int maxfreq, int printall)
{
        GT4SequenceStream *stream;
  querystructure qs = {0};
  int result;
  GT4FastaReader r;

  qs.map = map;
  qs.p = p;
  qs.minfreq = minfreq;
  qs.maxfreq = maxfreq;
  qs.printall = printall;

  stream = gt4_sequence_stream_new (seqfilename);
  fasta_reader_init (&r, p->wordlength, 0, GT4_SEQUENCE_STREAM_SEQUENCE_SOURCE_IMPLEMENTATION(stream), &stream->source_instance);

  result = fasta_reader_read_nwords (&r, 1000000000000ULL, NULL, NULL, NULL, NULL, process_word, (void *) &qs);
  /* v = fasta_reader_read_nwords (ff, size, p->wordlength, (void *) &qs, 0, 0, NULL, NULL, process_word); */
  fasta_reader_release (&r);

  az_object_unref (AZ_OBJECT (stream));

  return result;
}

int search_list (GT4WordMap *map, const char *querylistfilename, parameters *p, unsigned int minfreq, unsigned int maxfreq, int printall)
{
  GT4WordMap *qmap;
  unsigned long long i, word = 0L;
  unsigned int freq;
  
  qmap = gt4_word_map_new (querylistfilename, VERSION_MAJOR, use_scouts);

  if (map->header->wordlength != qmap->header->wordlength) return GT_INCOMPATIBLE_WORDLENGTH_ERROR;
  
  for (i = 0; i < qmap->header->nwords; i++) {
    word = *((unsigned long long *) (qmap->wordlist + i * (sizeof (unsigned long long) + sizeof (unsigned))));
    freq = word_map_search_query (map, word, p, printall, 0, 0, NULL);
    if (!printall && freq >= minfreq && freq <= maxfreq) fprintf (stdout, "%s\t%u\n", word_to_string (word, map->header->wordlength), freq);
  }
  return 0;
}

unsigned int
search_lists_multi (const char *list, const char *lists[], unsigned int n_lists)
{
  AZObject *maps[MAX_LISTS + 1];
  GT4WordSListImplementation *impls[MAX_LISTS + 1];
  GT4WordSListInstance *insts[MAX_LISTS + 1];
  unsigned int i;

  maps[0] = (AZObject *) gt4_word_list_stream_new (list, VERSION_MAJOR);
  if (!maps[0]) exit (1);
  impls[0] = (GT4WordSListImplementation *) az_object_get_interface (AZ_OBJECT(maps[0]), GT4_TYPE_WORD_SLIST, (void **) &insts[0]);
  if (!impls[0]) exit (1);
  gt4_word_slist_get_first_word (impls[0], insts[0]);
  for (i = 0; i < n_lists; i++) {
    maps[i + 1] = (AZObject *) gt4_word_list_stream_new (lists[i], VERSION_MAJOR);
    if (!maps[i + 1]) exit (1);
    impls[i + 1] = (GT4WordSListImplementation *) az_object_get_interface (AZ_OBJECT(maps[i + 1]), GT4_TYPE_WORD_SLIST, (void **) &insts[i + 1]);
    if (!impls[i + 1]) exit (1);
    gt4_word_slist_get_first_word (impls[i + 1], insts[i + 1]);
  }

  while (insts[0]->idx < insts[0]->num_words) {
    unsigned long long word = insts[0]->word;
    unsigned int printed = 0;
    for (i = 1; i <= n_lists; i++) {
      while ((insts[i]->idx < insts[i]->num_words) && (insts[i]->word < word)) {
        if (!gt4_word_slist_get_next_word (impls[i], insts[i])) break;
      }
      if ((insts[i]->idx < insts[i]->num_words) && (insts[i]->word == word)) {
        if (!printed) {
          char b[64];
          word2string (b, word, insts[0]->word_length);
          fprintf (stdout, "%s", b);
          printed = 1;
        }
        fprintf (stdout, "\t%u:%u", i - 1, insts[i]->count);
      }
    }
    if (printed) fprintf (stdout, "\n");
    gt4_word_slist_get_next_word (impls[0], insts[0]);
  }

  return 0;
}

int process_word (GT4FastaReader *reader, unsigned long long word, void *data)
{
  unsigned int freq = 0;
  querystructure *qs = (querystructure *) data;
  freq = word_map_search_query (qs->map, word, qs->p, qs->printall, 0, 0, NULL);
  if (!qs->printall && freq >= qs->minfreq && freq <= qs->maxfreq) fprintf (stdout, "%s\t%u\n", word_to_string (word, reader->wordlength), freq);
  return 0;
}

void get_statistics (AZObject *obj)
{
        GT4WordSListImplementation *impl;
        GT4WordSListInstance *inst;
        impl = (GT4WordSListImplementation *) az_object_get_interface (obj, GT4_TYPE_WORD_SLIST, (void **) &inst);
        if (GT4_IS_WORD_MAP (obj)) {
                GT4WordMap *map = GT4_WORD_MAP(obj);
          fprintf (stdout, "Statistics of list file %s <<Built with glistmaker version %d.%d>>\n", map->filename, map->header->version_major, map->header->version_minor);
        } else if (GT4_IS_INDEX_MAP (obj)) {
                GT4IndexMap *imap = GT4_INDEX_MAP(obj);
                fprintf (stdout, "Statistics of index file %s <<Built with glistmaker version %d.%d>>\n", imap->filename, imap->header->version_major, imap->header->version_minor);
        }
  fprintf (stdout, "Wordlength\t%u\n", inst->word_length);
  fprintf (stdout, "NUnique\t%llu\n", inst->num_words);
  fprintf (stdout, "NTotal\t%llu\n", inst->sum_counts);
  return;
}

void print_median (AZObject *obj)
{
  unsigned int min, max, med, gmin, gmax;
  unsigned long long i;
        GT4WordSArrayImplementation *impl;
        GT4WordSArrayInstance *inst;
        impl = (GT4WordSArrayImplementation *) az_object_get_interface (obj, GT4_TYPE_WORD_SARRAY, (void **) &inst);
  gmin = 0xffffffff;
  gmax = 0;
  if (debug > 0) fprintf (stderr, "Finding min/max...");
  for (i = 0; i < inst->slist_inst.num_words; i++) {
          gt4_word_sarray_get_word (impl, inst, i);
    if (inst->slist_inst.count < gmin) gmin = inst->slist_inst.count;
    if (inst->slist_inst.count > gmax) gmax = inst->slist_inst.count;
  }
  if (debug > 0) fprintf (stderr, "done (%u %u)\n", gmin, gmax);
  min = gmin;
  max = gmax;
  med = (unsigned int) (((unsigned long long) min + max) / 2);
  while (max > min) {
    unsigned long long above = 0, below = 0, equal;
    for (i = 0; i < inst->slist_inst.num_words; i++) {
            gt4_word_sarray_get_word (impl, inst, i);
      if (inst->slist_inst.count > med) above += 1;
      if (inst->slist_inst.count < med) below += 1;
    }
    equal = inst->slist_inst.num_words - above - below;
    if (debug > 0) fprintf (stderr, "Trying median %u - equal %llu, below %llu, above %llu\n", med, equal, below, above);
    /* Special case: min == med, max == med + 1 */
    if (max == (min + 1)) {
      if (above > (below + equal)) {
        /* Max is true median */
        med = max;
      }
      break;
    }
    if (above > below) {
      if ((above - below) < equal) break;
      min = med;
    } else if (below > above) {
      if ((below - above) < equal) break;
      max = med;
    } else {
      break;
    }
    med = (min + max) / 2;
  }
  
        if (GT4_IS_WORD_MAP (obj)) {
                GT4WordMap *map = GT4_WORD_MAP(obj);
          fprintf (stdout, "Statistics of list file %s <<Built with glistmaker version %d.%d>>\n", map->filename, map->header->version_major, map->header->version_minor);
        } else if (GT4_IS_INDEX_MAP (obj)) {
                GT4IndexMap *imap = GT4_INDEX_MAP(obj);
                fprintf (stdout, "Statistics of index file %s <<Built with glistmaker version %d.%d>>\n", imap->filename, imap->header->version_major, imap->header->version_minor);
        }
  fprintf (stdout, "Wordlength\t%u\n", inst->slist_inst.word_length);
  fprintf (stdout, "NUnique\t%llu\n", inst->slist_inst.num_words);
  fprintf (stdout, "NTotal\t%llu\n", inst->slist_inst.sum_counts);
  fprintf (stdout, "Min %u Max %u Median %u Average %.2f\n", gmin, gmax, med, (double) inst->slist_inst.sum_counts / inst->slist_inst.num_words);
}

void
print_distro (GT4WordMap *map, unsigned int max)
{
  unsigned long long i;
  unsigned long long *d = (unsigned long long *) malloc (max * 8);
  memset (d, 0, max * 8);
  for (i = 0; i < map->header->nwords; i++) {
    unsigned int freq = WORDMAP_FREQ (map, i);
    if (freq < max) d[freq] += 1;
  }
  for (i = 0; i < max; i++) {
    fprintf (stdout, "%llu\t%llu\n", i, d[i]);
  }
}

void
print_gc (GT4WordMap *map)
{
  unsigned long long i;
  unsigned long long count = 0;
  for (i = 0; i < map->header->nwords; i++) {
    unsigned long long word = WORDMAP_WORD (map, i);
    unsigned int freq = WORDMAP_FREQ (map, i);
    unsigned int j;
    for (j = 0; j < map->header->wordlength; j++) {
      /* if (((word & 3) == 1) || ((word & 3) == 2)) count += freq; */
      count += freq * ((word ^ (word >> 1)) & 1);
      word = word >> 2;
    }
  }
  printf ("GC\t%g\n", (double) count / (map->header->totalfreq * map->header->wordlength));
}

void print_help (int exit_value)
{
    fprintf (stderr, "glistquery version %u.%u.%u (%s)\n", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, VERSION_QUALIFIER);
  fprintf (stderr, "Usage: glistquery <INPUTLIST> [OPTIONS]\n");
  fprintf (stderr, "Options:\n");
  fprintf (stderr, "    -v, --version             - print version information and exit\n");
  fprintf (stderr, "    -h, --help                - print this usage screen and exit\n");
  fprintf (stderr, "    -stat                     - print statistics of the list file and exit\n");
  fprintf (stderr, "    -median                   - print min/max/median/average and exit\n");
  fprintf (stderr, "    -distribution MAX         - print distribution up to MAX\n");
  fprintf (stderr, "    -gc                       - print average GC content of all words\n");
  fprintf (stderr, "    -q, --query               - single query word\n");
  fprintf (stderr, "    -f, --queryfile           - list of query words in a file\n");
  fprintf (stderr, "    -s, --seqfile             - FastA/FastQ file\n");
  fprintf (stderr, "    -l, --listfile            - list file made by glistmaker\n");
  fprintf (stderr, "    -mm, --mismatch NUMBER    - specify number of mismatches (default 0)\n");
  fprintf (stderr, "    -p, --perfectmatch NUMBER - specify number of 3' perfect matches (default 0)\n");
  fprintf (stderr, "    -min, --minfreq NUMBER    - minimum frequency of the printed words (default 0)\n");
  fprintf (stderr, "    -max, --maxfreq NUMBER    - maximum frequency of the printed words (default MAX_UINT)\n");
  fprintf (stderr, "    -all                      - in case of mismatches prints all found words\n");
  fprintf (stderr, "    -D                        - increase debug level\n");
  exit (exit_value);
}



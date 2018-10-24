#define __GT4_WORD_TABLE_C__

/*
 * GenomeTester4
 *
 * A toolkit for creating and manipulating k-mer lists from biological sequences
 * 
 * Cpyright (C) 2014 University of Tartu
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

#include <stdlib.h>
#include <string.h>

#include "sequence.h"
#include "common.h"
#include "utils.h"
#include "version.h"
#include "word-list.h"

#include "word-table.h"

unsigned int debug_tables = 0;
unsigned long long total_memory = 0;

GT4WordTable *
gt4_word_table_new (unsigned int wordlength, unsigned long long size)
{
  GT4WordTable *table = (GT4WordTable *) malloc (sizeof (GT4WordTable));
  if (!table) return NULL;
  memset (table, 0, sizeof (GT4WordTable));
  table->wordlength = wordlength;
  table->data_size = 4;
  if (gt4_word_table_ensure_size (table, size)) {
    gt4_word_table_delete (table);
    return NULL;
  }
  return table;
}

void 
gt4_word_table_delete (GT4WordTable *table)
{
  if (debug_tables) {
    unsigned long long size =  table->n_word_slots * 8 + table->n_data_slots * table->data_size;
    fprintf (stderr, "wordtable_delete: Releasing %llu total %.2fG\n", (unsigned long long) size, (double) total_memory / 1073741824.0);
    total_memory -= size;
  }
  free (table->words);
  if (table->data) free (table->data);
  free (table);
}

void 
gt4_word_table_clear (GT4WordTable *table)
{
  table->wordlength = 0;
  table->n_words = 0L;
}

int 
gt4_word_table_ensure_size (GT4WordTable *table, unsigned long long size)
{
	if (table->n_word_slots < size) {
		table->n_word_slots = size;
		table->words = (unsigned long long *) realloc (table->words, table->n_word_slots * sizeof (unsigned long long));
		if (!table->words) return GT_OUT_OF_MEMORY_ERROR;
	}
	if (table->data) {
		return gt4_word_table_ensure_data_size (table, size);
	}
	return 0;
}

int
gt4_word_table_ensure_data_size (GT4WordTable *table, unsigned long long size)
{
	if (table->n_data_slots < size) {
		table->n_data_slots = size;
		table->data = (unsigned char *) realloc (table->data, table->n_data_slots * table->data_size);
		if (!table->data) return GT_OUT_OF_MEMORY_ERROR;
	}
	return 0;
}

#define WORDTABLE_MIN_SIZE 20000000

static int 
wordtable_enlarge (GT4WordTable *table)
{
	unsigned long long nslots;
	int v;
	if (table->n_word_slots < WORDTABLE_MIN_SIZE && table->n_data_slots < WORDTABLE_MIN_SIZE) {
		nslots = WORDTABLE_MIN_SIZE;
	} else {
		nslots = (table->n_word_slots > table->n_data_slots ? table->n_word_slots : table->n_data_slots) * 2;
	}
	v = gt4_word_table_ensure_size (table, nslots);
	if (v) return v;
	return 0;
}

static int 
wordtable_enlarge_nofreq (GT4WordTable *table)
{
	unsigned long long nslots;
	int v;
	if (table->n_word_slots < 10000000 && table->n_data_slots < 10000000) {
		nslots = 10000000;
	} else {
		nslots = (table->n_word_slots > table->n_data_slots ? table->n_word_slots : table->n_data_slots) * 2;
	}
	v = gt4_word_table_ensure_size (table, nslots);
	if (v) return v;
	return 0;
}

int 
gt4_word_table_add_word (GT4WordTable *table, unsigned long long word, unsigned int freq)
{
	unsigned int *freqs = (unsigned int *) table->data;
	if (table->n_words == table->n_data_slots || table->n_words == table->n_word_slots) {
		int v;
		v = wordtable_enlarge (table);
		if (v > 0) return v;
	}
	table->words[table->n_words] = word;
	freqs[table->n_words] = freq;
	table->n_words += 1;
	return 0;
}

int 
gt4_word_table_add_word_nofreq (GT4WordTable *table, unsigned long long word)
{
	if (table->n_words >= table->n_word_slots) {
		int v;
		v = wordtable_enlarge_nofreq (table);
		if (v > 0) return v;
	}
	table->words[table->n_words] = word;
	table->n_words += 1;
	return 0;
}

int 
wordtable_merge (GT4WordTable *table, GT4WordTable *other)
{
	long long i = 0L, j = 0L, k;
	unsigned long long nnew, incr, nequals;
	int v;
	unsigned int *t_freqs = (unsigned int *) table->data;
	unsigned int *o_freqs = (unsigned int *) other->data;

	if (table->wordlength != other->wordlength) return GT_INCOMPATIBLE_WORDLENGTH_ERROR;

	nequals = 0L;
	nnew = 0L;
	while ((unsigned long long) j < other->n_words && (unsigned long long) i < table->n_words) {

		if (table->words[i] == other->words[j]) {
			t_freqs[i] += o_freqs[j];
			i += 1;
			j += 1;
			nequals += 1;
		} else if (table->words[i] < other->words[j]) {
			i += 1;
		} else {
			j += 1;
		}
	}
	nnew = other->n_words - nequals;
	/* if (nnew == 0) return 0; */
	incr = nnew;
	if ((table->n_words + incr) > table->n_word_slots) {
		unsigned long long step = (table->n_word_slots + 7) >> 3;
		if (incr < step) incr = step;
	}

	v = gt4_word_table_ensure_size (table, table->n_words + incr);
	if (v > 0) return v;

	i = table->n_words - 1;
	j = other->n_words - 1;

	for (k = table->n_words + nnew - 1; k >= 0; k--) {

		if (j >= 0 && i >= 0 && table->words[i] == other->words[j]) {
			table->words[k] = table->words[i];
			t_freqs[k] = t_freqs[i];
			i -= 1;
			j -= 1;
		} else if ((j < 0) || (i >= 0 && table->words[i] > other->words[j])) {
			table->words[k] = table->words[i];
			t_freqs[k] = t_freqs[i];
			i -= 1;
		} else {
			table->words[k] = other->words[j];
			t_freqs[k] = o_freqs[j];
			j -= 1;
		}
	}

	table->n_words += nnew;
	return 0;
}

void 
wordtable_sort (GT4WordTable *table, int sortfreqs)
{
	unsigned int firstshift = 0;
	if (table->n_words == 0) return;

	/* calculate the number of shifted positions for making radix sort faster (no need to sort digits that are all zeros)*/
	while (firstshift + 8 < table->wordlength * 2) {
		firstshift += 8;
	}

	if (sortfreqs) {
		hybridInPlaceRadixSort256 (table->words, table->words + table->n_words, table->data, table->data_size, firstshift);
		return;
	}
	hybridInPlaceRadixSort256 (table->words, table->words + table->n_words, NULL, 0, firstshift);
	return;
}

int 
wordtable_find_frequencies (GT4WordTable *table)
{
	unsigned long long ri, wi, count;
	unsigned long long nunique;
	int v;
	unsigned int *freqs = (unsigned int *) table->data;

	wi = 0;
	count = 1;
	if (table->n_words == 0) return 0;

	nunique = wordtable_count_unique (table);
	v = gt4_word_table_ensure_data_size (table, nunique);
	if (v > 0) return v;

	for (ri = 1; ri < table->n_words; ri++) {
		if (table->words[ri] == table->words[ri - 1]) {
			count += 1;
		} else {
			table->words[wi] = table->words[ri - 1];
			freqs[wi] = count;
			count = 1;
			wi += 1;
		}
	}

	table->words[wi] = table->words[ri - 1];
	freqs[wi] = count;
	table->n_words = wi + 1;
	return 0;
}

void 
wordtable_merge_freqs (GT4WordTable *table)
{
	unsigned long long ri, wi, count;
	unsigned int *freqs = (unsigned int *) table->data;

	wi = 0;
	if (table->n_words == 0) return;

	count = freqs[0];
	for (ri = 1; ri < table->n_words; ri++) {
		if (table->words[ri] == table->words[ri - 1]) {
			count += freqs[ri];
		} else {
			table->words[wi] = table->words[ri - 1];
			freqs[wi] = count;
			count = freqs[ri];
			wi += 1;
		}
	}

	table->words[wi] = table->words[ri - 1];
	freqs[wi] = count;
	table->n_words = wi + 1;
	return;
}

unsigned long long 
wordtable_count_unique (GT4WordTable *table)
{
	unsigned long long i, count;
	count = 0;
	for (i = 0; i < table->n_words; i++) {
		if (i == 0 || table->words[i] != table->words[i - 1]) {
			count += 1;
		}
	}
	return count;
}

#define BSIZE 10000

unsigned int
wordtable_write_to_file (GT4WordTable *table, const char *outputname, unsigned int cutoff)
{
	unsigned long long i, count, totalfreq;
	char fname[256]; /* the length of the output name is limited and checked in main(..) method */
	FILE *f;
	GT4ListHeader h;
	char b[BSIZE + 12];
	unsigned int bp;
	unsigned int *freqs = (unsigned int *) table->data;
	if (table->n_words == 0) return 0;

	memset (&h, 0, sizeof (GT4ListHeader));

	sprintf (fname, "%s_%d.list", outputname, table->wordlength);
	f = fopen (fname, "w");
	if (!f) {
		fprintf (stderr, "Cannot open output file %s\n", fname);
		return 1;
	}
#if 0
	b = malloc (1024 * 1024);
	setvbuf (f, b, _IOFBF, 1024 * 1024);
#endif
	fwrite (&h, sizeof (GT4ListHeader), 1, f);

	count = 0;
	totalfreq = 0;
	bp = 0;
	for (i = 0; i < table->n_words; i++) {
		if (freqs[i] >= cutoff) {
			memcpy (b + bp, &table->words[i], 8);
			bp += 8;
			memcpy (b + bp, &freqs[i], 4);
			bp += 4;
			if (bp >= BSIZE) {
				fwrite (b, 1, bp, f);
				bp = 0;
			}
#if 0
			fwrite (&table->words[i], sizeof (table->words[i]), 1, f);
			fwrite (&freqs[i], sizeof (freqs[i]), 1, f);
#endif
			count += 1;
			totalfreq += freqs[i];
		}
	}
	if (bp) {
		fwrite (b, 1, bp, f);
	}

	h.code = GT4_LIST_CODE;
	h.version_major = VERSION_MAJOR;
	h.version_minor = VERSION_MINOR;
	h.wordlength = table->wordlength;
	h.nwords = count;
	h.totalfreq = totalfreq;
	h.list_start = sizeof (GT4ListHeader);
	fseek (f, 0, SEEK_SET);
	fwrite (&h, sizeof (GT4ListHeader), 1, f);
	fclose (f);
#if 0
	free (b);
#endif
	return 0;
}

unsigned long long generate_mismatches (GT4WordTable *mmtable, unsigned long long word, unsigned int wordlength,
		unsigned int givenfreq, unsigned int nmm, unsigned int startsite, int usesmallercomplement, int countonly,
		int equalmmonly)
{
	unsigned long long mask = 0L, count = 0L, mismatch = 0L;
	unsigned int i;

	/* first I put the current word into the table */
	if (!countonly && (nmm == 0 || !equalmmonly)) {
		if (usesmallercomplement) {
			word = get_canonical_word (word, wordlength);
		}
		gt4_word_table_add_word (mmtable, word, givenfreq);
	}
	if (nmm == 0) return 1;

	/* generating mm-s */
	for (i = startsite; i < wordlength; i++) {
		for (mismatch = 1; mismatch < 4; mismatch++) {
			if (!countonly) {
				mask = mismatch << (2 * i);
			}
			count += generate_mismatches (mmtable, word ^ mask, wordlength, givenfreq, nmm - 1, i + 1, usesmallercomplement, countonly, equalmmonly);
		}
	}
	return count;
}


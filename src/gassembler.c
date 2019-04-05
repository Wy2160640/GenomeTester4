#define __GASSEMBLER_C__

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <binomial.h>
#include <database.h>
#include <math.h>
#include <matrix.h>
#include <queue.h>
#include <sequence.h>
#include <utils.h>
#include <version.h>

#define OUTPUT_FULL_STATS

unsigned int debug = 0;
unsigned int debug_groups = 0;

unsigned int max_regions = 1000000000;
unsigned int n_threads = 4;
static unsigned int min_coverage = 6;
static double min_p = 0.5;

/*
 * <0 - dynamic
 *  0  - median
 * >0  - value
 */

static float coverage = 0;

#define WORDLEN 25
#define MAX_THREADS 256
#define MAX_KMERS 1024
#define MAX_READS_PER_KMER 100
#define MAX_READS 4096
#define MIN_READS 10
#define MAX_ALIGNED_READS 1024
#define MAX_READ_LENGTH 128
#define MAX_REFERENCE_LENGTH 256
#define MAX_GROUPS MAX_ALIGNED_READS

enum { CHR_NONE, CHR_1, CHR_2, CHR_3, CHR_4, CHR_5, CHR_6, CHR_7, CHR_8, CHR_9, CHR_10, CHR_11, CHR_12, CHR_13, CHR_14, CHR_15, CHR_16, CHR_17, CHR_18, CHR_19, CHR_20, CHR_21, CHR_22, CHR_X, CHR_Y };
static const char *chr_names[] = { "INVALID", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21", "22", "X", "Y" };

typedef struct _GASMQueue GASMQueue;
typedef struct _GASMRead GASMRead;
typedef struct _ReadInfo ReadInfo;
typedef struct _SeqFile SeqFile;
typedef struct _SNV SNV;
typedef struct _SWCell SWCell;
typedef struct _AssemblyData AssemblyData;
typedef struct _Call Call;
typedef struct _CallExtra CallExtra;

/* Returns number of aligned positions */
static int align (AssemblyData *adata, const char *kmers[], unsigned int nkmers);
static int group (AssemblyData *adata, unsigned int print);
static int assemble (AssemblyData *adata, const char *kmers[], unsigned int nkmers, unsigned int print);
static int assemble_recursive (KMerDB *db, SeqFile *files, unsigned int ref_chr, unsigned int ref_start, unsigned int ref_end, const char *ref, const char *kmers[], unsigned int nkmers);
unsigned int align_reads_to_reference (NSeq *ref_seq, GASMRead *reads[], unsigned int nreads, GASMRead *a_reads[], short a[][MAX_REFERENCE_LENGTH], SWCell *sw_matrix);
unsigned int create_gapped_alignment (NSeq *ref_seq, unsigned int ref_start, GASMRead *a_reads[], unsigned int na, short a[][MAX_REFERENCE_LENGTH], unsigned int aligned_ref[], int ref_pos[], short _p[][MAX_REFERENCE_LENGTH * 2]);
static unsigned int chr_from_text (const char *name);
static void test_alignment (const char *a, const char *b);
static float find_coverage (GT4Index *index);

struct _GASMRead {
  char *name;
  char *seq;
  NSeq *nseq;
  unsigned long long tag;
  unsigned long long mask;
  unsigned long long unknown;
  unsigned int group;
};

struct _ReadInfo {
  unsigned long long name_pos;
  unsigned int kmer_pos;
  unsigned int file_idx;
  unsigned int dir;
};

static GASMRead *gasm_read_new (const char *name, const char *seq, unsigned int wlen);
static void gasm_read_delete (GASMRead *read);

struct _SeqFile {
  const char *name;
  const unsigned char *cdata;
  unsigned long long csize;
};

struct _SNV {
  unsigned int chr;
  unsigned long long pos;
  char *id;
  unsigned short ref_allele;
  unsigned short alt_allele;
  unsigned short genotype;
};

struct _SWCell {
  int16_t score;
  int16_t left_gap_score;
  int16_t top_gap_score;
  int8_t sx;
  int8_t sy;
  int8_t left_gap_len;
  int8_t top_gap_len;
};

struct _CallExtra {
  float prob;
  float rprob;
  float hzprob;
  unsigned short end_dist;
  unsigned short n_groups_total;
  unsigned short n_groups;
  unsigned short div_0;
  unsigned short div_1;
  unsigned short max_cov_0;
  unsigned short max_cov_1;
  unsigned short compat_0;
  unsigned short compat_1;
  unsigned short compat_both;
};

struct _Call {
  unsigned int pos;
  unsigned char sub;
  unsigned char ref;
  unsigned short cov;
  unsigned short counts[GAP + 1];
  unsigned short nucl[2];
  unsigned short poly;
  unsigned short prev_ref;
  float p;
  CallExtra extra;
};

typedef struct _CallBlock CallBlock;

struct _AssemblyData {
  GASMQueue *queue;
  KMerDB *db;
  SeqFile *files;
  /* Reference data */
  unsigned int chr;
  unsigned int start;
  unsigned int end;
  const char *ref;
  NSeq *ref_seq;
  /* Extracted reads */
  GASMRead *reads[MAX_READS];
  unsigned int nreads;
  /* Smith-Waterman table */
  SWCell *sw_matrix;
  /* Gapped alignment (nucleotide values) */
  short (*alignment)[MAX_REFERENCE_LENGTH * 2];

  GASMRead *aligned_reads[MAX_ALIGNED_READS];
  unsigned int aligned_ref[MAX_REFERENCE_LENGTH * 2];
  int ref_pos[MAX_REFERENCE_LENGTH * 2];
  unsigned int na, p_len;

  /* Number of reads per position */
  short *coverage;
  /* Nucleotide counts */
  short (*nucl_counts)[GAP + 1];
  /* Group compatibility */
  unsigned char (*is_compat)[MAX_GROUPS];
  unsigned short (*n_common)[MAX_GROUPS];
  /* Calls */
  CallBlock *cblock;
};

struct _CallBlock {
  CallBlock *next;
  unsigned int chr;
  unsigned int start;
  unsigned int end;
  unsigned int n_calls;
  /* Calls */
  Call calls[MAX_REFERENCE_LENGTH * 2];
};

struct _GASMQueue {
  GT4Queue gt4_queue;
  /* Source */
  const unsigned char *cdata;
  unsigned long long csize;
  unsigned long long cpos;
  unsigned int line;
  unsigned int nrunning;
  unsigned int finished;
  AssemblyData *adata[MAX_THREADS];
  CallBlock *free_blocks;
  CallBlock *processing_blocks;
  CallBlock *finished_blocks;
  /* Last printed position */
  unsigned int last_chr;
  unsigned int last_pos;
};

static void
queue_setup (GASMQueue *queue, unsigned int nthreads)
{
  gt4_queue_setup (&queue->gt4_queue, nthreads);
  queue->cpos = 0;
  queue->line = 0;
  queue->nrunning = 0;
  queue->finished = 0;
}

static CallBlock *
queue_get_call_block (GASMQueue *gq, unsigned int chr, unsigned int start, unsigned int end)
{
  CallBlock *cb;
  if (gq->free_blocks) {
    cb = gq->free_blocks;
    gq->free_blocks = cb->next;
  } else {
    cb = (CallBlock *) malloc (sizeof (CallBlock));
  }
  memset (cb, 0, sizeof (CallBlock));
  cb->chr = chr;
  cb->start = start;
  cb->end = end;
  cb->next = gq->processing_blocks;
  gq->processing_blocks = cb;
  return cb;
}

static void
queue_finish_call_block (GASMQueue *gq, CallBlock *cb)
{
  CallBlock *prev = NULL;
  CallBlock *cur = gq->processing_blocks;
  while (cur != cb) {
    prev = cur;
    cur = cur->next;
  }
  if (!prev) {
    gq->processing_blocks = cb->next;
  } else {
    prev->next = cb->next;
  }
  cb->next = gq->finished_blocks;
  gq->finished_blocks = cb;
}

static void
queue_free_call_block (GASMQueue *gq, CallBlock *cb)
{
  CallBlock *prev = NULL;
  CallBlock *cur = gq->finished_blocks;
  while (cur != cb) {
    prev = cur;
    cur = cur->next;
  }
  if (!prev) {
    gq->finished_blocks = cb->next;
  } else {
    prev->next = cb->next;
  }
  cb->next = gq->free_blocks;
  gq->free_blocks = cb;
}

static AssemblyData *
assembly_data_new (KMerDB *db, SeqFile *files)
{
  AssemblyData *adata = (AssemblyData *) malloc (sizeof (AssemblyData));
  memset (adata, 0, sizeof (AssemblyData));
  adata->db = db;
  adata->files = files;
  adata->sw_matrix = (SWCell *) malloc ((MAX_REFERENCE_LENGTH + 1) * (MAX_READ_LENGTH + 1) * sizeof (SWCell));
  adata->alignment = (short (*)[MAX_REFERENCE_LENGTH * 2]) malloc (MAX_ALIGNED_READS * MAX_REFERENCE_LENGTH * 2 * 2);
  adata->coverage = (short *) malloc (MAX_REFERENCE_LENGTH * 2 * 2);
  adata->nucl_counts = (short (*)[GAP + 1]) malloc (MAX_REFERENCE_LENGTH * 2 * (GAP + 1) * 2);
  adata->is_compat = (unsigned char (*)[MAX_GROUPS]) malloc (MAX_GROUPS * MAX_GROUPS);
  adata->n_common = (unsigned short (*)[MAX_GROUPS]) malloc (MAX_GROUPS * MAX_GROUPS * 2);
  return adata;
}

static void
assembly_data_clear (AssemblyData *adata)
{
  unsigned int i;
  for (i = 0; i < adata->nreads; i++) gasm_read_delete (adata->reads[i]);
  adata->nreads = 0;
  if (adata->ref_seq) {
    n_seq_delete (adata->ref_seq);
    adata->ref_seq = NULL;
  }
  adata->cblock = NULL;
}

static unsigned int print_counts = 0;
static unsigned int print_all = 0;

static void
print_header (FILE *ofs) {
  fprintf (ofs, "CHR\tPOS\tREF\tCOVERAGE\tCALL\tCLASS\tP\tPREV");
  if (print_counts) {
    fprintf (ofs, "\tA\tC\tG\tT\tN\tGAP");
  }
  if (print_all) {
    fprintf (ofs, "\tPROB\tRPROB\tEDIST\tGRP_ALL\tGRP\tDIV0\tDIV1\tG0\tG1\tG0_COMP\tG1_COMP\tCOMP_2");
  }
}

static double
pmin (double a, double b) {
  return (a < b) ? a : b;
}

static double
calc_p (Call *call, CallExtra *extra, unsigned int KMER_COVERAGE)
{
  // linpred = -1.447 +0.6845*(CALLtype=="homoMUT")
  int homoMUT = ((call->nucl[0] == call->nucl[1]) && (call->nucl[0] != call->ref));
  double icept = -1.447;
  double term0 = 0.6845 * homoMUT;
  // +0.05935*G0_COMP +0.1621*COMP_2
  double term1 = 0.05935 * extra->compat_0 + 0.1621 * extra->compat_both;
  // -0.8501*(CLASS=="I") +0.4295*(CLASS=="S")
  double term2 = -0.8501 * (call->ref == GAP) + 0.4295 * ((call->nucl[1] != GAP) && call->poly);
  // +1.568*(COV>=4 & COV<0.75*k_mer_coverage)
  double term3 = 1.568 * ((call->cov >= 4) && (call->cov < (0.75 * KMER_COVERAGE)));
  // +1.778*(COV>=0.75*k_mer_coverage & COV<1.25*k_mer_coverage)
  double term4 = 1.778 * ((call->cov >= (0.75 * KMER_COVERAGE)) && (call->cov < (1.25 * KMER_COVERAGE)));
  // +2.340*(COV>=1.25*k_mer_coverage & COV<1.9*k_mer_coverage)
  double term5 = 2.340 * ((call->cov >= (1.25 * KMER_COVERAGE)) && (call->cov < (1.9 * KMER_COVERAGE)));
  // +0.1781*(COV>=1.9*k_mer_coverage)
  double term6 = 0.1781 * (call->cov >= (1.9 * KMER_COVERAGE));
  // +5.989*pvalue2 -4.546*pvalue2**2
  // -3.002*pvalue2**3
  int kokku = call->counts[A] + call->counts[C] + call->counts[G] + call->counts[T] + call->counts[GAP];
  double p1 = poisson (kokku, KMER_COVERAGE);
  double pvalue_COV = pow (pmin (p1, 1 - p1), 2);
  int G1koht = call->counts[call->nucl[1]];
  double p2 = dbinom(G1koht, kokku, 0.5);
  double pvalue2 = pow (pmin (p2, 1 - p2), 2);
  if (extra->n_groups_total < 2) pvalue2 = 1;
  double term7 = 5.989 * pvalue2 - 4.546 * pvalue2 * pvalue2 - 3.002 * pvalue2 * pvalue2 * pvalue2;
  // +0.06952*G1_COMP +1.040*(G1_COMP > 0.75 * G0_COMP)
  double term8 = 0.06952 * extra->compat_1 + 1.040 * (extra->compat_1 > (0.75 * extra->compat_0));
  // -0.1063*(abs(kokku - k_mer_coverage)) +0.6887*pvalue_COV -1.619*pvalue_COV**2
  double term9 = -0.1063 * abs (kokku - KMER_COVERAGE) + 0.6887 * pvalue_COV - 1.619 * pvalue_COV * pvalue_COV;
  // +0.1251*EDIST -0.001694*EDIST**2
  double term10 = 0.1251 * extra->end_dist - 0.001694 * extra->end_dist * extra->end_dist;
  // +0.06204*G0_COMP*(CALLtype=="homoMUT")
  double term11 = 0.06204 * extra->compat_0 * homoMUT;
  // -0.02578*(CALLtype=="homoMUT")*COMP_2 -0.002912*G0_COMP*G1_COMP
  double term12 = -0.02578 * homoMUT * extra->compat_both;
  double term13 = -0.002912 * extra->compat_0 * extra->compat_1;
  // +0.06077*(CALLtype=="homoMUT")*(abs(kokku - k_mer_coverage))
  double term14 = 0.06077 * homoMUT * abs (kokku - KMER_COVERAGE);
  // +2.158*(CALLtype=="homoMUT")*pvalue_COV-0.001164*EDIST*(abs(kokku - k_mer_coverage))
  double term15 = 2.158 * homoMUT * pvalue_COV;
  double term16 = -0.001164 * extra->end_dist * abs (kokku - KMER_COVERAGE);
  double linpred = icept + term0 + term1 + term2 + term3 + term4 + term5 + term6 + term7 + term8 + term9 + term10 + term11 + term12 + term13 + term14 + term15 + term16;
  return exp (linpred) / (1 + exp (linpred));
}

static void
print_call (CallBlock *cb, unsigned int pos, unsigned int print_counts, unsigned int print_all)
{
  Call *call = &cb->calls[pos];
  /* CHR POS REF COV */
  fprintf (stdout, "%s\t%u\t%c\t%u", chr_names[cb->chr], call->pos, n2c[call->ref], call->cov);
  /* CALL */
  if ((call->cov >= min_coverage) && (call->p >= min_p)) {
    if (call->nucl[0] == NONE) {
      fprintf (stdout, "\tNC");
    } else {
      fprintf (stdout, "\t%c%c", n2c[call->nucl[0]], n2c[call->nucl[1]]);
    }
    /* CLASS */
    if (call->ref == GAP) {
      fprintf (stdout, "\tI");
    } else if (call->nucl[1] == GAP) {
      fprintf (stdout, "\tD");
    } else if (call->poly) {
      fprintf (stdout, "\tS");
    } else {
      fprintf (stdout, "\t0");
    }
    /* PVALUE */
    fprintf (stdout, "\t%.3f", call->p);
    fprintf (stdout, "\t%c", call->prev_ref);
  } else {
    fprintf (stdout, "\tNC\t\t0\t");
  }
  if (print_counts) {
    /* A C G T GAP */
    fprintf (stdout, "\t%u\t%u\t%u\t%u\t%u", call->counts[A], call->counts[C], call->counts[G], call->counts[T], call->counts[GAP]);
  }
#ifdef OUTPUT_FULL_STATS
  if (print_all) {
    fprintf (stdout, "\t%.5f\t%.5f\t%.5f", call->extra.prob, call->extra.rprob, call->extra.hzprob);
    fprintf (stdout, "\t%2u", call->extra.end_dist);
    fprintf (stdout, "\t%2u\t%2u\t%2u\t%2u", call->extra.n_groups_total, call->extra.n_groups, call->extra.div_0, call->extra.div_1);
    fprintf (stdout, "\t%2u\t%2u\t%2u\t%2u\t%2u", call->extra.max_cov_0, call->extra.max_cov_1, call->extra.compat_0, call->extra.compat_1, call->extra.compat_both);
  }
#endif
}

static void
print_calls (GASMQueue *queue, unsigned int print_counts, unsigned int print_all)
{
  unsigned int min_chr_p = 0xffffffff;
  unsigned int min_start_p = 0xffffffff;
  CallBlock *cb;
  /* Find smallest processing */
  for (cb = queue->processing_blocks; cb; cb = cb->next) {
    if ((cb->chr < min_chr_p) || ((cb->chr == min_chr_p) && (cb->start < min_start_p))) {
      min_chr_p = cb->chr;
      min_start_p = cb->start;
    }
  }
  /* fprintf (stderr, "Smallest processing: %u %u\n", min_chr_p, min_start_p); */
  while (queue->finished_blocks) {
    CallBlock *cb_f = NULL;
    unsigned int min_chr_f = 0xffffffff;
    unsigned int min_start_f = 0xffffffff;
    unsigned int i;
    /* Find block with smallest start address */
    for (cb = queue->finished_blocks; cb; cb = cb->next) {
      if ((cb->chr < min_chr_f) || ((cb->chr == min_chr_f) && (cb->start < min_start_f))) {
        min_chr_f = cb->chr;
        min_start_f = cb->start;
        cb_f = cb;
      }
    }
    if (!cb_f) return;
    if (cb_f->chr > min_chr_p) return;
    if ((cb_f->chr == min_chr_p) && (cb_f->end > min_start_p)) return;

    /* Print all relevant positions */
    for (i = cb_f->start; i < cb_f->end; i++) {
      if ((cb_f->chr == queue->last_chr) && (i <= queue->last_pos)) continue;
      unsigned int pos = i;
      /* Find call for given position with best aggregate value */
      CallBlock *best_cb = cb_f;
      float best_p = 0;
      int best_cov = 0;
      CallBlock *ccb;
      unsigned int j;
      unsigned int has_poly = 0;
      for (ccb = queue->finished_blocks; ccb; ccb = ccb->next) {
        if (ccb->chr > cb_f->chr) continue;
        if (ccb->start > pos) continue;
        for (j = 0; j < ccb->n_calls; j++) {
          if (ccb->calls[j].pos != pos) continue;
          if (ccb->calls[j].p < best_p) continue;
          if (ccb->calls[j].cov < best_cov) continue;
          best_cb = ccb;
          best_p = ccb->calls[j].p;
          best_cov = ccb->calls[j].cov;
          if (ccb->calls[j].poly) has_poly = 1;
        }
      }
      if (has_poly || (best_cov == 0)) {
        /* Iterate over best call block & print all calls for this position */
        for (j = 0; j < best_cb->n_calls; j++) {
          if (best_cb->calls[j].pos == pos) {
            if (best_p >= min_p) {
              if (best_cb->calls[j].poly) {
                /* Polymorphism with p >= cutoff */
                print_call (best_cb, j, print_counts, print_all);
                fprintf (stdout, "\n");
              }
            } else {
              /* p < cutoff but there was polymorphism in some block */
              print_call (best_cb, j, print_counts, print_all);
              fprintf (stdout, "\n");
            }
            /* if (best_p == 0) break; */
            /* if (best_cb->calls[j].prev_ref == '!') break; */
          } else if (best_cb->calls[j].pos > pos) {
            break;
          }
        }
      }
      queue->last_chr = cb_f->chr;
      queue->last_pos = pos;
    }
    queue_free_call_block (queue, cb_f);
  }
}

static void
process (GT4Queue *queue, unsigned int idx, void *data)
{
  GASMQueue *gasm_queue = (GASMQueue *) queue;
  AssemblyData *adata = gasm_queue->adata[idx];
  gt4_queue_lock (queue);
  while (!gasm_queue->finished) {
    if ((gasm_queue->cpos < gasm_queue->csize) && (gasm_queue->line < max_regions)) {
      const unsigned char *tokenz[MAX_KMERS + 4];
      unsigned int lengths[MAX_KMERS + 4];
      unsigned int ntokenz;
      //fprintf (stderr, "process: Thread %u reading file from %llu\n", idx, gasm_queue->cpos);
      ntokenz = split_line (gasm_queue->cdata + gasm_queue->cpos, gasm_queue->csize - gasm_queue->cpos, tokenz, lengths, MAX_KMERS + 4);
      while ((gasm_queue->cpos < gasm_queue->csize) && (gasm_queue->cdata[gasm_queue->cpos] != '\n')) gasm_queue->cpos += 1;
      while ((gasm_queue->cpos < gasm_queue->csize) && (gasm_queue->cdata[gasm_queue->cpos] <= ' ')) gasm_queue->cpos += 1;
      gasm_queue->line += 1;
      //fprintf (stderr, "process: Thread %u line %u\n", idx, gasm_queue->line);
      if (ntokenz < 5) {
        fprintf (stderr, "process: Too few tokens at line %u\n", gasm_queue->line);
      } else {
        char *kmers[MAX_KMERS];
        unsigned int nkmers;
        unsigned int i;
        char chr[32];
        gasm_queue->nrunning += 1;
        if (lengths[0] > 31) lengths[0] = 31;
        memcpy (chr, tokenz[0], lengths[0]);
        chr[lengths[0]] = 0;
        nkmers = 0;
        for (i = 4; i < ntokenz; i++) {
          kmers[nkmers++] = strndup ((const char *) tokenz[i], lengths[i]);
        }
        adata->chr = chr_from_text (chr);
        adata->start = strtol ((const char *) tokenz[1], NULL, 10);
        adata->end = strtol ((const char *) tokenz[2], NULL, 10);
        adata->ref = (const char *) tokenz[3];
        adata->cblock = queue_get_call_block (gasm_queue, adata->chr, adata->start, adata->end);
        /* Now the new block is allocated so it is safe to print all completed blocks */
        print_calls (gasm_queue, 0, 0);
        gt4_queue_unlock (queue);
        assemble (adata, (const char **) kmers, nkmers, 0);
        gt4_queue_lock (queue);
        queue_finish_call_block (gasm_queue, adata->cblock);
        assembly_data_clear (adata);
        for (i = 0; i < nkmers; i++) free (kmers[i]);
        gasm_queue->nrunning -= 1;
      }
      gt4_queue_broadcast (queue);
    } else {
      if (!gasm_queue->nrunning) {
        gasm_queue->finished = 1;
      } else {
        gt4_queue_wait (queue);
      }
    }
  }
  //fprintf (stderr, "Thread %u exiting\n", idx);
  gt4_queue_broadcast (queue);
  gt4_queue_unlock (queue);
}

static SNV *read_snvs (const char *filename, unsigned int *n_snvs);
static SNV *read_fps (const char *filename, unsigned int *n_snvs);
/* Get index of SNV at or next to pos */
static unsigned int lookup_snv (SNV *snvs, unsigned int n_snvs, unsigned int chr, unsigned long long pos);

static void load_db_or_die (KMerDB *db, const char *db_name, const char *seq_dir, const char *id);
static SeqFile *map_sequences (KMerDB *db, const char *seq_dir);

/* Get list of unique reads containing at lest one kmer from list */
static unsigned int get_unique_reads (ReadInfo reads[], unsigned int nreads, KMerDB *db, SeqFile files[], const char *kmers[], unsigned int nkmers);
/* Get proper read sequences (forward direction) */
static unsigned int get_read_sequences (GASMRead *reads[], const ReadInfo read_info[], unsigned int nreads, SeqFile files[]);
static void print_db_reads (GT4Index *index, SeqFile *files, unsigned long long kmer_idx, unsigned int kmer_dir, FILE *ofs);

unsigned int smith_waterman_seq (unsigned int a_pos[], unsigned int b_pos[], const NSeq *a, const NSeq *b, SWCell *t, unsigned int debug);
static void print_alignment (FILE *ofs, unsigned int a_pos[], unsigned int b_pos[], unsigned int len, NSeq *a, NSeq *b);


/* Basic parameters */
const char *db_name = NULL;
const char *snv_db_name = NULL;
const char *fp_db_name = NULL;
const char *seq_dir = NULL;
static unsigned int print_reads = 0;
static unsigned int prefetch_db = 1;
static unsigned int prefetch_seq = 1;
/* Advanced parameters */
SNV *snvs = NULL;
SNV *fps = NULL;
unsigned int n_snvs = 0;
unsigned int n_fps = 0;
static unsigned int min_end_distance = 0;
static unsigned int min_confirming = 2;
static unsigned int min_group_coverage = 1;
static unsigned int max_divergent = 4;
static unsigned int min_align_len = 25;
static unsigned int min_group_size = 2;
static float min_group_rsize = 0.05f;
static unsigned int max_group_divergence = 3;
static unsigned int max_group_rdivergence = 3;
static unsigned int max_uncovered = 10;

static void
print_usage (FILE *ofs, unsigned int advanced, int exit_value)
{
  fprintf (ofs, "gassembler version %u.%u.%u (%s)\n", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, VERSION_QUALIFIER);
  fprintf (ofs, "Usage: gassembler [OPTIONS] [KMERS...]\n");
  fprintf (ofs, "Options:\n");
  fprintf (ofs, "    -v, --version                      - print version information and exit\n");
  fprintf (ofs, "    -h, --help                         - print this usage screen and exit\n");
  fprintf (ofs, "    -dbi FILENAME                      - read index file\n");
  fprintf (ofs, "    --seq_dir DIRECTORY                - directory of fastq files (overrides index location)\n");
  fprintf (ofs, "    --reference CHR START END SEQ      - reference region to be called\n");
  fprintf (ofs, "    --file FILENAME                    - read reference region and kmers from file (one line at time)\n");
  fprintf (ofs, "    --min_coverage INTEGER             - minimum coverage for a call (default %u)\n", min_coverage);
  fprintf (ofs, "    --coverage FLOAT | median | local  - average sequencing depth (default - median, local - use local number of reads)\n");
  fprintf (ofs, "    --num_threads                      - number of threads to use (default %u)\n", n_threads);
  fprintf (ofs, "    --min_p FLOAT                      - minimum call quality (default %.2f)\n", min_p);
  fprintf (ofs, "    --advanced                         - print advanced usage options\n");
  if (advanced) {
    fprintf (ofs, "Advanced options:\n");
    fprintf (ofs, "    --snvs FILENAME                  - gmer_caller called SNVs\n");
    fprintf (ofs, "    --fp FILENAME                    - List of known false positives\n");
    fprintf (ofs, "    --min_end_distance INTEGER       - minimum distance from segment end to call (default %u)\n", min_end_distance);
    fprintf (ofs, "    --min_confirming INTEGER         - minimum confirming nucleotide count for a call (default %u)\n", min_confirming);
    fprintf (ofs, "    --min_group_coverage INTEGER     - minimum coverage of group (default %u)\n", min_group_coverage);
    fprintf (ofs, "    --max_divergent INTEGER          - maximum number of mismatches per read (default %u)\n", max_divergent);
    fprintf (ofs, "    --min_align_len INTEGER          - minimum alignment length (default %u)\n", min_align_len);
    fprintf (ofs, "    --min_group_size INTEGER         - minimum group size (default %u)\n", min_group_size);
    fprintf (ofs, "    --min_group_rsize FLOAT          - minimum relative group size (default %.2f)\n", min_group_rsize);
    fprintf (ofs, "    --max_group_divergence INTEGER   - maximum divergence in group (default %u)\n", max_group_divergence);
    fprintf (ofs, "    --max_group_rdivergence INTEGER  - maximum relative divergence in group (default %u)\n", max_group_rdivergence);
    fprintf (ofs, "    --max_uncovered INTEGER          - maximum length of sequence end not covered by group (default %u)\n", max_uncovered);
    fprintf (ofs, "    -D                               - increase debug level\n");
    fprintf (ofs, "    -DG                              - increase group debug level\n");
  }
  exit (exit_value);
}

int
main (int argc, const char *argv[])
{
  unsigned int i;
  const char *input_name = NULL;
  const char *kmers[MAX_KMERS];
  unsigned int nkmers = 0;
  unsigned int ref_chr = CHR_NONE;
  unsigned int ref_start = 0, ref_end = 0;
  const char *ref = NULL;
  unsigned int only_pos = 0;

  KMerDB db;
  SeqFile *files;
    
  for (i = 1; i < argc; i++) {
    if (!strcmp (argv[i], "-v") || !strcmp (argv[i], "--version")) {
      fprintf (stdout, "gassembler version %u.%u.%u (%s)\n", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, VERSION_QUALIFIER);
    } else if (!strcmp (argv[i], "-h") || !strcmp (argv[i], "--help")) {
      print_usage (stdout, 0, 0);
    } else if (!strcmp (argv[i], "--advanced")) {
      print_usage (stdout, 1, 0);
    } else if (!strcmp (argv[i], "-dbi") || !strcmp (argv[i], "-dbb") || !strcmp (argv[i], "-db")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      db_name = argv[i];
    } else if (!strcmp (argv[i], "--reference")) {
      if ((i + 4) >= argc) print_usage (stderr, 0, 1);
      ref_chr = chr_from_text (argv[i + 1]);
      if (!ref_chr) print_usage (stderr, 0, 1);
      ref_start = atoi (argv[i + 2]);
      ref_end = atoi (argv[i + 3]);
      ref = (const char *) argv[i + 4];
      i += 4;
    } else if (!strcmp (argv[i], "--snvs")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      snv_db_name = argv[i];
    } else if (!strcmp (argv[i], "--fp")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      fp_db_name = argv[i];
    } else if (!strcmp (argv[i], "--file")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      input_name = argv[i];
    } else if (!strcmp (argv[i], "--pos")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      only_pos = strtol (argv[i], NULL, 10);
    } else if (!strcmp (argv[i], "--max_regions")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      max_regions = strtol (argv[i], NULL, 10);
    } else if (!strcmp (argv[i], "--min_coverage")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      min_coverage = strtol (argv[i], NULL, 10);
    } else if (!strcmp (argv[i], "--min_end_distance")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      min_end_distance = strtol (argv[i], NULL, 10);
    } else if (!strcmp (argv[i], "--min_confirming")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      min_confirming = strtol (argv[i], NULL, 10);
    } else if (!strcmp (argv[i], "--min_group_coverage")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      min_group_coverage = strtol (argv[i], NULL, 10);
    } else if (!strcmp (argv[i], "--max_divergent")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      max_divergent = strtol (argv[i], NULL, 10);
    } else if (!strcmp (argv[i], "--min_align_len")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      min_align_len = strtol (argv[i], NULL, 10);
    } else if (!strcmp (argv[i], "--min_group_size")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      min_group_size = strtol (argv[i], NULL, 10);
    } else if (!strcmp (argv[i], "--min_group_rsize")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      min_group_rsize = atof (argv[i]);
    } else if (!strcmp (argv[i], "--max_group_divergence")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      max_group_divergence = strtol (argv[i], NULL, 10);
    } else if (!strcmp (argv[i], "--max_group_rdivergence")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      max_group_rdivergence = strtol (argv[i], NULL, 10);
    } else if (!strcmp (argv[i], "--max_uncovered")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      max_uncovered = strtol (argv[i], NULL, 10);
    } else if (!strcmp (argv[i], "--coverage")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      if (!strcmp (argv[i], "local")) {
        coverage = -1;
      } else if (!strcmp (argv[i], "median")) {
        coverage = 0;
      } else {
        coverage = (float) atof (argv[i]);
        if (!coverage) {
          fprintf (stderr, "Coverage has to be positive real value\n");
          exit (1);
        }
      }
    } else if (!strcmp (argv[i], "--min_p")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      min_p = (float) atof (argv[i]);
    } else if (!strcmp (argv[i], "--num_threads")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      n_threads = strtol (argv[i], NULL, 10);
    } else if (!strcmp (argv[i], "--print_reads")) {
      print_reads = 1;
    } else if (!strcmp (argv[i], "--seq_dir")) {
      i += 1;
      if (i >= argc) print_usage (stderr, 0, 1);
      seq_dir = argv[i];
    } else if (!strcmp (argv[i], "-D")) {
      debug += 1;
    } else if (!strcmp (argv[i], "-DG")) {
      debug_groups += 1;
    } else if (!strcmp (argv[i], "-ta")) {
      test_alignment (argv[i + 1], argv[i + 2]);
      exit (0);
    } else  {
      if (nkmers < MAX_KMERS) kmers[nkmers++] = argv[i];
    }
  }

  if (debug > debug_groups) debug_groups = debug;

  /* Check arguments */
  if (!db_name) {
    print_usage (stderr, 0, 1);
  }

  /* Read databases */
  load_db_or_die (&db, db_name, seq_dir, "reads");

  if (coverage == 0) coverage = find_coverage (&db.index);

  if (snv_db_name) {
    fprintf (stderr, "Loading SNV database\n");
    snvs = read_snvs (snv_db_name, &n_snvs);
    fprintf (stderr, "Num SNVs %u\n", n_snvs);
  }

  if (fp_db_name) {
    fprintf (stderr, "Loading known false positives\n");
    fps = read_fps (fp_db_name, &n_fps);
    fprintf (stderr, "Num false positives %u\n", n_fps);
  }

  /* Set up file lists */
  fprintf (stderr, "Loading read sequences\n");
  files = map_sequences (&db, seq_dir);
  if (!files) {
    fprintf (stderr, "Cannot read sequences: terminating\n");
    exit (1);
  }

  if (input_name) {
    if (!only_pos) {
      GASMQueue queue;
      unsigned int i;
      queue.cdata = gt4_mmap (input_name, &queue.csize);
      if (!queue.cdata) {
        fprintf (stderr, "Cannot mmap input file %s\n", input_name);
        exit (1);
      }
      /* Print info */
      fprintf (stdout, "#KATK version: %u.%u.%u\n", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
      fprintf (stdout, "#KMer Database: %s\n", db_name);
      if (coverage >= 0) {
        fprintf (stdout, "#Coverage: %.2f\n", coverage);
      } else {
        fprintf (stdout, "#Coverage: local\n");
      }
      print_header (stdout);
      fprintf (stdout, "\n");
      queue_setup (&queue, n_threads);
      queue.cpos = 0;
      queue.line = 0;
      queue.last_chr = 0;
      queue.last_pos = 0;
      for (i = 0; i < n_threads; i++) {
        queue.adata[i] = assembly_data_new (&db, files);
      }
      gt4_queue_create_threads (&queue.gt4_queue, process, NULL);
      process (&queue.gt4_queue, 0, NULL);
      gt4_queue_lock (&queue.gt4_queue);
      while (queue.gt4_queue.nthreads_running > 1) {
        gt4_queue_wait (&queue.gt4_queue);
      }
      print_calls (&queue, 0, 0);
      gt4_queue_unlock (&queue.gt4_queue);
    } else {
      const unsigned char *cdata;
      unsigned long long csize, cpos;
      cdata = gt4_mmap (input_name, &csize);
      if (!cdata) {
        fprintf (stderr, "Cannot mmap input file %s\n", input_name);
        exit (1);
      }
      cpos = 0;
      while (cpos < csize) {
        const unsigned char *tokenz[MAX_KMERS + 4];
        unsigned int lengths[MAX_KMERS + 4];
        unsigned int ntokenz;
        ntokenz = split_line (cdata + cpos, csize - cpos, tokenz, lengths, MAX_KMERS + 4);
        while ((cpos < csize) && (cdata[cpos] != '\n')) cpos += 1;
        while ((cpos < csize) && (cdata[cpos] <= ' ')) cpos += 1;
        if (ntokenz < 5) {
          fprintf (stderr, "process: Too few tokens at line\n");
        } else {
          const char *kmers[MAX_KMERS];
          unsigned int chr, start, end, nkmers;
          const char *ref;
          unsigned int i;
          char chrc[32];
          if (lengths[0] > 31) lengths[0] = 31;
          memcpy (chrc, tokenz[0], lengths[0]);
          chrc[lengths[0]] = 0;
          chr = chr_from_text (chrc);
          start = strtol ((const char *) tokenz[1], NULL, 10);
          /* if (seen && (start > only_pos)) break; */
          if (start > only_pos) continue;
          end = strtol ((const char *) tokenz[2], NULL, 10);
          if (end <= only_pos) continue;
          ref = (const char *) tokenz[3];
          nkmers = 0;
          for (i = 4; i < ntokenz; i++) {
            kmers[nkmers++] = strndup ((const char *) tokenz[i], lengths[i]);
          }
          assemble_recursive (&db, files, chr, start, end, ref, kmers, nkmers);
        }
      }
    }
  } else {
    assemble_recursive (&db, files, ref_chr, ref_start, ref_end, ref, kmers, nkmers);
  }

  if (prefetch_db || prefetch_seq) {
    delete_scouts ();
  }

  return 0;
}

static int
assemble_recursive (KMerDB *db, SeqFile *files, unsigned int ref_chr, unsigned int ref_start, unsigned int ref_end, const char *ref, const char *kmers[], unsigned int nkmers)
{
  AssemblyData *adata = assembly_data_new (db, files);
  int result;
  unsigned int len;
  char *dup;
  adata->chr = ref_chr;
  adata->start = ref_start;
  adata->end = ref_end;
  len = ref_end - ref_start;
  dup = (char *) malloc (len + 1);
  strncpy (dup, ref, len);
  adata->ref = dup;
  adata->cblock = (CallBlock *) malloc (sizeof (CallBlock));
  adata->cblock->chr = adata->chr;
  adata->cblock->start = adata->start;
  adata->cblock->end = adata->end;
  result = align (adata, kmers, nkmers);
  if (result > 0) {
    result = group (adata, 1);
  } else if (result == 0) {
    unsigned int mid;
    mid = (ref_start + ref_end) / 2;
    result = 0;
    result += assemble_recursive (db, files, ref_chr, ref_start, mid, ref, kmers, nkmers);
    result += assemble_recursive (db, files, ref_chr, mid, ref_end, ref + (mid - ref_start), kmers, nkmers);
  }
  assembly_data_clear (adata);
  free (adata->cblock);
  free (dup);
  return result;
}

#define ERROR_PROB 0.01f

static float
gt1_prob (unsigned int gt_count, unsigned int total_count)
{
  double q0, q1;
  unsigned int err_count = total_count - gt_count;
  q0 = poisson (err_count, ERROR_PROB);
  q1 = poisson (gt_count, total_count);
  return (float) (q0 * q1);
}

static float
gt2_prob (unsigned int gt1_count, unsigned int gt2_count, unsigned int total_count)
{
  double q0, q1, q2;
  unsigned int err_count = total_count - gt1_count - gt2_count;
  q0 = poisson (err_count, ERROR_PROB);
  q1 = poisson (gt1_count, total_count / 2.0);
  q2 = poisson (gt2_count, total_count / 2.0);
  return (float) (q0 * q1 * q2);
}

static unsigned int
count_divergent_from_alignment (NSeq *a, NSeq *b, unsigned int a_p[], unsigned int b_p[], unsigned int align_len, unsigned int *n_gaps, unsigned int *s_gap, unsigned int *e_gap, unsigned int *gaps_total)
{
  unsigned int n_divergent, i;
  *n_gaps = 0;
  *gaps_total = 0;
  if ((a_p[0] > 0) && (b_p[0] > 0)) {
    /* Starts are unaligned */
    unsigned int gap_a = a_p[0];
    unsigned int gap_b = b_p[0];
    unsigned int min = (gap_a < gap_b) ? gap_a : gap_b;
    *n_gaps += 1;
    *s_gap = min;
    *gaps_total = *gaps_total + min;
  }
  if ((a_p[align_len - 1] < (a->len - 1)) && (b_p[align_len - 1] < (b->len - 1))) {
    /* Ends are unaligned */
    unsigned int gap_a = a->len - 1 - a_p[align_len - 1];
    unsigned int gap_b = b->len - 1 - b_p[align_len - 1];
    unsigned int min = (gap_a < gap_b) ? gap_a : gap_b;
    *n_gaps += 1;
    *e_gap = min;
    *gaps_total = *gaps_total + min;
  }
  n_divergent = *n_gaps;
  for (i = 0; i < align_len; i++) {
    if (a->pos[a_p[i]].nucl != b->pos[b_p[i]].nucl) n_divergent += 1;
  }
  return n_divergent;
}

typedef struct _Group Group;

struct _Group {
  unsigned long long tag;
  unsigned long long mask;
  unsigned int size;
  unsigned int included;
  unsigned int compat;
  unsigned int min_cov;
  unsigned int max_cov;
  unsigned int has_start;
  unsigned int has_end;
  unsigned int divergent;
  unsigned int *consensus;
};

#define MAX_UNALIGNED_SIZE 5

static int
align (AssemblyData *adata, const char *kmers[], unsigned int nkmers)
{
  ReadInfo read_info[MAX_READS];
  short (*alignment)[MAX_REFERENCE_LENGTH];
  unsigned int i;
  adata->ref_seq = n_seq_new_length (adata->ref, adata->end - adata->start, WORDLEN);
  /* Check reference length */
  if ((adata->end - adata->start) > MAX_REFERENCE_LENGTH) {
    fprintf (stderr, "align: reference length (%u) too big (max %u)\n", adata->end - adata->start, MAX_REFERENCE_LENGTH);
    return 0;
  }
  /* Get all unique reads */
  adata->nreads = get_unique_reads (read_info, MAX_READS, adata->db, adata->files, kmers, nkmers);
  if (debug) fprintf (stderr, "Got %u unique reads\n", adata->nreads);
  /* Create actual sequences in correct direction */
  get_read_sequences (adata->reads, read_info, adata->nreads, adata->files);
  if (print_reads) {
    for (i = 0; i < adata->nreads; i++) {
      fprintf (stdout, ">Read_%u\n", i);
      fprintf (stdout, "%s\n", adata->reads[i]->seq);
    }
  }
  /* Sanitize */
  if (debug) fprintf (stderr, "Number of usable reads: %u\n", adata->nreads);
  if (print_reads) {
    for (i = 0; i < adata->nreads; i++) {
      fprintf (stdout, ">Read_%u\n", i);
      fprintf (stdout, "%s\n", adata->reads[i]->seq);
    }
  }
  if (adata->nreads < MIN_READS) {
    fprintf (stderr, "Final number of reads (%u) too low (min %u)\n", adata->nreads, MIN_READS);
    return -1;
  }
  /* Align all reads to reference */
  if (debug) fprintf (stderr, "Aligning reads to reference...");
  alignment = (short (*)[MAX_REFERENCE_LENGTH]) malloc (MAX_ALIGNED_READS * MAX_REFERENCE_LENGTH * 2);
  adata->na = align_reads_to_reference (adata->ref_seq, adata->reads, adata->nreads, adata->aligned_reads, alignment, adata->sw_matrix);
  if (debug == 1) fprintf (stderr, "\n");
  /* Generate gapped alignment */
  adata->p_len = create_gapped_alignment (adata->ref_seq, adata->start, adata->aligned_reads, adata->na, alignment, adata->aligned_ref, adata->ref_pos, adata->alignment);
  /* Calculate totals */
  memset (adata->coverage, 0, adata->p_len * 2);
  memset (adata->nucl_counts, 0, adata->p_len * (GAP + 1) * 2);
  for (i = 0; i < adata->p_len; i++) {
    unsigned int j;
    for (j = 0; j < adata->na; j++) {
      if (adata->alignment[j][i] <= GAP) {
        int nucl = adata->alignment[j][i];
        adata->nucl_counts[i][nucl] += 1;
        adata->coverage[i] += 1;
      }
    }
  }
  /* Tag all reads by divergent positions */
  unsigned int n_divergent = 0;
  for (i = 0; i < adata->p_len; i++) {
    unsigned int j;
    unsigned int diverges = 0;
    for (j = 0; j <= GAP; j++) {
      if (j == adata->aligned_ref[i]) continue;
      if (j == N) continue;
      if (adata->nucl_counts[i][j] >= 2) diverges = 1;
    }
    if (diverges) {
      unsigned int known = 0;
      unsigned int ref_allele = 0, alt_allele = 0;
      if (n_divergent >= 21) {
        fprintf (stderr, "assemble: Too many divergent positions (max 21), ignoring the rest\n");
        break;
      }
      if (debug > 0) fprintf (stderr, "Divergent position: %u\n", adata->ref_pos[i]);
      if (snvs) {
        unsigned int snv = lookup_snv (snvs, n_snvs, adata->chr, adata->start + i);
        if ((snv < n_snvs) && (snvs[snv].chr == adata->chr) && (snvs[snv].pos == (adata->start + i))) {
          if (debug > 0) fprintf (stderr, "Known SNV %s (%c/%c)\n", snvs[snv].id, n2c[snvs[snv].ref_allele], n2c[snvs[snv].alt_allele]);
          known = 1;
          ref_allele = snvs[snv].ref_allele;
          alt_allele = snvs[snv].alt_allele;
        } else {
          if (debug > 0) fprintf (stderr, "Potential DeNovo\n");
        }
      }
      for (j = 0; j < adata->na; j++) {
        unsigned int ref = adata->aligned_ref[i];
        unsigned int nucl = adata->alignment[j][i];
        unsigned int mask = 7;
        /* Do not count single nucleotides */
        if ((nucl <= GAP) && (adata->nucl_counts[i][nucl] < 2)) mask = 0;
        /* N-s are counted same as reference */
        if (nucl == N) nucl = ref;
        /* Uncovered positions are counted as reference but masked */
        if (nucl > GAP) {
          nucl = ref;
          mask = 0;
        }
        adata->aligned_reads[j]->unknown = adata->aligned_reads[j]->unknown << 3;
        if (!known || ((nucl != ref_allele) && (nucl != alt_allele))) {
          adata->aligned_reads[j]->unknown |= 7;
        }
        /* Make 000 reference variant */
        nucl = nucl ^ ref;
        adata->aligned_reads[j]->tag = (adata->aligned_reads[j]->tag << 3) | nucl;
        adata->aligned_reads[j]->mask = (adata->aligned_reads[j]->mask << 3) | mask;
      }
      n_divergent += 1;
    }
  }
  free (alignment);

  return adata->nreads;
}

static int
group (AssemblyData *adata, unsigned int print)
{
  Group groups[MAX_ALIGNED_READS];
  unsigned int i, j, k;

  /* Recalculate totals */
  memset (adata->coverage, 0, adata->p_len * 2);
  memset (adata->nucl_counts, 0, adata->p_len * (GAP + 1) * 2);
  for (i = 0; i < adata->p_len; i++) {
    unsigned int j;
    for (j = 0; j < adata->na; j++) {
      int nucl = adata->alignment[j][i];
      if (nucl <= GAP) {
        adata->nucl_counts[i][nucl] += 1;
        adata->coverage[i] += 1;
      }
    }
  }

  memset (groups, 0, sizeof (groups));
  unsigned int n_groups = adata->na;
  for (i = 0; i < adata->na; i++) {
    adata->aligned_reads[i]->group = i;
    groups[i].size = 1;
    groups[i].tag = adata->aligned_reads[i]->tag & adata->aligned_reads[i]->mask;
    groups[i].mask = adata->aligned_reads[i]->mask;
  }
  if (debug > 1) {
    for (i = 0; i < n_groups; i++) fprintf (stderr, "%llu\t", groups[i].tag);
    fprintf (stderr, "\n");
    for (i = 0; i < n_groups; i++) fprintf (stderr, "%llu\t", groups[i].mask);
    fprintf (stderr, "\n");
  }
  memset (adata->is_compat, 0, n_groups * MAX_GROUPS);
  memset (adata->n_common, 0, n_groups * MAX_GROUPS * 2);

  while (n_groups > 1) {
    for (i = 0; i < n_groups; i++) {
      for (j = 0; j < n_groups; j++) {
        if (j == i) {
          adata->is_compat[i][j] = 0;
          adata->n_common[i][j] = 0;
          continue;
        }
        unsigned long long common = groups[i].mask & groups[j].mask;
        if ((groups[i].tag & common) == (groups[j].tag & common)) {
          /* Groups are compatible */
          adata->is_compat[i][j] = 1;
        } else {
          adata->is_compat[i][j] = 0;
        }
        adata->n_common[i][j] = 0;
        while (common != 0) {
          if (common & 7) adata->n_common[i][j] += 1;
          common = common >> 3;
        }
      }
    }
    unsigned int max_i = 0;
    unsigned int max_j = 0;
    unsigned int found = 0;
    for (i = 0; i < n_groups; i++) {
      for (j = i + 1; j < n_groups; j++) {
        if (adata->is_compat[i][j]) {
          if (!found) {
            max_i = i;
            max_j = j;
            found = 1;
          } else {
            if (adata->n_common[i][j] > adata->n_common[max_i][max_j]) {
              max_i = i;
              max_j = j;
            } else if (adata->n_common[i][j] == adata->n_common[max_i][max_j]) {
              if ((groups[i].size + groups[j].size) > (groups[max_i].size + groups[max_j].size)) {
                max_i = i;
                max_j = j;
              }
            }
          }
        }
      }
    }
    if (max_i == max_j) break;
    /* Can merge groups i and j */
    if (debug > 0) fprintf (stderr, "Merging groups %u (size %u) and %u (size %u) (common %u): %llu %llu %llu %llu -> ", max_i, groups[max_i].size, max_j, groups[max_j].size, adata->n_common[max_i][max_j], groups[max_i].tag, groups[max_i].mask, groups[max_j].tag, groups[max_j].mask);
    groups[max_i].tag = (groups[max_i].tag & groups[max_i].mask) | (groups[max_j].tag & groups[max_j].mask);
    groups[max_i].mask = groups[max_i].mask | groups[max_j].mask;
    groups[max_i].size += groups[max_j].size;
    if (debug > 0) fprintf (stderr, "%llu %llu\n", groups[max_i].tag, groups[max_i].tag);
    for (i = 0; i < adata->na; i++) if (adata->aligned_reads[i]->group == max_j) adata->aligned_reads[i]->group = max_i;
    n_groups -= 1;
    groups[max_j].tag = groups[n_groups].tag;
    groups[max_j].mask = groups[n_groups].mask;
    groups[max_j].size = groups[n_groups].size;
    for (i = 0; i < adata->na; i++) if (adata->aligned_reads[i]->group == n_groups) adata->aligned_reads[i]->group = max_j;
  }
  if (debug > 1) fprintf (stderr, "Num remaining groups: %u\n", n_groups);
  
  /* Calculate group min and max */
  for (i = 0; i < n_groups; i++) {
    groups[i].min_cov = adata->na;
    for (j = 0; j < adata->p_len; j++) {
      unsigned int cov = 0;
      for (k = 0; k < adata->na; k++) {
        if (adata->aligned_reads[k]->group != i) continue;
        if (adata->alignment[k][j] <= GAP) cov += 1;
      }
      if (cov < groups[i].min_cov) groups[i].min_cov = cov;
      if (cov > groups[i].max_cov) groups[i].max_cov = cov;
      if (cov) {
        if (j <= max_uncovered) groups[i].has_start = 1;
        if (j >= (adata->p_len - 1 - max_uncovered)) groups[i].has_end = 1;
      }
    }
    /* Calculate compat */
    for (j = 0; j < adata->na; j++) {
      unsigned long long common = groups[i].mask & adata->aligned_reads[j]->mask;
      if ((groups[i].tag & common) == (adata->aligned_reads[j]->tag & common)) {
        /* Read is compatible with this group */
        groups[i].compat += 1;
      }
    }
  }

  /* Create group consensus and calculate divergences */
  unsigned int *g_cons = (unsigned int *) malloc (n_groups * adata->p_len * 4);
  unsigned int last_aligned_ref = N;
  unsigned int last_consensus = N;
  for (j = 0; j < n_groups; j++) {
    groups[j].consensus = &g_cons[j * adata->p_len];
    for (i = 0; i < adata->p_len; i++) {
      unsigned int c[10] = { 0 };
      for (k = 0; k < adata->na; k++) {
        if (adata->aligned_reads[k]->group == j) c[adata->alignment[k][i]] += 1;
      }
      unsigned int best = adata->aligned_ref[i];
      for (k = 0; k <= GAP; k++) {
        if (k == N) continue;
        if ((adata->nucl_counts[i][k] > 1) && (c[k] > c[best])) best = k;
      }
      g_cons[j * adata->p_len + i] = best;
      if (adata->ref_pos[i] == 952510) {
        fprintf (stderr, "Group %u consensus %u counts %u %u %u %u %u nucl counts %u %u %u %u %u\n", j, best, c[0], c[1], c[2], c[3], c[4], adata->nucl_counts[i][0], adata->nucl_counts[i][1], adata->nucl_counts[i][2], adata->nucl_counts[i][3], adata->nucl_counts[i][4]);
      }
      if (best != adata->aligned_ref[i]) {
        unsigned int snv;
        if (debug > 0) fprintf (stderr, "Divergent position in group %u %u:%u\n", j, adata->chr, adata->ref_pos[i]);
        snv = lookup_snv (snvs, n_snvs, adata->chr, adata->start + i);
        if ((snv < n_snvs) && (snvs[snv].chr == adata->chr) && (snvs[snv].pos == (adata->start + i))) {
          fprintf (stderr, "Known SNV (%c/%c)\n", n2c[snvs[snv].ref_allele], n2c[snvs[snv].alt_allele]);
        } else {
          if (debug > 0) fprintf (stderr, "Potential DeNovo\n");
          if (((last_aligned_ref != GAP) || (adata->aligned_ref[i] != GAP)) && ((last_consensus != GAP) || (best != GAP))) {
            groups[j].divergent += 1;
          }
        }
      }
      last_aligned_ref = adata->aligned_ref[i];
      last_consensus = best;
    }
  }

  /* Sort groups by divergence/size */
  for (i = 0; i < n_groups; i++) {
    for (j = i + 1; j < n_groups; j++) {
      if ((groups[j].divergent < groups[i].divergent) || ((groups[j].divergent == groups[i].divergent) && (groups[j].size > groups[i].size))) {
        Group t = groups[i];
        groups[i] = groups[j];
        groups[j] = t;
        for (k = 0; k < adata->na; k++) {
          if (adata->aligned_reads[k]->group == i) {
            adata->aligned_reads[k]->group = j;
          } else {
            if (adata->aligned_reads[k]->group == j) adata->aligned_reads[k]->group = i;
          }
        }
      }
    }
  }

  if (debug > 1) {
    for (i = 0; i < n_groups; i++) fprintf (stderr, "%llu\t", groups[i].tag);
    fprintf (stderr, "\n");
    for (i = 0; i < n_groups; i++) fprintf (stderr, "%llu\t", groups[i].mask);
    fprintf (stderr, "\n");
  }

  if (debug > 1) {
    fprintf (stderr, "Read groups:");
    for (i = 0; i < adata->na; i++) fprintf (stderr, " %u:%u", i, adata->aligned_reads[i]->group);
    fprintf (stderr, "\n");
  }

  if (debug_groups > 0) {
    for (i = 0; i < n_groups; i++) {
      unsigned int j;
      if (debug_groups > 0) fprintf (stderr, "Group %u size %u divergent %u, min %u max %u\n", i, groups[i].size, groups[i].divergent, groups[i].min_cov, groups[i].max_cov);
      if (debug_groups > 1) {
        for (j = 0; j < adata->p_len; j++) fprintf (stderr, "%c", n2c[groups[i].consensus[j]]);
        fprintf (stderr, "\n");
        for (j = 0; j < adata->na; j++) {
          if (adata->aligned_reads[j]->group == i) {
            fprintf (stderr, "%s\n", adata->aligned_reads[j]->name);
          }
        }
      }
    }
  }

  /* Discard groups with too many divergent positions */
  unsigned int min_div = groups[0].divergent;
  for (i = 0; i < n_groups; i++) if (groups[i].divergent < min_div) min_div = groups[i].divergent;
  unsigned int good_groups[2];
  unsigned int n_included = 0;
  for (i = 0; i < n_groups; i++) {
    groups[i].included = (n_included < 2);
    if (!groups[i].has_start) {
      groups[i].included = 0;
      fprintf (stderr, "Discarded group %u (%u): Start position not covered\n", i, groups[i].size);
    }
    if (!groups[i].has_end) {
      groups[i].included = 0;
      fprintf (stderr, "Discarded group %u (%u): End position not covered\n", i, groups[i].size);
    }
    if (groups[i].min_cov < min_group_coverage) {
      groups[i].included = 0;
      if (debug_groups > 0) fprintf (stderr, "Discarded group %u (%u): Minimum coverage is 0\\n", i, groups[i].size);
    }
    if (groups[i].size < min_group_size) {
      groups[i].included = 0;
      if (debug_groups > 0) fprintf (stderr, "Discarded group %u (%u): size too small (%u < %u)\n", i, groups[i].size, groups[i].size, min_group_size);
    }
    if (groups[i].divergent > max_group_divergence) {
      groups[i].included = 0;
      if (debug_groups > 0) fprintf (stderr, "Discarded group %u (%u): too big divergence (%u > %u)\n", i, groups[i].size, groups[i].divergent, max_group_divergence);
    }
    if (groups[i].divergent > (min_div + max_group_rdivergence)) {
      groups[i].included = 0;
      if (debug_groups > 0) fprintf (stderr, "Discarded group %u (%u): too big relative divergence (%u > %u)\n", i, groups[i].size, groups[i].divergent, min_div + max_group_rdivergence);
    }
    if ((float) groups[i].size < (groups[0].size * min_group_rsize)) {
      groups[i].included = 0;
      if (debug_groups > 0) fprintf (stderr, "Discarded group %u (%u): relative size too small (%.2f < %.2f)\n", i, groups[i].size, (double) groups[i].size / groups[0].size, min_group_rsize);
    }
    if (groups[i].included) good_groups[n_included++] = i;
  }

  if (n_included < 1) {
    free (g_cons);
    return 0;
  }

  unsigned int max_cov_0 = groups[good_groups[0]].max_cov;
  unsigned int div_0 = groups[good_groups[0]].divergent;
  unsigned int compat_0 = groups[good_groups[0]].compat;
  unsigned int max_cov_1 = 0;
  unsigned int div_1 = 0;
  unsigned int compat_1 = 0;
  unsigned int compat_both = 0;
  if (n_included > 1) {
    max_cov_1 = groups[good_groups[1]].max_cov;
    div_1 = groups[good_groups[1]].divergent;
    compat_1 = groups[good_groups[1]].compat;
    /* Calculate compatible common */
    for (j = 0; j < adata->na; j++) {
      unsigned long long common = groups[good_groups[0]].mask & adata->aligned_reads[j]->mask;
      if ((groups[good_groups[0]].tag & common) != (adata->aligned_reads[j]->tag & common)) continue;
      common = groups[good_groups[1]].mask & adata->aligned_reads[j]->mask;
      if ((groups[good_groups[1]].tag & common) != (adata->aligned_reads[j]->tag & common)) continue;
      /* Read is compatible with both groups */
      compat_both += 1;
    }
  }
    
  if (debug_groups > 0) {
    for (i = 0; i < n_groups; i++) {
      unsigned int j;
      if (debug_groups > 0) fprintf (stderr, "Group %u size %u divergent %u, min %u max %u, included %u\n", i, groups[i].size, groups[i].divergent, groups[i].min_cov, groups[i].max_cov, groups[i].included);
      if (debug_groups > 1) {
        for (j = 0; j < adata->p_len; j++) fprintf (stderr, "%c", n2c[groups[i].consensus[j]]);
        fprintf (stderr, "\n");
        for (j = 0; j < adata->na; j++) {
          if (adata->aligned_reads[j]->group == i) {
            fprintf (stderr, "%s\n", adata->aligned_reads[j]->name);
          }
        }
      }
    }
  }

  /* Recalculate totals */
  unsigned int max_coverage = 0;
  memset (adata->coverage, 0, adata->p_len * 2);
  memset (adata->nucl_counts, 0, adata->p_len * (GAP + 1) * 2);
  for (i = 0; i < adata->p_len; i++) {
    unsigned int j;
    for (j = 0; j < adata->na; j++) {
      if (adata->ref_pos[i] == 952510) {
        fprintf (stderr, "Read %u group %u included %u nucl %u\n", j, adata->aligned_reads[j]->group, groups[adata->aligned_reads[j]->group].included, adata->alignment[j][i]);
      }
      unsigned int grp = adata->aligned_reads[j]->group;
      if (!groups[grp].included) continue;
      if (adata->alignment[j][i] <= GAP) {
        int nucl = adata->alignment[j][i];
        if (nucl != groups[grp].consensus[i]) continue;
        adata->nucl_counts[i][nucl] += 1;
        adata->coverage[i] += 1;
      }
    }
    if (adata->coverage[i] > max_coverage) max_coverage = adata->coverage[i];
  }

  /* Call */
  unsigned int last_call_pos = 0;
  unsigned int sub = 0;
  unsigned int call_alignment[1024];
  CallBlock *cb = adata->cblock;
  cb->n_calls = 0;
  for (i = 0; i < adata->p_len; i++) {
    Call *call = &cb->calls[cb->n_calls];
    CallExtra extra;

    /* NC if too small coverage */
    /* if (adata->coverage[i] < min_coverage) continue; */

    memset (call, 0, sizeof (Call));
    call->pos = adata->ref_pos[i];

    if (call->pos == last_call_pos) {
      sub += 1;
    } else {
      sub = 0;
    }
    call->sub = sub;
    last_call_pos = call->pos;
    
    call->ref = adata->aligned_ref[i];
    if (call->ref == GAP) {
      /* Insertion, prev is reference nucleotide */
      call->prev_ref = adata->ref[call->pos - adata->start];
    } else {
      if (call->pos > adata->start) {
        call->prev_ref = adata->ref[call->pos - adata->start - 1];
      } else {
        call->prev_ref = '!';
      }
    }
    call->cov = adata->coverage[i];
    for (j = A; j <= GAP; j++) {
      call->counts[j] = adata->nucl_counts[i][j];
    }
    call->nucl[0] = call->nucl[1] = NONE;

    extra.n_groups_total = n_groups;
    extra.n_groups = n_included;
    extra.div_0 = div_0;
    extra.div_1 = div_1;
    extra.max_cov_0 = max_cov_0;
    extra.max_cov_1 = max_cov_1;
    extra.compat_0 = compat_0;
    extra.compat_1 = compat_1;
    extra.compat_both = compat_both;
    extra.end_dist = (i < (adata->p_len - 1 - i)) ? i : adata->p_len - 1 - i;
#if 0
    /* NC if too close to end */
    if ((i < min_end_distance) || (i > (adata->p_len - 1 - min_end_distance))) continue;
#endif

    unsigned int best = 0, n;
    unsigned int fp;
    fp = lookup_snv (fps, n_fps, adata->chr, adata->start + i);
    /* fprintf (stderr, "FP (%u:%u) %u\n", adata->chr, adata->start + i, fp); */
    /* NC if known false positive */
    if ((fp < n_fps) && (fps[fp].chr == adata->chr) && (fps[fp].pos == (adata->start + i))) continue;

    for (n = A; n <= GAP; n++) if (adata->nucl_counts[i][n] > best) best = adata->nucl_counts[i][n];
    /* NC if too small coverage of most numerous allele */
    if (best < min_confirming) continue;
    unsigned int n1, n2, best_n1 = A, best_n2 = A;
    float best_prob = 0;
    float sum_probs = 0;
    for (n1 = A; n1 <= GAP; n1++) {
      if (n1 == N) continue;
      unsigned int c1 = adata->nucl_counts[i][n1];
      if (c1 < 2) continue;
      for (n2 = n1; n2 <= GAP; n2++) {
        float prob;
        if (n2 == N) continue;
        unsigned int c2 = adata->nucl_counts[i][n2];
        if (c2 < 2) continue;
        if (n2 == n1) {
          prob = gt1_prob (c1, adata->coverage[i] - adata->nucl_counts[i][N]);
        } else {
          prob = gt2_prob (c1, c2, adata->coverage[i] - adata->nucl_counts[i][N]);
        }
        if (prob > best_prob) {
          best_n1 = n1;
          best_n2 = n2;
          best_prob = prob;
        }
        sum_probs += prob;
      }
    }
    double p = 1;
    if (best_n1 != best_n2) {
      /* Heterozygote */
      p = dbinom (adata->nucl_counts[i][best_n2], adata->nucl_counts[i][best_n1] + adata->nucl_counts[i][best_n2], 0.5);
    }
    if (!sum_probs) {
      best_prob = 0;
      sum_probs = 1;
    }
    call->nucl[0] = best_n1;
    call->nucl[1] = best_n2;
    call->poly = ((best_n1 != adata->aligned_ref[i]) || (best_n2 != adata->aligned_ref[i]));

    extra.prob = best_prob;
    extra.rprob = best_prob / sum_probs;
    extra.hzprob = p;

    call->p = calc_p (call, &extra, (coverage >= 0) ? coverage : max_coverage);
    call->extra = extra;
    call_alignment[cb->n_calls] = i;
    cb->n_calls += 1;
  }
  /*cb->n_calls = adata->p_len;*/

  /* Output alignment */
  if (print) {
    print_header (stdout);
    if (debug) {
      fprintf (stdout, "\t ");
      for (i = 0; i < n_included; i++) {
        unsigned int j;
        fprintf (stdout, "       ");
        for (j = 0; j < adata->na; j++) {
          if (adata->aligned_reads[j]->group == good_groups[i]) fprintf (stdout, "%c", 'A' + i);
        }
      }
    }
    fprintf (stdout, "\n");
    for (i = 0; i < cb->n_calls; i++) {
      print_call (cb, i, 1, 0);
      if (debug_groups) {
        /* Aligned reads */
        unsigned int a_i = call_alignment[i];
        fprintf (stdout, "\t%c", n2c[adata->aligned_ref[a_i]]);
        for (j = 0; j < n_groups; j++) {
          fprintf (stdout, "  [%c%c] ", n2c[groups[j].consensus[a_i]], (groups[j].consensus[a_i] == adata->aligned_ref[a_i]) ? ' ' : '*');
          for (k = 0; k < adata->na; k++) {
            if (adata->aligned_reads[k]->group == j) fprintf (stdout, "%c", n2c[adata->alignment[k][a_i]]);
          }
        }
      }
      fprintf (stdout, "\n");
    }
  }
  free (g_cons);
  
  return adata->p_len;
}

static int
assemble (AssemblyData *adata, const char *kmers[], unsigned int nkmers, unsigned int print)
{
  unsigned int i;
  int result;

  /* Print virtual command line to simplify debugging */
  if (debug) {
    fprintf (stderr, "Arguments: -db %s --reference %s %u %u ", db_name, chr_names[adata->chr], adata->start, adata->end);
    for (i = adata->start; i < adata->end; i++) fprintf (stderr, "%c", adata->ref[i - adata->start]);
    for (i = 0; i < nkmers; i++) fprintf (stderr, " %s", kmers[i]);
    fprintf (stderr, "\n");
  }
  result = align (adata, kmers, nkmers);
  if (result > 0) {
    result = group (adata, print);
  }
  if (result <= 0) {
    /* Fill call block with NC-s */
    CallBlock *cb = adata->cblock;
    cb->n_calls = adata->end - adata->start;
    for (i = 0; i < cb->n_calls; i++) {
      memset (&cb->calls[i], 0, sizeof (Call));
      cb->calls[i].pos = adata->start + i;
      cb->calls[i].ref = adata->ref_seq->pos[i].nucl;
    }
  }
  
  return result;
}

/*
 * Align all reads to reference, discard too divergent
 * 
 * Return number of remaining reads
 * a is an alignment table (num_reads * reference_length)
 */

static const unsigned int max_endgap = 1;
static const unsigned int max_gaps = 10;

static void
test_alignment (const char *a, const char *b)
{
  NSeq *a_seq, *b_seq;
  unsigned int ref_p[MAX_REFERENCE_LENGTH], read_p[MAX_READ_LENGTH];
  unsigned int align_len;
  SWCell *sw_matrix = (SWCell *) malloc (1000 * 1000 * sizeof (SWCell));
  a_seq = n_seq_new (a, 25);
  b_seq = n_seq_new (b, 25);
  align_len = smith_waterman_seq (ref_p, read_p, a_seq, b_seq, sw_matrix, 1);
  print_alignment (stdout, ref_p, read_p, align_len, a_seq, b_seq);
}


unsigned int
align_reads_to_reference (NSeq *ref_seq, GASMRead *reads[], unsigned int nreads, GASMRead *a_reads[], short a[][MAX_REFERENCE_LENGTH], SWCell *sw_matrix)
{
  unsigned int i;
  unsigned int na = 0;
  assert (ref_seq->len <= MAX_REFERENCE_LENGTH);
  assert (nreads <= MAX_READS);
  for (i = 0; i < nreads; i++) {
    unsigned int ref_p[MAX_REFERENCE_LENGTH], read_p[MAX_READ_LENGTH];
    unsigned int align_len, n_divergent, n_gaps, s_gap = 0, e_gap = 0, gaps_total;
    int j, r_p;
    unsigned int last;
    
    /* Align seq to reference */
    align_len = smith_waterman_seq (ref_p, read_p, ref_seq, reads[i]->nseq, sw_matrix, 0 /* (i == 14) && (na == 9)*/);
    /* Calculate divergence */
    n_divergent = count_divergent_from_alignment (ref_seq, reads[i]->nseq, ref_p, read_p, align_len, &n_gaps, &s_gap, &e_gap, &gaps_total);
    if (debug) fprintf (stderr, "Read %u: %u divergen %u gaps %u gap length start %u end %u\n", i, n_divergent, n_gaps, gaps_total, s_gap, e_gap);

    if (debug > 1) {
      fprintf (stderr, ">%u/%u\n", i, na);
      print_alignment (stderr, ref_p, read_p, align_len, ref_seq, reads[i]->nseq);
    }

    /* Ignore too divergent */
    if (n_divergent > max_divergent) {
      if (debug > 0) {
        fprintf (stderr, "Read %u: %s\n", i, reads[i]->seq);
        fprintf (stderr, "  has too many divergences: %u total, %u gaps (len = %u)\n", n_divergent, n_gaps, gaps_total);
      }
      continue;
    }
    /* Ignore too short alignments */
    if (align_len < min_align_len) {
      if (debug > 0) {
        fprintf (stderr, "Read %u: %s\n", i, reads[i]->seq);
        fprintf (stderr, "  has too short alignment: %u\n", align_len);
      }
      continue;
    }
    /* Ignore end and start gaps */
    if ((s_gap > max_endgap) || (e_gap > max_endgap)) {
      if (debug > 0) {
        fprintf (stderr, "Read %u: %s\n", i, reads[i]->seq);
        fprintf (stderr, "  has too long endgaps: %u/%u\n", s_gap, e_gap);
      }
      continue;
    }
    /* Ignore too many gaps */
    if (gaps_total > max_gaps) {
      /* fixme: Potential long indel */
      if (debug > 0) {
        fprintf (stderr, "Read %u: %s\n", i, reads[i]->seq);
        fprintf (stderr, "  has too long gaps: %u\n", gaps_total);
      }
      continue;
    }
    a_reads[na] = reads[i];

    /* fixme: Remove this */
    for (j = 0; j < ref_seq->len; j++) a[na][j] = -1000;
    /* Initial part */
    for (j = 0; j < ref_p[0]; j++) {
      int d = j - (int) ref_p[0];
      r_p = (int) read_p[0] + d;
      a[na][j] = (r_p < 0) ? BEFORE : UNKNOWN;
    }
    /* Aligned part */
    a[na][ref_p[0]] = read_p[0];
    last = ref_p[0];
    for (j = 1; j < align_len; j++) {
      unsigned int k;
      for (k = last + 1; k < ref_p[j]; k++) a[na][k] = a[na][last];
      if (ref_p[j] > ref_p[j - 1]) a[na][ref_p[j]] = read_p[j];
      last = ref_p[j];
    }
    /* Final part */
    for (j = ref_p[align_len - 1] + 1; j < ref_seq->len; j++) {
      int d = j - (int) ref_p[align_len - 1];
      r_p = (int) read_p[align_len - 1] + d;
      a[na][j] = (r_p >= a_reads[na]->nseq->len) ? AFTER : UNKNOWN;
    }
    for (j = 0; j < ref_seq->len; j++) {
      assert (a[na][j] >= -3);
      assert (a[na][j] < 1000);
    }
    na += 1;
    if (na >= MAX_ALIGNED_READS) {
      fprintf (stderr, "align_reads_to_reference: maximum number of aligned reads (%u) achieved\n", MAX_ALIGNED_READS);
      break;
    }
  }
  assert (na <= MAX_ALIGNED_READS);
  return na;
}

unsigned int
create_gapped_alignment (NSeq *ref_seq, unsigned int ref_start, GASMRead *a_reads[], unsigned int na, short a[][MAX_REFERENCE_LENGTH], unsigned int aligned_ref[], int ref_pos[], short _p[][MAX_REFERENCE_LENGTH * 2])
{
  int ref_p = 0, last_ref_p = UNKNOWN;
  int read_p[1024], last_read_p[1024];
  unsigned int p_len = 0;
  unsigned int i;
  for (i = 0; i < na; i++) {
    read_p[i] = a[i][0];
    last_read_p[i] = UNKNOWN;
  }
  while (ref_p < ref_seq->len) {
    /* Write alignment */
    /* Ref */
    if ((last_ref_p < 0) || (ref_p > last_ref_p)) {
      aligned_ref[p_len] = ref_seq->pos[ref_p].nucl;
      ref_pos[p_len] = ref_start + ref_p;
      last_ref_p = ref_p;
    } else {
      aligned_ref[p_len] = GAP;
      ref_pos[p_len] = ref_start + ref_p;
    }
    /* Reads */
    for (i = 0; i < na; i++) {
      if ((read_p[i] >= 0) && ((last_read_p[i] < 0) || (read_p[i] > last_read_p[i]))) {
        _p[i][p_len] = a_reads[i]->nseq->pos[read_p[i]].nucl;
        last_read_p[i] = read_p[i];
      } else if (read_p[i] >= 0) {
        _p[i][p_len] = GAP;
      } else {
        _p[i][p_len] = NONE;
      }
    }
    /* Advance */
    int rgap = 1;
    if (ref_p < (ref_seq->len - 1)) {
      int next_ref_p = ref_p + 1;
      for (i = 0; i < na; i++) {
        int next_read_p = a[i][next_ref_p];
        if ((read_p[i] >= 0) && (next_read_p >= 0)) {
          int gap = next_read_p - read_p[i];
          if (gap > rgap) rgap = gap;
        }
      }
    }
    if (ref_p < (ref_seq->len - 1)) {
      int next_ref_p = ref_p + 1;
      for (i = 0; i < na; i++) {
        int next_read_p = a[i][next_ref_p];
        if (next_read_p >= 0) {
          if (read_p[i] < 0) {
            if (rgap == 1) read_p[i] = next_read_p;
          } else if (read_p[i] < next_read_p) {
            int delta = next_read_p - read_p[i];
            if (delta == rgap) read_p[i] += 1;
          }
        } else {
          read_p[i] = next_read_p;
        }
      }
    }
    if (rgap == 1) ref_p += 1;
    p_len += 1;
  }
  return p_len;
}

static void
print_alignment (FILE *ofs, unsigned int a_pos[], unsigned int b_pos[], unsigned int len, NSeq *a, NSeq *b)
{
  int left, i, last_a, last_b;
  /* A */
  /* Left unaligned part */
  left = (a_pos[0] > b_pos[0]) ? a_pos[0] : b_pos[0];
  for (i = 0; i < left; i++) {
    int a_p = (int) a_pos[0] - (left - i);
    if (a_p >= 0) {
      fprintf (ofs, "%c", n2c[a->pos[a_p].nucl]);
    } else {
      fprintf (ofs, " ");
    }
  }
  /* Aligned part */
  last_a = a_pos[0];
  last_b = b_pos[0];
  for (i = 0; i < len; i++) {
    while ((int) b_pos[i] > last_b) {
      fprintf (ofs, "-");
      last_b += 1;
    }
    while (last_a <= (int) a_pos[i]) {
      fprintf (ofs, "%c", n2c[a->pos[last_a].nucl]);
      last_a += 1;
    }
    last_b = b_pos[i] + 1;
  }
  /* Final unaligned part */
  for (i = a_pos[len - 1] + 1; i < a->len; i++) {
    fprintf (ofs, "%c", n2c[a->pos[i].nucl]);
  }
  fprintf (ofs, "\n");
  
  /* Alignment */
  /* Left */
  for (i = 0; i < left; i++) {
    fprintf (ofs, " ");
  }
  /* Aligned part */
  last_a = a_pos[0];
  last_b = b_pos[0];
  for (i = 0; i < len; i++) {
    while ((int) b_pos[i] > last_b) {
      fprintf (ofs, " ");
      last_b += 1;
    }
    while ((int) a_pos[i] > last_a) {
      fprintf (ofs, " ");
      last_a += 1;
    }
    if (a->pos[a_pos[i]].nucl == b->pos[b_pos[i]].nucl) {
      fprintf (ofs, "|");
    } else {
      fprintf (stderr, " ");
    }
    last_a = a_pos[i] + 1;
    last_b = b_pos[i] + 1;
  }
  fprintf (ofs, "\n");

  /* B */
  /* Left unaligned part */
  for (i = 0; i < left; i++) {
    int b_p = (int) b_pos[0] - (left - i);
    if (b_p >= 0) {
      fprintf (ofs, "%c", n2c[b->pos[b_p].nucl]);
    } else {
      fprintf (ofs, " ");
    }
  }
  /* Aligned part */
  last_a = a_pos[0];
  last_b = b_pos[0];
  for (i = 0; i < len; i++) {
    while ((int) a_pos[i] > last_a) {
      fprintf (ofs, "-");
      last_a += 1;
    }
    while (last_b <= (int) b_pos[i]) {
      fprintf (ofs, "%c", n2c[b->pos[last_b].nucl]);
      last_b += 1;
    }
    last_a = a_pos[i] + 1;
  }
  /* Final unaligned part */
  for (i = b_pos[len - 1] + 1; i < b->len; i++) {
    fprintf (ofs, "%c", n2c[b->pos[i].nucl]);
  }
  fprintf (ofs, "\n");
}

/* Smith-Waterman algorithm */

#define M_SCORE 1
#define N_SCORE 0
#define MM_SCORE -2
#define GAP_OPEN_SCORE -2
#define GAP_SCORE -1

#define NROWS (n + 1)
#define NCOLS (m + 1)

#define TBACK 10

unsigned int
smith_waterman_seq (unsigned int a_pos[], unsigned int b_pos[], const NSeq *a, const NSeq *b, SWCell *t, unsigned int debug)
{
  unsigned int i, j;
  unsigned int n = a->len, m = b->len;
  /* Fill first column with zeroes */
  /* Fill first row with zeroes */
  /* Fill table starting from 1-st row and pick maximum score */
  int max_i = 0, max_j = 0;
  memset (t, 0, NCOLS * sizeof (SWCell));
  for (j = 0; j <= m; j++) {
    t[0 * NCOLS + j].left_gap_score = -1000;
    t[0 * NCOLS + j].top_gap_score = -1000;
  }
  for (i = 1; i <= n; i++) {
    memset (t + i * NCOLS, 0, sizeof (SWCell));
    t[i * NCOLS + 0].left_gap_score = -1000;
    t[i * NCOLS + 0].top_gap_score = -1000;
    for (j = 1; j <= m; j++) {
      int score = ((a->pos[i - 1].nucl >= N) || (b->pos[j - 1].nucl >= N)) ? N_SCORE : (a->pos[i - 1].nucl == b->pos[j - 1].nucl) ? M_SCORE : MM_SCORE;
      t[i * NCOLS + j].score = 0;
      /* Diagonal */
      if ((t[(i - 1) * NCOLS + (j - 1)].score + score) > 0) {
        t[i * NCOLS + j].score = t[(i - 1) * NCOLS + (j - 1)].score + score;
        t[i * NCOLS + j].sx = -1;
        t[i * NCOLS + j].sy = -1;
      }
#if 1
      /* Left */
      t[i * NCOLS + j].left_gap_score = t[i * NCOLS + j].score + GAP_OPEN_SCORE;
      t[i * NCOLS + j].left_gap_len = 0;
      if ((t[i * NCOLS + (j - 1)].left_gap_score + GAP_SCORE) > t[i * NCOLS + j].left_gap_score) {
        t[i * NCOLS + j].left_gap_score = t[i * NCOLS + (j - 1)].left_gap_score + GAP_SCORE;
        t[i * NCOLS + j].left_gap_len = t[i * NCOLS + (j - 1)].left_gap_len + 1;
      }
      if (t[i * NCOLS + j].left_gap_score >= t[i * NCOLS + j].score) {
        t[i * NCOLS + j].score = t[i * NCOLS + j].left_gap_score;
        t[i * NCOLS + j].sx = -t[i * NCOLS + j].left_gap_len;
        t[i * NCOLS + j].sy = 0;
      }
      t[i * NCOLS + j].top_gap_score = t[i * NCOLS + j].score + GAP_OPEN_SCORE;
      t[i * NCOLS + j].top_gap_len = 0;
      if ((t[(i - 1) * NCOLS + j].top_gap_score + GAP_SCORE) > t[i * NCOLS + j].top_gap_score) {
        t[i * NCOLS + j].top_gap_score = t[(i - 1) * NCOLS + j].top_gap_score + GAP_SCORE;
        t[i * NCOLS + j].top_gap_len = t[(i - 1) * NCOLS + j].top_gap_len + 1;
      }
      if (t[i * NCOLS + j].top_gap_score >= t[i * NCOLS + j].score) {
        t[i * NCOLS + j].score = t[i * NCOLS + j].top_gap_score;
        t[i * NCOLS + j].sx = 0;
        t[i * NCOLS + j].sy = -t[i * NCOLS + j].top_gap_len;
      }
#else
      /* Left */
      if ((t[i * NCOLS + (j - 1)].left_gap_score + GAP_SCORE) > 0) {
        t[i * NCOLS + j].left_gap_score = t[i * NCOLS + (j - 1)].left_gap_score + GAP_SCORE;
        t[i * NCOLS + j].left_gap_len = t[i * NCOLS + (j - 1)].left_gap_len + 1;
        if (t[i * NCOLS + j].left_gap_score >= t[i * NCOLS + j].score) {
          t[i * NCOLS + j].score = t[i * NCOLS + j].left_gap_score;
          t[i * NCOLS + j].sx = -t[i * NCOLS + j].left_gap_len;
          t[i * NCOLS + j].sy = 0;
        } else if ((t[i * NCOLS + j].score + GAP_OPEN_SCORE) >= t[i * NCOLS + j].left_gap_score) {
          t[i * NCOLS + j].left_gap_score = t[i * NCOLS + j].score + GAP_OPEN_SCORE;
          t[i * NCOLS + j].left_gap_len = 0;
        }
      } else {
        t[i * NCOLS + j].left_gap_score = t[i * NCOLS + j].score + GAP_OPEN_SCORE;
        t[i * NCOLS + j].left_gap_len = 0;
      }
      if ((t[(i - 1) * NCOLS + j].top_gap_score + GAP_SCORE) > 0) {
        t[i * NCOLS + j].top_gap_score = t[(i - 1) * NCOLS + j].top_gap_score + GAP_SCORE;
        t[i * NCOLS + j].top_gap_len = t[(i - 1) * NCOLS + j].top_gap_len + 1;
        if (t[i * NCOLS + j].top_gap_score >= t[i * NCOLS + j].score) {
          t[i * NCOLS + j].score = t[i * NCOLS + j].top_gap_score;
          t[i * NCOLS + j].sx = 0;
          t[i * NCOLS + j].sy = -t[i * NCOLS + j].top_gap_len;
        } else if ((t[i * NCOLS + j].score + GAP_OPEN_SCORE) >= t[i * NCOLS + j].top_gap_score) {
          t[i * NCOLS + j].left_gap_score = t[i * NCOLS + j].score + GAP_OPEN_SCORE;
          t[i * NCOLS + j].top_gap_len = 0;
        }
      } else {
        t[i * NCOLS + j].top_gap_score = t[i * NCOLS + j].score + GAP_OPEN_SCORE;
        t[i * NCOLS + j].top_gap_len = 0;
      }
#endif
      if (t[i * NCOLS + j].score > t[max_i * NCOLS + max_j].score) {
        max_i = i;
        max_j = j;
      }
    }
  }
  if (debug) {
    fprintf (stderr, "    ");
    for (j = 0; j < m; j++) fprintf (stderr, "%c          ", n2c[b->pos[j].nucl]);
    fprintf (stderr, "\n");
    for (i = 0; i < n; i++) {
      fprintf (stderr, "%c ", n2c[a->pos[i].nucl]);
      for (j = 0; j < m; j++) {
        fprintf (stderr, "%3d(%2d/%2d)[%2d/%2d/%2d/%2d] ", t[(i + 1) * NCOLS + (j + 1)].score, t[(i + 1) * NCOLS + (j + 1)].sx, t[(i + 1) * NCOLS + (j + 1)].sy,
          t[(i + 1) * NCOLS + (j + 1)].left_gap_score, t[(i + 1) * NCOLS + (j + 1)].left_gap_len, t[(i + 1) * NCOLS + (j + 1)].top_gap_score, t[(i + 1) * NCOLS + (j + 1)].top_gap_len);
      }
      fprintf (stderr, "  %c\n", n2c[a->pos[i].nucl]);
    }
    fprintf (stderr, "    ");
    for (j = 0; j < m; j++) fprintf (stderr, "%c          ", n2c[b->pos[j].nucl]);
    fprintf (stderr, "\n");
  }
  unsigned int len = 0;
  while ((max_i > 0) && (max_j > 0)) {
    int sx = t[max_i * NCOLS + max_j].sx;
    int sy = t[max_i * NCOLS + max_j].sy;
    if (!sx && !sy) break;
    if (t[max_i * NCOLS + max_j].score < 1) break;
    if (sx && sy) {
      a_pos[len] = max_i - 1;
      b_pos[len] = max_j - 1;
      len += 1;
    }
    max_i += sy;
    max_j += sx;
  }
  /* Write result in reverse */
  for (i = 0; i < len / 2; i++) {
    unsigned int t = a_pos[i];
    a_pos[i] = a_pos[len - 1 - i];
    a_pos[len - 1 - i] = t;
    t = b_pos[i];
    b_pos[i] = b_pos[len - 1 - i];
    b_pos[len - 1 - i] = t;
  }
  if (debug) {
    for (i = 0; i < len; i++) fprintf (stderr, "%c", n2c[a->pos[a_pos[i]].nucl]);
    fprintf (stderr, "\n");
    for (i = 0; i < len; i++) fprintf (stderr, "%c", n2c[b->pos[b_pos[i]].nucl]);
    fprintf (stderr, "\n");
  }
  return len;
}

static SNV *
read_snvs (const char *filename, unsigned int *n_snvs)
{
  const unsigned char *cdata;
  unsigned long long csize, cpos;
  unsigned int n_lines;
  SNV *snvs;
  *n_snvs = 0;
  if (!filename) return NULL;
  cdata = gt4_mmap (filename, &csize);
  if (!cdata) return NULL;
  /* Count lines */
  n_lines = 1;
  for (cpos = 0; cpos < csize; cpos++) {
    if (cdata[cpos] == '\n') n_lines += 1;
  }
  snvs = (SNV *) malloc (n_lines * sizeof (SNV));
  cpos = 0;
  while (cpos < csize) {
    if (cdata[cpos] != '#') {
      const unsigned char *tokenz[5];
      unsigned int lengths[5];
      unsigned int ntokenz;
      ntokenz = split_line (cdata + cpos, csize - cpos, tokenz, lengths, 5);
      if (ntokenz < 2) {
        fprintf (stderr, "read_snvs: too few tokens at line %u\n", *n_snvs);
      } else {
        const unsigned char *stok[4];
        unsigned int slen[4];
        char chr[32];
        split_line_chr (tokenz[0], lengths[0], stok, slen, 5, ':');
        if (slen[0] > 31) slen[0] = 31;
        memcpy (chr, stok[0], slen[0]);
        chr[slen[0]] = 0;
        snvs[*n_snvs].chr = chr_from_text (chr);
        if (!snvs[*n_snvs].chr) {
          static unsigned int warned = 0;
          if (!warned) {
            fprintf (stderr, "read_snvs: invalid chromosome name %s\n", chr);
            warned = 1;
          }
        } else {
          snvs[*n_snvs].pos = strtol ((const char *) stok[1], NULL, 10) - 1;
          /*snvs[*n_snvs].id = strndup ((const char *) tokenz[2], lengths[2]);*/
          snvs[*n_snvs].id = "*";
          snvs[*n_snvs].ref_allele = c2n (stok[3][0]);
          snvs[*n_snvs].alt_allele = c2n (stok[3][2]);
          snvs[*n_snvs].genotype = (tokenz[1][0] != 'A') || (tokenz[1][1] != 'A');
          /* if (debug) {
            fprintf (stderr, "SNV %llu %u%u\n", snvs[*n_snvs].pos, snvs[*n_snvs].ref_allele, snvs[*n_snvs].alt_allele);
          } */
          *n_snvs += 1;
        }
      }
    }
    while ((cpos < csize) && (cdata[cpos] != '\n')) cpos += 1;
    while ((cpos < csize) && (cdata[cpos] <= ' ')) cpos += 1;
  }
  return snvs;
}

static SNV *
read_fps (const char *filename, unsigned int *n_snvs)
{
  const unsigned char *cdata;
  unsigned long long csize, cpos;
  unsigned int n_lines;
  SNV *snvs;
  *n_snvs = 0;
  if (!filename) return NULL;
  cdata = gt4_mmap (filename, &csize);
  if (!cdata) return NULL;
  /* Count lines */
  n_lines = 1;
  for (cpos = 0; cpos < csize; cpos++) {
    if (cdata[cpos] == '\n') n_lines += 1;
  }
  snvs = (SNV *) malloc (n_lines * sizeof (SNV));
  memset (snvs, 0, n_lines * sizeof (SNV));
  cpos = 0;
  while (cpos < csize) {
    if (cdata[cpos] != '#') {
      const unsigned char *tokenz[5];
      unsigned int lengths[5];
      unsigned int ntokenz;
      ntokenz = split_line (cdata + cpos, csize - cpos, tokenz, lengths, 5);
      if (ntokenz < 2) {
        fprintf (stderr, "read_fps: too few tokens at line %u\n", *n_snvs);
      } else {
        const unsigned char *stok[4];
        unsigned int slen[4];
        char chr[32];
        split_line_chr (tokenz[0], lengths[0], stok, slen, 5, ':');
        if (slen[0] > 31) slen[0] = 31;
        memcpy (chr, stok[0], slen[0]);
        chr[slen[0]] = 0;
        snvs[*n_snvs].chr = chr_from_text (chr);
        if (!snvs[*n_snvs].chr) {
          static unsigned int warned = 0;
          if (!warned) {
            fprintf (stderr, "read_fps: invalid chromosome name %s\n", chr);
            warned = 1;
          }
        } else {
          snvs[*n_snvs].pos = strtol ((const char *) stok[1], NULL, 10);
          if (debug > 2) fprintf (stderr, "FP: %u %llu\n", snvs[*n_snvs].chr, snvs[*n_snvs].pos);
          *n_snvs += 1;
        }
      }
    }
    while ((cpos < csize) && (cdata[cpos] != '\n')) cpos += 1;
    while ((cpos < csize) && (cdata[cpos] <= ' ')) cpos += 1;
  }
  return snvs;
}

/* Get index of SNV at or next to pos */

static unsigned int
lookup_snv (SNV *snvs, unsigned int n_snvs, unsigned int chr, unsigned long long pos)
{
  unsigned int min, max, mid;
  min = 0;
  max = n_snvs;
  mid = (min + max) / 2;
  /* fprintf (stderr, "Looking up %u %llu\n", chr, pos); */
  while ((mid != min) && (mid != max)) {
    /* fprintf (stderr, "%u\t%u\t%u\t%u\t%llu\n", min, mid, max, snvs[mid].chr, snvs[mid].pos); */
    if (mid >= n_snvs) {
      break;
    } else if (snvs[mid].chr < chr) {
      min = mid;
    } else if (snvs[mid].chr > chr) {
      max = mid;
    } else if (snvs[mid].pos < pos) {
      min = mid;
    } else if (snvs[mid].pos > pos) {
      max = mid;
    } else {
      break;
    }
    mid = (min + max) / 2;
  }
  return mid;
}

static void
load_db_or_die (KMerDB *db, const char *db_name, const char *seq_dir, const char *id)
{
  const unsigned char *cdata;
  unsigned long long csize;
  /* Read database */
  if (debug) fprintf (stderr, "Loading %s database %s... ", id, db_name);
  cdata = gt4_mmap (db_name, &csize);
  if (!cdata) {
    fprintf (stderr, "cannot mmap (no such file?)\n");
    exit (1);
  }
  if (prefetch_db) {
    scout_mmap (cdata, csize);
    sleep (10);
  }
  if (!read_database_from_binary (db, cdata, csize)) {
    fprintf (stderr, "cannot read (wrong file format?)\n");
    exit (1);
  }
  if (!db->index.read_blocks) {
    fprintf (stderr, "no index\n");
    exit (1);
  }
  if (debug) fprintf (stderr, "done\n");
}

static char *
get_seq_name (const char *in_name, const char *seq_dir)
{
  if (!seq_dir) {
    return strdup (in_name);
  } else {
    unsigned int dir_len = (unsigned int) strlen (seq_dir);
    unsigned int len = (unsigned int) strlen (in_name);
    unsigned int i;
    for (i = 0; i < len; i++) {
      if (in_name[len - 1 - i] == '/') break;
    }
    if (i >= len) {
      char *c = (char *) malloc (dir_len + 1 + len + 1);
      memcpy (c, seq_dir, dir_len);
      c[dir_len] = '/';
      memcpy (c + dir_len + 1, in_name, len);
      c[dir_len + 1 + len] = 0;
      return c;
    } else {
      char *c = (char *) malloc (dir_len + 1 + i + 1);
      memcpy (c, seq_dir, dir_len);
      c[dir_len] = '/';
      memcpy (c + dir_len + 1, in_name + len - i, i);
      c[dir_len + 1 + i] = 0;
      return c;
    }
  }
}

static SeqFile *
map_sequences (KMerDB *db, const char *seq_dir)
{
  unsigned int i;
  SeqFile *files = (SeqFile *) malloc (db->index.n_files * sizeof (SeqFile));
  memset (files, 0, db->index.n_files * sizeof (SeqFile));
  for (i = 0; i < db->index.n_files; i++) {
    files[i].name = get_seq_name (db->index.files[i], seq_dir);
    if (!files[i].cdata) {
      files[i].cdata = gt4_mmap (files[i].name, &files[i].csize);
      if (!files[i].cdata) {
        fprintf (stderr, "Cannot memory map %s\n", files[i].name);
        free (files);
        return NULL;
      }
      if (prefetch_seq) {
        scout_mmap (files[i].cdata, files[i].csize);
      }
    }
  }
  return files;
}

static unsigned int
get_unique_reads (ReadInfo reads[], unsigned int max_reads, KMerDB *db, SeqFile files[], const char *kmers[], unsigned int nkmers)
{
  unsigned int nreads = 0, i;

  for (i = 0; i < nkmers; i++) {
    unsigned long long first_read;
    unsigned int n_reads, n_new_reads, j, kmer_dir;
    unsigned long long word, rword;
    unsigned int code, node_idx, node_kmer, kmer_idx;
  
    word = string_to_word (kmers[i], strlen (kmers[i]));
    rword = get_reverse_complement (word, strlen (kmers[i]));
    if (rword < word) word = rword;
    code = trie_lookup (&db->trie, word);
    if (!code) {
      fprintf (stderr, "No such kmer: %s\n", kmers[i]);
      exit (0);
    }
    kmer_dir = ((code & 0x80000000) != 0);
    if (debug > 1) fprintf (stderr, "Kmer %s word %llu code %u\n", kmers[i], word, code);
    code &= 0x7fffffff;
    node_idx = (code >> db->kmer_bits) - 1;
    node_kmer = code & ((1 << db->kmer_bits) - 1);
    kmer_idx = db->nodes[node_idx].kmers + node_kmer;
    if (debug > 1) fprintf (stderr, "Node %u kmer %u idx %u dir %u\n", node_idx, node_kmer, kmer_idx, kmer_dir);
    if (debug > 2) print_db_reads (&db->index, files, kmer_idx, kmer_dir, stderr);
    first_read = gt4_index_get_kmer_info (&db->index, kmer_idx, &n_reads);
    if (n_reads > MAX_READS_PER_KMER) {
      if (debug > 1) fprintf (stderr, "Kmer %u has too many reads: %u\n", i, n_reads);
      continue;
    }
    if (debug > 1) fprintf (stderr, "Num reads %u\n", n_reads);
    n_new_reads = 0;
    for (j = 0; j < n_reads; j++) {
      unsigned long long name_pos;
      unsigned int file_idx, dir;
      unsigned int k;
      gt4_index_get_read_info (&db->index, first_read + j, &file_idx, &name_pos, &dir);
      for (k = 0; k < nreads; k++) {
        if ((reads[k].file_idx == file_idx) && (reads[k].name_pos == name_pos)) break;
      }
      if (k >= nreads) {
        n_new_reads += 1;
        if (debug > 1) fprintf (stderr, "Adding read %u dir %u\n", nreads, dir);
        reads[nreads].name_pos = name_pos;
        /* Reads are shared between k-kmers */
        reads[nreads].kmer_pos = 0;
        reads[nreads].file_idx = file_idx;
        /* fixme: What to do if two kmers have conflicting read directions? */
        reads[nreads].dir = (dir != kmer_dir);
        nreads += 1;
        if (nreads >= max_reads) break;
      } else {
        if (debug > 2) fprintf (stderr, "  Already registered as %u\n", k);
      }
    }
    if (debug > 1) fprintf (stderr, "Kmer %u %s reads %u new %u\n", i, kmers[i], n_reads, n_new_reads);
  }
  return nreads;
}

static unsigned int
get_read_sequences (GASMRead *seqs[], const ReadInfo reads[], unsigned int nreads, SeqFile files[])
{
  unsigned int i;
  /* Create actual sequences */
  /* Change directionality of reads if needed */
  for (i = 0; i < nreads; i++) {
    char name[2048], seq[2048];
    if (!files[reads[i].file_idx].cdata) {
      files[reads[i].file_idx].cdata = gt4_mmap (files[reads[i].file_idx].name, &files[reads[i].file_idx].csize);
      if (!files[reads[i].file_idx].cdata) {
        fprintf (stderr, "Cannot mmap %s\n", files[reads[i].file_idx].name);
        return 0;
      }
    }
    const unsigned char *p = files[reads[i].file_idx].cdata + reads[i].name_pos;
    unsigned int len = 0;
    while (p[len] != '\n') len += 1;
    memcpy (name, p, len);
    name[len] = 0;
    p += len;
    p += 1;
    len = 0;
    while (p[len] >= 'A') len += 1;
    if (len > 2047) len = 2047;
    memcpy (seq, p, len);
    seq[len] = 0;
    if (reads[i].dir) gt4_string_revcomp_inplace (seq, len);
    seqs[i] = gasm_read_new (name, seq, WORDLEN);
    if (debug > 1) fprintf (stderr, "Read %2u(%u): >%s\n%s\n", i, reads[i].dir, seqs[i]->name, seqs[i]->seq);
  }
  return 1;
}

static void
print_db_reads (GT4Index *index, SeqFile *files, unsigned long long kmer_idx, unsigned int kmer_dir, FILE *ofs)
{
  unsigned long long first_read;
  unsigned int i, num_reads;
  char c[2048];

  first_read = gt4_index_get_kmer_info (index, kmer_idx, &num_reads);
  fprintf (stderr, "Reads %llu first %llu num %u\n", index->read_blocks[kmer_idx], first_read, num_reads);

  for (i = 0; i < num_reads; i++) {
    unsigned long long kmer_pos, name_pos;
    unsigned int file_idx, dir;
    unsigned long long cpos;
    unsigned int j, k, len;

    kmer_pos = gt4_index_get_read_info (index, first_read + i, &file_idx, &name_pos, &dir);
    fprintf (stderr, "%u %s %u %llu %llu (dir %u)\n", i, index->files[file_idx], file_idx, name_pos, kmer_pos, dir);
    if (!files[file_idx].cdata) {
      files[file_idx].cdata = gt4_mmap (files[file_idx].name, &files[file_idx].csize);
    }
    const unsigned char *cdata = files[file_idx].cdata;

    /* Name */
    fprintf (ofs, ">");
    cpos = name_pos + 1;
    j = 0;
    while (cdata[cpos + j] >= ' ') {
      fprintf (ofs, "%c", cdata[cpos + j]);
      j += 1;
    }
    fprintf (ofs, "\n");

    while (cdata[cpos + j] < ' ') j++;
    len = 0;
    while (cdata[cpos + j] >= ' ') {
      c[len++] = cdata[cpos + j];
      j += 1;
    }
    c[len] = 0;
    if (kmer_dir != dir) {
      for (k = 0; k < (len / 2); k++) {
        char t = c[k];
        c[k] = c[len - 1 - k];
        c[len - 1 - k] = t;
      }
      for (k = 0; k < len; k++) {
        if (c[k] == 'A') c[k] = 'T';
        else if (c[k] == 'C') c[k] = 'G';
        else if (c[k] == 'G') c[k] = 'C';
        else if (c[k] == 'T') c[k] = 'A';
      }
    }
    fprintf (ofs, "%s", c);
    fprintf (ofs, "\n");
  }
}

static float
find_coverage (GT4Index *index)
{
  /* Find coverage */
  unsigned int MEDIAN_KMERS = 10000;
  unsigned int *counts = (unsigned int *) malloc (MEDIAN_KMERS * 4);
  unsigned int max = 0;
  unsigned int min = 0xffffffff;
  unsigned int i, ci = 0;
  while(ci < MEDIAN_KMERS) {
    unsigned int kmer_idx = rand () % index->n_kmers;
    gt4_index_get_kmer_info (index, kmer_idx, &counts[ci]);
    if (!counts[ci]) continue;
    if (counts[ci] < min) min = counts[ci];
    if (counts[ci] > max) max = counts[ci];
    ci += 1;
  }
  if (debug) fprintf (stderr, "Sample min %u max %u\n", min, max);
  unsigned int med = (min + max) / 2;
  while (max > min) {
    unsigned int above = 0, below = 0, equal;
    med = (min + max) / 2;
    for (i = 0; i < MEDIAN_KMERS; i++) {
      if (counts[i] < med) below += 1;
      if (counts[i] > med) above += 1;
    }
    equal = MEDIAN_KMERS - above - below;
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
  }
  if (debug) fprintf (stderr, "Sample median %u\n", med);
  if (debug > 1) {
    int bins[100] = { 0 };
    for (i = 0; i < MEDIAN_KMERS; i++) {
      bins[(counts[i] < 100) ? counts[i] : 99] += 1;
    }
    for (i = 0; i < 2 * med; i++) fprintf (stderr, "%u\t%u\n", i, bins[i]);
  }
  free (counts);
  return med;
}

static unsigned int
chr_from_text (const char *name)
{
  unsigned int val;
  char *e;
  if (!strcmp (name, "X")) return CHR_X;
  if (!strcmp (name, "Y")) return CHR_Y;
  val = strtol (name, &e, 10);
  if (*e) return CHR_NONE;
  if (val > CHR_22) return CHR_NONE;
  return val;
}

static GASMRead *
gasm_read_new (const char *name, const char *seq, unsigned int wlen)
{
  GASMRead *read = (GASMRead *) malloc (sizeof (GASMRead));
  memset (read, 0, sizeof (GASMRead));
  read->name = strdup (name);
  read->seq = strdup (seq);
  read->nseq = n_seq_new (seq, wlen);
  return read;
}

static void
gasm_read_delete (GASMRead *read)
{
  free (read->name);
  free (read->seq);
  n_seq_delete (read->nseq);
  free (read);
}


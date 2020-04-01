#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <assert.h>

#include "fes.h"
#include "ffs.h"
#include "monomials.h"
#include <immintrin.h>

#define LANES 8
#define L 8
#define VERBOSE 0

struct solution_t {
	u32 x;
	u32 mask;
};


extern struct solution_t * feslite_avx2_asm_enum_8x32(const __m256i * Fq, __m256i * Fl, 
	u64 alpha, u64 beta, u64 gamma, struct solution_t *local_buffer);



struct context_t {
	int n;
	int m;
	const __m256i * Fq;
	__m256i * Fl;

	const u32 * Fq_start;
	const u32 * Fl_start;

	int count;
	u32 *buffer;
	int *size;

	/* local solution buffer */
	struct solution_t local_buffer[(1 << L)];
	
	/* candidates */
	u32 candidates[LANES][32];
	int n_candidates[LANES];
	bool overflow;

	/* counter */
	struct ffs_t ffs;
};

static const u32 MASK[LANES] = { 0x0000000f, 0x000000f0, 0x00000f00, 0x0000f000,
                                 0x000f0000, 0x00f00000, 0x0f000000, 0xf0000000 };


/* batch-eval all the candidates */
static inline void FLUSH_CANDIDATES(struct context_t *context, int lane)
{
	int max_solutions = context->count - context->size[lane];

//	printf("# [DEBUG] FLUSH_CANDIDATES (lane %d) %d candidates, %d solutions, max_allowed=%d\n", 
//		lane, context->n_candidates[lane], context->size[lane], max_solutions);

	int k;
	u32 * outbuf = context->buffer + context->count * lane + context->size[lane];

	feslite_generic_eval_32(context->n, context->Fq_start, context->Fl_start + lane, LANES, 
				context->n_candidates[lane], context->candidates[lane], 
				max_solutions, outbuf, &k);

//	printf("# [DEBUG] FLUSH_CANDIDATES %d candidates passed for lane %d\n", k, lane);
	context->size[lane] += k;
	context->n_candidates[lane] = 0;
	if (context->size[lane] == context->count)
		context->overflow = true;
}


static inline void NEW_CANDIDATE(struct context_t *context, u32 x, int lane)
{
	// u32 y = feslite_naive_evaluation(context->n, context->Fq_start, context->Fl_start + lane, 4, x);
	// printf("# [DEBUG] candidate %08x in lane %d, with F[%d][%08x] = %08x\n", x, lane, lane, x, y);
	//assert(y == 0);

	int i = context->n_candidates[lane];
	context->candidates[lane][i] = x;
	context->n_candidates[lane] = i + 1;

	if (context->n_candidates[lane] == 32)
		FLUSH_CANDIDATES(context, lane);
}


static inline bool FLUSH_BUFFER(struct context_t *context, struct solution_t * top, u64 i)
{	
	// if (top != context->local_buffer)
	// 	printf("[DEBUG]  FLUSH_BUFFER %p -> %p (%ld solutions found)\n", 
	// 		context->local_buffer, top, top - context->local_buffer);
	for (struct solution_t * bot = context->local_buffer; bot != top; bot++) {
		u32 x = to_gray(bot->x + i);
		u32 mask = bot->mask;
		// printf("Got x = %08x / mask = %08x\n", x, mask);
		if (mask & MASK[0])             // lane 0
			NEW_CANDIDATE(context, x, 0);
		if (mask & MASK[1])             // lane 1
			NEW_CANDIDATE(context, x, 1);
		if (mask & MASK[2])             // lane 2
			NEW_CANDIDATE(context, x, 2);
		if (mask & MASK[3])             // lane 3
			NEW_CANDIDATE(context, x, 3);
		if (mask & MASK[4])             // lane 0
			NEW_CANDIDATE(context, x, 4);
		if (mask & MASK[5])             // lane 1
			NEW_CANDIDATE(context, x, 5);
		if (mask & MASK[6])             // lane 2
			NEW_CANDIDATE(context, x, 6);
		if (mask & MASK[7])             // lane 3
			NEW_CANDIDATE(context, x, 7);
	}
	return context->overflow;
}				


void feslite_avx2_enum_8x32(int n, int m, const u32 * Fq, const u32 * Fl, int count, u32 * buffer, int *size)
{
	/* verify input parameters */
	if (count <= 0 || n < L || n > 32 || m != LANES) {
		size[0] = -1;
		return;
	}

	struct context_t context;
	context.n = n;
	context.m = m;
	context.count = count;
	context.buffer = buffer;
	context.size = size;
	for (int i = 0; i < LANES; i++) {
		context.n_candidates[i] = 0;
		context.size[i] = 0;
	}
	context.overflow = false;
	context.Fq_start = Fq;
	context.Fl_start = Fl;

	__m256i Fq_[529];
	__m256i Fl_[33];
	int N = idxq(0, n);
	for (int i = 0; i < N; i++)
		Fq_[i] = _mm256_set1_epi32(Fq[i]);
	Fq_[idxq(0, n)] = _mm256_setzero_si256();
	for (int i = 1; i < n; i++)
		Fq_[idxq(i, n)] = Fq_[idxq(i-1, i)];
	Fq_[idxq(n, n)] = _mm256_setzero_si256();
	for (int i = 0; i < n + 1; i++)
		Fl_[i] = _mm256_set_epi32(Fl[8*i + 7], Fl[8*i + 6], Fl[8*i + 5],  Fl[8*i + 4], 
			                  Fl[8*i + 3], Fl[8*i + 2], Fl[8*i + 1],  Fl[8*i + 0]);
	context.Fq = Fq_;
	context.Fl = Fl_;

	// for (int i = 0; i <= idxq(n, n); i++) {
	// 	u32 *x = (u32 *) &context.Fq[i];
	// 	printf("Fq[%d] = %08x %08x %08x %08x\n", i, x[0], x[1], x[2], x[3]);
	// }
	// printf("\n");

	// for (int i = 0; i < n + 1; i++) {
	// 	u32 *x = (u32 *) &context.Fl[i];
	// 	printf("Fl[%d] = %08x %08x %08x %08x\n", i, x[0], x[1], x[2], x[3]);
	// }

	ffs_reset(&context.ffs, n-L);
	int k1 = context.ffs.k1 + L;
	int k2 = context.ffs.k2 + L;

	u64 iterations = 1ul << (n - L);
	for (u64 j = 0; j < iterations; j++) {
		u64 alpha = idxq(0, k1);
		ffs_step(&context.ffs);	
		k1 = context.ffs.k1 + L;
		k2 = context.ffs.k2 + L;
		u64 beta = 1 + k1;
		u64 gamma = idxq(k1, k2);
		//struct solution_t *top = UNROLLED_CHUNK(context.Fq, context.Fl, alpha, beta, gamma, context.local_buffer);
		struct solution_t *top = feslite_avx2_asm_enum_8x32(context.Fq, context.Fl, 
		 	32*alpha, 32*beta, 32*gamma, context.local_buffer);
		//printf("j = %lx (alpha=%ld, beta=%ld, gamma=%ld)\n", j, alpha, beta, gamma);
		//for (int i = 0; i < n + 1; i++) {
		//	u32 *x = (u32 *) &context.Fl[i];
		//	printf("Fl[%d] = %08x %08x %08x %08x\n", i, x[0], x[1], x[2], x[3]);
		//}
		if (FLUSH_BUFFER(&context, top, j << L)) {
			//printf("Early abort at j=%ld\n", j);
			break;
		}
	}
	for (int i = 0; i < LANES; i++)
		FLUSH_CANDIDATES(&context, i);
}
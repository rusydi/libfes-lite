#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <getopt.h>
#include <immintrin.h>

#include "feslite.h"
#include "monomials.h"

#ifdef __AVX2__

struct solution_t {
  uint32_t x;
  uint32_t mask;
};

struct context_t {
	int n;
	const  uint32_t * const F_start;
	__m256i * F;
	struct solution_t buffer[512*8 + 32];
	size_t buffer_size;
	uint32_t *solutions;
	size_t n_solution_found;
	size_t max_solutions;
	size_t n_solutions;
	int verbose;
};


/* invoked when (at least) one lane is a solution. Both are pushed to the Buffer.
   Designed to be as quick as possible. */
static inline void CHECK_SOLUTION(struct context_t *context, uint32_t index)
{
	__m256i zero = _mm256_setzero_si256();
	__m256i cmp = _mm256_cmpeq_epi32(context->F[0], zero);
    	uint32_t mask = _mm256_movemask_epi8(cmp);
	if (unlikely(mask)) {
		context->buffer[context->buffer_size].mask = mask;
		context->buffer[context->buffer_size].x = index;
		context->buffer_size++;
	}
}

static inline void STEP_0(struct context_t *context, uint32_t index)
{
	CHECK_SOLUTION(context, index);
}

static inline void STEP_1(struct context_t *context, int a, uint32_t index)
{
	context->F[0] ^= context->F[a];
	STEP_0(context, index);
}

static inline void STEP_2(struct context_t *context, int a, int b, uint32_t index)
{
	context->F[a] ^= context->F[b];
	STEP_1(context, a, index);
}

/* Empty the Buffer. For each entry, check which half is correct,
   add it to the solutions. */
static inline void FLUSH_BUFFER(struct context_t *context)
{		
	for (size_t i = 0; i < context->buffer_size; i++) {
		uint32_t x = to_gray(context->buffer[i].x);
		printf("found: %08x with mask=%08x\n", x, context->buffer[i].mask);
		if ((context->buffer[i].mask & 0x0000000f)) {
			context->solutions[context->n_solutions++] = x;
			if (context->n_solutions == context->max_solutions)
				return;
		}
		if ((context->buffer[i].mask & 0x000000f0)) {
			context->solutions[context->n_solutions++] = x + (1 << (context->n - 3));
			if (context->n_solutions == context->max_solutions)
				return;
		}
		if ((context->buffer[i].mask & 0x00000f00)) {
			context->solutions[context->n_solutions++] = x + 2 * (1 << (context->n - 3));
			if (context->n_solutions == context->max_solutions)
				return;
		}
		if ((context->buffer[i].mask & 0x0000f000)) {
			context->solutions[context->n_solutions++] = x + 3 * (1 << (context->n - 3));
			if (context->n_solutions == context->max_solutions)
				return;
		}
		if ((context->buffer[i].mask & 0x000f0000)) {
			context->solutions[context->n_solutions++] = x + 4 * (1 << (context->n - 3));
			if (context->n_solutions == context->max_solutions)
				return;
		}
		if ((context->buffer[i].mask & 0x00f00000)) {
			context->solutions[context->n_solutions++] = x + 5 * (1 << (context->n - 3));
			if (context->n_solutions == context->max_solutions)
				return;
		}
		if ((context->buffer[i].mask & 0x0f000000)) {
			context->solutions[context->n_solutions++] = x + 6 * (1 << (context->n - 3));
			if (context->n_solutions == context->max_solutions)
				return;
		}
		if ((context->buffer[i].mask & 0xf0000000)) {
			context->solutions[context->n_solutions++] = x + 7 * (1 << (context->n - 3));
			if (context->n_solutions == context->max_solutions)
				return;
		}
	}
	context->buffer_size = 0;
}				

// generated with L = 9
size_t avx2_enum_8x32(int n, const uint32_t * const F_,
			    uint32_t * solutions, size_t max_solutions,
			    int verbose)
{
	struct context_t context = { .F_start = F_ };
	context.n = n;
	context.solutions = solutions;
	context.n_solutions = 0;
	context.max_solutions = max_solutions;
	context.verbose = verbose;
	context.buffer_size = 0;

	uint64_t init_start_time = Now();
	size_t N = idx_1(n);
	__m256i F[N];
	context.F = F;

	for (size_t i = 0; i < N; i++)
		F[i] = _mm256_set1_epi32(F_[i]);

    	__m256i v0 = _mm256_set_epi32(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000);
	__m256i v1 = _mm256_set_epi32(0xffffffff, 0xffffffff, 0x00000000, 0x00000000, 0xffffffff, 0xffffffff, 0x00000000, 0x00000000);
	__m256i v2 = _mm256_set_epi32(0xffffffff, 0x00000000, 0xffffffff, 0x00000000, 0xffffffff, 0x00000000, 0xffffffff, 0x00000000);
	 
	// the constant term is affected by [n-1]
	F[0] ^= F[idx_1(n - 1)] & v0;
	
	// [i] is affected by [i, n-1]
	for (int i = 0; i < n - 1; i++)
		F[idx_1(i)] ^= F[idx_2(i, n - 1)] & v0;
	
	// the constant term is affected by [n-2]
	F[0] ^= F[idx_1(n - 2)] & v1;
	
      	// [i] is affected by [i, n-2]
	for (int i = 0; i < n - 2; i++)
		F[idx_1(i)] ^= F[idx_2(i, n - 2)] & v1;
	
	// the constant term is affected by [n-3]
	F[0] ^= F[idx_1(n - 3)] & v2;
	
      	// [i] is affected by [i, n-3]
	for (int i = 0; i < n - 3; i++)
		F[idx_1(i)] ^= F[idx_2(i, n - 3)] & v2;
	
	/******** compute "derivatives" */
	/* degree-1 terms are affected by degree-2 terms */
	for (int i = 1; i < n - 3; i++)
		F[idx_1(i)] ^= F[idx_2(i - 1, i)];

	if (verbose)
		printf("fes: initialisation = %" PRIu64 " cycles\n",
		       Now() - init_start_time);
	uint64_t enumeration_start_time = Now();

	// special case for i=0
	const uint64_t weight_0_start = 0;
	STEP_0(&context, 0);

	// from now on, hamming weight is >= 1
	for (int idx_0 = 0; idx_0 < n - 3; idx_0++) {

		// special case when i has hamming weight exactly 1
		const uint64_t weight_1_start = weight_0_start + (1ll << idx_0);
		STEP_1(&context, idx_1(idx_0), weight_1_start);

		// we are now inside the critical part where the hamming weight is known to be >= 2
		// Thus, there are no special cases from now on

		// Because of the last step, the current iteration counter is a multiple of 512 plus one
		// This loop sets it to `rolled_end`, which is a multiple of 512, if possible

		const uint64_t rolled_end =
		    weight_1_start + (1ll << min(9, idx_0));
		for (uint64_t i = 1 + weight_1_start; i < rolled_end; i++) {
			int pos = 0;
			/* k1 == rightmost 1 bit */
			uint64_t _i = i;
			while ((_i & 0x0001) == 0) {
				_i >>= 1;
				pos++;
			}
			const int k_1 = pos;
			/* k2 == second rightmost 1 bit */
			_i >>= 1;
			pos++;
			while ((_i & 0x0001) == 0) {
				_i >>= 1;
				pos++;
			}
			const int k_2 = pos;
			STEP_2(&context, idx_1(k_1), idx_2(k_1, k_2), i);
		}
		

		FLUSH_BUFFER(&context);
		if (context.n_solutions == context.max_solutions)
			return context.n_solutions;

		// Here, the number of iterations to perform is (supposedly) sufficiently large
		// We will therefore unroll the loop 512 times

		// unrolled critical section where the hamming weight is >= 2
		for (uint64_t j = 512; j < (1ull << idx_0); j += 512) {
			#if 1
			const uint64_t i = j + weight_1_start;
			// printf("testing idx %08x : F[0] = %08x\n", i, F[0]);

			int pos = 0;
			uint64_t _i = i;
			while ((_i & 0x0001) == 0) {
				_i >>= 1;
				pos++;
			}
			const int k_1 = pos;
			_i >>= 1;
			pos++;
			while ((_i & 0x0001) == 0) {
				_i >>= 1;
				pos++;
			}
			const int k_2 = pos;
			const int alpha = idx_1(k_1);
			const int beta = idx_2(k_1, k_2);

			STEP_2(&context, alpha, beta, i);
			printf("going for ASM\n");
          		avx2_asm_enum_8x32(F, (uint64_t) alpha * 32, context.buffer, &context.buffer_size, (uint64_t) i);
			#else
			const uint64_t i = j + weight_1_start;
			// printf("testing idx %08x : F[0] = %08x\n", i, F[0]);

			int pos = 0;
			uint64_t _i = i;
			while ((_i & 0x0001) == 0) {
				_i >>= 1;
				pos++;
			}
			const int k_1 = pos;
			_i >>= 1;
			pos++;
			while ((_i & 0x0001) == 0) {
				_i >>= 1;
				pos++;
			}
			const int k_2 = pos;
			const int alpha = idx_1(k_1);
			const int beta = idx_2(k_1, k_2);

			STEP_2(&context, alpha, beta, i);

			for (uint32_t k = 1; k < 512; k++) {
				int pos = 0;
				uint32_t _i = i + k;
				while ((_i & 0x0001) == 0) {
					_i >>= 1;
					pos++;
				}
				const int k_1 = pos;
				_i >>= 1;
				pos++;
				while ((_i & 0x0001) == 0) {
					_i >>= 1;
					pos++;
				}
				const int k_2 = pos;
				STEP_2(&context, idx_1(k_1), idx_2(k_1, k_2), i + k);
			}
			#endif

			printf("End of actual asm step\n");
			uint32_t *G = (uint32_t *) F;
			for (int foo = 0; foo < 16; foo++) {
				printf("F[%2d] = ", foo);
				for (int bar = 0; bar < 8; bar++)
					printf("%08x ", G[8*foo+bar]);
				printf("\n");
			}

			FLUSH_BUFFER(&context);
			if (context.n_solutions == context.max_solutions)
				return context.n_solutions;
		}
	}
	uint64_t end_time = Now();
	

	if (verbose)
		printf("fes: enumeration+check = %" PRIu64 " cycles\n",
		       end_time - enumeration_start_time);


	return context.n_solutions;
}
#endif
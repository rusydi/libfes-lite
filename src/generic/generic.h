/** generic 32-bit code **/

int feslite_generic_enum_1x32(int n, const uint32_t * const F, uint32_t * solutions, int max_solutions);

int feslite_generic_eval_32(int n, const uint32_t * const F,
			    int eq_from, int eq_to,
			    uint32_t *input, int n_input,
			    uint32_t *solutions, int max_solutions,
			    bool verbose);

#if 0
int generic_enum_2x16(int n, const uint32_t * const F,
			    uint32_t * solutions, int max_solutions,
			    int verbose);

/** generic 64-bit code **/

int generic_enum_2x32(int n, const uint32_t * const F,
			    uint32_t * solutions, int max_solutions,
			    int verbose);
int generic_enum_4x16(int n, const uint32_t * const F,
			    uint32_t * solutions, int max_solutions,
			    int verbose);
#endif
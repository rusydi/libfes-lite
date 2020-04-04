import sys
L = 9

def ffs(i):
    if i == 0:
        return -1
    k = 0
    while i & 0x0001 == 0:
        k += 1
        i >>= 1
    return k

def idxq(i, j):
    assert i < j
    return i + j * (j - 1) // 2


print("""
################################################################################
# This file was auto-generated by {script}
# The loop is unrolled {unroll} times
################################################################################
""".format(script=sys.argv[0], unroll=2**L))

print("""
# the System V AMD64 ABI says that :
# A) The first six integer or pointer arguments are passed in registers RDI, RSI, RDX, RCX, R8, R9
# B) we should preserve the values of %rbx, %rbp, %r12...%r15 [callee-save registers]
# C) We will receive the arguments of the function in registers :
#       Fq           in %rdi
#       Fl           in %rsi
#       alpha        in %rdx
#       beta         in %rcx
#       gamma        in %r8
#       local_buffer in %r9
# D) we return local_buffer in %rax

# no need to save the callee-save registers (we do not touch them)
# Load the 14 most used values into ZMM0-ZMM29
# %zmm31 is pinned to zero
# me move %r9 to %rax because it will be the return value.
# we may still use %9, %r10 and %r11
# %r11 contains the comparison output mask 
# %r9 and %r10 are available

# One step = 5 instructions
#  Dependency chain of length 4
#  Lower-bound = 2 cycles (two in port 0, two in port 5, on in port 6)
#  Measured : 3 cycles with 1 thread/Core vs 2.3 cycles with HT.
#  Probably hit by the latency of VPCMPEQW (3 cycles)

### extern struct solution_t * UNROLLED_CHUNK(const __m512i * Fq, __m512i * Fl, u64 alpha, 
###                                           u64 beta, u64 gamma, struct solution_t *local_buffer)

.text
.p2align 6

.globl feslite_avx512x2bw_asm_enum
feslite_avx512x2bw_asm_enum:

# multiply alpha, beta, gamma by 128
shlq $6, %rdx
shlq $7, %rcx
shlq $6, %r8

vpxord %zmm31, %zmm31, %zmm31        # set %zmm31 to always-zero
movq %r9, %rax                       # prepare the return value
""")

Fl = {}
Fl[0] = "%zmm0", "%zmm1"  # 1
Fl[1] = "%zmm2", "%zmm3"   # 1/2
Fl[2] = "%zmm4", "%zmm5"  # 1/4
Fl[3] = "%zmm6", "%zmm7"  # 1/8
Fl[4] = "%zmm8", "%zmm9"  # 1/16
Fl[5] = "%zmm10", "%zmm11"  # 1/32
Fl[6] = "%zmm12", "%zmm13"  # 1/64
Fl[7] = "%zmm14", "%zmm15"  # 1/128


Fq = {}
Fq[idxq(0, 1)] = "%zmm16"    # 1/4
Fq[idxq(0, 2)] = "%zmm17"    # 1/8
Fq[idxq(1, 2)] = "%zmm18"    # 1/8
Fq[idxq(0, 3)] = "%zmm19"    # 1/16
Fq[idxq(1, 3)] = "%zmm20"    # 1/16
Fq[idxq(2, 3)] = "%zmm21"    # 1/16
Fq[idxq(0, 4)] = "%zmm22"    # 1/32
Fq[idxq(1, 4)] = "%zmm23"    # 1/32
Fq[idxq(2, 4)] = "%zmm24"    # 1/32
Fq[idxq(3, 4)] = "%zmm25"    # 1/32
Fq[idxq(0, 5)] = "%zmm26"    # 1/64
Fq[idxq(1, 5)] = "%zmm27"    # 1/64
Fq[idxq(2, 5)] = "%zmm28"    # 1/64
Fq[idxq(3, 5)] = "%zmm29"    # 1/64


def output_comparison(i):
    # before the XORs, the comparison
    print('vpcmpequw %zmm0, %zmm31, %k0')
    print('vpcmpequw %zmm1, %zmm31, %k1')
    print('kortestd %k0, %k1')
    print('jne ._report_solution_{0}'.format(i))
    print()
    print('._step_{0}_end:'.format(i))



###################""

print( "# load the most-frequently used values into vector registers" )
for i, (reg_a, reg_b) in Fl.items():
    print("vmovdqa32 {offset}(%rsi), {reg}   ## {reg} = Fl[{i}]".format(offset=i*128, reg=reg_a, i=i))
    print("vmovdqa32 {offset}(%rsi), {reg}   ## {reg} = Fl[{i}]".format(offset=i*128+64, reg=reg_b, i=i))
print()
for x, reg in Fq.items():
    print("vmovdqa32 {offset}(%rdi), {reg}   ## {reg} = Fq[{idx}]".format(offset=x*64, reg=reg, idx=x))
print()

alpha = 0
for i in range((1 << L) - 1):
    ########################## UNROLLED LOOP #######################################
    idx1 = ffs(i + 1)                       
    idx2 = ffs((i + 1) ^ (1 << idx1))
    a = idx1 + 1                              # offset dans Fl
    Fq_memref = None
    if idx2 == -1:
        Fq_memref = "{offset}(%rdi, %rdx)".format(offset=64*alpha)
        b = "alpha + {}".format(alpha)
        alpha += 1
    else:
        assert idx1 < idx2
        b = idxq(idx1, idx2)                  # offset dans Fq
    
    print()
    print('##### step {:3d} : Fl[0] ^= (Fl[{}] ^= Fq[{}])'.format(i, a, b))
    print()
    output_comparison(i)


    # There are 3 possible cases :
    # 1a. Fq in register, Fl in register
    # 1b. Fq in memory,   Fl in register
    #  2. Fq in memory,   Fl in memory

    if a in Fl:
        Fl_l, Fl_r = Fl[a]
        if b in Fq: # reg / reg
            print("vpxord {src}, {dst}, {dst}".format(src=Fq[b], dst=Fl_l))
            print("vpxord {src}, {dst}, {dst}".format(src=Fq[b], dst=Fl_r))
        elif Fq_memref is None: # mem / reg
            print("vpxord {offset}(%rdi), {dst}, {dst}".format(offset=64*b, dst=Fl_l))
            print("vpxord {offset}(%rdi), {dst}, {dst}".format(offset=64*b, dst=Fl_r))
        else: # mem(alpha) / reg
            print("vpxord {src}, {dst}, {dst}".format(src=Fq_memref, dst=Fl_l))
            print("vpxord {src}, {dst}, {dst}".format(src=Fq_memref, dst=Fl_r))
        print("vpxord {src}, %zmm0, %zmm0".format(src=Fl_l))
        print("vpxord {src}, %zmm1, %zmm1".format(src=Fl_r))
    
    else:  #(a not in Fl)
        assert b not in Fq
        # côté gauche
        print("vmovdqa32 {offset}(%rsi), %zmm30".format(offset=128*a)) # load Fl[a]
        print("vmovdqa32 {offset}(%rsi), %zmm31".format(offset=128*a+64)) # load Fl[a]
        if Fq_memref is None: 
            print("vpxord {offset}(%rdi), %zmm30, %zmm30".format(offset=64*b))
            print("vpxord {offset}(%rdi), %zmm31, %zmm31".format(offset=64*b))
        else:
            print("vpxord {src}, %zmm30, %zmm30".format(src=Fq_memref))
            print("vpxord {src}, %zmm31, %zmm31".format(src=Fq_memref))
        print("vmovdqa32 %zmm30, {offset}(%rsi)".format(offset=128*a))    # store Fl[a]
        print("vmovdqa32 %zmm31, {offset}(%rsi)".format(offset=128*a+64)) # store Fl[a]
        print("vpxord %zmm30, %zmm0, %zmm0")
        print("vpxord %zmm31, %zmm1, %zmm1")
        print("vpxord %zmm31, %zmm31, %zmm31")
    print()

####### ne pas oublier le dernier tour special
print('#############################')
print('# end of the unrolled chunk #')
print('#############################')
print()
print("# Save the Fl[1:] back to memory")
for i, (reg_l, reg_r) in Fl.items():
    if i == 0:
        continue
    print("vmovdqa32 {reg}, {offset:2d}(%rsi)     #Fl[{i}] <-- {reg}".format(offset=i*128, reg=reg_l, i=i))
    print("vmovdqa32 {reg}, {offset:2d}(%rsi)     #Fl[{i}] <-- {reg}".format(offset=i*128+64, reg=reg_r, i=i))
print()
print('##### special last step {:3d} : Fl[0] ^= (Fl[beta] ^= Fq[gamma])'.format((1 << L) - 1))
print()
output_comparison((1 << L) - 1)
print("vmovdqa32   (%rsi, %rcx), %zmm30")            # load Fl[beta]
print("vmovdqa32 64(%rsi, %rcx), %zmm31")            # load Fl[beta]
print("vpxord (%rdi, %r8), %zmm30, %zmm30")          # xor Fq[gamma]
print("vpxord (%rdi, %r8), %zmm31, %zmm31")          # xor Fq[gamma]
print("vmovdqa32 %zmm30,   (%rsi, %rcx)")              # store Fl[beta]
print("vmovdqa32 %zmm31, 64(%rsi, %rcx)")              # store Fl[beta]
print("vpxord %zmm30, %zmm0, %zmm0")
print("vpxord %zmm31, %zmm1, %zmm1")
print()
print("# Save Fl[0] back to memory")
print("vmovdqa32 %zmm0,   (%rsi)     #Fl[0] <-- %zmm0")
print("vmovdqa32 %zmm1, 64(%rsi)     #Fl[0] <-- %zmm0")
print()
print('ret')
print()
print()

########################## WHEN SOLUTION FOUND #######################################

print('########### now the code that reports solutions')
print()

for i in range(1<<L):
    # the mask is in %k0.
    # available registers: %r9, %r10, %r11
    print('._report_solution_{i}:          # GrayCode(i + {i}) is a solution'.format(i=i))
    # we no longer need to reset %zmm31
    print('movq  ${i}, 0(%rax)                # buffer.x = {i}'.format(i=i))
    print('kmovd %k0,  8(%rax)                # buffer.mask = %k0 / %k1')
    print('kmovd %k1, 12(%rax)                #')
    print('addq $16, %rax                     # buffer++'); 
    print('jmp ._step_{i}_end'.format(i=i))  # return to the enumeration 
    print()
	; ARGS: vvadd_vector.s
	.file	"vvadd_kernel_be.c"
	.option nopic
	.text
	.align	2
	.globl	tril_vvadd
	.type	tril_vvadd, @function
	tril_vvadd:
	.insn i 0x77, 0, x0, a0, 0x401
	lw	a3,0(sp)
	subw	a5,a5,a4
	divw	a7,a5,a3
	li	a5,15
	mv	a0,a7
	bgt	a7,a5,.SCALAR16
	mv	a6,a7
	blez	a7,.SCALAR3
.SCALAR2:
	slli	a5,a4,2
	add	t3,a1,a5
	li	t1,0
	.insn sb 0x23, 0x6, t1, 16(t3)
	.insn sb 0x23, 0x7, t1, 16(t3)
	li	t1,1
	add	a5,a2,a5
	.insn sb 0x23, 0x6, t1, 16(a5)
	.insn sb 0x23, 0x7, t1, 16(a5)
	li	a5,1
	beq	a6,a5,.SCALAR3
	add	a5,a3,a4
	slli	a5,a5,2
	add	t3,a1,a5
	li	t1,2
	.insn sb 0x23, 0x6, t1, 16(t3)
	.insn sb 0x23, 0x7, t1, 16(t3)
	li	t1,3
	add	a5,a2,a5
	.insn sb 0x23, 0x6, t1, 16(a5)
	.insn sb 0x23, 0x7, t1, 16(a5)
	li	a5,2
	beq	a6,a5,.SCALAR3
	slliw	t1,a3,1
	mv	a5,t1
	add	t1,t1,a4
	slli	t1,t1,2
	add	t4,a1,t1
	li	t3,4
	.insn sb 0x23, 0x6, t3, 16(t4)
	.insn sb 0x23, 0x7, t3, 16(t4)
	li	t3,5
	add	t1,a2,t1
	.insn sb 0x23, 0x6, t3, 16(t1)
	.insn sb 0x23, 0x7, t3, 16(t1)
	li	t1,3
	beq	a6,t1,.SCALAR3
	addw	t1,a5,a3
	mv	a5,t1
	add	t1,t1,a4
	slli	t1,t1,2
	add	t4,a1,t1
	li	t3,6
	.insn sb 0x23, 0x6, t3, 16(t4)
	.insn sb 0x23, 0x7, t3, 16(t4)
	li	t3,7
	add	t1,a2,t1
	.insn sb 0x23, 0x6, t3, 16(t1)
	.insn sb 0x23, 0x7, t3, 16(t1)
	li	t1,4
	beq	a6,t1,.SCALAR3
	addw	t1,a5,a3
	mv	a5,t1
	add	t1,t1,a4
	slli	t1,t1,2
	add	t4,a1,t1
	li	t3,8
	.insn sb 0x23, 0x6, t3, 16(t4)
	.insn sb 0x23, 0x7, t3, 16(t4)
	li	t3,9
	add	t1,a2,t1
	.insn sb 0x23, 0x6, t3, 16(t1)
	.insn sb 0x23, 0x7, t3, 16(t1)
	li	t1,5
	beq	a6,t1,.SCALAR3
	addw	t1,a3,a5
	mv	a5,t1
	add	t1,t1,a4
	slli	t1,t1,2
	add	t4,a1,t1
	li	t3,10
	.insn sb 0x23, 0x6, t3, 16(t4)
	.insn sb 0x23, 0x7, t3, 16(t4)
	li	t3,11
	add	t1,a2,t1
	.insn sb 0x23, 0x6, t3, 16(t1)
	.insn sb 0x23, 0x7, t3, 16(t1)
	li	t1,6
	beq	a6,t1,.SCALAR3
	addw	t1,a3,a5
	mv	a5,t1
	add	t1,t1,a4
	slli	t1,t1,2
	add	t4,a1,t1
	li	t3,12
	.insn sb 0x23, 0x6, t3, 16(t4)
	.insn sb 0x23, 0x7, t3, 16(t4)
	li	t3,13
	add	t1,a2,t1
	.insn sb 0x23, 0x6, t3, 16(t1)
	.insn sb 0x23, 0x7, t3, 16(t1)
	li	t1,7
	beq	a6,t1,.SCALAR3
	addw	t1,a3,a5
	mv	a5,t1
	add	t1,t1,a4
	slli	t1,t1,2
	add	t4,a1,t1
	li	t3,14
	.insn sb 0x23, 0x6, t3, 16(t4)
	.insn sb 0x23, 0x7, t3, 16(t4)
	li	t3,15
	add	t1,a2,t1
	.insn sb 0x23, 0x6, t3, 16(t1)
	.insn sb 0x23, 0x7, t3, 16(t1)
	li	t1,8
	beq	a6,t1,.SCALAR3
	addw	t1,a3,a5
	mv	a5,t1
	add	t1,t1,a4
	slli	t1,t1,2
	add	t4,a1,t1
	li	t3,16
	.insn sb 0x23, 0x6, t3, 16(t4)
	.insn sb 0x23, 0x7, t3, 16(t4)
	li	t3,17
	add	t1,a2,t1
	.insn sb 0x23, 0x6, t3, 16(t1)
	.insn sb 0x23, 0x7, t3, 16(t1)
	li	t1,9
	beq	a6,t1,.SCALAR3
	addw	t1,a3,a5
	mv	a5,t1
	add	t1,t1,a4
	slli	t1,t1,2
	add	t4,a1,t1
	li	t3,18
	.insn sb 0x23, 0x6, t3, 16(t4)
	.insn sb 0x23, 0x7, t3, 16(t4)
	li	t3,19
	add	t1,a2,t1
	.insn sb 0x23, 0x6, t3, 16(t1)
	.insn sb 0x23, 0x7, t3, 16(t1)
	li	t1,10
	beq	a6,t1,.SCALAR3
	addw	t1,a3,a5
	mv	a5,t1
	add	t1,t1,a4
	slli	t1,t1,2
	add	t4,a1,t1
	li	t3,20
	.insn sb 0x23, 0x6, t3, 16(t4)
	.insn sb 0x23, 0x7, t3, 16(t4)
	li	t3,21
	add	t1,a2,t1
	.insn sb 0x23, 0x6, t3, 16(t1)
	.insn sb 0x23, 0x7, t3, 16(t1)
	li	t1,11
	beq	a6,t1,.SCALAR3
	addw	t1,a3,a5
	mv	a5,t1
	add	t1,t1,a4
	slli	t1,t1,2
	add	t4,a1,t1
	li	t3,22
	.insn sb 0x23, 0x6, t3, 16(t4)
	.insn sb 0x23, 0x7, t3, 16(t4)
	li	t3,23
	add	t1,a2,t1
	.insn sb 0x23, 0x6, t3, 16(t1)
	.insn sb 0x23, 0x7, t3, 16(t1)
	li	t1,12
	beq	a6,t1,.SCALAR3
	addw	t1,a3,a5
	mv	a5,t1
	add	t1,t1,a4
	slli	t1,t1,2
	add	t4,a1,t1
	li	t3,24
	.insn sb 0x23, 0x6, t3, 16(t4)
	.insn sb 0x23, 0x7, t3, 16(t4)
	li	t3,25
	add	t1,a2,t1
	.insn sb 0x23, 0x6, t3, 16(t1)
	.insn sb 0x23, 0x7, t3, 16(t1)
	li	t1,13
	beq	a6,t1,.SCALAR3
	addw	t1,a3,a5
	mv	a5,t1
	add	t1,t1,a4
	slli	t1,t1,2
	add	t4,a1,t1
	li	t3,26
	.insn sb 0x23, 0x6, t3, 16(t4)
	.insn sb 0x23, 0x7, t3, 16(t4)
	li	t3,27
	add	t1,a2,t1
	.insn sb 0x23, 0x6, t3, 16(t1)
	.insn sb 0x23, 0x7, t3, 16(t1)
	li	t1,14
	beq	a6,t1,.SCALAR3
	addw	t1,a3,a5
	mv	a5,t1
	add	t1,t1,a4
	slli	t1,t1,2
	add	t4,a1,t1
	li	t3,28
	.insn sb 0x23, 0x6, t3, 16(t4)
	.insn sb 0x23, 0x7, t3, 16(t4)
	li	t3,29
	add	t1,a2,t1
	.insn sb 0x23, 0x6, t3, 16(t1)
	.insn sb 0x23, 0x7, t3, 16(t1)
	li	t1,16
	bne	a6,t1,.SCALAR18
	addw	a5,a5,a3
	add	a5,a5,a4
	slli	a5,a5,2
	add	t3,a1,a5
	li	t1,30
	.insn sb 0x23, 0x6, t1, 16(t3)
	.insn sb 0x23, 0x7, t1, 16(t3)
	li	t1,31
	add	a5,a2,a5
	.insn sb 0x23, 0x6, t1, 16(a5)
	.insn sb 0x23, 0x7, t1, 16(a5)
.SCALAR3:
	.insn uj 0x6b, x0, .SCALAR4
	.insn uj 0x6b, x0, .SCALAR5
	slliw	a5,a6,1
	bge	a6,a7,.SCALAR6
	mulw	t1,a6,a3
	mv	t3,a6
	slli	a3,a3,2
	li	t5,512
	add	a4,t1,a4
	slli	a4,a4,2
.SCALAR7:
	.insn uj 0x6b, x0, .SCALAR8
	add	t1,a1,a4
	.insn sb 0x23, 0x6, a5, 16(t1)
	.insn sb 0x23, 0x7, a5, 16(t1)
	addiw	t1,a5,1
	add	t4,a2,a4
	.insn sb 0x23, 0x6, t1, 16(t4)
	.insn sb 0x23, 0x7, t1, 16(t4)
	addiw	a5,a5,2
	addiw	t3,t3,1
	add	a4,a4,a3
	beq	a5,t5,.SCALAR23
	bne	a7,t3,.SCALAR7
.SCALAR6:
	subw	a0,a0,a6
	ble	a7,a0,.SCALAR11
.SCALAR12:
	.insn uj 0x6b, x0, .SCALAR8
	addiw	a0,a0,1
	bne	a7,a0,.SCALAR12
.SCALAR11:
	.insn uj 0x6b, x0, .SCALAR13
.SCALAR14:
	# trillium: scalar stack cleanup begin
	# trillium: scalar stack cleanup end
.SCALAR14DEVEC:
	.insn uj 0x2b, x0, .SCALAR14DEVEC
	fence
	ret
	# trillium: auxiliary blocks begin
.SCALAR16:
	li	a6,16
	j	.SCALAR2
.SCALAR23:
	beq	a7,t3,.SCALAR6
	li	a5,0
	j	.SCALAR7
.SCALAR18:
	li	a6,15
	j	.SCALAR3
	# trillium: auxiliary blocks end
	# trillium: vector vissue blocks begin
.SCALAR4:  # vector_init vissue block
	#prepended trillium_init block here (See docs for more info)
	#trillium_init begin
	addi	sp,sp,-32
	sd	ra,24(sp)
	sd	s0,16(sp)
	mv	a0,a6
	#trillium_init end
	add	s0,a4,a7
	li	a1,0
	slli	s0,s0,2
	add	s0,a3,s0
	call	getSpAddr
	.insn i 0x1b, 0x7, x0, x0, 0
.SCALAR8:  # vector_body vissue block
	.insn i 0x1b, 0x3, x0, a3, 0
	slli	a4,a5,2
	add	a4,a0,a4
	lw	a2,0(a4)
	lw	a4,4(a4)
	.insn i 0x1b, 0x2, x0, a3, 0
	addw	a4,a2,a4
	.insn sb 0x23, 0x5, a4, 0(s0)
	addi	a5,a5,2
	add	s0,s0,a1
	andi	a5,a5,511
	.insn i 0x1b, 0x7, x0, x0, 0
.SCALAR5:  # trillium_junk0 vissue block
	lw	a5,12(sp)
	sext.w	a5,a5
	beqz	a5,.VEC2
	lw	a1,32(sp)
	li	a5,0
	li	a3,2
	slli	a1,a1,2
.VEC3:
	.insn i 0x1b, 0x7, x0, x0, 0
.SCALAR13:  # vector_return vissue block
	ld	ra,24(sp)
	ld	s0,16(sp)
	addi	sp,sp,32
	.insn i 0x1b, 0x7, x0, x0, 0
	# trillium: vector vissue blocks end
	# trillium: footer begin
	.size	tril_vvadd, .-tril_vvadd
	# trillium: footer end
	.comm	start_barrier,32,8
	.ident	"GCC: (GNU) 8.3.0"
	.section	.note.GNU-stack,"",@progbits

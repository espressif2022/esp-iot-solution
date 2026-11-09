
/tmp/present-s31-layout-20261109/baseline/s31-rgb/benchmark/adapter/build/present_benchmark.elf:     file format elf32-littleriscv


Disassembly of section .iram0.text:

Disassembly of section .flash.text:

40062ab8 <lv_font_get_bitmap_fmt_txt>:
40062ab8:	7139                	addi	sp,sp,-64
40062aba:	d452                	sw	s4,40(sp)
40062abc:	00852a03          	lw	s4,8(a0)
40062ac0:	de06                	sw	ra,60(sp)
40062ac2:	0e0a0863          	beqz	s4,40062bb2 <lv_font_get_bitmap_fmt_txt+0xfa>
40062ac6:	4118                	lw	a4,0(a0)
40062ac8:	02054783          	lbu	a5,32(a0)
40062acc:	dc22                	sw	s0,56(sp)
40062ace:	4310                	lw	a2,0(a4)
40062ad0:	cc62                	sw	s8,24(sp)
40062ad2:	8b89                	andi	a5,a5,2
40062ad4:	4240                	lw	s0,4(a2)
40062ad6:	8c2e                	mv	s8,a1
40062ad8:	208a6433          	sh3add	s0,s4,s0
40062adc:	e3ed                	bnez	a5,40062bbe <lv_font_get_bitmap_fmt_txt+0x106>
40062ade:	00444703          	lbu	a4,4(s0)
40062ae2:	00544783          	lbu	a5,5(s0)
40062ae6:	9fd9                	mul	a5,a5,a4
40062ae8:	c3f9                	beqz	a5,40062bae <lv_font_get_bitmap_fmt_txt+0xf6>
40062aea:	01464803          	lbu	a6,20(a2)
40062aee:	06087813          	andi	a6,a6,96
40062af2:	0a081e63          	bnez	a6,40062bae <lv_font_get_bitmap_fmt_txt+0xf6>
40062af6:	01664783          	lbu	a5,22(a2)
40062afa:	da26                	sw	s1,52(sp)
40062afc:	d84a                	sw	s2,48(sp)
40062afe:	d64e                	sw	s3,44(sp)
40062b00:	4984                	lw	s1,16(a1)
40062b02:	01e55983          	lhu	s3,30(a0)
40062b06:	00062903          	lw	s2,0(a2)
40062b0a:	efe9                	bnez	a5,40062be4 <lv_font_get_bitmap_fmt_txt+0x12c>
40062b0c:	401c                	lw	a5,0(s0)
40062b0e:	07b2                	slli	a5,a5,0xc
40062b10:	83b1                	srli	a5,a5,0xc
40062b12:	00f908b3          	add	a7,s2,a5
40062b16:	34088363          	beqz	a7,40062e5c <lv_font_get_bitmap_fmt_txt+0x3a4>
40062b1a:	d256                	sw	s5,36(sp)
40062b1c:	45b9                	li	a1,14
40062b1e:	853a                	mv	a0,a4
40062b20:	c232                	sw	a2,4(sp)
40062b22:	c646                	sw	a7,12(sp)
40062b24:	c442                	sw	a6,8(sp)
40062b26:	bb8bd0ef          	jal	4001fede <lv_draw_buf_width_to_stride>
40062b2a:	00544583          	lbu	a1,5(s0)
40062b2e:	4612                	lw	a2,4(sp)
40062b30:	8aaa                	mv	s5,a0
40062b32:	2a058363          	beqz	a1,40062dd8 <lv_font_get_bitmap_fmt_txt+0x320>
40062b36:	01464303          	lbu	t1,20(a2)
40062b3a:	d05a                	sw	s6,32(sp)
40062b3c:	ce5e                	sw	s7,28(sp)
40062b3e:	4822                	lw	a6,8(sp)
40062b40:	48b2                	lw	a7,12(sp)
40062b42:	00f37793          	andi	a5,t1,15
40062b46:	30098263          	beqz	s3,40062e4a <lv_font_get_bitmap_fmt_txt+0x392>
40062b4a:	4711                	li	a4,4
40062b4c:	4501                	li	a0,0
40062b4e:	02e78863          	beq	a5,a4,40062b7e <lv_font_get_bitmap_fmt_txt+0xc6>
40062b52:	46a1                	li	a3,8
40062b54:	2cd78063          	beq	a5,a3,40062e14 <lv_font_get_bitmap_fmt_txt+0x35c>
40062b58:	17fd                	addi	a5,a5,-1
40062b5a:	9fe1                	zext.b	a5,a5
40062b5c:	4705                	li	a4,1
40062b5e:	0af77663          	bgeu	a4,a5,40062c0a <lv_font_get_bitmap_fmt_txt+0x152>
40062b62:	4281                	li	t0,0
40062b64:	0505                	addi	a0,a0,1
40062b66:	26b55763          	bge	a0,a1,40062dd4 <lv_font_get_bitmap_fmt_txt+0x31c>
40062b6a:	01464783          	lbu	a5,20(a2)
40062b6e:	405982b3          	sub	t0,s3,t0
40062b72:	4711                	li	a4,4
40062b74:	8bbd                	andi	a5,a5,15
40062b76:	9896                	add	a7,a7,t0
40062b78:	94d6                	add	s1,s1,s5
40062b7a:	fce79ce3          	bne	a5,a4,40062b52 <lv_font_get_bitmap_fmt_txt+0x9a>
40062b7e:	00444283          	lbu	t0,4(s0)
40062b82:	4785                	li	a5,1
40062b84:	46a1                	li	a3,8
40062b86:	4e01                	li	t3,0
40062b88:	1a57c163          	blt	a5,t0,40062d2a <lv_font_get_bitmap_fmt_txt+0x272>
40062b8c:	fcf29be3          	bne	t0,a5,40062b62 <lv_font_get_bitmap_fmt_txt+0xaa>
40062b90:	0008c803          	lbu	a6,0(a7)
40062b94:	40094eb7          	lui	t4,0x40094
40062b98:	3e8e8e93          	addi	t4,t4,1000 # 400943e8 <opa4_table>
40062b9c:	00485793          	srli	a5,a6,0x4
40062ba0:	97f6                	add	a5,a5,t4
40062ba2:	839c                	lbu	a5,0(a5)
40062ba4:	9896                	add	a7,a7,t0
40062ba6:	889c                	sb	a5,0(s1)
40062ba8:	00544583          	lbu	a1,5(s0)
40062bac:	bf65                	j	40062b64 <lv_font_get_bitmap_fmt_txt+0xac>
40062bae:	5462                	lw	s0,56(sp)
40062bb0:	4c62                	lw	s8,24(sp)
40062bb2:	4881                	li	a7,0
40062bb4:	50f2                	lw	ra,60(sp)
40062bb6:	5a22                	lw	s4,40(sp)
40062bb8:	8546                	mv	a0,a7
40062bba:	6121                	addi	sp,sp,64
40062bbc:	8082                	ret
40062bbe:	01664783          	lbu	a5,22(a2)
40062bc2:	da26                	sw	s1,52(sp)
40062bc4:	4204                	lw	s1,0(a2)
40062bc6:	1c079163          	bnez	a5,40062d88 <lv_font_get_bitmap_fmt_txt+0x2d0>
40062bca:	401c                	lw	a5,0(s0)
40062bcc:	50f2                	lw	ra,60(sp)
40062bce:	5462                	lw	s0,56(sp)
40062bd0:	07b2                	slli	a5,a5,0xc
40062bd2:	83b1                	srli	a5,a5,0xc
40062bd4:	00f488b3          	add	a7,s1,a5
40062bd8:	4c62                	lw	s8,24(sp)
40062bda:	54d2                	lw	s1,52(sp)
40062bdc:	5a22                	lw	s4,40(sp)
40062bde:	8546                	mv	a0,a7
40062be0:	6121                	addi	sp,sp,64
40062be2:	8082                	ret
40062be4:	00490513          	addi	a0,s2,4
40062be8:	c432                	sw	a2,8(sp)
40062bea:	c242                	sw	a6,4(sp)
40062bec:	e7ed40ef          	jal	4003726a <lv_mutex_lock>
40062bf0:	00092783          	lw	a5,0(s2)
40062bf4:	ad72                	cm.mva01s	s2,s4
40062bf6:	9782                	jalr	a5
40062bf8:	88aa                	mv	a7,a0
40062bfa:	24050b63          	beqz	a0,40062e50 <lv_font_get_bitmap_fmt_txt+0x398>
40062bfe:	d256                	sw	s5,36(sp)
40062c00:	00444703          	lbu	a4,4(s0)
40062c04:	4812                	lw	a6,4(sp)
40062c06:	4622                	lw	a2,8(sp)
40062c08:	bf11                	j	40062b1c <lv_font_get_bitmap_fmt_txt+0x64>
40062c0a:	00444b83          	lbu	s7,4(s0)
40062c0e:	f40b8ae3          	beqz	s7,40062b62 <lv_font_get_bitmap_fmt_txt+0xaa>
40062c12:	01462b03          	lw	s6,20(a2)
40062c16:	4f21                	li	t5,8
40062c18:	5efd                	li	t4,-1
40062c1a:	00fb7793          	andi	a5,s6,15
40062c1e:	02ff4933          	div	s2,t5,a5
40062c22:	40094e37          	lui	t3,0x40094
40062c26:	00fe9eb3          	sll	t4,t4,a5
40062c2a:	3e4e0e13          	addi	t3,t3,996 # 400943e4 <opa2_table>
40062c2e:	4281                	li	t0,0
40062c30:	4581                	li	a1,0
40062c32:	4a1d                	li	s4,7
40062c34:	4f85                	li	t6,1
40062c36:	18da5a63          	bge	s4,a3,40062dca <lv_font_get_bitmap_fmt_txt+0x312>
40062c3a:	0008c803          	lbu	a6,0(a7)
40062c3e:	0285                	addi	t0,t0,1
40062c40:	874a                	mv	a4,s2
40062c42:	0885                	addi	a7,a7,1
40062c44:	4681                	li	a3,0
40062c46:	40bb83b3          	sub	t2,s7,a1
40062c4a:	0ae3c3b3          	min	t2,t2,a4
40062c4e:	00b48733          	add	a4,s1,a1
40062c52:	00e38333          	add	t1,t2,a4
40062c56:	00039f63          	bnez	t2,40062c74 <lv_font_get_bitmap_fmt_txt+0x1bc>
40062c5a:	a2a5                	j	40062dc2 <lv_font_get_bitmap_fmt_txt+0x30a>
40062c5c:	000bc783          	lbu	a5,0(s7)
40062c60:	0705                	addi	a4,a4,1
40062c62:	fef70fa3          	sb	a5,-1(a4)
40062c66:	01462b03          	lw	s6,20(a2)
40062c6a:	00fb7793          	andi	a5,s6,15
40062c6e:	96be                	add	a3,a3,a5
40062c70:	02e30f63          	beq	t1,a4,40062cae <lv_font_get_bitmap_fmt_txt+0x1f6>
40062c74:	01464b03          	lbu	s6,20(a2)
40062c78:	40ff07b3          	sub	a5,t5,a5
40062c7c:	8f95                	sub	a5,a5,a3
40062c7e:	40f857b3          	sra	a5,a6,a5
40062c82:	41d7f7b3          	andn	a5,a5,t4
40062c86:	00fb7b13          	andi	s6,s6,15
40062c8a:	01c78bb3          	add	s7,a5,t3
40062c8e:	fdfb17e3          	bne	s6,t6,40062c5c <lv_font_get_bitmap_fmt_txt+0x1a4>
40062c92:	00f037b3          	snez	a5,a5
40062c96:	40f007b3          	neg	a5,a5
40062c9a:	9fe1                	zext.b	a5,a5
40062c9c:	8b1c                	sb	a5,0(a4)
40062c9e:	01462b03          	lw	s6,20(a2)
40062ca2:	0705                	addi	a4,a4,1
40062ca4:	00fb7793          	andi	a5,s6,15
40062ca8:	96be                	add	a3,a3,a5
40062caa:	fce315e3          	bne	t1,a4,40062c74 <lv_font_get_bitmap_fmt_txt+0x1bc>
40062cae:	00444b83          	lbu	s7,4(s0)
40062cb2:	959e                	add	a1,a1,t2
40062cb4:	f975c1e3          	blt	a1,s7,40062c36 <lv_font_get_bitmap_fmt_txt+0x17e>
40062cb8:	47a1                	li	a5,8
40062cba:	0af6c6b3          	min	a3,a3,a5
40062cbe:	00544583          	lbu	a1,5(s0)
40062cc2:	ea0991e3          	bnez	s3,40062b64 <lv_font_get_bitmap_fmt_txt+0xac>
40062cc6:	0505                	addi	a0,a0,1
40062cc8:	10b55663          	bge	a0,a1,40062dd4 <lv_font_get_bitmap_fmt_txt+0x31c>
40062ccc:	01464303          	lbu	t1,20(a2)
40062cd0:	94d6                	add	s1,s1,s5
40062cd2:	00f37793          	andi	a5,t1,15
40062cd6:	4e11                	li	t3,4
40062cd8:	4ea1                	li	t4,8
40062cda:	4f05                	li	t5,1
40062cdc:	00f37f93          	andi	t6,t1,15
40062ce0:	fff78713          	addi	a4,a5,-1
40062ce4:	9f61                	zext.b	a4,a4
40062ce6:	03c78063          	beq	a5,t3,40062d06 <lv_font_get_bitmap_fmt_txt+0x24e>
40062cea:	15d78663          	beq	a5,t4,40062e36 <lv_font_get_bitmap_fmt_txt+0x37e>
40062cee:	10ef7c63          	bgeu	t5,a4,40062e06 <lv_font_get_bitmap_fmt_txt+0x34e>
40062cf2:	0505                	addi	a0,a0,1
40062cf4:	0eb55063          	bge	a0,a1,40062dd4 <lv_font_get_bitmap_fmt_txt+0x31c>
40062cf8:	87fe                	mv	a5,t6
40062cfa:	fff78713          	addi	a4,a5,-1
40062cfe:	94d6                	add	s1,s1,s5
40062d00:	9f61                	zext.b	a4,a4
40062d02:	ffc794e3          	bne	a5,t3,40062cea <lv_font_get_bitmap_fmt_txt+0x232>
40062d06:	16f69163          	bne	a3,a5,40062e68 <lv_font_get_bitmap_fmt_txt+0x3b0>
40062d0a:	40094eb7          	lui	t4,0x40094
40062d0e:	3e8e8e93          	addi	t4,t4,1000 # 400943e8 <opa4_table>
40062d12:	00f87793          	andi	a5,a6,15
40062d16:	97f6                	add	a5,a5,t4
40062d18:	839c                	lbu	a5,0(a5)
40062d1a:	4709                	li	a4,2
40062d1c:	46a1                	li	a3,8
40062d1e:	889c                	sb	a5,0(s1)
40062d20:	4e05                	li	t3,1
40062d22:	00444783          	lbu	a5,4(s0)
40062d26:	14f75463          	bge	a4,a5,40062e6e <lv_font_get_bitmap_fmt_txt+0x3b6>
40062d2a:	40094eb7          	lui	t4,0x40094
40062d2e:	3e8e8e93          	addi	t4,t4,1000 # 400943e8 <opa4_table>
40062d32:	01c487b3          	add	a5,s1,t3
40062d36:	4281                	li	t0,0
40062d38:	0008c803          	lbu	a6,0(a7)
40062d3c:	003e0713          	addi	a4,t3,3
40062d40:	0885                	addi	a7,a7,1
40062d42:	00485313          	srli	t1,a6,0x4
40062d46:	00f87593          	andi	a1,a6,15
40062d4a:	9376                	add	t1,t1,t4
40062d4c:	95f6                	add	a1,a1,t4
40062d4e:	818c                	lbu	a1,0(a1)
40062d50:	00034303          	lbu	t1,0(t1)
40062d54:	0285                	addi	t0,t0,1
40062d56:	8bcc                	sb	a1,1(a5)
40062d58:	00678023          	sb	t1,0(a5)
40062d5c:	00444583          	lbu	a1,4(s0)
40062d60:	0789                	addi	a5,a5,2
40062d62:	0e09                	addi	t3,t3,2
40062d64:	fcb74ae3          	blt	a4,a1,40062d38 <lv_font_get_bitmap_fmt_txt+0x280>
40062d68:	f4be5be3          	bge	t3,a1,40062cbe <lv_font_get_bitmap_fmt_txt+0x206>
40062d6c:	0008c803          	lbu	a6,0(a7)
40062d70:	9e26                	add	t3,t3,s1
40062d72:	0285                	addi	t0,t0,1
40062d74:	00485793          	srli	a5,a6,0x4
40062d78:	9ebe                	add	t4,t4,a5
40062d7a:	000ec783          	lbu	a5,0(t4)
40062d7e:	0885                	addi	a7,a7,1
40062d80:	4691                	li	a3,4
40062d82:	00fe0023          	sb	a5,0(t3)
40062d86:	bf25                	j	40062cbe <lv_font_get_bitmap_fmt_txt+0x206>
40062d88:	00448413          	addi	s0,s1,4
40062d8c:	8522                	mv	a0,s0
40062d8e:	c232                	sw	a2,4(sp)
40062d90:	cdad40ef          	jal	4003726a <lv_mutex_lock>
40062d94:	409c                	lw	a5,0(s1)
40062d96:	acf2                	cm.mva01s	s1,s4
40062d98:	9782                	jalr	a5
40062d9a:	88aa                	mv	a7,a0
40062d9c:	cd79                	beqz	a0,40062e7a <lv_font_get_bitmap_fmt_txt+0x3c2>
40062d9e:	4612                	lw	a2,4(sp)
40062da0:	01664783          	lbu	a5,22(a2)
40062da4:	c799                	beqz	a5,40062db2 <lv_font_get_bitmap_fmt_txt+0x2fa>
40062da6:	c22a                	sw	a0,4(sp)
40062da8:	4208                	lw	a0,0(a2)
40062daa:	0511                	addi	a0,a0,4
40062dac:	d16d40ef          	jal	400372c2 <lv_mutex_unlock>
40062db0:	4892                	lw	a7,4(sp)
40062db2:	5462                	lw	s0,56(sp)
40062db4:	50f2                	lw	ra,60(sp)
40062db6:	54d2                	lw	s1,52(sp)
40062db8:	4c62                	lw	s8,24(sp)
40062dba:	5a22                	lw	s4,40(sp)
40062dbc:	8546                	mv	a0,a7
40062dbe:	6121                	addi	sp,sp,64
40062dc0:	8082                	ret
40062dc2:	ef75dbe3          	bge	a1,s7,40062cb8 <lv_font_get_bitmap_fmt_txt+0x200>
40062dc6:	00fb7793          	andi	a5,s6,15
40062dca:	02f6c733          	div	a4,a3,a5
40062dce:	40e90733          	sub	a4,s2,a4
40062dd2:	bd95                	j	40062c46 <lv_font_get_bitmap_fmt_txt+0x18e>
40062dd4:	5b02                	lw	s6,32(sp)
40062dd6:	4bf2                	lw	s7,28(sp)
40062dd8:	01664783          	lbu	a5,22(a2)
40062ddc:	c789                	beqz	a5,40062de6 <lv_font_get_bitmap_fmt_txt+0x32e>
40062dde:	4208                	lw	a0,0(a2)
40062de0:	0511                	addi	a0,a0,4
40062de2:	ce0d40ef          	jal	400372c2 <lv_mutex_unlock>
40062de6:	8562                	mv	a0,s8
40062de8:	4581                	li	a1,0
40062dea:	916bd0ef          	jal	4001ff00 <lv_draw_buf_flush_cache>
40062dee:	50f2                	lw	ra,60(sp)
40062df0:	5462                	lw	s0,56(sp)
40062df2:	88e2                	mv	a7,s8
40062df4:	54d2                	lw	s1,52(sp)
40062df6:	5942                	lw	s2,48(sp)
40062df8:	59b2                	lw	s3,44(sp)
40062dfa:	5a92                	lw	s5,36(sp)
40062dfc:	4c62                	lw	s8,24(sp)
40062dfe:	5a22                	lw	s4,40(sp)
40062e00:	8546                	mv	a0,a7
40062e02:	6121                	addi	sp,sp,64
40062e04:	8082                	ret
40062e06:	00444b83          	lbu	s7,4(s0)
40062e0a:	e00b94e3          	bnez	s7,40062c12 <lv_font_get_bitmap_fmt_txt+0x15a>
40062e0e:	0bd6c6b3          	min	a3,a3,t4
40062e12:	bd55                	j	40062cc6 <lv_font_get_bitmap_fmt_txt+0x20e>
40062e14:	00444703          	lbu	a4,4(s0)
40062e18:	d40705e3          	beqz	a4,40062b62 <lv_font_get_bitmap_fmt_txt+0xaa>
40062e1c:	4781                	li	a5,0
40062e1e:	0008c583          	lbu	a1,0(a7)
40062e22:	00f48733          	add	a4,s1,a5
40062e26:	0785                	addi	a5,a5,1
40062e28:	8b0c                	sb	a1,0(a4)
40062e2a:	00444283          	lbu	t0,4(s0)
40062e2e:	0885                	addi	a7,a7,1
40062e30:	fe57c7e3          	blt	a5,t0,40062e1e <lv_font_get_bitmap_fmt_txt+0x366>
40062e34:	b569                	j	40062cbe <lv_font_get_bitmap_fmt_txt+0x206>
40062e36:	00444783          	lbu	a5,4(s0)
40062e3a:	f3ed                	bnez	a5,40062e1c <lv_font_get_bitmap_fmt_txt+0x364>
40062e3c:	0505                	addi	a0,a0,1
40062e3e:	f8b55be3          	bge	a0,a1,40062dd4 <lv_font_get_bitmap_fmt_txt+0x31c>
40062e42:	94d6                	add	s1,s1,s5
40062e44:	00f37793          	andi	a5,t1,15
40062e48:	bd61                	j	40062ce0 <lv_font_get_bitmap_fmt_txt+0x228>
40062e4a:	46a1                	li	a3,8
40062e4c:	4501                	li	a0,0
40062e4e:	b561                	j	40062cd6 <lv_font_get_bitmap_fmt_txt+0x21e>
40062e50:	c22a                	sw	a0,4(sp)
40062e52:	00490513          	addi	a0,s2,4
40062e56:	c6cd40ef          	jal	400372c2 <lv_mutex_unlock>
40062e5a:	4892                	lw	a7,4(sp)
40062e5c:	5462                	lw	s0,56(sp)
40062e5e:	54d2                	lw	s1,52(sp)
40062e60:	5942                	lw	s2,48(sp)
40062e62:	59b2                	lw	s3,44(sp)
40062e64:	4c62                	lw	s8,24(sp)
40062e66:	b3b9                	j	40062bb4 <lv_font_get_bitmap_fmt_txt+0xfc>
40062e68:	4705                	li	a4,1
40062e6a:	4e01                	li	t3,0
40062e6c:	bd5d                	j	40062d22 <lv_font_get_bitmap_fmt_txt+0x26a>
40062e6e:	4281                	li	t0,0
40062e70:	00fe4863          	blt	t3,a5,40062e80 <lv_font_get_bitmap_fmt_txt+0x3c8>
40062e74:	00544583          	lbu	a1,5(s0)
40062e78:	b5b9                	j	40062cc6 <lv_font_get_bitmap_fmt_txt+0x20e>
40062e7a:	c22a                	sw	a0,4(sp)
40062e7c:	8522                	mv	a0,s0
40062e7e:	b73d                	j	40062dac <lv_font_get_bitmap_fmt_txt+0x2f4>
40062e80:	40094eb7          	lui	t4,0x40094
40062e84:	3e8e8e93          	addi	t4,t4,1000 # 400943e8 <opa4_table>
40062e88:	b5d5                	j	40062d6c <lv_font_get_bitmap_fmt_txt+0x2b4>

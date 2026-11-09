
/tmp/present-s31-stable-20261109/pad24/s31-rgb/benchmark/present/build/present_benchmark.elf:     file format elf32-littleriscv


Disassembly of section .iram0.text:

Disassembly of section .flash.text:

40051d4e <lv_font_get_bitmap_fmt_txt>:
40051d4e:	7139                	addi	sp,sp,-64
40051d50:	d452                	sw	s4,40(sp)
40051d52:	00852a03          	lw	s4,8(a0)
40051d56:	de06                	sw	ra,60(sp)
40051d58:	0e0a0863          	beqz	s4,40051e48 <lv_font_get_bitmap_fmt_txt+0xfa>
40051d5c:	4118                	lw	a4,0(a0)
40051d5e:	02054783          	lbu	a5,32(a0)
40051d62:	dc22                	sw	s0,56(sp)
40051d64:	4310                	lw	a2,0(a4)
40051d66:	cc62                	sw	s8,24(sp)
40051d68:	8b89                	andi	a5,a5,2
40051d6a:	4240                	lw	s0,4(a2)
40051d6c:	8c2e                	mv	s8,a1
40051d6e:	208a6433          	sh3add	s0,s4,s0
40051d72:	e3ed                	bnez	a5,40051e54 <lv_font_get_bitmap_fmt_txt+0x106>
40051d74:	00444703          	lbu	a4,4(s0)
40051d78:	00544783          	lbu	a5,5(s0)
40051d7c:	9fd9                	mul	a5,a5,a4
40051d7e:	c3f9                	beqz	a5,40051e44 <lv_font_get_bitmap_fmt_txt+0xf6>
40051d80:	01464803          	lbu	a6,20(a2)
40051d84:	06087813          	andi	a6,a6,96
40051d88:	0a081e63          	bnez	a6,40051e44 <lv_font_get_bitmap_fmt_txt+0xf6>
40051d8c:	01664783          	lbu	a5,22(a2)
40051d90:	da26                	sw	s1,52(sp)
40051d92:	d84a                	sw	s2,48(sp)
40051d94:	d64e                	sw	s3,44(sp)
40051d96:	4984                	lw	s1,16(a1)
40051d98:	01e55983          	lhu	s3,30(a0)
40051d9c:	00062903          	lw	s2,0(a2)
40051da0:	efe9                	bnez	a5,40051e7a <lv_font_get_bitmap_fmt_txt+0x12c>
40051da2:	401c                	lw	a5,0(s0)
40051da4:	07b2                	slli	a5,a5,0xc
40051da6:	83b1                	srli	a5,a5,0xc
40051da8:	00f908b3          	add	a7,s2,a5
40051dac:	34088363          	beqz	a7,400520f2 <lv_font_get_bitmap_fmt_txt+0x3a4>
40051db0:	d256                	sw	s5,36(sp)
40051db2:	45b9                	li	a1,14
40051db4:	853a                	mv	a0,a4
40051db6:	c232                	sw	a2,4(sp)
40051db8:	c646                	sw	a7,12(sp)
40051dba:	c442                	sw	a6,8(sp)
40051dbc:	846d60ef          	jal	40027e02 <lv_draw_buf_width_to_stride>
40051dc0:	00544583          	lbu	a1,5(s0)
40051dc4:	4612                	lw	a2,4(sp)
40051dc6:	8aaa                	mv	s5,a0
40051dc8:	2a058363          	beqz	a1,4005206e <lv_font_get_bitmap_fmt_txt+0x320>
40051dcc:	01464303          	lbu	t1,20(a2)
40051dd0:	d05a                	sw	s6,32(sp)
40051dd2:	ce5e                	sw	s7,28(sp)
40051dd4:	4822                	lw	a6,8(sp)
40051dd6:	48b2                	lw	a7,12(sp)
40051dd8:	00f37793          	andi	a5,t1,15
40051ddc:	30098263          	beqz	s3,400520e0 <lv_font_get_bitmap_fmt_txt+0x392>
40051de0:	4711                	li	a4,4
40051de2:	4501                	li	a0,0
40051de4:	02e78863          	beq	a5,a4,40051e14 <lv_font_get_bitmap_fmt_txt+0xc6>
40051de8:	46a1                	li	a3,8
40051dea:	2cd78063          	beq	a5,a3,400520aa <lv_font_get_bitmap_fmt_txt+0x35c>
40051dee:	17fd                	addi	a5,a5,-1
40051df0:	9fe1                	zext.b	a5,a5
40051df2:	4705                	li	a4,1
40051df4:	0af77663          	bgeu	a4,a5,40051ea0 <lv_font_get_bitmap_fmt_txt+0x152>
40051df8:	4281                	li	t0,0
40051dfa:	0505                	addi	a0,a0,1
40051dfc:	26b55763          	bge	a0,a1,4005206a <lv_font_get_bitmap_fmt_txt+0x31c>
40051e00:	01464783          	lbu	a5,20(a2)
40051e04:	405982b3          	sub	t0,s3,t0
40051e08:	4711                	li	a4,4
40051e0a:	8bbd                	andi	a5,a5,15
40051e0c:	9896                	add	a7,a7,t0
40051e0e:	94d6                	add	s1,s1,s5
40051e10:	fce79ce3          	bne	a5,a4,40051de8 <lv_font_get_bitmap_fmt_txt+0x9a>
40051e14:	00444283          	lbu	t0,4(s0)
40051e18:	4785                	li	a5,1
40051e1a:	46a1                	li	a3,8
40051e1c:	4e01                	li	t3,0
40051e1e:	1a57c163          	blt	a5,t0,40051fc0 <lv_font_get_bitmap_fmt_txt+0x272>
40051e22:	fcf29be3          	bne	t0,a5,40051df8 <lv_font_get_bitmap_fmt_txt+0xaa>
40051e26:	0008c803          	lbu	a6,0(a7)
40051e2a:	400a1eb7          	lui	t4,0x400a1
40051e2e:	4e0e8e93          	addi	t4,t4,1248 # 400a14e0 <opa4_table>
40051e32:	00485793          	srli	a5,a6,0x4
40051e36:	97f6                	add	a5,a5,t4
40051e38:	839c                	lbu	a5,0(a5)
40051e3a:	9896                	add	a7,a7,t0
40051e3c:	889c                	sb	a5,0(s1)
40051e3e:	00544583          	lbu	a1,5(s0)
40051e42:	bf65                	j	40051dfa <lv_font_get_bitmap_fmt_txt+0xac>
40051e44:	5462                	lw	s0,56(sp)
40051e46:	4c62                	lw	s8,24(sp)
40051e48:	4881                	li	a7,0
40051e4a:	50f2                	lw	ra,60(sp)
40051e4c:	5a22                	lw	s4,40(sp)
40051e4e:	8546                	mv	a0,a7
40051e50:	6121                	addi	sp,sp,64
40051e52:	8082                	ret
40051e54:	01664783          	lbu	a5,22(a2)
40051e58:	da26                	sw	s1,52(sp)
40051e5a:	4204                	lw	s1,0(a2)
40051e5c:	1c079163          	bnez	a5,4005201e <lv_font_get_bitmap_fmt_txt+0x2d0>
40051e60:	401c                	lw	a5,0(s0)
40051e62:	50f2                	lw	ra,60(sp)
40051e64:	5462                	lw	s0,56(sp)
40051e66:	07b2                	slli	a5,a5,0xc
40051e68:	83b1                	srli	a5,a5,0xc
40051e6a:	00f488b3          	add	a7,s1,a5
40051e6e:	4c62                	lw	s8,24(sp)
40051e70:	54d2                	lw	s1,52(sp)
40051e72:	5a22                	lw	s4,40(sp)
40051e74:	8546                	mv	a0,a7
40051e76:	6121                	addi	sp,sp,64
40051e78:	8082                	ret
40051e7a:	00490513          	addi	a0,s2,4
40051e7e:	c432                	sw	a2,8(sp)
40051e80:	c242                	sw	a6,4(sp)
40051e82:	af1de0ef          	jal	40030972 <lv_mutex_lock>
40051e86:	00092783          	lw	a5,0(s2)
40051e8a:	ad72                	cm.mva01s	s2,s4
40051e8c:	9782                	jalr	a5
40051e8e:	88aa                	mv	a7,a0
40051e90:	24050b63          	beqz	a0,400520e6 <lv_font_get_bitmap_fmt_txt+0x398>
40051e94:	d256                	sw	s5,36(sp)
40051e96:	00444703          	lbu	a4,4(s0)
40051e9a:	4812                	lw	a6,4(sp)
40051e9c:	4622                	lw	a2,8(sp)
40051e9e:	bf11                	j	40051db2 <lv_font_get_bitmap_fmt_txt+0x64>
40051ea0:	00444b83          	lbu	s7,4(s0)
40051ea4:	f40b8ae3          	beqz	s7,40051df8 <lv_font_get_bitmap_fmt_txt+0xaa>
40051ea8:	01462b03          	lw	s6,20(a2)
40051eac:	4f21                	li	t5,8
40051eae:	5efd                	li	t4,-1
40051eb0:	00fb7793          	andi	a5,s6,15
40051eb4:	02ff4933          	div	s2,t5,a5
40051eb8:	400a1e37          	lui	t3,0x400a1
40051ebc:	00fe9eb3          	sll	t4,t4,a5
40051ec0:	4dce0e13          	addi	t3,t3,1244 # 400a14dc <opa2_table>
40051ec4:	4281                	li	t0,0
40051ec6:	4581                	li	a1,0
40051ec8:	4a1d                	li	s4,7
40051eca:	4f85                	li	t6,1
40051ecc:	18da5a63          	bge	s4,a3,40052060 <lv_font_get_bitmap_fmt_txt+0x312>
40051ed0:	0008c803          	lbu	a6,0(a7)
40051ed4:	0285                	addi	t0,t0,1
40051ed6:	874a                	mv	a4,s2
40051ed8:	0885                	addi	a7,a7,1
40051eda:	4681                	li	a3,0
40051edc:	40bb83b3          	sub	t2,s7,a1
40051ee0:	0ae3c3b3          	min	t2,t2,a4
40051ee4:	00b48733          	add	a4,s1,a1
40051ee8:	00e38333          	add	t1,t2,a4
40051eec:	00039f63          	bnez	t2,40051f0a <lv_font_get_bitmap_fmt_txt+0x1bc>
40051ef0:	a2a5                	j	40052058 <lv_font_get_bitmap_fmt_txt+0x30a>
40051ef2:	000bc783          	lbu	a5,0(s7)
40051ef6:	0705                	addi	a4,a4,1
40051ef8:	fef70fa3          	sb	a5,-1(a4)
40051efc:	01462b03          	lw	s6,20(a2)
40051f00:	00fb7793          	andi	a5,s6,15
40051f04:	96be                	add	a3,a3,a5
40051f06:	02e30f63          	beq	t1,a4,40051f44 <lv_font_get_bitmap_fmt_txt+0x1f6>
40051f0a:	01464b03          	lbu	s6,20(a2)
40051f0e:	40ff07b3          	sub	a5,t5,a5
40051f12:	8f95                	sub	a5,a5,a3
40051f14:	40f857b3          	sra	a5,a6,a5
40051f18:	41d7f7b3          	andn	a5,a5,t4
40051f1c:	00fb7b13          	andi	s6,s6,15
40051f20:	01c78bb3          	add	s7,a5,t3
40051f24:	fdfb17e3          	bne	s6,t6,40051ef2 <lv_font_get_bitmap_fmt_txt+0x1a4>
40051f28:	00f037b3          	snez	a5,a5
40051f2c:	40f007b3          	neg	a5,a5
40051f30:	9fe1                	zext.b	a5,a5
40051f32:	8b1c                	sb	a5,0(a4)
40051f34:	01462b03          	lw	s6,20(a2)
40051f38:	0705                	addi	a4,a4,1
40051f3a:	00fb7793          	andi	a5,s6,15
40051f3e:	96be                	add	a3,a3,a5
40051f40:	fce315e3          	bne	t1,a4,40051f0a <lv_font_get_bitmap_fmt_txt+0x1bc>
40051f44:	00444b83          	lbu	s7,4(s0)
40051f48:	959e                	add	a1,a1,t2
40051f4a:	f975c1e3          	blt	a1,s7,40051ecc <lv_font_get_bitmap_fmt_txt+0x17e>
40051f4e:	47a1                	li	a5,8
40051f50:	0af6c6b3          	min	a3,a3,a5
40051f54:	00544583          	lbu	a1,5(s0)
40051f58:	ea0991e3          	bnez	s3,40051dfa <lv_font_get_bitmap_fmt_txt+0xac>
40051f5c:	0505                	addi	a0,a0,1
40051f5e:	10b55663          	bge	a0,a1,4005206a <lv_font_get_bitmap_fmt_txt+0x31c>
40051f62:	01464303          	lbu	t1,20(a2)
40051f66:	94d6                	add	s1,s1,s5
40051f68:	00f37793          	andi	a5,t1,15
40051f6c:	4e11                	li	t3,4
40051f6e:	4ea1                	li	t4,8
40051f70:	4f05                	li	t5,1
40051f72:	00f37f93          	andi	t6,t1,15
40051f76:	fff78713          	addi	a4,a5,-1
40051f7a:	9f61                	zext.b	a4,a4
40051f7c:	03c78063          	beq	a5,t3,40051f9c <lv_font_get_bitmap_fmt_txt+0x24e>
40051f80:	15d78663          	beq	a5,t4,400520cc <lv_font_get_bitmap_fmt_txt+0x37e>
40051f84:	10ef7c63          	bgeu	t5,a4,4005209c <lv_font_get_bitmap_fmt_txt+0x34e>
40051f88:	0505                	addi	a0,a0,1
40051f8a:	0eb55063          	bge	a0,a1,4005206a <lv_font_get_bitmap_fmt_txt+0x31c>
40051f8e:	87fe                	mv	a5,t6
40051f90:	fff78713          	addi	a4,a5,-1
40051f94:	94d6                	add	s1,s1,s5
40051f96:	9f61                	zext.b	a4,a4
40051f98:	ffc794e3          	bne	a5,t3,40051f80 <lv_font_get_bitmap_fmt_txt+0x232>
40051f9c:	16f69163          	bne	a3,a5,400520fe <lv_font_get_bitmap_fmt_txt+0x3b0>
40051fa0:	400a1eb7          	lui	t4,0x400a1
40051fa4:	4e0e8e93          	addi	t4,t4,1248 # 400a14e0 <opa4_table>
40051fa8:	00f87793          	andi	a5,a6,15
40051fac:	97f6                	add	a5,a5,t4
40051fae:	839c                	lbu	a5,0(a5)
40051fb0:	4709                	li	a4,2
40051fb2:	46a1                	li	a3,8
40051fb4:	889c                	sb	a5,0(s1)
40051fb6:	4e05                	li	t3,1
40051fb8:	00444783          	lbu	a5,4(s0)
40051fbc:	14f75463          	bge	a4,a5,40052104 <lv_font_get_bitmap_fmt_txt+0x3b6>
40051fc0:	400a1eb7          	lui	t4,0x400a1
40051fc4:	4e0e8e93          	addi	t4,t4,1248 # 400a14e0 <opa4_table>
40051fc8:	01c487b3          	add	a5,s1,t3
40051fcc:	4281                	li	t0,0
40051fce:	0008c803          	lbu	a6,0(a7)
40051fd2:	003e0713          	addi	a4,t3,3
40051fd6:	0885                	addi	a7,a7,1
40051fd8:	00485313          	srli	t1,a6,0x4
40051fdc:	00f87593          	andi	a1,a6,15
40051fe0:	9376                	add	t1,t1,t4
40051fe2:	95f6                	add	a1,a1,t4
40051fe4:	818c                	lbu	a1,0(a1)
40051fe6:	00034303          	lbu	t1,0(t1)
40051fea:	0285                	addi	t0,t0,1
40051fec:	8bcc                	sb	a1,1(a5)
40051fee:	00678023          	sb	t1,0(a5)
40051ff2:	00444583          	lbu	a1,4(s0)
40051ff6:	0789                	addi	a5,a5,2
40051ff8:	0e09                	addi	t3,t3,2
40051ffa:	fcb74ae3          	blt	a4,a1,40051fce <lv_font_get_bitmap_fmt_txt+0x280>
40051ffe:	f4be5be3          	bge	t3,a1,40051f54 <lv_font_get_bitmap_fmt_txt+0x206>
40052002:	0008c803          	lbu	a6,0(a7)
40052006:	9e26                	add	t3,t3,s1
40052008:	0285                	addi	t0,t0,1
4005200a:	00485793          	srli	a5,a6,0x4
4005200e:	9ebe                	add	t4,t4,a5
40052010:	000ec783          	lbu	a5,0(t4)
40052014:	0885                	addi	a7,a7,1
40052016:	4691                	li	a3,4
40052018:	00fe0023          	sb	a5,0(t3)
4005201c:	bf25                	j	40051f54 <lv_font_get_bitmap_fmt_txt+0x206>
4005201e:	00448413          	addi	s0,s1,4
40052022:	8522                	mv	a0,s0
40052024:	c232                	sw	a2,4(sp)
40052026:	94dde0ef          	jal	40030972 <lv_mutex_lock>
4005202a:	409c                	lw	a5,0(s1)
4005202c:	acf2                	cm.mva01s	s1,s4
4005202e:	9782                	jalr	a5
40052030:	88aa                	mv	a7,a0
40052032:	cd79                	beqz	a0,40052110 <lv_font_get_bitmap_fmt_txt+0x3c2>
40052034:	4612                	lw	a2,4(sp)
40052036:	01664783          	lbu	a5,22(a2)
4005203a:	c799                	beqz	a5,40052048 <lv_font_get_bitmap_fmt_txt+0x2fa>
4005203c:	c22a                	sw	a0,4(sp)
4005203e:	4208                	lw	a0,0(a2)
40052040:	0511                	addi	a0,a0,4
40052042:	989de0ef          	jal	400309ca <lv_mutex_unlock>
40052046:	4892                	lw	a7,4(sp)
40052048:	5462                	lw	s0,56(sp)
4005204a:	50f2                	lw	ra,60(sp)
4005204c:	54d2                	lw	s1,52(sp)
4005204e:	4c62                	lw	s8,24(sp)
40052050:	5a22                	lw	s4,40(sp)
40052052:	8546                	mv	a0,a7
40052054:	6121                	addi	sp,sp,64
40052056:	8082                	ret
40052058:	ef75dbe3          	bge	a1,s7,40051f4e <lv_font_get_bitmap_fmt_txt+0x200>
4005205c:	00fb7793          	andi	a5,s6,15
40052060:	02f6c733          	div	a4,a3,a5
40052064:	40e90733          	sub	a4,s2,a4
40052068:	bd95                	j	40051edc <lv_font_get_bitmap_fmt_txt+0x18e>
4005206a:	5b02                	lw	s6,32(sp)
4005206c:	4bf2                	lw	s7,28(sp)
4005206e:	01664783          	lbu	a5,22(a2)
40052072:	c789                	beqz	a5,4005207c <lv_font_get_bitmap_fmt_txt+0x32e>
40052074:	4208                	lw	a0,0(a2)
40052076:	0511                	addi	a0,a0,4
40052078:	953de0ef          	jal	400309ca <lv_mutex_unlock>
4005207c:	8562                	mv	a0,s8
4005207e:	4581                	li	a1,0
40052080:	d93d50ef          	jal	40027e12 <lv_draw_buf_flush_cache>
40052084:	50f2                	lw	ra,60(sp)
40052086:	5462                	lw	s0,56(sp)
40052088:	88e2                	mv	a7,s8
4005208a:	54d2                	lw	s1,52(sp)
4005208c:	5942                	lw	s2,48(sp)
4005208e:	59b2                	lw	s3,44(sp)
40052090:	5a92                	lw	s5,36(sp)
40052092:	4c62                	lw	s8,24(sp)
40052094:	5a22                	lw	s4,40(sp)
40052096:	8546                	mv	a0,a7
40052098:	6121                	addi	sp,sp,64
4005209a:	8082                	ret
4005209c:	00444b83          	lbu	s7,4(s0)
400520a0:	e00b94e3          	bnez	s7,40051ea8 <lv_font_get_bitmap_fmt_txt+0x15a>
400520a4:	0bd6c6b3          	min	a3,a3,t4
400520a8:	bd55                	j	40051f5c <lv_font_get_bitmap_fmt_txt+0x20e>
400520aa:	00444703          	lbu	a4,4(s0)
400520ae:	d40705e3          	beqz	a4,40051df8 <lv_font_get_bitmap_fmt_txt+0xaa>
400520b2:	4781                	li	a5,0
400520b4:	0008c583          	lbu	a1,0(a7)
400520b8:	00f48733          	add	a4,s1,a5
400520bc:	0785                	addi	a5,a5,1
400520be:	8b0c                	sb	a1,0(a4)
400520c0:	00444283          	lbu	t0,4(s0)
400520c4:	0885                	addi	a7,a7,1
400520c6:	fe57c7e3          	blt	a5,t0,400520b4 <lv_font_get_bitmap_fmt_txt+0x366>
400520ca:	b569                	j	40051f54 <lv_font_get_bitmap_fmt_txt+0x206>
400520cc:	00444783          	lbu	a5,4(s0)
400520d0:	f3ed                	bnez	a5,400520b2 <lv_font_get_bitmap_fmt_txt+0x364>
400520d2:	0505                	addi	a0,a0,1
400520d4:	f8b55be3          	bge	a0,a1,4005206a <lv_font_get_bitmap_fmt_txt+0x31c>
400520d8:	94d6                	add	s1,s1,s5
400520da:	00f37793          	andi	a5,t1,15
400520de:	bd61                	j	40051f76 <lv_font_get_bitmap_fmt_txt+0x228>
400520e0:	46a1                	li	a3,8
400520e2:	4501                	li	a0,0
400520e4:	b561                	j	40051f6c <lv_font_get_bitmap_fmt_txt+0x21e>
400520e6:	c22a                	sw	a0,4(sp)
400520e8:	00490513          	addi	a0,s2,4
400520ec:	8dfde0ef          	jal	400309ca <lv_mutex_unlock>
400520f0:	4892                	lw	a7,4(sp)
400520f2:	5462                	lw	s0,56(sp)
400520f4:	54d2                	lw	s1,52(sp)
400520f6:	5942                	lw	s2,48(sp)
400520f8:	59b2                	lw	s3,44(sp)
400520fa:	4c62                	lw	s8,24(sp)
400520fc:	b3b9                	j	40051e4a <lv_font_get_bitmap_fmt_txt+0xfc>
400520fe:	4705                	li	a4,1
40052100:	4e01                	li	t3,0
40052102:	bd5d                	j	40051fb8 <lv_font_get_bitmap_fmt_txt+0x26a>
40052104:	4281                	li	t0,0
40052106:	00fe4863          	blt	t3,a5,40052116 <lv_font_get_bitmap_fmt_txt+0x3c8>
4005210a:	00544583          	lbu	a1,5(s0)
4005210e:	b5b9                	j	40051f5c <lv_font_get_bitmap_fmt_txt+0x20e>
40052110:	c22a                	sw	a0,4(sp)
40052112:	8522                	mv	a0,s0
40052114:	b73d                	j	40052042 <lv_font_get_bitmap_fmt_txt+0x2f4>
40052116:	400a1eb7          	lui	t4,0x400a1
4005211a:	4e0e8e93          	addi	t4,t4,1248 # 400a14e0 <opa4_table>
4005211e:	b5d5                	j	40052002 <lv_font_get_bitmap_fmt_txt+0x2b4>

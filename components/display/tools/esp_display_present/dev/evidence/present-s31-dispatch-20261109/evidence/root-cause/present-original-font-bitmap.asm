
/tmp/present-s31-layout-20261109/baseline/s31-rgb/benchmark/present/build/present_benchmark.elf:     file format elf32-littleriscv


Disassembly of section .iram0.text:

Disassembly of section .flash.text:

40051d36 <lv_font_get_bitmap_fmt_txt>:
40051d36:	7139                	addi	sp,sp,-64
40051d38:	d452                	sw	s4,40(sp)
40051d3a:	00852a03          	lw	s4,8(a0)
40051d3e:	de06                	sw	ra,60(sp)
40051d40:	0e0a0863          	beqz	s4,40051e30 <lv_font_get_bitmap_fmt_txt+0xfa>
40051d44:	4118                	lw	a4,0(a0)
40051d46:	02054783          	lbu	a5,32(a0)
40051d4a:	dc22                	sw	s0,56(sp)
40051d4c:	4310                	lw	a2,0(a4)
40051d4e:	cc62                	sw	s8,24(sp)
40051d50:	8b89                	andi	a5,a5,2
40051d52:	4240                	lw	s0,4(a2)
40051d54:	8c2e                	mv	s8,a1
40051d56:	208a6433          	sh3add	s0,s4,s0
40051d5a:	e3ed                	bnez	a5,40051e3c <lv_font_get_bitmap_fmt_txt+0x106>
40051d5c:	00444703          	lbu	a4,4(s0)
40051d60:	00544783          	lbu	a5,5(s0)
40051d64:	9fd9                	mul	a5,a5,a4
40051d66:	c3f9                	beqz	a5,40051e2c <lv_font_get_bitmap_fmt_txt+0xf6>
40051d68:	01464803          	lbu	a6,20(a2)
40051d6c:	06087813          	andi	a6,a6,96
40051d70:	0a081e63          	bnez	a6,40051e2c <lv_font_get_bitmap_fmt_txt+0xf6>
40051d74:	01664783          	lbu	a5,22(a2)
40051d78:	da26                	sw	s1,52(sp)
40051d7a:	d84a                	sw	s2,48(sp)
40051d7c:	d64e                	sw	s3,44(sp)
40051d7e:	4984                	lw	s1,16(a1)
40051d80:	01e55983          	lhu	s3,30(a0)
40051d84:	00062903          	lw	s2,0(a2)
40051d88:	efe9                	bnez	a5,40051e62 <lv_font_get_bitmap_fmt_txt+0x12c>
40051d8a:	401c                	lw	a5,0(s0)
40051d8c:	07b2                	slli	a5,a5,0xc
40051d8e:	83b1                	srli	a5,a5,0xc
40051d90:	00f908b3          	add	a7,s2,a5
40051d94:	34088363          	beqz	a7,400520da <lv_font_get_bitmap_fmt_txt+0x3a4>
40051d98:	d256                	sw	s5,36(sp)
40051d9a:	45b9                	li	a1,14
40051d9c:	853a                	mv	a0,a4
40051d9e:	c232                	sw	a2,4(sp)
40051da0:	c646                	sw	a7,12(sp)
40051da2:	c442                	sw	a6,8(sp)
40051da4:	846d60ef          	jal	40027dea <lv_draw_buf_width_to_stride>
40051da8:	00544583          	lbu	a1,5(s0)
40051dac:	4612                	lw	a2,4(sp)
40051dae:	8aaa                	mv	s5,a0
40051db0:	2a058363          	beqz	a1,40052056 <lv_font_get_bitmap_fmt_txt+0x320>
40051db4:	01464303          	lbu	t1,20(a2)
40051db8:	d05a                	sw	s6,32(sp)
40051dba:	ce5e                	sw	s7,28(sp)
40051dbc:	4822                	lw	a6,8(sp)
40051dbe:	48b2                	lw	a7,12(sp)
40051dc0:	00f37793          	andi	a5,t1,15
40051dc4:	30098263          	beqz	s3,400520c8 <lv_font_get_bitmap_fmt_txt+0x392>
40051dc8:	4711                	li	a4,4
40051dca:	4501                	li	a0,0
40051dcc:	02e78863          	beq	a5,a4,40051dfc <lv_font_get_bitmap_fmt_txt+0xc6>
40051dd0:	46a1                	li	a3,8
40051dd2:	2cd78063          	beq	a5,a3,40052092 <lv_font_get_bitmap_fmt_txt+0x35c>
40051dd6:	17fd                	addi	a5,a5,-1
40051dd8:	9fe1                	zext.b	a5,a5
40051dda:	4705                	li	a4,1
40051ddc:	0af77663          	bgeu	a4,a5,40051e88 <lv_font_get_bitmap_fmt_txt+0x152>
40051de0:	4281                	li	t0,0
40051de2:	0505                	addi	a0,a0,1
40051de4:	26b55763          	bge	a0,a1,40052052 <lv_font_get_bitmap_fmt_txt+0x31c>
40051de8:	01464783          	lbu	a5,20(a2)
40051dec:	405982b3          	sub	t0,s3,t0
40051df0:	4711                	li	a4,4
40051df2:	8bbd                	andi	a5,a5,15
40051df4:	9896                	add	a7,a7,t0
40051df6:	94d6                	add	s1,s1,s5
40051df8:	fce79ce3          	bne	a5,a4,40051dd0 <lv_font_get_bitmap_fmt_txt+0x9a>
40051dfc:	00444283          	lbu	t0,4(s0)
40051e00:	4785                	li	a5,1
40051e02:	46a1                	li	a3,8
40051e04:	4e01                	li	t3,0
40051e06:	1a57c163          	blt	a5,t0,40051fa8 <lv_font_get_bitmap_fmt_txt+0x272>
40051e0a:	fcf29be3          	bne	t0,a5,40051de0 <lv_font_get_bitmap_fmt_txt+0xaa>
40051e0e:	0008c803          	lbu	a6,0(a7)
40051e12:	400a1eb7          	lui	t4,0x400a1
40051e16:	4e0e8e93          	addi	t4,t4,1248 # 400a14e0 <opa4_table>
40051e1a:	00485793          	srli	a5,a6,0x4
40051e1e:	97f6                	add	a5,a5,t4
40051e20:	839c                	lbu	a5,0(a5)
40051e22:	9896                	add	a7,a7,t0
40051e24:	889c                	sb	a5,0(s1)
40051e26:	00544583          	lbu	a1,5(s0)
40051e2a:	bf65                	j	40051de2 <lv_font_get_bitmap_fmt_txt+0xac>
40051e2c:	5462                	lw	s0,56(sp)
40051e2e:	4c62                	lw	s8,24(sp)
40051e30:	4881                	li	a7,0
40051e32:	50f2                	lw	ra,60(sp)
40051e34:	5a22                	lw	s4,40(sp)
40051e36:	8546                	mv	a0,a7
40051e38:	6121                	addi	sp,sp,64
40051e3a:	8082                	ret
40051e3c:	01664783          	lbu	a5,22(a2)
40051e40:	da26                	sw	s1,52(sp)
40051e42:	4204                	lw	s1,0(a2)
40051e44:	1c079163          	bnez	a5,40052006 <lv_font_get_bitmap_fmt_txt+0x2d0>
40051e48:	401c                	lw	a5,0(s0)
40051e4a:	50f2                	lw	ra,60(sp)
40051e4c:	5462                	lw	s0,56(sp)
40051e4e:	07b2                	slli	a5,a5,0xc
40051e50:	83b1                	srli	a5,a5,0xc
40051e52:	00f488b3          	add	a7,s1,a5
40051e56:	4c62                	lw	s8,24(sp)
40051e58:	54d2                	lw	s1,52(sp)
40051e5a:	5a22                	lw	s4,40(sp)
40051e5c:	8546                	mv	a0,a7
40051e5e:	6121                	addi	sp,sp,64
40051e60:	8082                	ret
40051e62:	00490513          	addi	a0,s2,4
40051e66:	c432                	sw	a2,8(sp)
40051e68:	c242                	sw	a6,4(sp)
40051e6a:	af1de0ef          	jal	4003095a <lv_mutex_lock>
40051e6e:	00092783          	lw	a5,0(s2)
40051e72:	ad72                	cm.mva01s	s2,s4
40051e74:	9782                	jalr	a5
40051e76:	88aa                	mv	a7,a0
40051e78:	24050b63          	beqz	a0,400520ce <lv_font_get_bitmap_fmt_txt+0x398>
40051e7c:	d256                	sw	s5,36(sp)
40051e7e:	00444703          	lbu	a4,4(s0)
40051e82:	4812                	lw	a6,4(sp)
40051e84:	4622                	lw	a2,8(sp)
40051e86:	bf11                	j	40051d9a <lv_font_get_bitmap_fmt_txt+0x64>
40051e88:	00444b83          	lbu	s7,4(s0)
40051e8c:	f40b8ae3          	beqz	s7,40051de0 <lv_font_get_bitmap_fmt_txt+0xaa>
40051e90:	01462b03          	lw	s6,20(a2)
40051e94:	4f21                	li	t5,8
40051e96:	5efd                	li	t4,-1
40051e98:	00fb7793          	andi	a5,s6,15
40051e9c:	02ff4933          	div	s2,t5,a5
40051ea0:	400a1e37          	lui	t3,0x400a1
40051ea4:	00fe9eb3          	sll	t4,t4,a5
40051ea8:	4dce0e13          	addi	t3,t3,1244 # 400a14dc <opa2_table>
40051eac:	4281                	li	t0,0
40051eae:	4581                	li	a1,0
40051eb0:	4a1d                	li	s4,7
40051eb2:	4f85                	li	t6,1
40051eb4:	18da5a63          	bge	s4,a3,40052048 <lv_font_get_bitmap_fmt_txt+0x312>
40051eb8:	0008c803          	lbu	a6,0(a7)
40051ebc:	0285                	addi	t0,t0,1
40051ebe:	874a                	mv	a4,s2
40051ec0:	0885                	addi	a7,a7,1
40051ec2:	4681                	li	a3,0
40051ec4:	40bb83b3          	sub	t2,s7,a1
40051ec8:	0ae3c3b3          	min	t2,t2,a4
40051ecc:	00b48733          	add	a4,s1,a1
40051ed0:	00e38333          	add	t1,t2,a4
40051ed4:	00039f63          	bnez	t2,40051ef2 <lv_font_get_bitmap_fmt_txt+0x1bc>
40051ed8:	a2a5                	j	40052040 <lv_font_get_bitmap_fmt_txt+0x30a>
40051eda:	000bc783          	lbu	a5,0(s7)
40051ede:	0705                	addi	a4,a4,1
40051ee0:	fef70fa3          	sb	a5,-1(a4)
40051ee4:	01462b03          	lw	s6,20(a2)
40051ee8:	00fb7793          	andi	a5,s6,15
40051eec:	96be                	add	a3,a3,a5
40051eee:	02e30f63          	beq	t1,a4,40051f2c <lv_font_get_bitmap_fmt_txt+0x1f6>
40051ef2:	01464b03          	lbu	s6,20(a2)
40051ef6:	40ff07b3          	sub	a5,t5,a5
40051efa:	8f95                	sub	a5,a5,a3
40051efc:	40f857b3          	sra	a5,a6,a5
40051f00:	41d7f7b3          	andn	a5,a5,t4
40051f04:	00fb7b13          	andi	s6,s6,15
40051f08:	01c78bb3          	add	s7,a5,t3
40051f0c:	fdfb17e3          	bne	s6,t6,40051eda <lv_font_get_bitmap_fmt_txt+0x1a4>
40051f10:	00f037b3          	snez	a5,a5
40051f14:	40f007b3          	neg	a5,a5
40051f18:	9fe1                	zext.b	a5,a5
40051f1a:	8b1c                	sb	a5,0(a4)
40051f1c:	01462b03          	lw	s6,20(a2)
40051f20:	0705                	addi	a4,a4,1
40051f22:	00fb7793          	andi	a5,s6,15
40051f26:	96be                	add	a3,a3,a5
40051f28:	fce315e3          	bne	t1,a4,40051ef2 <lv_font_get_bitmap_fmt_txt+0x1bc>
40051f2c:	00444b83          	lbu	s7,4(s0)
40051f30:	959e                	add	a1,a1,t2
40051f32:	f975c1e3          	blt	a1,s7,40051eb4 <lv_font_get_bitmap_fmt_txt+0x17e>
40051f36:	47a1                	li	a5,8
40051f38:	0af6c6b3          	min	a3,a3,a5
40051f3c:	00544583          	lbu	a1,5(s0)
40051f40:	ea0991e3          	bnez	s3,40051de2 <lv_font_get_bitmap_fmt_txt+0xac>
40051f44:	0505                	addi	a0,a0,1
40051f46:	10b55663          	bge	a0,a1,40052052 <lv_font_get_bitmap_fmt_txt+0x31c>
40051f4a:	01464303          	lbu	t1,20(a2)
40051f4e:	94d6                	add	s1,s1,s5
40051f50:	00f37793          	andi	a5,t1,15
40051f54:	4e11                	li	t3,4
40051f56:	4ea1                	li	t4,8
40051f58:	4f05                	li	t5,1
40051f5a:	00f37f93          	andi	t6,t1,15
40051f5e:	fff78713          	addi	a4,a5,-1
40051f62:	9f61                	zext.b	a4,a4
40051f64:	03c78063          	beq	a5,t3,40051f84 <lv_font_get_bitmap_fmt_txt+0x24e>
40051f68:	15d78663          	beq	a5,t4,400520b4 <lv_font_get_bitmap_fmt_txt+0x37e>
40051f6c:	10ef7c63          	bgeu	t5,a4,40052084 <lv_font_get_bitmap_fmt_txt+0x34e>
40051f70:	0505                	addi	a0,a0,1
40051f72:	0eb55063          	bge	a0,a1,40052052 <lv_font_get_bitmap_fmt_txt+0x31c>
40051f76:	87fe                	mv	a5,t6
40051f78:	fff78713          	addi	a4,a5,-1
40051f7c:	94d6                	add	s1,s1,s5
40051f7e:	9f61                	zext.b	a4,a4
40051f80:	ffc794e3          	bne	a5,t3,40051f68 <lv_font_get_bitmap_fmt_txt+0x232>
40051f84:	16f69163          	bne	a3,a5,400520e6 <lv_font_get_bitmap_fmt_txt+0x3b0>
40051f88:	400a1eb7          	lui	t4,0x400a1
40051f8c:	4e0e8e93          	addi	t4,t4,1248 # 400a14e0 <opa4_table>
40051f90:	00f87793          	andi	a5,a6,15
40051f94:	97f6                	add	a5,a5,t4
40051f96:	839c                	lbu	a5,0(a5)
40051f98:	4709                	li	a4,2
40051f9a:	46a1                	li	a3,8
40051f9c:	889c                	sb	a5,0(s1)
40051f9e:	4e05                	li	t3,1
40051fa0:	00444783          	lbu	a5,4(s0)
40051fa4:	14f75463          	bge	a4,a5,400520ec <lv_font_get_bitmap_fmt_txt+0x3b6>
40051fa8:	400a1eb7          	lui	t4,0x400a1
40051fac:	4e0e8e93          	addi	t4,t4,1248 # 400a14e0 <opa4_table>
40051fb0:	01c487b3          	add	a5,s1,t3
40051fb4:	4281                	li	t0,0
40051fb6:	0008c803          	lbu	a6,0(a7)
40051fba:	003e0713          	addi	a4,t3,3
40051fbe:	0885                	addi	a7,a7,1
40051fc0:	00485313          	srli	t1,a6,0x4
40051fc4:	00f87593          	andi	a1,a6,15
40051fc8:	9376                	add	t1,t1,t4
40051fca:	95f6                	add	a1,a1,t4
40051fcc:	818c                	lbu	a1,0(a1)
40051fce:	00034303          	lbu	t1,0(t1)
40051fd2:	0285                	addi	t0,t0,1
40051fd4:	8bcc                	sb	a1,1(a5)
40051fd6:	00678023          	sb	t1,0(a5)
40051fda:	00444583          	lbu	a1,4(s0)
40051fde:	0789                	addi	a5,a5,2
40051fe0:	0e09                	addi	t3,t3,2
40051fe2:	fcb74ae3          	blt	a4,a1,40051fb6 <lv_font_get_bitmap_fmt_txt+0x280>
40051fe6:	f4be5be3          	bge	t3,a1,40051f3c <lv_font_get_bitmap_fmt_txt+0x206>
40051fea:	0008c803          	lbu	a6,0(a7)
40051fee:	9e26                	add	t3,t3,s1
40051ff0:	0285                	addi	t0,t0,1
40051ff2:	00485793          	srli	a5,a6,0x4
40051ff6:	9ebe                	add	t4,t4,a5
40051ff8:	000ec783          	lbu	a5,0(t4)
40051ffc:	0885                	addi	a7,a7,1
40051ffe:	4691                	li	a3,4
40052000:	00fe0023          	sb	a5,0(t3)
40052004:	bf25                	j	40051f3c <lv_font_get_bitmap_fmt_txt+0x206>
40052006:	00448413          	addi	s0,s1,4
4005200a:	8522                	mv	a0,s0
4005200c:	c232                	sw	a2,4(sp)
4005200e:	94dde0ef          	jal	4003095a <lv_mutex_lock>
40052012:	409c                	lw	a5,0(s1)
40052014:	acf2                	cm.mva01s	s1,s4
40052016:	9782                	jalr	a5
40052018:	88aa                	mv	a7,a0
4005201a:	cd79                	beqz	a0,400520f8 <lv_font_get_bitmap_fmt_txt+0x3c2>
4005201c:	4612                	lw	a2,4(sp)
4005201e:	01664783          	lbu	a5,22(a2)
40052022:	c799                	beqz	a5,40052030 <lv_font_get_bitmap_fmt_txt+0x2fa>
40052024:	c22a                	sw	a0,4(sp)
40052026:	4208                	lw	a0,0(a2)
40052028:	0511                	addi	a0,a0,4
4005202a:	989de0ef          	jal	400309b2 <lv_mutex_unlock>
4005202e:	4892                	lw	a7,4(sp)
40052030:	5462                	lw	s0,56(sp)
40052032:	50f2                	lw	ra,60(sp)
40052034:	54d2                	lw	s1,52(sp)
40052036:	4c62                	lw	s8,24(sp)
40052038:	5a22                	lw	s4,40(sp)
4005203a:	8546                	mv	a0,a7
4005203c:	6121                	addi	sp,sp,64
4005203e:	8082                	ret
40052040:	ef75dbe3          	bge	a1,s7,40051f36 <lv_font_get_bitmap_fmt_txt+0x200>
40052044:	00fb7793          	andi	a5,s6,15
40052048:	02f6c733          	div	a4,a3,a5
4005204c:	40e90733          	sub	a4,s2,a4
40052050:	bd95                	j	40051ec4 <lv_font_get_bitmap_fmt_txt+0x18e>
40052052:	5b02                	lw	s6,32(sp)
40052054:	4bf2                	lw	s7,28(sp)
40052056:	01664783          	lbu	a5,22(a2)
4005205a:	c789                	beqz	a5,40052064 <lv_font_get_bitmap_fmt_txt+0x32e>
4005205c:	4208                	lw	a0,0(a2)
4005205e:	0511                	addi	a0,a0,4
40052060:	953de0ef          	jal	400309b2 <lv_mutex_unlock>
40052064:	8562                	mv	a0,s8
40052066:	4581                	li	a1,0
40052068:	d93d50ef          	jal	40027dfa <lv_draw_buf_flush_cache>
4005206c:	50f2                	lw	ra,60(sp)
4005206e:	5462                	lw	s0,56(sp)
40052070:	88e2                	mv	a7,s8
40052072:	54d2                	lw	s1,52(sp)
40052074:	5942                	lw	s2,48(sp)
40052076:	59b2                	lw	s3,44(sp)
40052078:	5a92                	lw	s5,36(sp)
4005207a:	4c62                	lw	s8,24(sp)
4005207c:	5a22                	lw	s4,40(sp)
4005207e:	8546                	mv	a0,a7
40052080:	6121                	addi	sp,sp,64
40052082:	8082                	ret
40052084:	00444b83          	lbu	s7,4(s0)
40052088:	e00b94e3          	bnez	s7,40051e90 <lv_font_get_bitmap_fmt_txt+0x15a>
4005208c:	0bd6c6b3          	min	a3,a3,t4
40052090:	bd55                	j	40051f44 <lv_font_get_bitmap_fmt_txt+0x20e>
40052092:	00444703          	lbu	a4,4(s0)
40052096:	d40705e3          	beqz	a4,40051de0 <lv_font_get_bitmap_fmt_txt+0xaa>
4005209a:	4781                	li	a5,0
4005209c:	0008c583          	lbu	a1,0(a7)
400520a0:	00f48733          	add	a4,s1,a5
400520a4:	0785                	addi	a5,a5,1
400520a6:	8b0c                	sb	a1,0(a4)
400520a8:	00444283          	lbu	t0,4(s0)
400520ac:	0885                	addi	a7,a7,1
400520ae:	fe57c7e3          	blt	a5,t0,4005209c <lv_font_get_bitmap_fmt_txt+0x366>
400520b2:	b569                	j	40051f3c <lv_font_get_bitmap_fmt_txt+0x206>
400520b4:	00444783          	lbu	a5,4(s0)
400520b8:	f3ed                	bnez	a5,4005209a <lv_font_get_bitmap_fmt_txt+0x364>
400520ba:	0505                	addi	a0,a0,1
400520bc:	f8b55be3          	bge	a0,a1,40052052 <lv_font_get_bitmap_fmt_txt+0x31c>
400520c0:	94d6                	add	s1,s1,s5
400520c2:	00f37793          	andi	a5,t1,15
400520c6:	bd61                	j	40051f5e <lv_font_get_bitmap_fmt_txt+0x228>
400520c8:	46a1                	li	a3,8
400520ca:	4501                	li	a0,0
400520cc:	b561                	j	40051f54 <lv_font_get_bitmap_fmt_txt+0x21e>
400520ce:	c22a                	sw	a0,4(sp)
400520d0:	00490513          	addi	a0,s2,4
400520d4:	8dfde0ef          	jal	400309b2 <lv_mutex_unlock>
400520d8:	4892                	lw	a7,4(sp)
400520da:	5462                	lw	s0,56(sp)
400520dc:	54d2                	lw	s1,52(sp)
400520de:	5942                	lw	s2,48(sp)
400520e0:	59b2                	lw	s3,44(sp)
400520e2:	4c62                	lw	s8,24(sp)
400520e4:	b3b9                	j	40051e32 <lv_font_get_bitmap_fmt_txt+0xfc>
400520e6:	4705                	li	a4,1
400520e8:	4e01                	li	t3,0
400520ea:	bd5d                	j	40051fa0 <lv_font_get_bitmap_fmt_txt+0x26a>
400520ec:	4281                	li	t0,0
400520ee:	00fe4863          	blt	t3,a5,400520fe <lv_font_get_bitmap_fmt_txt+0x3c8>
400520f2:	00544583          	lbu	a1,5(s0)
400520f6:	b5b9                	j	40051f44 <lv_font_get_bitmap_fmt_txt+0x20e>
400520f8:	c22a                	sw	a0,4(sp)
400520fa:	8522                	mv	a0,s0
400520fc:	b73d                	j	4005202a <lv_font_get_bitmap_fmt_txt+0x2f4>
400520fe:	400a1eb7          	lui	t4,0x400a1
40052102:	4e0e8e93          	addi	t4,t4,1248 # 400a14e0 <opa4_table>
40052106:	b5d5                	j	40051fea <lv_font_get_bitmap_fmt_txt+0x2b4>

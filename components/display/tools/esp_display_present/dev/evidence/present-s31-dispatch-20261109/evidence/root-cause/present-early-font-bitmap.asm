
/tmp/present-s31-layout-20261109/early/s31-rgb/benchmark/present/build/present_benchmark.elf:     file format elf32-littleriscv


Disassembly of section .iram0.text:

Disassembly of section .flash.text:

40057dae <lv_font_get_bitmap_fmt_txt>:
40057dae:	7139                	addi	sp,sp,-64
40057db0:	d452                	sw	s4,40(sp)
40057db2:	00852a03          	lw	s4,8(a0)
40057db6:	de06                	sw	ra,60(sp)
40057db8:	0e0a0863          	beqz	s4,40057ea8 <lv_font_get_bitmap_fmt_txt+0xfa>
40057dbc:	4118                	lw	a4,0(a0)
40057dbe:	02054783          	lbu	a5,32(a0)
40057dc2:	dc22                	sw	s0,56(sp)
40057dc4:	4310                	lw	a2,0(a4)
40057dc6:	cc62                	sw	s8,24(sp)
40057dc8:	8b89                	andi	a5,a5,2
40057dca:	4240                	lw	s0,4(a2)
40057dcc:	8c2e                	mv	s8,a1
40057dce:	208a6433          	sh3add	s0,s4,s0
40057dd2:	e3ed                	bnez	a5,40057eb4 <lv_font_get_bitmap_fmt_txt+0x106>
40057dd4:	00444703          	lbu	a4,4(s0)
40057dd8:	00544783          	lbu	a5,5(s0)
40057ddc:	9fd9                	mul	a5,a5,a4
40057dde:	c3f9                	beqz	a5,40057ea4 <lv_font_get_bitmap_fmt_txt+0xf6>
40057de0:	01464803          	lbu	a6,20(a2)
40057de4:	06087813          	andi	a6,a6,96
40057de8:	0a081e63          	bnez	a6,40057ea4 <lv_font_get_bitmap_fmt_txt+0xf6>
40057dec:	01664783          	lbu	a5,22(a2)
40057df0:	da26                	sw	s1,52(sp)
40057df2:	d84a                	sw	s2,48(sp)
40057df4:	d64e                	sw	s3,44(sp)
40057df6:	4984                	lw	s1,16(a1)
40057df8:	01e55983          	lhu	s3,30(a0)
40057dfc:	00062903          	lw	s2,0(a2)
40057e00:	efe9                	bnez	a5,40057eda <lv_font_get_bitmap_fmt_txt+0x12c>
40057e02:	401c                	lw	a5,0(s0)
40057e04:	07b2                	slli	a5,a5,0xc
40057e06:	83b1                	srli	a5,a5,0xc
40057e08:	00f908b3          	add	a7,s2,a5
40057e0c:	34088363          	beqz	a7,40058152 <lv_font_get_bitmap_fmt_txt+0x3a4>
40057e10:	d256                	sw	s5,36(sp)
40057e12:	45b9                	li	a1,14
40057e14:	853a                	mv	a0,a4
40057e16:	c232                	sw	a2,4(sp)
40057e18:	c646                	sw	a7,12(sp)
40057e1a:	c442                	sw	a6,8(sp)
40057e1c:	94eda0ef          	jal	40031f6a <lv_draw_buf_width_to_stride>
40057e20:	00544583          	lbu	a1,5(s0)
40057e24:	4612                	lw	a2,4(sp)
40057e26:	8aaa                	mv	s5,a0
40057e28:	2a058363          	beqz	a1,400580ce <lv_font_get_bitmap_fmt_txt+0x320>
40057e2c:	01464303          	lbu	t1,20(a2)
40057e30:	d05a                	sw	s6,32(sp)
40057e32:	ce5e                	sw	s7,28(sp)
40057e34:	4822                	lw	a6,8(sp)
40057e36:	48b2                	lw	a7,12(sp)
40057e38:	00f37793          	andi	a5,t1,15
40057e3c:	30098263          	beqz	s3,40058140 <lv_font_get_bitmap_fmt_txt+0x392>
40057e40:	4711                	li	a4,4
40057e42:	4501                	li	a0,0
40057e44:	02e78863          	beq	a5,a4,40057e74 <lv_font_get_bitmap_fmt_txt+0xc6>
40057e48:	46a1                	li	a3,8
40057e4a:	2cd78063          	beq	a5,a3,4005810a <lv_font_get_bitmap_fmt_txt+0x35c>
40057e4e:	17fd                	addi	a5,a5,-1
40057e50:	9fe1                	zext.b	a5,a5
40057e52:	4705                	li	a4,1
40057e54:	0af77663          	bgeu	a4,a5,40057f00 <lv_font_get_bitmap_fmt_txt+0x152>
40057e58:	4281                	li	t0,0
40057e5a:	0505                	addi	a0,a0,1
40057e5c:	26b55763          	bge	a0,a1,400580ca <lv_font_get_bitmap_fmt_txt+0x31c>
40057e60:	01464783          	lbu	a5,20(a2)
40057e64:	405982b3          	sub	t0,s3,t0
40057e68:	4711                	li	a4,4
40057e6a:	8bbd                	andi	a5,a5,15
40057e6c:	9896                	add	a7,a7,t0
40057e6e:	94d6                	add	s1,s1,s5
40057e70:	fce79ce3          	bne	a5,a4,40057e48 <lv_font_get_bitmap_fmt_txt+0x9a>
40057e74:	00444283          	lbu	t0,4(s0)
40057e78:	4785                	li	a5,1
40057e7a:	46a1                	li	a3,8
40057e7c:	4e01                	li	t3,0
40057e7e:	1a57c163          	blt	a5,t0,40058020 <lv_font_get_bitmap_fmt_txt+0x272>
40057e82:	fcf29be3          	bne	t0,a5,40057e58 <lv_font_get_bitmap_fmt_txt+0xaa>
40057e86:	0008c803          	lbu	a6,0(a7)
40057e8a:	400a1eb7          	lui	t4,0x400a1
40057e8e:	eb0e8e93          	addi	t4,t4,-336 # 400a0eb0 <opa4_table>
40057e92:	00485793          	srli	a5,a6,0x4
40057e96:	97f6                	add	a5,a5,t4
40057e98:	839c                	lbu	a5,0(a5)
40057e9a:	9896                	add	a7,a7,t0
40057e9c:	889c                	sb	a5,0(s1)
40057e9e:	00544583          	lbu	a1,5(s0)
40057ea2:	bf65                	j	40057e5a <lv_font_get_bitmap_fmt_txt+0xac>
40057ea4:	5462                	lw	s0,56(sp)
40057ea6:	4c62                	lw	s8,24(sp)
40057ea8:	4881                	li	a7,0
40057eaa:	50f2                	lw	ra,60(sp)
40057eac:	5a22                	lw	s4,40(sp)
40057eae:	8546                	mv	a0,a7
40057eb0:	6121                	addi	sp,sp,64
40057eb2:	8082                	ret
40057eb4:	01664783          	lbu	a5,22(a2)
40057eb8:	da26                	sw	s1,52(sp)
40057eba:	4204                	lw	s1,0(a2)
40057ebc:	1c079163          	bnez	a5,4005807e <lv_font_get_bitmap_fmt_txt+0x2d0>
40057ec0:	401c                	lw	a5,0(s0)
40057ec2:	50f2                	lw	ra,60(sp)
40057ec4:	5462                	lw	s0,56(sp)
40057ec6:	07b2                	slli	a5,a5,0xc
40057ec8:	83b1                	srli	a5,a5,0xc
40057eca:	00f488b3          	add	a7,s1,a5
40057ece:	4c62                	lw	s8,24(sp)
40057ed0:	54d2                	lw	s1,52(sp)
40057ed2:	5a22                	lw	s4,40(sp)
40057ed4:	8546                	mv	a0,a7
40057ed6:	6121                	addi	sp,sp,64
40057ed8:	8082                	ret
40057eda:	00490513          	addi	a0,s2,4
40057ede:	c432                	sw	a2,8(sp)
40057ee0:	c242                	sw	a6,4(sp)
40057ee2:	fbdeb0ef          	jal	40043e9e <lv_mutex_lock>
40057ee6:	00092783          	lw	a5,0(s2)
40057eea:	ad72                	cm.mva01s	s2,s4
40057eec:	9782                	jalr	a5
40057eee:	88aa                	mv	a7,a0
40057ef0:	24050b63          	beqz	a0,40058146 <lv_font_get_bitmap_fmt_txt+0x398>
40057ef4:	d256                	sw	s5,36(sp)
40057ef6:	00444703          	lbu	a4,4(s0)
40057efa:	4812                	lw	a6,4(sp)
40057efc:	4622                	lw	a2,8(sp)
40057efe:	bf11                	j	40057e12 <lv_font_get_bitmap_fmt_txt+0x64>
40057f00:	00444b83          	lbu	s7,4(s0)
40057f04:	f40b8ae3          	beqz	s7,40057e58 <lv_font_get_bitmap_fmt_txt+0xaa>
40057f08:	01462b03          	lw	s6,20(a2)
40057f0c:	4f21                	li	t5,8
40057f0e:	5efd                	li	t4,-1
40057f10:	00fb7793          	andi	a5,s6,15
40057f14:	02ff4933          	div	s2,t5,a5
40057f18:	400a1e37          	lui	t3,0x400a1
40057f1c:	00fe9eb3          	sll	t4,t4,a5
40057f20:	eace0e13          	addi	t3,t3,-340 # 400a0eac <opa2_table>
40057f24:	4281                	li	t0,0
40057f26:	4581                	li	a1,0
40057f28:	4a1d                	li	s4,7
40057f2a:	4f85                	li	t6,1
40057f2c:	18da5a63          	bge	s4,a3,400580c0 <lv_font_get_bitmap_fmt_txt+0x312>
40057f30:	0008c803          	lbu	a6,0(a7)
40057f34:	0285                	addi	t0,t0,1
40057f36:	874a                	mv	a4,s2
40057f38:	0885                	addi	a7,a7,1
40057f3a:	4681                	li	a3,0
40057f3c:	40bb83b3          	sub	t2,s7,a1
40057f40:	0ae3c3b3          	min	t2,t2,a4
40057f44:	00b48733          	add	a4,s1,a1
40057f48:	00e38333          	add	t1,t2,a4
40057f4c:	00039f63          	bnez	t2,40057f6a <lv_font_get_bitmap_fmt_txt+0x1bc>
40057f50:	a2a5                	j	400580b8 <lv_font_get_bitmap_fmt_txt+0x30a>
40057f52:	000bc783          	lbu	a5,0(s7)
40057f56:	0705                	addi	a4,a4,1
40057f58:	fef70fa3          	sb	a5,-1(a4)
40057f5c:	01462b03          	lw	s6,20(a2)
40057f60:	00fb7793          	andi	a5,s6,15
40057f64:	96be                	add	a3,a3,a5
40057f66:	02e30f63          	beq	t1,a4,40057fa4 <lv_font_get_bitmap_fmt_txt+0x1f6>
40057f6a:	01464b03          	lbu	s6,20(a2)
40057f6e:	40ff07b3          	sub	a5,t5,a5
40057f72:	8f95                	sub	a5,a5,a3
40057f74:	40f857b3          	sra	a5,a6,a5
40057f78:	41d7f7b3          	andn	a5,a5,t4
40057f7c:	00fb7b13          	andi	s6,s6,15
40057f80:	01c78bb3          	add	s7,a5,t3
40057f84:	fdfb17e3          	bne	s6,t6,40057f52 <lv_font_get_bitmap_fmt_txt+0x1a4>
40057f88:	00f037b3          	snez	a5,a5
40057f8c:	40f007b3          	neg	a5,a5
40057f90:	9fe1                	zext.b	a5,a5
40057f92:	8b1c                	sb	a5,0(a4)
40057f94:	01462b03          	lw	s6,20(a2)
40057f98:	0705                	addi	a4,a4,1
40057f9a:	00fb7793          	andi	a5,s6,15
40057f9e:	96be                	add	a3,a3,a5
40057fa0:	fce315e3          	bne	t1,a4,40057f6a <lv_font_get_bitmap_fmt_txt+0x1bc>
40057fa4:	00444b83          	lbu	s7,4(s0)
40057fa8:	959e                	add	a1,a1,t2
40057faa:	f975c1e3          	blt	a1,s7,40057f2c <lv_font_get_bitmap_fmt_txt+0x17e>
40057fae:	47a1                	li	a5,8
40057fb0:	0af6c6b3          	min	a3,a3,a5
40057fb4:	00544583          	lbu	a1,5(s0)
40057fb8:	ea0991e3          	bnez	s3,40057e5a <lv_font_get_bitmap_fmt_txt+0xac>
40057fbc:	0505                	addi	a0,a0,1
40057fbe:	10b55663          	bge	a0,a1,400580ca <lv_font_get_bitmap_fmt_txt+0x31c>
40057fc2:	01464303          	lbu	t1,20(a2)
40057fc6:	94d6                	add	s1,s1,s5
40057fc8:	00f37793          	andi	a5,t1,15
40057fcc:	4e11                	li	t3,4
40057fce:	4ea1                	li	t4,8
40057fd0:	4f05                	li	t5,1
40057fd2:	00f37f93          	andi	t6,t1,15
40057fd6:	fff78713          	addi	a4,a5,-1
40057fda:	9f61                	zext.b	a4,a4
40057fdc:	03c78063          	beq	a5,t3,40057ffc <lv_font_get_bitmap_fmt_txt+0x24e>
40057fe0:	15d78663          	beq	a5,t4,4005812c <lv_font_get_bitmap_fmt_txt+0x37e>
40057fe4:	10ef7c63          	bgeu	t5,a4,400580fc <lv_font_get_bitmap_fmt_txt+0x34e>
40057fe8:	0505                	addi	a0,a0,1
40057fea:	0eb55063          	bge	a0,a1,400580ca <lv_font_get_bitmap_fmt_txt+0x31c>
40057fee:	87fe                	mv	a5,t6
40057ff0:	fff78713          	addi	a4,a5,-1
40057ff4:	94d6                	add	s1,s1,s5
40057ff6:	9f61                	zext.b	a4,a4
40057ff8:	ffc794e3          	bne	a5,t3,40057fe0 <lv_font_get_bitmap_fmt_txt+0x232>
40057ffc:	16f69163          	bne	a3,a5,4005815e <lv_font_get_bitmap_fmt_txt+0x3b0>
40058000:	400a1eb7          	lui	t4,0x400a1
40058004:	eb0e8e93          	addi	t4,t4,-336 # 400a0eb0 <opa4_table>
40058008:	00f87793          	andi	a5,a6,15
4005800c:	97f6                	add	a5,a5,t4
4005800e:	839c                	lbu	a5,0(a5)
40058010:	4709                	li	a4,2
40058012:	46a1                	li	a3,8
40058014:	889c                	sb	a5,0(s1)
40058016:	4e05                	li	t3,1
40058018:	00444783          	lbu	a5,4(s0)
4005801c:	14f75463          	bge	a4,a5,40058164 <lv_font_get_bitmap_fmt_txt+0x3b6>
40058020:	400a1eb7          	lui	t4,0x400a1
40058024:	eb0e8e93          	addi	t4,t4,-336 # 400a0eb0 <opa4_table>
40058028:	01c487b3          	add	a5,s1,t3
4005802c:	4281                	li	t0,0
4005802e:	0008c803          	lbu	a6,0(a7)
40058032:	003e0713          	addi	a4,t3,3
40058036:	0885                	addi	a7,a7,1
40058038:	00485313          	srli	t1,a6,0x4
4005803c:	00f87593          	andi	a1,a6,15
40058040:	9376                	add	t1,t1,t4
40058042:	95f6                	add	a1,a1,t4
40058044:	818c                	lbu	a1,0(a1)
40058046:	00034303          	lbu	t1,0(t1)
4005804a:	0285                	addi	t0,t0,1
4005804c:	8bcc                	sb	a1,1(a5)
4005804e:	00678023          	sb	t1,0(a5)
40058052:	00444583          	lbu	a1,4(s0)
40058056:	0789                	addi	a5,a5,2
40058058:	0e09                	addi	t3,t3,2
4005805a:	fcb74ae3          	blt	a4,a1,4005802e <lv_font_get_bitmap_fmt_txt+0x280>
4005805e:	f4be5be3          	bge	t3,a1,40057fb4 <lv_font_get_bitmap_fmt_txt+0x206>
40058062:	0008c803          	lbu	a6,0(a7)
40058066:	9e26                	add	t3,t3,s1
40058068:	0285                	addi	t0,t0,1
4005806a:	00485793          	srli	a5,a6,0x4
4005806e:	9ebe                	add	t4,t4,a5
40058070:	000ec783          	lbu	a5,0(t4)
40058074:	0885                	addi	a7,a7,1
40058076:	4691                	li	a3,4
40058078:	00fe0023          	sb	a5,0(t3)
4005807c:	bf25                	j	40057fb4 <lv_font_get_bitmap_fmt_txt+0x206>
4005807e:	00448413          	addi	s0,s1,4
40058082:	8522                	mv	a0,s0
40058084:	c232                	sw	a2,4(sp)
40058086:	e19eb0ef          	jal	40043e9e <lv_mutex_lock>
4005808a:	409c                	lw	a5,0(s1)
4005808c:	acf2                	cm.mva01s	s1,s4
4005808e:	9782                	jalr	a5
40058090:	88aa                	mv	a7,a0
40058092:	cd79                	beqz	a0,40058170 <lv_font_get_bitmap_fmt_txt+0x3c2>
40058094:	4612                	lw	a2,4(sp)
40058096:	01664783          	lbu	a5,22(a2)
4005809a:	c799                	beqz	a5,400580a8 <lv_font_get_bitmap_fmt_txt+0x2fa>
4005809c:	c22a                	sw	a0,4(sp)
4005809e:	4208                	lw	a0,0(a2)
400580a0:	0511                	addi	a0,a0,4
400580a2:	e55eb0ef          	jal	40043ef6 <lv_mutex_unlock>
400580a6:	4892                	lw	a7,4(sp)
400580a8:	5462                	lw	s0,56(sp)
400580aa:	50f2                	lw	ra,60(sp)
400580ac:	54d2                	lw	s1,52(sp)
400580ae:	4c62                	lw	s8,24(sp)
400580b0:	5a22                	lw	s4,40(sp)
400580b2:	8546                	mv	a0,a7
400580b4:	6121                	addi	sp,sp,64
400580b6:	8082                	ret
400580b8:	ef75dbe3          	bge	a1,s7,40057fae <lv_font_get_bitmap_fmt_txt+0x200>
400580bc:	00fb7793          	andi	a5,s6,15
400580c0:	02f6c733          	div	a4,a3,a5
400580c4:	40e90733          	sub	a4,s2,a4
400580c8:	bd95                	j	40057f3c <lv_font_get_bitmap_fmt_txt+0x18e>
400580ca:	5b02                	lw	s6,32(sp)
400580cc:	4bf2                	lw	s7,28(sp)
400580ce:	01664783          	lbu	a5,22(a2)
400580d2:	c789                	beqz	a5,400580dc <lv_font_get_bitmap_fmt_txt+0x32e>
400580d4:	4208                	lw	a0,0(a2)
400580d6:	0511                	addi	a0,a0,4
400580d8:	e1feb0ef          	jal	40043ef6 <lv_mutex_unlock>
400580dc:	8562                	mv	a0,s8
400580de:	4581                	li	a1,0
400580e0:	e9bd90ef          	jal	40031f7a <lv_draw_buf_flush_cache>
400580e4:	50f2                	lw	ra,60(sp)
400580e6:	5462                	lw	s0,56(sp)
400580e8:	88e2                	mv	a7,s8
400580ea:	54d2                	lw	s1,52(sp)
400580ec:	5942                	lw	s2,48(sp)
400580ee:	59b2                	lw	s3,44(sp)
400580f0:	5a92                	lw	s5,36(sp)
400580f2:	4c62                	lw	s8,24(sp)
400580f4:	5a22                	lw	s4,40(sp)
400580f6:	8546                	mv	a0,a7
400580f8:	6121                	addi	sp,sp,64
400580fa:	8082                	ret
400580fc:	00444b83          	lbu	s7,4(s0)
40058100:	e00b94e3          	bnez	s7,40057f08 <lv_font_get_bitmap_fmt_txt+0x15a>
40058104:	0bd6c6b3          	min	a3,a3,t4
40058108:	bd55                	j	40057fbc <lv_font_get_bitmap_fmt_txt+0x20e>
4005810a:	00444703          	lbu	a4,4(s0)
4005810e:	d40705e3          	beqz	a4,40057e58 <lv_font_get_bitmap_fmt_txt+0xaa>
40058112:	4781                	li	a5,0
40058114:	0008c583          	lbu	a1,0(a7)
40058118:	00f48733          	add	a4,s1,a5
4005811c:	0785                	addi	a5,a5,1
4005811e:	8b0c                	sb	a1,0(a4)
40058120:	00444283          	lbu	t0,4(s0)
40058124:	0885                	addi	a7,a7,1
40058126:	fe57c7e3          	blt	a5,t0,40058114 <lv_font_get_bitmap_fmt_txt+0x366>
4005812a:	b569                	j	40057fb4 <lv_font_get_bitmap_fmt_txt+0x206>
4005812c:	00444783          	lbu	a5,4(s0)
40058130:	f3ed                	bnez	a5,40058112 <lv_font_get_bitmap_fmt_txt+0x364>
40058132:	0505                	addi	a0,a0,1
40058134:	f8b55be3          	bge	a0,a1,400580ca <lv_font_get_bitmap_fmt_txt+0x31c>
40058138:	94d6                	add	s1,s1,s5
4005813a:	00f37793          	andi	a5,t1,15
4005813e:	bd61                	j	40057fd6 <lv_font_get_bitmap_fmt_txt+0x228>
40058140:	46a1                	li	a3,8
40058142:	4501                	li	a0,0
40058144:	b561                	j	40057fcc <lv_font_get_bitmap_fmt_txt+0x21e>
40058146:	c22a                	sw	a0,4(sp)
40058148:	00490513          	addi	a0,s2,4
4005814c:	dabeb0ef          	jal	40043ef6 <lv_mutex_unlock>
40058150:	4892                	lw	a7,4(sp)
40058152:	5462                	lw	s0,56(sp)
40058154:	54d2                	lw	s1,52(sp)
40058156:	5942                	lw	s2,48(sp)
40058158:	59b2                	lw	s3,44(sp)
4005815a:	4c62                	lw	s8,24(sp)
4005815c:	b3b9                	j	40057eaa <lv_font_get_bitmap_fmt_txt+0xfc>
4005815e:	4705                	li	a4,1
40058160:	4e01                	li	t3,0
40058162:	bd5d                	j	40058018 <lv_font_get_bitmap_fmt_txt+0x26a>
40058164:	4281                	li	t0,0
40058166:	00fe4863          	blt	t3,a5,40058176 <lv_font_get_bitmap_fmt_txt+0x3c8>
4005816a:	00544583          	lbu	a1,5(s0)
4005816e:	b5b9                	j	40057fbc <lv_font_get_bitmap_fmt_txt+0x20e>
40058170:	c22a                	sw	a0,4(sp)
40058172:	8522                	mv	a0,s0
40058174:	b73d                	j	400580a2 <lv_font_get_bitmap_fmt_txt+0x2f4>
40058176:	400a1eb7          	lui	t4,0x400a1
4005817a:	eb0e8e93          	addi	t4,t4,-336 # 400a0eb0 <opa4_table>
4005817e:	b5d5                	j	40058062 <lv_font_get_bitmap_fmt_txt+0x2b4>
